#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include "app/app_state.hpp"

namespace remixer::desktop {

// Standard Menu Command IDs
enum MenuCommand : int {
    CMD_FILE_NEW = 1001,
    CMD_FILE_OPEN_AUDIO,
    CMD_FILE_DOWNLOAD_ONLINE,
    CMD_FILE_IMPORT_STEMS,
    CMD_FILE_SAVE,
    CMD_FILE_EXPORT_AUDIO,
    CMD_FILE_EXIT,

    CMD_EDIT_UNDO = 1101,
    CMD_EDIT_REDO,
    CMD_EDIT_CUT,
    CMD_EDIT_COPY,
    CMD_EDIT_PASTE,
    CMD_EDIT_PREFERENCES,

    CMD_AUDIO_PLAY_PAUSE = 1201,
    CMD_AUDIO_STOP,
    CMD_AUDIO_RESET_ENGINE,
    CMD_AUDIO_DEVICE_SETTINGS,

    CMD_MODULES_LOAD_PLUGIN = 1301,
    CMD_MODULES_RESCAN,
    CMD_MODULES_STORE_CATALOG,

    CMD_VIEW_TOGGLE_SIDEBAR = 1401,
    CMD_VIEW_RESET_LAYOUT,
    CMD_VIEW_ZOOM_IN,
    CMD_VIEW_ZOOM_OUT,

    CMD_HELP_DOCS = 1501,
    CMD_HELP_DIAGNOSTICS,
    CMD_HELP_ABOUT
};

/**
 * @brief Win32 Window with integrated Menubar, Sidebar & Statusbar.
 * Provides a dedicated top menubar, left collapsible sidebar, bottom statusbar with live telemetry,
 * and leaves the entire central canvas area ready for custom UI design.
 */
class Win32Window {
public:
    Win32Window();
    virtual ~Win32Window();

    bool create(HINSTANCE hInstance, int nCmdShow, int width = 1200, int height = 750);
    int run_message_loop();

    // Sidebar Tab IDs
    enum SidebarTabId : int {
        TAB_SESSION_TRACKS = 0,
        TAB_AUDIO_BROWSER  = 1,
        TAB_STEM_SEPARATOR = 2,
        TAB_AUDIO_TO_MIDI  = 3,
        TAB_DSP_MODULES    = 4,
        TAB_SETTINGS       = 5,
        TAB_CUSTOM_MODULE  = 100
    };

    struct SidebarItem {
        std::wstring icon;
        std::wstring title;
        std::wstring subtitle;
        std::string module_id; // Empty string for built-in core program menus
        bool is_module{false};  // true if it is an optional installable module
        int tab_id{TAB_SESSION_TRACKS};
    };

    void rebuild_sidebar_items();
    [[nodiscard]] int get_active_tab_id() const noexcept { return active_tab_id_; }

    HWND get_hwnd() const noexcept { return hwnd_; }

    static constexpr int MENUBAR_HEIGHT = 30;
    static constexpr int STATUSBAR_HEIGHT = 28;
    static constexpr int SIDEBAR_WIDTH = 220;

    void set_sidebar_visible(bool visible) noexcept { sidebar_visible_ = visible; }
    [[nodiscard]] bool is_sidebar_visible() const noexcept { return sidebar_visible_; }
    [[nodiscard]] int get_active_sidebar_tab() const noexcept { return active_sidebar_tab_; }

protected:
    // -----------------------------------------------------------------------
    // Canvas Workspace Hooks: Override or implement custom UI design here
    // -----------------------------------------------------------------------
    virtual void on_paint_canvas(HDC hdc, int x, int y, int width, int height);
    virtual void on_canvas_lbutton_down(int x, int y);
    virtual void on_canvas_mouse_move(int x, int y, WPARAM flags);
    virtual void on_canvas_lbutton_up(int x, int y);
    virtual void on_canvas_resize(int width, int height);
    virtual void on_sidebar_tab_changed(int tab_id);

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT handle_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Internal Component Renderers
    void render_menubar(HDC hdc, int width);
    void render_sidebar(HDC hdc, int width, int height);
    void render_statusbar(HDC hdc, int width, int height);

    // Interactive Menu & Sidebar Handlers
    int get_menu_item_at_x(int x, int width);
    void open_menu_dropdown(int menu_index, int x, int y);
    void handle_menu_command(int command_id);
    void open_audio_file_dialog();
    int get_sidebar_item_at_y(int y);

    // Tab Canvas Renderers
    void render_session_tracks_view(HDC hdc, int x, int y, int w, int h);
    void render_audio_browser_view(HDC hdc, int x, int y, int w, int h);
    void render_stem_separator_view(HDC hdc, int x, int y, int w, int h);
    void render_audio_to_midi_view(HDC hdc, int x, int y, int w, int h);
    void render_dsp_modules_view(HDC hdc, int x, int y, int w, int h);
    void render_settings_view(HDC hdc, int x, int y, int w, int h);

    HWND hwnd_{nullptr};
    HINSTANCE hinstance_{nullptr};
    AppState& state_;

    // UI Navigation State
    bool sidebar_visible_{true};
    int active_sidebar_tab_{0};
    int active_tab_id_{TAB_SESSION_TRACKS};
    int hovered_sidebar_idx_{-1};
    int hovered_menu_idx_{-1};
    uint32_t timer_ticks_{0};

    // Dynamic Sidebar Items (Built-in menus + only currently installed modules)
    std::vector<SidebarItem> active_sidebar_items_;

    // Interactive Canvas Buttons (For installing/uninstalling modules in Store/DSP tab)
    struct CanvasButton {
        RECT rect;
        std::string module_id;
        bool is_install_btn{true};
    };
    std::vector<CanvasButton> canvas_buttons_;

    // Interactive Setting Actions (For changing settings on the Settings page)
    struct SettingAction {
        RECT rect;
        enum Type {
            DRIVER,
            SAMPLE_RATE,
            BUFFER_SIZE,
            AI_BACKEND,
            AI_THREADS,
            TOGGLE_PRIORITY,
            TOGGLE_IPC,
            RESET_DSP,
            RUN_SECURITY_DIAGNOSTICS
        } type;
        int int_val{0};
        uint32_t uint_val{0};
    };
    std::vector<SettingAction> setting_actions_;

    // Interactive Downloader Actions (Full yt-dlp Suite in Audio Browser Tab)
    struct DownloaderAction {
        RECT rect;
        enum Type {
            PASTE_CLIPBOARD,
            CLEAR_URL,
            PROBE_URL,
            TOGGLE_MODE,
            SET_AUDIO_FORMAT,
            SET_VIDEO_QUALITY,
            TOGGLE_SPLIT_CHAPTERS,
            TOGGLE_PLAYLIST,
            TOGGLE_EMBED_METADATA,
            TOGGLE_MULTITHREAD,
            CYCLE_COOKIES,
            START_DOWNLOAD,
            LOAD_LAST_TO_DECK,
            SEND_LAST_TO_STEMS
        } type;
        int int_val{0};
    };
    std::vector<DownloaderAction> downloader_actions_;

    // GDI Double Buffering (Flicker-free)
    HDC mem_dc_{nullptr};
    HBITMAP mem_bitmap_{nullptr};
    HBITMAP old_bitmap_{nullptr};
    int client_w_{0};
    int client_h_{0};

    // Fonts & GDI Resources
    HFONT font_ui_{nullptr};
    HFONT font_bold_{nullptr};
    HFONT font_small_{nullptr};
    HFONT font_icon_{nullptr};
    HBRUSH bg_canvas_brush_{nullptr};
    HBRUSH bg_bar_brush_{nullptr};
    HBRUSH bg_sidebar_brush_{nullptr};
    HBRUSH active_sidebar_brush_{nullptr};
    HBRUSH hover_bar_brush_{nullptr};
    HPEN border_pen_{nullptr};
    HPEN accent_pen_{nullptr};
};

} // namespace remixer::desktop

#endif // _WIN32
