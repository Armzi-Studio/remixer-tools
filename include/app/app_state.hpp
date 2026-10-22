#pragma once

#include <core/audio_engine.hpp>
#include <downloader/downloader.hpp>
#include <string>
#include <array>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace remixer::desktop {

enum class AppTab {
    PlayerDeck,
    StemMixer,
    StoreCatalog,
    Settings
};

struct ModuleInfo {
    std::string module_id;
    std::wstring name;
    std::wstring subtitle;
    std::wstring icon;
    std::wstring description;
    std::string version;
    bool is_installed{false};
    std::filesystem::path local_path;
};

struct StemChannelState {
    std::string name;
    float volume_linear{1.0f};  // 0.0 to 1.0
    float pan{-0.0f};           // -1.0 (Left) to +1.0 (Right)
    bool is_muted{false};
    bool is_solo{false};
    float current_level{0.0f};  // VU meter peak
};

/**
 * @brief Central Application State Manager for Remixer Tools Desktop App.
 * Manages UI state, communicates with remixer-tools-core-engine, and handles audio events.
 */
class AppState {
public:
    static AppState& get_instance() {
        static AppState instance;
        return instance;
    }

    // Initialize the core audio DSP engine
    bool initialize_engine() {
        if (!engine_) {
            engine_ = std::make_unique<remixer::core::AudioEngine>(48000, 512);
            rmx_status_t status = engine_->initialize();
            return rmx_is_ok(status);
        }
        return true;
    }

    remixer::core::AudioEngine* get_engine() noexcept { return engine_.get(); }

    // Tab Navigation
    AppTab get_current_tab() const noexcept { return current_tab_; }
    void set_current_tab(AppTab tab) {
        current_tab_ = tab;
        if (on_tab_changed) on_tab_changed(tab);
    }

    // Playback state
    bool is_playing() const noexcept { return engine_ ? engine_->is_playing() : false; }
    void play() { if (engine_) engine_->play(); }
    void pause() { if (engine_) engine_->pause(); }
    void stop() { if (engine_) engine_->stop(); }

    // Stem Channels
    std::array<StemChannelState, 4>& get_stems() noexcept { return stems_; }

    // Status Bar Telemetry & Messages
    const std::wstring& get_status_message() const noexcept { return status_message_; }
    void set_status_message(const std::wstring& msg) { status_message_ = msg; }
    void set_status_message(const std::string& msg) {
        status_message_.assign(msg.begin(), msg.end());
    }

    float get_cpu_load_pct() const noexcept { return cpu_load_pct_; }
    void set_cpu_load_pct(float pct) noexcept { cpu_load_pct_ = pct; }

    uint32_t get_xruns() const noexcept { return xruns_; }
    void increment_xruns() noexcept { ++xruns_; }

    uint32_t get_sample_rate() const noexcept { return sample_rate_; }
    void set_sample_rate(uint32_t sr) noexcept { sample_rate_ = sr; }

    uint32_t get_buffer_size() const noexcept { return buffer_size_; }
    void set_buffer_size(uint32_t bs) noexcept { buffer_size_ = bs; }

    // -----------------------------------------------------------------------
    // Audio Engine & Hardware Device Settings
    // -----------------------------------------------------------------------
    int get_audio_driver() const noexcept { return audio_driver_; }
    void set_audio_driver(int d) noexcept { audio_driver_ = d; }

    int get_ai_backend() const noexcept { return ai_backend_; }
    void set_ai_backend(int b) noexcept { ai_backend_ = b; }

    int get_ai_threads() const noexcept { return ai_threads_; }
    void set_ai_threads(int t) noexcept { ai_threads_ = t; }

    bool is_high_priority_audio() const noexcept { return high_priority_audio_; }
    void set_high_priority_audio(bool enable) noexcept { high_priority_audio_ = enable; }

    bool is_ipc_bridge_enabled() const noexcept { return ipc_bridge_enabled_; }
    void set_ipc_bridge_enabled(bool enable) noexcept { ipc_bridge_enabled_ = enable; }

    // -----------------------------------------------------------------------
    // Dynamic Module Lifecycle & Installation Management
    // -----------------------------------------------------------------------
    bool is_module_installed(const std::string& module_id) const noexcept {
        for (const auto& mod : modules_) {
            if (mod.module_id == module_id) return mod.is_installed;
        }
        return false;
    }

    void install_module(const std::string& module_id, const std::filesystem::path& path = {}) {
        for (auto& mod : modules_) {
            if (mod.module_id == module_id) {
                mod.is_installed = true;
                if (!path.empty()) mod.local_path = path;
                set_status_message(std::wstring(L"Module installed: ") + mod.name);
                if (on_modules_changed) on_modules_changed();
                return;
            }
        }
        // If not in catalog, register as a custom module
        ModuleInfo custom_mod;
        custom_mod.module_id = module_id;
        custom_mod.name = std::wstring(module_id.begin(), module_id.end());
        custom_mod.subtitle = L"Custom Dynamic Module";
        custom_mod.icon = L"\uE74C";
        custom_mod.description = L"User-loaded custom DSP module";
        custom_mod.version = "1.0.0";
        custom_mod.is_installed = true;
        custom_mod.local_path = path;
        modules_.push_back(custom_mod);

        set_status_message(std::wstring(L"Custom module installed: ") + custom_mod.name);
        if (on_modules_changed) on_modules_changed();
    }

    void uninstall_module(const std::string& module_id) {
        for (auto& mod : modules_) {
            if (mod.module_id == module_id) {
                mod.is_installed = false;
                set_status_message(std::wstring(L"Module uninstalled: ") + mod.name);
                if (on_modules_changed) on_modules_changed();
                return;
            }
        }
    }

    void toggle_module_installation(const std::string& module_id) {
        if (is_module_installed(module_id)) {
            uninstall_module(module_id);
        } else {
            install_module(module_id);
        }
    }

    void scan_modules_directory(const std::filesystem::path& dir = {}) {
        std::vector<std::filesystem::path> scan_dirs;
        if (!dir.empty()) scan_dirs.push_back(dir);
        scan_dirs.push_back("modules");
        scan_dirs.push_back("plugins");
        scan_dirs.push_back("../modules");
        scan_dirs.push_back("../plugins");

        for (const auto& d : scan_dirs) {
            std::error_code ec;
            if (std::filesystem::exists(d, ec) && std::filesystem::is_directory(d, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(d, ec)) {
                    if (entry.is_regular_file(ec)) {
                        std::string filename = entry.path().filename().string();
                        std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
                        if (filename.find("demucs") != std::string::npos || filename.find("stem") != std::string::npos) {
                            install_module("com.armzistudio.demucs_v4", entry.path());
                        } else if (filename.find("pitch") != std::string::npos || filename.find("midi") != std::string::npos) {
                            install_module("com.armzistudio.pitch_tracker", entry.path());
                        } else if (filename.find("downloader") != std::string::npos || filename.find("ytdlp") != std::string::npos) {
                            install_module("com.armzistudio.media_downloader", entry.path());
                        }
                    }
                }
            }
        }
    }

    const std::vector<ModuleInfo>& get_available_modules() const noexcept { return modules_; }
    std::vector<ModuleInfo>& get_available_modules() noexcept { return modules_; }

    // -----------------------------------------------------------------------
    // Online Downloader Integration (yt-dlp + FFmpeg)
    // -----------------------------------------------------------------------
    bool has_downloader_ytdlp() const noexcept { return const_cast<remixer::downloader::MediaDownloader&>(downloader_).has_ytdlp(); }
    bool has_downloader_ffmpeg() const noexcept { return const_cast<remixer::downloader::MediaDownloader&>(downloader_).has_ffmpeg(); }
    bool is_downloader_ready() const noexcept {
        return has_downloader_ytdlp() && has_downloader_ffmpeg();
    }

    bool is_downloading() const noexcept { return is_downloading_.load(); }
    float get_download_progress() const noexcept { return download_progress_pct_.load(); }

    std::string get_download_url() const {
        std::lock_guard<std::mutex> lock(download_mutex_);
        return download_url_;
    }
    void set_download_url(const std::string& url) {
        std::lock_guard<std::mutex> lock(download_mutex_);
        download_url_ = url;
    }

    std::string get_download_status() const {
        std::lock_guard<std::mutex> lock(download_mutex_);
        return download_status_text_;
    }

    std::string get_download_current_op() const {
        std::lock_guard<std::mutex> lock(download_mutex_);
        return download_current_op_;
    }

    std::string get_download_last_file() const {
        std::lock_guard<std::mutex> lock(download_mutex_);
        return download_last_file_;
    }

    remixer::downloader::DownloadConfig& get_download_config() noexcept { return download_config_; }
    const remixer::downloader::DownloadConfig& get_download_config() const noexcept { return download_config_; }

    const remixer::downloader::MediaMetadata& get_probed_meta() const noexcept { return probed_meta_; }
    bool is_probing() const noexcept { return is_probing_.load(); }

    const std::vector<std::string>& get_download_last_files() const noexcept { return download_last_files_; }

    bool probe_online_url(const std::string& url_or_source = "", std::function<void(bool, const remixer::downloader::MediaMetadata&)> on_complete = nullptr) {
        if (is_probing_.load() || is_downloading_.load()) return false;

        std::string source = url_or_source;
        if (source.empty()) {
            std::lock_guard<std::mutex> lock(download_mutex_);
            source = download_url_;
        }
        if (source.empty()) return false;

        is_probing_.store(true);
        {
            std::lock_guard<std::mutex> lock(download_mutex_);
            download_status_text_ = "Probing media info with yt-dlp...";
            download_current_op_ = "Inspecting stream manifest...";
        }
        set_status_message(L"Downloader: Probing stream metadata...");

        std::thread([this, source, on_complete]() {
            remixer::downloader::MediaMetadata meta;
            bool ok = downloader_.probe(source, meta, download_config_);
            is_probing_.store(false);

            if (ok) {
                {
                    std::lock_guard<std::mutex> lock(download_mutex_);
                    probed_meta_ = meta;
                    download_status_text_ = "Stream Ready: " + meta.title + " [" + meta.duration_string + "]";
                    download_current_op_ = "Channel: " + meta.uploader;
                }
                set_status_message(std::wstring(L"Stream: ") + std::wstring(meta.title.begin(), meta.title.end()));
                if (on_complete) on_complete(true, meta);
                if (on_probe_finished) on_probe_finished(true, meta);
            } else {
                {
                    std::lock_guard<std::mutex> lock(download_mutex_);
                    download_status_text_ = "Probe failed. Check URL or internet connection.";
                }
                set_status_message(L"Downloader: Metadata inspection failed.");
                if (on_complete) on_complete(false, meta);
                if (on_probe_finished) on_probe_finished(false, meta);
            }
        }).detach();

        return true;
    }

    bool start_online_download(const std::string& url_or_source = "", std::function<void(bool, const std::string&)> on_complete = nullptr) {
        if (is_downloading_.load()) {
            return false;
        }

        std::string source = url_or_source;
        if (source.empty()) {
            std::lock_guard<std::mutex> lock(download_mutex_);
            source = download_url_;
        }
        if (source.empty()) {
            return false;
        }

        is_downloading_.store(true);
        download_progress_pct_.store(0.0f);
        {
            std::lock_guard<std::mutex> lock(download_mutex_);
            download_url_ = source;
            download_status_text_ = "Connecting with yt-dlp...";
            download_current_op_ = "Initiating multi-threaded download...";
        }

        set_status_message(L"Downloader: Ingesting media stream...");

        std::thread([this, source, on_complete]() {
            std::error_code ec;
            std::filesystem::path dl_dir = "downloads";
            if (!std::filesystem::exists(dl_dir, ec)) {
                std::filesystem::create_directories(dl_dir, ec);
            }

            std::vector<std::filesystem::path> out_files;
            bool ok = downloader_.download(source, dl_dir, download_config_, out_files, [this](const remixer::downloader::DownloadProgress& p) {
                download_progress_pct_.store(p.percentage);
                {
                    std::lock_guard<std::mutex> lock(download_mutex_);
                    download_current_op_ = p.current_operation;
                    if (!p.error_message.empty()) {
                        download_status_text_ = p.error_message;
                    } else if (p.stage == remixer::downloader::DownloadStage::DownloadingMedia) {
                        download_status_text_ = "Downloading stream: " + std::to_string(static_cast<int>(p.percentage)) + "%";
                    } else if (p.stage == remixer::downloader::DownloadStage::ExtractingAudio) {
                        download_status_text_ = "FFmpeg: Decoding audio stream...";
                    } else if (p.stage == remixer::downloader::DownloadStage::SplittingChapters) {
                        download_status_text_ = "Splitting tracks by chapters...";
                    } else if (p.stage == remixer::downloader::DownloadStage::EmbeddingMetadata) {
                        download_status_text_ = "Embedding ID3 tags & album art...";
                    }
                }
                if (on_download_progress) {
                    on_download_progress(p.percentage, p.current_operation);
                }
            });

            is_downloading_.store(false);

            if (ok && !out_files.empty()) {
                std::string primary_file = std::filesystem::absolute(out_files.front(), ec).string();
                {
                    std::lock_guard<std::mutex> lock(download_mutex_);
                    download_last_file_ = primary_file;
                    download_last_files_.clear();
                    for (const auto& f : out_files) {
                        download_last_files_.push_back(std::filesystem::absolute(f, ec).string());
                    }
                    download_status_text_ = "Ingested " + std::to_string(out_files.size()) + " file(s): " + out_files.front().filename().string();
                    download_progress_pct_.store(100.0f);
                }
                set_status_message(std::wstring(L"Downloader: Complete -> ") + out_files.front().filename().wstring());
                if (on_complete) on_complete(true, primary_file);
                if (on_download_finished) on_download_finished(true, primary_file);
            } else {
                {
                    std::lock_guard<std::mutex> lock(download_mutex_);
                    download_status_text_ = "Download failed. Check URL, format, or connection.";
                }
                set_status_message(L"Downloader: Execution failed. Please check options.");
                if (on_complete) on_complete(false, "");
                if (on_download_finished) on_download_finished(false, "");
            }
        }).detach();

        return true;
    }

    // Callbacks
    std::function<void(AppTab)> on_tab_changed;
    std::function<void()> on_modules_changed;
    std::function<void(float, const std::string&)> on_download_progress;
    std::function<void(bool, const std::string&)> on_download_finished;
    std::function<void(bool, const remixer::downloader::MediaMetadata&)> on_probe_finished;

private:
    AppState() {
        stems_[0] = { "Vocals", 1.0f, 0.0f, false, false, 0.0f };
        stems_[1] = { "Drums",  1.0f, 0.0f, false, false, 0.0f };
        stems_[2] = { "Bass",   1.0f, 0.0f, false, false, 0.0f };
        stems_[3] = { "Other",  1.0f, 0.0f, false, false, 0.0f };

        // Prepopulate module catalog (Uninstalled by default - only shows in sidebar when installed)
        modules_ = {
            {
                "com.armzistudio.demucs_v4",
                L"Stem Separation",
                L"Vocals, Drums, Bass",
                L"\uE9E9",
                L"Demucs v4 Neural Stem Separator (Vocals, Drums, Bass, Other)",
                "4.0.1",
                false, // Not installed initially
                {}
            },
            {
                "com.armzistudio.pitch_tracker",
                L"Audio to MIDI",
                L"Pitch Detection",
                L"\uE765",
                L"Polyphonic Audio to MIDI Transcriber & Note Quantizer",
                "2.1.0",
                false, // Not installed initially
                {}
            },
            {
                "com.armzistudio.media_downloader",
                L"Audio Downloader",
                L"yt-dlp Engine",
                L"\uE8D6",
                L"Full-Featured Media Downloader & yt-dlp Audio Ingestion Engine",
                "1.0.0",
                false, // Not installed initially
                {}
            }
        };

        // Scan local directories if any modules are placed on disk
        scan_modules_directory();
    }

    AppTab current_tab_{AppTab::PlayerDeck};
    std::unique_ptr<remixer::core::AudioEngine> engine_{nullptr};
    std::array<StemChannelState, 4> stems_;
    std::vector<ModuleInfo> modules_;

    std::wstring status_message_{L"System Ready | Real-Time Audio DSP Engine Online"};
    float cpu_load_pct_{0.9f};
    uint32_t xruns_{0};
    uint32_t sample_rate_{48000};
    uint32_t buffer_size_{512};

    int audio_driver_{0}; // 0 = WASAPI Exclusive, 1 = ASIO Bridge, 2 = DirectSound
    int ai_backend_{0};   // 0 = DirectML (GPU Acceleration), 1 = CPU AVX-512 / AVX2, 2 = Native ONNX Runtime
    int ai_threads_{4};   // 2, 4, 8
    bool high_priority_audio_{true};
    bool ipc_bridge_enabled_{true};

    // Downloader engine & state
    remixer::downloader::MediaDownloader downloader_;
    mutable std::mutex download_mutex_;
    std::atomic<bool> is_downloading_{false};
    std::atomic<bool> is_probing_{false};
    std::atomic<float> download_progress_pct_{0.0f};
    std::string download_url_{"https://www.youtube.com/watch?v=dQw4w9WgXcQ"};
    std::string download_status_text_{"Downloader Engine Ready (yt-dlp + FFmpeg)"};
    std::string download_current_op_{"Idle"};
    std::string download_last_file_;
    std::vector<std::string> download_last_files_;
    remixer::downloader::DownloadConfig download_config_;
    remixer::downloader::MediaMetadata probed_meta_;
};

} // namespace remixer::desktop
