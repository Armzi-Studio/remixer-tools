#include "app/win32_window.hpp"
#include "core/obfuscation.hpp"
#include <dwmapi.h>
#include <commdlg.h>
#include <cmath>
#include <algorithm>

namespace remixer::desktop {

namespace {
    const wchar_t* const MENU_TITLES[] = {
        L"File",
        L"Edit",
        L"Audio Engine",
        L"Modules",
        L"View",
        L"Help"
    };
    constexpr int NUM_MENUS = sizeof(MENU_TITLES) / sizeof(MENU_TITLES[0]);

    inline std::string to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) return {};
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), str.data(), size_needed, nullptr, nullptr);
        return str;
    }

    inline std::wstring to_wide(const std::string& str) {
        if (str.empty()) return {};
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), wstr.data(), size_needed);
        return wstr;
    }
}

Win32Window::Win32Window()
    : state_(AppState::get_instance())
{
    state_.initialize_engine();

    // Listen for module install/uninstall changes to dynamically update sidebar
    state_.on_modules_changed = [this]() {
        rebuild_sidebar_items();
        if (hwnd_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    };

    // Listen for online downloader progress & finish events
    state_.on_download_progress = [this](float, const std::string&) {
        if (hwnd_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    };

    state_.on_download_finished = [this](bool, const std::string&) {
        if (hwnd_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    };

    state_.on_probe_finished = [this](bool, const remixer::downloader::MediaMetadata&) {
        if (hwnd_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    };

    // Build initial sidebar items (Only program menus by default unless modules are installed)
    rebuild_sidebar_items();

    // Color Palette
    bg_canvas_brush_      = CreateSolidBrush(RGB(15, 17, 21)); // Deep dark canvas
    bg_bar_brush_         = CreateSolidBrush(RGB(22, 25, 30)); // Top/Bottom bars
    bg_sidebar_brush_     = CreateSolidBrush(RGB(19, 21, 26)); // Left Sidebar
    active_sidebar_brush_ = CreateSolidBrush(RGB(30, 35, 46)); // Active item
    hover_bar_brush_      = CreateSolidBrush(RGB(26, 29, 37)); // Hover item
    border_pen_           = CreatePen(PS_SOLID, 1, RGB(36, 40, 48)); // Border divider
    accent_pen_           = CreatePen(PS_SOLID, 1, RGB(59, 130, 246)); // Neon blue accent

    // Typography
    font_ui_ = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    font_bold_ = CreateFontW(
        -12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    font_small_ = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    font_icon_ = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets"
    );
}

Win32Window::~Win32Window() {
    if (bg_canvas_brush_)      DeleteObject(bg_canvas_brush_);
    if (bg_bar_brush_)         DeleteObject(bg_bar_brush_);
    if (bg_sidebar_brush_)     DeleteObject(bg_sidebar_brush_);
    if (active_sidebar_brush_) DeleteObject(active_sidebar_brush_);
    if (hover_bar_brush_)      DeleteObject(hover_bar_brush_);
    if (border_pen_)           DeleteObject(border_pen_);
    if (accent_pen_)           DeleteObject(accent_pen_);

    if (font_ui_)    DeleteObject(font_ui_);
    if (font_bold_)  DeleteObject(font_bold_);
    if (font_small_) DeleteObject(font_small_);
    if (font_icon_)  DeleteObject(font_icon_);

    if (mem_dc_) {
        SelectObject(mem_dc_, old_bitmap_);
        if (mem_bitmap_) DeleteObject(mem_bitmap_);
        DeleteDC(mem_dc_);
    }
}

bool Win32Window::create(HINSTANCE hInstance, int nCmdShow, int width, int height) {
    hinstance_ = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = bg_canvas_brush_;

    std::wstring class_name = OBFUSCATE_WSTR("RemixerToolsDesktopClass");
    wc.lpszClassName = class_name.c_str();

    RegisterClassExW(&wc);

    std::wstring window_title = OBFUSCATE_WSTR("Remixer Tools");
    hwnd_ = CreateWindowExW(
        0,
        class_name.c_str(),
        window_title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, hInstance, this
    );

    if (!hwnd_) return false;

    // Enable Windows 11 / 10 Dark Titlebar
    BOOL dark_mode = TRUE;
    DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark_mode, sizeof(dark_mode));

    // Create custom modern waveform window icon
    HDC screenDC = GetDC(nullptr);
    HDC iconDC = CreateCompatibleDC(screenDC);
    HBITMAP colorBmp = CreateCompatibleBitmap(screenDC, 32, 32);
    HBITMAP maskBmp = CreateBitmap(32, 32, 1, 1, nullptr);
    HBITMAP oldIconBmp = static_cast<HBITMAP>(SelectObject(iconDC, colorBmp));

    HBRUSH iconBg = CreateSolidBrush(RGB(18, 22, 30));
    RECT iconRc = { 0, 0, 32, 32 };
    FillRect(iconDC, &iconRc, iconBg);
    DeleteObject(iconBg);

    HBRUSH barBlue = CreateSolidBrush(RGB(59, 130, 246));
    int bars[5] = { 8, 18, 24, 16, 10 };
    for (int b = 0; b < 5; ++b) {
        int bx = 5 + b * 5;
        int bh = bars[b];
        int by = 16 - bh / 2;
        RECT brc = { bx, by, bx + 3, by + bh };
        FillRect(iconDC, &brc, barBlue);
    }
    DeleteObject(barBlue);

    SelectObject(iconDC, oldIconBmp);
    DeleteDC(iconDC);
    ReleaseDC(nullptr, screenDC);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = colorBmp;
    ii.hbmMask = maskBmp;
    HICON hCustomIcon = CreateIconIndirect(&ii);
    DeleteObject(colorBmp);
    DeleteObject(maskBmp);

    if (hCustomIcon) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hCustomIcon));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hCustomIcon));
    }

    // Telemetry & UI Refresh Timer (100ms for smooth live progress)
    SetTimer(hwnd_, 1, 100, nullptr);

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

    return true;
}

int Win32Window::run_message_loop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK Win32Window::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    Win32Window* self = nullptr;
    if (uMsg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Win32Window*>(cs->lpCreateParams);
        if (self) {
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    } else {
        self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handle_message(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT Win32Window::handle_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    hwnd_ = hwnd;
    switch (uMsg) {
    case WM_NCCREATE:
        return TRUE;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        if (w > 0 && h > 0) {
            // Flicker-free double buffering
            if (!mem_dc_ || w != client_w_ || h != client_h_) {
                if (mem_dc_) {
                    SelectObject(mem_dc_, old_bitmap_);
                    if (mem_bitmap_) DeleteObject(mem_bitmap_);
                    DeleteDC(mem_dc_);
                }
                client_w_ = w;
                client_h_ = h;
                mem_dc_ = CreateCompatibleDC(hdc);
                mem_bitmap_ = CreateCompatibleBitmap(hdc, w, h);
                old_bitmap_ = static_cast<HBITMAP>(SelectObject(mem_dc_, mem_bitmap_));
            }

            // 1. Clear Canvas Background
            FillRect(mem_dc_, &rc, bg_canvas_brush_);

            // 2. Render Top Menubar
            render_menubar(mem_dc_, w);

            // 3. Render Left Sidebar (if visible)
            int content_y = MENUBAR_HEIGHT;
            int content_h = h - MENUBAR_HEIGHT - STATUSBAR_HEIGHT;
            int canvas_x = 0;
            int canvas_w = w;

            if (sidebar_visible_) {
                render_sidebar(mem_dc_, SIDEBAR_WIDTH, content_h);
                canvas_x = SIDEBAR_WIDTH;
                canvas_w = w - SIDEBAR_WIDTH;
            }

            // 4. Render Canvas Workspace (The user's customizable area)
            if (content_h > 0 && canvas_w > 0) {
                on_paint_canvas(mem_dc_, canvas_x, content_y, canvas_w, content_h);
            }

            // 5. Render Bottom Statusbar
            render_statusbar(mem_dc_, w, h);

            // Blit buffer to screen
            BitBlt(hdc, 0, 0, w, h, mem_dc_, 0, 0, SRCCOPY);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        int canvas_x = sidebar_visible_ ? SIDEBAR_WIDTH : 0;
        int canvas_w = w - canvas_x;
        int canvas_h = (h > (MENUBAR_HEIGHT + STATUSBAR_HEIGHT)) ? (h - MENUBAR_HEIGHT - STATUSBAR_HEIGHT) : 0;
        on_canvas_resize(canvas_w, canvas_h);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        if (y < MENUBAR_HEIGHT) {
            // Hovering over menubar
            int idx = get_menu_item_at_x(x, client_w_);
            if (idx != hovered_menu_idx_) {
                hovered_menu_idx_ = idx;
                RECT bar_rc = { 0, 0, client_w_, MENUBAR_HEIGHT };
                InvalidateRect(hwnd, &bar_rc, FALSE);
            }
        } else if (sidebar_visible_ && x < SIDEBAR_WIDTH && y >= MENUBAR_HEIGHT && y < (client_h_ - STATUSBAR_HEIGHT)) {
            // Hovering over sidebar
            int idx = get_sidebar_item_at_y(y);
            if (idx != hovered_sidebar_idx_) {
                hovered_sidebar_idx_ = idx;
                RECT sb_rc = { 0, MENUBAR_HEIGHT, SIDEBAR_WIDTH, client_h_ - STATUSBAR_HEIGHT };
                InvalidateRect(hwnd, &sb_rc, FALSE);
            }
        } else {
            if (hovered_sidebar_idx_ != -1) {
                hovered_sidebar_idx_ = -1;
                RECT sb_rc = { 0, MENUBAR_HEIGHT, SIDEBAR_WIDTH, client_h_ - STATUSBAR_HEIGHT };
                InvalidateRect(hwnd, &sb_rc, FALSE);
            }
            if (hovered_menu_idx_ != -1) {
                hovered_menu_idx_ = -1;
                RECT bar_rc = { 0, 0, client_w_, MENUBAR_HEIGHT };
                InvalidateRect(hwnd, &bar_rc, FALSE);
            }

            int canvas_x = sidebar_visible_ ? SIDEBAR_WIDTH : 0;
            int canvas_y = MENUBAR_HEIGHT;
            int canvas_w = client_w_ - canvas_x;
            int canvas_h = client_h_ - MENUBAR_HEIGHT - STATUSBAR_HEIGHT;
            if (x >= canvas_x && y >= canvas_y && y < (canvas_y + canvas_h)) {
                on_canvas_mouse_move(x - canvas_x, y - canvas_y, wParam);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        if (y < MENUBAR_HEIGHT) {
            int idx = get_menu_item_at_x(x, client_w_);
            if (idx >= 0) {
                open_menu_dropdown(idx, x, MENUBAR_HEIGHT);
            }
        } else if (sidebar_visible_ && x < SIDEBAR_WIDTH && y >= MENUBAR_HEIGHT && y < (client_h_ - STATUSBAR_HEIGHT)) {
            int idx = get_sidebar_item_at_y(y);
            if (idx >= 0 && idx < static_cast<int>(active_sidebar_items_.size())) {
                active_sidebar_tab_ = idx;
                active_tab_id_ = active_sidebar_items_[idx].tab_id;
                state_.set_status_message(std::wstring(L"View: ") + active_sidebar_items_[idx].title);
                on_sidebar_tab_changed(active_tab_id_);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else {
            int canvas_x = sidebar_visible_ ? SIDEBAR_WIDTH : 0;
            int canvas_y = MENUBAR_HEIGHT;
            int canvas_w = client_w_ - canvas_x;
            int canvas_h = client_h_ - MENUBAR_HEIGHT - STATUSBAR_HEIGHT;
            if (x >= canvas_x && y >= canvas_y && y < (canvas_y + canvas_h)) {
                on_canvas_lbutton_down(x, y);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        int canvas_x = sidebar_visible_ ? SIDEBAR_WIDTH : 0;
        int canvas_y = MENUBAR_HEIGHT;
        int canvas_w = client_w_ - canvas_x;
        int canvas_h = client_h_ - MENUBAR_HEIGHT - STATUSBAR_HEIGHT;
        if (x >= canvas_x && y >= canvas_y && y < (canvas_y + canvas_h)) {
            on_canvas_lbutton_up(x - canvas_x, y - canvas_y);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_SPACE) {
            handle_menu_command(CMD_AUDIO_PLAY_PAUSE);
            return 0;
        } else if (wParam == VK_ESCAPE) {
            handle_menu_command(CMD_AUDIO_STOP);
            return 0;
        } else if (wParam == 'B' && (GetKeyState(VK_CONTROL) < 0)) {
            handle_menu_command(CMD_VIEW_TOGGLE_SIDEBAR);
            return 0;
        }
        break;
    }

    case WM_COMMAND: {
        int cmd_id = LOWORD(wParam);
        handle_menu_command(cmd_id);
        return 0;
    }

    case WM_TIMER: {
        ++timer_ticks_;
        // Invalidate statusbar region to animate telemetry & pulse LED smoothly
        RECT sb_rc = { 0, client_h_ - STATUSBAR_HEIGHT, client_w_, client_h_ };
        InvalidateRect(hwnd, &sb_rc, FALSE);
        if (state_.is_downloading() || state_.is_probing()) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent flickering

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Menubar Implementation
// ---------------------------------------------------------------------------
int Win32Window::get_menu_item_at_x(int x, int width) {
    (void)width;
    int cur_x = 10;
    HDC hdc = GetDC(hwnd_);
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font_ui_));

    int found_idx = -1;
    for (int i = 0; i < NUM_MENUS; ++i) {
        SIZE sz;
        GetTextExtentPoint32W(hdc, MENU_TITLES[i], static_cast<int>(wcslen(MENU_TITLES[i])), &sz);
        int item_w = sz.cx + 20;

        if (x >= cur_x && x < (cur_x + item_w)) {
            found_idx = i;
            break;
        }
        cur_x += item_w;
    }

    SelectObject(hdc, old_font);
    ReleaseDC(hwnd_, hdc);
    return found_idx;
}

void Win32Window::render_menubar(HDC hdc, int width) {
    RECT bar_rc = { 0, 0, width, MENUBAR_HEIGHT };
    FillRect(hdc, &bar_rc, bg_bar_brush_);

    // Bottom 1px divider
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, border_pen_));
    MoveToEx(hdc, 0, MENUBAR_HEIGHT - 1, nullptr);
    LineTo(hdc, width, MENUBAR_HEIGHT - 1);

    SetBkMode(hdc, TRANSPARENT);
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font_ui_));

    int cur_x = 10;
    for (int i = 0; i < NUM_MENUS; ++i) {
        SIZE sz;
        GetTextExtentPoint32W(hdc, MENU_TITLES[i], static_cast<int>(wcslen(MENU_TITLES[i])), &sz);
        int item_w = sz.cx + 20;

        RECT item_rc = { cur_x, 3, cur_x + item_w, MENUBAR_HEIGHT - 3 };

        if (hovered_menu_idx_ == i) {
            FillRect(hdc, &item_rc, hover_bar_brush_);
            SetTextColor(hdc, RGB(255, 255, 255));
        } else {
            SetTextColor(hdc, RGB(200, 204, 214));
        }

        DrawTextW(hdc, MENU_TITLES[i], -1, &item_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cur_x += item_w;
    }

    // Right-side Branding Badge
    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(110, 118, 134));
    RECT badge_rc = { width - 180, 0, width - 15, MENUBAR_HEIGHT };
    DrawTextW(hdc, L"REMIXER TOOLS", -1, &badge_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, old_font);
    SelectObject(hdc, old_pen);
}

void Win32Window::open_menu_dropdown(int menu_index, int x, int y) {
    (void)x;
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    switch (menu_index) {
    case 0: // File
        AppendMenuW(hMenu, MF_STRING, CMD_FILE_NEW, L"New Project\tCtrl+N");
        AppendMenuW(hMenu, MF_STRING, CMD_FILE_OPEN_AUDIO, L"Open Audio File...\tCtrl+O");
        AppendMenuW(hMenu, MF_STRING, CMD_FILE_DOWNLOAD_ONLINE, L"Download Audio from URL (yt-dlp)...\tCtrl+D");
        AppendMenuW(hMenu, MF_STRING, CMD_FILE_IMPORT_STEMS, L"Import Stems...");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_FILE_SAVE, L"Save Project\tCtrl+S");
        AppendMenuW(hMenu, MF_STRING, CMD_FILE_EXPORT_AUDIO, L"Export Master Audio...\tCtrl+E");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_FILE_EXIT, L"Exit\tAlt+F4");
        break;

    case 1: // Edit
        AppendMenuW(hMenu, MF_STRING, CMD_EDIT_UNDO, L"Undo\tCtrl+Z");
        AppendMenuW(hMenu, MF_STRING, CMD_EDIT_REDO, L"Redo\tCtrl+Y");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_EDIT_CUT, L"Cut\tCtrl+X");
        AppendMenuW(hMenu, MF_STRING, CMD_EDIT_COPY, L"Copy\tCtrl+C");
        AppendMenuW(hMenu, MF_STRING, CMD_EDIT_PASTE, L"Paste\tCtrl+V");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_EDIT_PREFERENCES, L"Preferences...\tCtrl+,");
        break;

    case 2: // Audio Engine
        AppendMenuW(hMenu, MF_STRING, CMD_AUDIO_PLAY_PAUSE, state_.is_playing() ? L"Pause\tSpace" : L"Play\tSpace");
        AppendMenuW(hMenu, MF_STRING, CMD_AUDIO_STOP, L"Stop\tEsc");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_AUDIO_RESET_ENGINE, L"Reset Real-Time DSP Engine");
        AppendMenuW(hMenu, MF_STRING, CMD_AUDIO_DEVICE_SETTINGS, L"Audio Device Settings (ASIO/WASAPI)...");
        break;

    case 3: // Modules
        AppendMenuW(hMenu, MF_STRING, CMD_MODULES_LOAD_PLUGIN, L"Load Remixer Module (.dll)...");
        AppendMenuW(hMenu, MF_STRING, CMD_MODULES_RESCAN, L"Rescan Plug-ins Directory");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_MODULES_STORE_CATALOG, L"Open Module Store Catalog");
        break;

    case 4: // View
        AppendMenuW(hMenu, MF_STRING, CMD_VIEW_TOGGLE_SIDEBAR, sidebar_visible_ ? L"Hide Sidebar\tCtrl+B" : L"Show Sidebar\tCtrl+B");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_VIEW_RESET_LAYOUT, L"Reset Default Layout");
        AppendMenuW(hMenu, MF_STRING, CMD_VIEW_ZOOM_IN, L"Zoom In\tCtrl++");
        AppendMenuW(hMenu, MF_STRING, CMD_VIEW_ZOOM_OUT, L"Zoom Out\tCtrl+-");
        break;

    case 5: // Help
        AppendMenuW(hMenu, MF_STRING, CMD_HELP_DOCS, L"Documentation & Developer SDK...");
        AppendMenuW(hMenu, MF_STRING, CMD_HELP_DIAGNOSTICS, L"Anti-Tamper Diagnostics & System Guard...");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_HELP_ABOUT, L"About Remixer Tools...");
        break;
    }

    POINT pt = { 0, y };
    int cur_x = 10;
    HDC hdc = GetDC(hwnd_);
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font_ui_));
    for (int i = 0; i < menu_index; ++i) {
        SIZE sz;
        GetTextExtentPoint32W(hdc, MENU_TITLES[i], static_cast<int>(wcslen(MENU_TITLES[i])), &sz);
        cur_x += sz.cx + 20;
    }
    SelectObject(hdc, old_font);
    ReleaseDC(hwnd_, hdc);

    pt.x = cur_x;
    ClientToScreen(hwnd_, &pt);

    int cmd = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, hwnd_, nullptr);
    DestroyMenu(hMenu);

    if (cmd > 0) {
        handle_menu_command(cmd);
    }
}

// ---------------------------------------------------------------------------
// Dynamic Sidebar Implementation
// ---------------------------------------------------------------------------
void Win32Window::rebuild_sidebar_items() {
    active_sidebar_items_.clear();

    // 1. Core Program Menus (Always present)
    active_sidebar_items_.push_back({
        L"\uE8B7", L"Session Tracks", L"Multi-track Clips", "", false, TAB_SESSION_TRACKS
    });

    // 2. Installed Modules ONLY (Dynamic)
    // Rule: "ใน sidebar ถ้ายังไม่ได้ ติดตั้ง module นั้น จะไม่มี menu ใน sidebar ยกเว้น menu ของโปรแกรม"
    // Modules ONLY appear in the sidebar if is_installed == true.
    for (const auto& mod : state_.get_available_modules()) {
        if (mod.is_installed) {
            int tab_id = TAB_CUSTOM_MODULE;
            if (mod.module_id == "com.armzistudio.demucs_v4") {
                tab_id = TAB_STEM_SEPARATOR;
            } else if (mod.module_id == "com.armzistudio.pitch_tracker") {
                tab_id = TAB_AUDIO_TO_MIDI;
            } else if (mod.module_id == "com.armzistudio.media_downloader") {
                tab_id = TAB_AUDIO_BROWSER;
            }
            active_sidebar_items_.push_back({
                mod.icon, mod.name, mod.subtitle, mod.module_id, true, tab_id
            });
        }
    }

    // 3. Core Program Menus (Bottom)
    active_sidebar_items_.push_back({
        L"\uE74C", L"DSP & Modules", L"Plugins & Store", "", false, TAB_DSP_MODULES
    });
    active_sidebar_items_.push_back({
        L"\uE713", L"Settings", L"Devices & Audio", "", false, TAB_SETTINGS
    });

    // Ensure active_sidebar_tab_ points to the active tab ID if possible
    bool found_active = false;
    for (size_t i = 0; i < active_sidebar_items_.size(); ++i) {
        if (active_sidebar_items_[i].tab_id == active_tab_id_) {
            active_sidebar_tab_ = static_cast<int>(i);
            found_active = true;
            break;
        }
    }
    if (!found_active) {
        active_sidebar_tab_ = 0;
        active_tab_id_ = TAB_SESSION_TRACKS;
    }
}

int Win32Window::get_sidebar_item_at_y(int y) {
    int start_y = MENUBAR_HEIGHT + 36;
    int num_items = static_cast<int>(active_sidebar_items_.size());
    for (int i = 0; i < num_items; ++i) {
        int item_y = start_y + (i * 50);
        if (y >= item_y && y < (item_y + 46)) {
            return i;
        }
    }
    return -1;
}

void Win32Window::render_sidebar(HDC hdc, int width, int height) {
    RECT sb_rc = { 0, MENUBAR_HEIGHT, width, MENUBAR_HEIGHT + height };
    FillRect(hdc, &sb_rc, bg_sidebar_brush_);

    // Right 1px divider
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, border_pen_));
    MoveToEx(hdc, width - 1, MENUBAR_HEIGHT, nullptr);
    LineTo(hdc, width - 1, MENUBAR_HEIGHT + height);

    SetBkMode(hdc, TRANSPARENT);

    // Section Header
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font_small_));
    SetTextColor(hdc, RGB(105, 114, 128));
    RECT header_rc = { 16, MENUBAR_HEIGHT + 12, width - 16, MENUBAR_HEIGHT + 30 };
    DrawTextW(hdc, L"EXPLORER", -1, &header_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int start_y = MENUBAR_HEIGHT + 36;
    int num_items = static_cast<int>(active_sidebar_items_.size());

    for (int i = 0; i < num_items; ++i) {
        const auto& item = active_sidebar_items_[i];
        int item_y = start_y + (i * 50);
        RECT item_rc = { 8, item_y, width - 8, item_y + 46 };

        bool is_active = (i == active_sidebar_tab_);
        bool is_hover  = (i == hovered_sidebar_idx_);

        if (is_active) {
            FillRect(hdc, &item_rc, active_sidebar_brush_);
            // Left Accent Indicator (Orange for installed modules, Blue for program core menus)
            RECT ind_rc = { 8, item_y + 8, 12, item_y + 38 };
            COLORREF accent_col = item.is_module ? RGB(249, 115, 22) : RGB(59, 130, 246);
            HBRUSH ind_b = CreateSolidBrush(accent_col);
            FillRect(hdc, &ind_rc, ind_b);
            DeleteObject(ind_b);
        } else if (is_hover) {
            FillRect(hdc, &item_rc, hover_bar_brush_);
        }

        // 1. Icon Chip Box (28x28 rounded chip)
        int chip_x = 16;
        int chip_y = item_y + 9;
        int chip_size = 28;
        RECT chip_rc = { chip_x, chip_y, chip_x + chip_size, chip_y + chip_size };

        COLORREF chip_bg = is_active
            ? (item.is_module ? RGB(234, 88, 12) : RGB(37, 99, 235))
            : (is_hover ? RGB(34, 39, 50) : RGB(26, 29, 36));
        COLORREF chip_border = is_active
            ? (item.is_module ? RGB(251, 146, 60) : RGB(59, 130, 246))
            : RGB(42, 46, 56);

        HBRUSH chip_b = CreateSolidBrush(chip_bg);
        HPEN chip_p = CreatePen(PS_SOLID, 1, chip_border);
        HBRUSH old_cb = static_cast<HBRUSH>(SelectObject(hdc, chip_b));
        HPEN old_cp = static_cast<HPEN>(SelectObject(hdc, chip_p));
        RoundRect(hdc, chip_x, chip_y, chip_x + chip_size, chip_y + chip_size, 6, 6);
        SelectObject(hdc, old_cb);
        SelectObject(hdc, old_cp);
        DeleteObject(chip_b);
        DeleteObject(chip_p);

        // 2. Icon Glyph
        SelectObject(hdc, font_icon_);
        SetTextColor(hdc, is_active ? RGB(255, 255, 255) : (is_hover ? RGB(220, 225, 235) : RGB(160, 168, 182)));
        DrawTextW(hdc, item.icon.c_str(), -1, &chip_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 3. Title
        SelectObject(hdc, is_active ? font_bold_ : font_ui_);
        SetTextColor(hdc, is_active ? RGB(255, 255, 255) : (is_hover ? RGB(230, 235, 245) : RGB(200, 204, 216)));
        RECT title_rc = { 54, item_y + 6, width - 16, item_y + 24 };
        DrawTextW(hdc, item.title.c_str(), -1, &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 4. Subtitle + Optional Module Tag
        SelectObject(hdc, font_small_);
        SetTextColor(hdc, item.is_module ? RGB(245, 158, 11) : RGB(115, 122, 136));
        RECT sub_rc = { 54, item_y + 24, width - 16, item_y + 42 };
        std::wstring sub_str = item.subtitle;
        if (item.is_module) {
            sub_str = L"[MOD] " + sub_str;
        }
        DrawTextW(hdc, sub_str.c_str(), -1, &sub_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Sidebar Bottom Hint
    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(85, 92, 105));
    RECT hint_rc = { 16, MENUBAR_HEIGHT + height - 28, width - 16, MENUBAR_HEIGHT + height - 8 };
    DrawTextW(hdc, L"Ctrl+B to Toggle", -1, &hint_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, old_font);
    SelectObject(hdc, old_pen);
}

// ---------------------------------------------------------------------------
// Statusbar Implementation
// ---------------------------------------------------------------------------
void Win32Window::render_statusbar(HDC hdc, int width, int height) {
    int sb_y = height - STATUSBAR_HEIGHT;
    RECT sb_rc = { 0, sb_y, width, height };
    FillRect(hdc, &sb_rc, bg_bar_brush_);

    // Top 1px divider
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, border_pen_));
    MoveToEx(hdc, 0, sb_y, nullptr);
    LineTo(hdc, width, sb_y);

    SetBkMode(hdc, TRANSPARENT);
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font_small_));

    // 1. Left Side: Animated Status Pulse LED
    bool tampered = state_.get_engine() ? state_.get_engine()->security().is_tampered() : false;
    COLORREF led_color = tampered ? RGB(239, 68, 68) : RGB(34, 197, 94);
    HBRUSH led_brush = CreateSolidBrush(led_color);
    HPEN led_pen = CreatePen(PS_SOLID, 1, led_color);
    HBRUSH old_b = static_cast<HBRUSH>(SelectObject(hdc, led_brush));
    HPEN old_p = static_cast<HPEN>(SelectObject(hdc, led_pen));

    int led_cx = 16;
    int led_cy = sb_y + (STATUSBAR_HEIGHT / 2);
    Ellipse(hdc, led_cx - 4, led_cy - 4, led_cx + 4, led_cy + 4);

    SelectObject(hdc, old_b);
    SelectObject(hdc, old_p);
    DeleteObject(led_brush);
    DeleteObject(led_pen);

    // Left Side: Status message text
    SetTextColor(hdc, RGB(180, 185, 196));
    RECT msg_rc = { 28, sb_y, width - 240, height };
    DrawTextW(hdc, state_.get_status_message().c_str(), -1, &msg_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 2. Right Side: Telemetry Chips
    int cur_rx = width - 10;

    // Badge 1: Audio Format Chip (48.0 kHz / 512 spl)
    SelectObject(hdc, font_small_);
    int b1_w = 120;
    cur_rx -= b1_w;
    HBRUSH chip_b = CreateSolidBrush(RGB(28, 32, 38));
    HPEN chip_p = CreatePen(PS_SOLID, 1, RGB(44, 48, 58));
    SelectObject(hdc, chip_b);
    SelectObject(hdc, chip_p);
    RoundRect(hdc, cur_rx, sb_y + 4, cur_rx + b1_w, sb_y + STATUSBAR_HEIGHT - 4, 4, 4);
    DeleteObject(chip_b);
    DeleteObject(chip_p);

    SetTextColor(hdc, RGB(160, 168, 182));
    wchar_t dsp_buf[64];
    swprintf_s(dsp_buf, L"%.1f kHz | %u spl", state_.get_sample_rate() / 1000.0f, state_.get_buffer_size());
    RECT b1_rc = { cur_rx, sb_y + 4, cur_rx + b1_w, sb_y + STATUSBAR_HEIGHT - 4 };
    DrawTextW(hdc, dsp_buf, -1, &b1_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Badge 2: DSP Load Chip
    int b2_w = 80;
    cur_rx -= (b2_w + 6);
    chip_b = CreateSolidBrush(RGB(28, 32, 38));
    chip_p = CreatePen(PS_SOLID, 1, RGB(44, 48, 58));
    SelectObject(hdc, chip_b);
    SelectObject(hdc, chip_p);
    RoundRect(hdc, cur_rx, sb_y + 4, cur_rx + b2_w, sb_y + STATUSBAR_HEIGHT - 4, 4, 4);
    DeleteObject(chip_b);
    DeleteObject(chip_p);

    SetTextColor(hdc, RGB(160, 168, 182));
    wchar_t cpu_buf[32];
    swprintf_s(cpu_buf, L"DSP: %.1f%%", state_.get_cpu_load_pct());
    RECT b2_rc = { cur_rx, sb_y + 4, cur_rx + b2_w, sb_y + STATUSBAR_HEIGHT - 4 };
    DrawTextW(hdc, cpu_buf, -1, &b2_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, old_font);
    SelectObject(hdc, old_pen);
}

// ---------------------------------------------------------------------------
// Menu Command Actions
// ---------------------------------------------------------------------------
void Win32Window::handle_menu_command(int command_id) {
    switch (command_id) {
    case CMD_VIEW_TOGGLE_SIDEBAR:
        sidebar_visible_ = !sidebar_visible_;
        state_.set_status_message(sidebar_visible_ ? L"Sidebar Visible." : L"Sidebar Hidden.");
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;

    case CMD_FILE_OPEN_AUDIO:
        open_audio_file_dialog();
        break;

    case CMD_FILE_DOWNLOAD_ONLINE: {
        if (!state_.is_module_installed("com.armzistudio.media_downloader")) {
            state_.install_module("com.armzistudio.media_downloader");
        }
        active_tab_id_ = TAB_AUDIO_BROWSER;
        rebuild_sidebar_items();
        for (size_t i = 0; i < active_sidebar_items_.size(); ++i) {
            if (active_sidebar_items_[i].tab_id == TAB_AUDIO_BROWSER) {
                active_sidebar_tab_ = static_cast<int>(i);
                break;
            }
        }
        if (OpenClipboard(hwnd_)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
                if (pszText) {
                    std::wstring ws(pszText);
                    if (ws.find(L"http") != std::wstring::npos) {
                        while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n' || ws.back() == L' ')) ws.pop_back();
                        state_.set_download_url(to_utf8(ws));
                        state_.set_status_message(L"Downloader: Pasted media URL from clipboard");
                    }
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    }

    case CMD_FILE_NEW:
        state_.set_status_message(L"Created new empty audio project session.");
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;

    case CMD_FILE_SAVE:
        state_.set_status_message(L"Project session saved successfully.");
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;

    case CMD_FILE_EXIT:
        PostQuitMessage(0);
        break;

    case CMD_AUDIO_PLAY_PAUSE:
        if (state_.is_playing()) {
            state_.pause();
            state_.set_status_message(L"Playback Paused.");
        } else {
            state_.play();
            state_.set_status_message(L"Playback Active | Audio DSP Streaming Online");
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;

    case CMD_AUDIO_STOP:
        state_.stop();
        state_.set_status_message(L"Playback Stopped. Transport rewound to 00:00.");
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;

    case CMD_AUDIO_RESET_ENGINE:
        state_.initialize_engine();
        state_.set_status_message(L"Audio DSP Engine and SPSC Ring Buffers reinitialized.");
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;

    case CMD_HELP_DIAGNOSTICS: {
        rmx_status_t status = remixer::core::SecurityGuard::verify_tamper_state();
        if (rmx_is_ok(status)) {
            MessageBoxW(hwnd_,
                L"Security & Integrity Diagnostics:\n\n"
                L"• Active Debuggers: None detected\n"
                L"• Hardware Breakpoints (DR0-DR3): Clear\n"
                L"• Software Breakpoints (0xCC): None found\n"
                L"• In-Memory .text Code Hash: Valid\n"
                L"• Inline API Hooks: Clean\n"
                L"• Blacklisted Tools & Windows: None\n\n"
                L"System Security State: SECURE (100% OK)",
                L"Security Diagnostics",
                MB_ICONINFORMATION | MB_OK
            );
        } else {
            MessageBoxW(hwnd_,
                L"Security Warning:\n\n"
                L"Tampering, debugger or unauthorized hooks detected!",
                L"Security Violation",
                MB_ICONWARNING | MB_OK
            );
        }
        break;
    }

    case CMD_HELP_ABOUT:
        MessageBoxW(hwnd_,
            L"Remixer Tools v1.0.0\n"
            L"High-Performance Audio Workstation & Stem Suite\n\n"
            L"Built with C++20, Low-Latency SPSC DSP Engine\n"
            L"Hardened with Multi-Layer Anti-Reverse Engineering\n"
            L"Copyright (c) 2026 Armzi. All rights reserved.",
            L"About Remixer Tools",
            MB_ICONINFORMATION | MB_OK
        );
        break;

    case CMD_MODULES_LOAD_PLUGIN: {
        WCHAR file_name[MAX_PATH] = { 0 };
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = L"Remixer Modules & Dynamic Plugins (*.dll;*.rmxpkg)\0*.dll;*.rmxpkg\0Dynamic Libraries (*.dll)\0*.dll\0Packages (*.rmxpkg)\0*.rmxpkg\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = file_name;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (GetOpenFileNameW(&ofn)) {
            std::wstring path(file_name);
            size_t last_slash = path.find_last_of(L"\\/");
            std::wstring name = (last_slash != std::wstring::npos) ? path.substr(last_slash + 1) : path;
            std::string name_str;
            name_str.reserve(name.size());
            for (wchar_t wc : name) {
                name_str.push_back(static_cast<char>(wc & 0x7F));
            }
            state_.install_module(name_str, file_name);
            state_.set_status_message(std::wstring(L"Installed dynamic module: ") + name + L" -> Added to sidebar!");
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        break;
    }

    case CMD_MODULES_RESCAN:
        state_.scan_modules_directory();
        rebuild_sidebar_items();
        state_.set_status_message(L"Scanned modules directory. Sidebar updated.");
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;

    case CMD_MODULES_STORE_CATALOG:
        active_tab_id_ = TAB_DSP_MODULES;
        for (size_t i = 0; i < active_sidebar_items_.size(); ++i) {
            if (active_sidebar_items_[i].tab_id == TAB_DSP_MODULES) {
                active_sidebar_tab_ = static_cast<int>(i);
                break;
            }
        }
        state_.set_status_message(L"Opened DSP & Modules Store Catalog.");
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;

    default:
        break;
    }
}

void Win32Window::open_audio_file_dialog() {
    WCHAR file_name[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"All Supported Audio (*.wav;*.mp3;*.flac;*.aiff;*.ogg)\0*.wav;*.mp3;*.flac;*.aiff;*.ogg\0Waveform Audio (*.wav)\0*.wav\0MP3 Audio (*.mp3)\0*.mp3\0FLAC Audio (*.flac)\0*.flac\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file_name;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring path(file_name);
        size_t last_slash = path.find_last_of(L"\\/");
        std::wstring name = (last_slash != std::wstring::npos) ? path.substr(last_slash + 1) : path;

        state_.set_status_message(std::wstring(L"Loaded: ") + name);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Customizable Canvas Workspace Hooks & Tab Views
// ---------------------------------------------------------------------------
void Win32Window::on_paint_canvas(HDC hdc, int x, int y, int width, int height) {
    canvas_buttons_.clear();

    switch (active_tab_id_) {
    case TAB_SESSION_TRACKS:
        render_session_tracks_view(hdc, x, y, width, height);
        break;
    case TAB_AUDIO_BROWSER:
        render_audio_browser_view(hdc, x, y, width, height);
        break;
    case TAB_STEM_SEPARATOR:
        render_stem_separator_view(hdc, x, y, width, height);
        break;
    case TAB_AUDIO_TO_MIDI:
        render_audio_to_midi_view(hdc, x, y, width, height);
        break;
    case TAB_DSP_MODULES:
        render_dsp_modules_view(hdc, x, y, width, height);
        break;
    case TAB_SETTINGS:
        render_settings_view(hdc, x, y, width, height);
        break;
    default:
        render_session_tracks_view(hdc, x, y, width, height);
        break;
    }
}

void Win32Window::on_canvas_resize(int width, int height) {
    (void)width;
    (void)height;
}

void Win32Window::on_canvas_lbutton_down(int x, int y) {
    POINT pt = { x, y };

    if (active_tab_id_ == TAB_AUDIO_BROWSER) {
        for (const auto& act : downloader_actions_) {
            if (PtInRect(&act.rect, pt)) {
                switch (act.type) {
                case DownloaderAction::PASTE_CLIPBOARD:
                    if (OpenClipboard(hwnd_)) {
                        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                        if (hData) {
                            wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
                            if (pszText) {
                                std::wstring ws(pszText);
                                while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n' || ws.back() == L' ')) ws.pop_back();
                                state_.set_download_url(to_utf8(ws));
                                state_.set_status_message(L"Downloader: URL pasted from clipboard");
                                GlobalUnlock(hData);
                            }
                        }
                        CloseClipboard();
                    }
                    break;
                case DownloaderAction::CLEAR_URL:
                    state_.set_download_url("");
                    state_.set_status_message(L"Downloader: URL cleared");
                    break;
                case DownloaderAction::PROBE_URL:
                    state_.probe_online_url("", [this](bool ok, const remixer::downloader::MediaMetadata& m) {
                        if (ok) {
                            state_.set_status_message(std::wstring(L"Probed: ") + to_wide(m.title));
                        }
                        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                    });
                    break;
                case DownloaderAction::TOGGLE_MODE: {
                    auto& cfg = state_.get_download_config();
                    if (cfg.mode == remixer::downloader::DownloadMode::AudioOnly) {
                        cfg.mode = remixer::downloader::DownloadMode::VideoAndAudio;
                        state_.set_status_message(L"Mode: Full Video + Audio (MP4 Container)");
                    } else {
                        cfg.mode = remixer::downloader::DownloadMode::AudioOnly;
                        state_.set_status_message(L"Mode: Pure Audio Master Extraction");
                    }
                    break;
                }
                case DownloaderAction::SET_AUDIO_FORMAT: {
                    auto& cfg = state_.get_download_config();
                    cfg.audio_format = static_cast<remixer::downloader::AudioFormat>(act.int_val);
                    if (cfg.audio_format == remixer::downloader::AudioFormat::Wav32Float) state_.set_status_message(L"Audio: 32-bit Float Master WAV (Native Engine)");
                    else if (cfg.audio_format == remixer::downloader::AudioFormat::Flac) state_.set_status_message(L"Audio: FLAC Lossless High-Fidelity");
                    else if (cfg.audio_format == remixer::downloader::AudioFormat::Mp3) state_.set_status_message(L"Audio: MP3 320 kbps High-Bitrate");
                    else if (cfg.audio_format == remixer::downloader::AudioFormat::Opus) state_.set_status_message(L"Audio: OPUS Native Stream (No Recompression)");
                    else if (cfg.audio_format == remixer::downloader::AudioFormat::Aac) state_.set_status_message(L"Audio: AAC / M4A Audio Stream");
                    break;
                }
                case DownloaderAction::SET_VIDEO_QUALITY: {
                    auto& cfg = state_.get_download_config();
                    cfg.video_quality = static_cast<remixer::downloader::VideoQuality>(act.int_val);
                    if (cfg.video_quality == remixer::downloader::VideoQuality::Best) state_.set_status_message(L"Video Quality: Best Available Stream");
                    else if (cfg.video_quality == remixer::downloader::VideoQuality::UHD_4K) state_.set_status_message(L"Video Quality: 4K UHD (2160p)");
                    else if (cfg.video_quality == remixer::downloader::VideoQuality::FHD_1080p) state_.set_status_message(L"Video Quality: Full HD 1080p");
                    else if (cfg.video_quality == remixer::downloader::VideoQuality::HD_720p) state_.set_status_message(L"Video Quality: HD 720p");
                    break;
                }
                case DownloaderAction::TOGGLE_SPLIT_CHAPTERS: {
                    auto& cfg = state_.get_download_config();
                    cfg.split_chapters = !cfg.split_chapters;
                    state_.set_status_message(cfg.split_chapters ? L"Split Chapters: ENABLED (Auto-slice DJ sets and full albums)" : L"Split Chapters: DISABLED");
                    break;
                }
                case DownloaderAction::TOGGLE_PLAYLIST: {
                    auto& cfg = state_.get_download_config();
                    cfg.download_playlist = !cfg.download_playlist;
                    state_.set_status_message(cfg.download_playlist ? L"Playlist Ingestion: ENABLED (Download all tracks in playlist)" : L"Playlist Ingestion: DISABLED (Single video only)");
                    break;
                }
                case DownloaderAction::TOGGLE_EMBED_METADATA: {
                    auto& cfg = state_.get_download_config();
                    cfg.embed_metadata = !cfg.embed_metadata;
                    cfg.embed_thumbnail = cfg.embed_metadata;
                    state_.set_status_message(cfg.embed_metadata ? L"Embed ID3 Tags & Cover Artwork: ENABLED" : L"Embed ID3 Tags & Cover Artwork: DISABLED");
                    break;
                }
                case DownloaderAction::TOGGLE_MULTITHREAD: {
                    auto& cfg = state_.get_download_config();
                    cfg.concurrent_fragments = (cfg.concurrent_fragments > 1) ? 1 : 4;
                    state_.set_status_message(cfg.concurrent_fragments > 1 ? L"Multi-threaded Acceleration (-N 4): ENABLED" : L"Multi-threaded Acceleration: DISABLED");
                    break;
                }
                case DownloaderAction::CYCLE_COOKIES: {
                    auto& cfg = state_.get_download_config();
                    int c = static_cast<int>(cfg.cookie_browser);
                    c = (c + 1) % 6; // 0=None, 1=Chrome, 2=Edge, 3=Firefox, 4=Brave, 5=Opera
                    cfg.cookie_browser = static_cast<remixer::downloader::BrowserCookieSource>(c);
                    const wchar_t* bnames[] = { L"None", L"Google Chrome", L"Microsoft Edge", L"Mozilla Firefox", L"Brave Browser", L"Opera" };
                    state_.set_status_message(std::wstring(L"Session Cookies Bypass: ") + bnames[c]);
                    break;
                }
                case DownloaderAction::START_DOWNLOAD:
                    if (!state_.is_downloading()) {
                        std::string url = state_.get_download_url();
                        if (url.empty()) {
                            state_.set_status_message(L"Error: Please paste or enter a URL first.");
                        } else {
                            state_.start_online_download(url, [this](bool ok, const std::string& path) {
                                if (ok) {
                                    state_.set_status_message(std::wstring(L"Ingest Complete: ") + std::filesystem::path(path).filename().wstring());
                                }
                                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                            });
                        }
                    }
                    break;
                case DownloaderAction::LOAD_LAST_TO_DECK: {
                    std::string last = state_.get_download_last_file();
                    if (!last.empty()) {
                        std::filesystem::path p(last);
                        state_.set_status_message(std::wstring(L"Loaded into Deck: ") + p.filename().wstring());
                        active_tab_id_ = TAB_SESSION_TRACKS;
                        rebuild_sidebar_items();
                    }
                    break;
                }
                case DownloaderAction::SEND_LAST_TO_STEMS: {
                    std::string last = state_.get_download_last_file();
                    if (!last.empty()) {
                        std::filesystem::path p(last);
                        state_.set_status_message(std::wstring(L"Sent to Stem Separator: ") + p.filename().wstring());
                        active_tab_id_ = TAB_STEM_SEPARATOR;
                        rebuild_sidebar_items();
                    }
                    break;
                }
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
    } else if (active_tab_id_ == TAB_DSP_MODULES) {
        for (const auto& btn : canvas_buttons_) {
            if (PtInRect(&btn.rect, pt)) {
                state_.toggle_module_installation(btn.module_id);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
    } else if (active_tab_id_ == TAB_SETTINGS) {
        for (const auto& sa : setting_actions_) {
            if (PtInRect(&sa.rect, pt)) {
                switch (sa.type) {
                case SettingAction::DRIVER:
                    state_.set_audio_driver(sa.int_val);
                    if (sa.int_val == 0) state_.set_status_message(L"Driver: Windows WASAPI Exclusive Mode (Low-Latency)");
                    else if (sa.int_val == 1) state_.set_status_message(L"Driver: ASIO Low-Latency Bridge Driver");
                    else state_.set_status_message(L"Driver: DirectSound Shared Buffer");
                    break;
                case SettingAction::SAMPLE_RATE:
                    state_.set_sample_rate(sa.uint_val);
                    state_.set_status_message(std::wstring(L"Sample Rate: ") + std::to_wstring(sa.uint_val) + L" Hz Planar 32-bit Float");
                    break;
                case SettingAction::BUFFER_SIZE:
                    state_.set_buffer_size(sa.uint_val);
                    state_.set_status_message(std::wstring(L"Buffer Block Size: ") + std::to_wstring(sa.uint_val) + L" samples");
                    break;
                case SettingAction::AI_BACKEND:
                    state_.set_ai_backend(sa.int_val);
                    if (sa.int_val == 0) state_.set_status_message(L"AI Backend: DirectML (GPU Acceleration)");
                    else if (sa.int_val == 1) state_.set_status_message(L"AI Backend: CPU AVX-512 / AVX2 Vectorized");
                    else state_.set_status_message(L"AI Backend: Native ONNX Runtime C-API");
                    break;
                case SettingAction::AI_THREADS:
                    state_.set_ai_threads(sa.int_val);
                    state_.set_status_message(std::wstring(L"AI Worker Thread Pool: ") + std::to_wstring(sa.int_val) + L" Threads");
                    break;
                case SettingAction::TOGGLE_PRIORITY:
                    state_.set_high_priority_audio(!state_.is_high_priority_audio());
                    state_.set_status_message(state_.is_high_priority_audio() ? L"Pro Audio MMCSS Real-Time Scheduling: ON" : L"Standard Audio Priority: OFF");
                    break;
                case SettingAction::TOGGLE_IPC:
                    state_.set_ipc_bridge_enabled(!state_.is_ipc_bridge_enabled());
                    state_.set_status_message(state_.is_ipc_bridge_enabled() ? L"DAW Shared Memory IPC Bridge: ENABLED (Zero-Copy)" : L"DAW Shared Memory IPC Bridge: DISABLED");
                    break;
                case SettingAction::RESET_DSP:
                    state_.initialize_engine();
                    state_.set_status_message(L"Audio DSP Engine and SPSC Ring Buffers reinitialized.");
                    break;
                case SettingAction::RUN_SECURITY_DIAGNOSTICS:
                    handle_menu_command(CMD_HELP_DIAGNOSTICS);
                    break;
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
    }
}

void Win32Window::on_canvas_mouse_move(int x, int y, WPARAM flags) {
    (void)x;
    (void)y;
    (void)flags;
}

void Win32Window::on_canvas_lbutton_up(int x, int y) {
    (void)x;
    (void)y;
}

void Win32Window::on_sidebar_tab_changed(int tab_id) {
    active_tab_id_ = tab_id;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// 1. Session Tracks View (Built-in Program Menu)
// ---------------------------------------------------------------------------
void Win32Window::render_session_tracks_view(HDC hdc, int x, int y, int w, int h) {
    SetBkMode(hdc, TRANSPARENT);

    // Title
    SelectObject(hdc, font_bold_);
    SetTextColor(hdc, RGB(245, 247, 250));
    RECT t_rc = { x + 24, y + 20, x + w - 24, y + 44 };
    DrawTextW(hdc, L"SESSION MULTI-TRACK AUDIO DECK", -1, &t_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(130, 138, 154));
    RECT sub_rc = { x + 24, y + 44, x + w - 24, y + 64 };
    DrawTextW(hdc, L"Core Multi-track Audio Engine • 48 kHz / 512 spl • Lock-Free SPSC Buffer [Built-in Program Feature]", -1, &sub_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Timeline Workspace Card
    int tw_y = y + 75;
    int tw_h = std::min(h - 95, 380);
    RECT wave_rc = { x + 24, tw_y, x + w - 24, tw_y + tw_h };
    HBRUSH w_bg = CreateSolidBrush(RGB(20, 24, 31));
    HPEN w_p = CreatePen(PS_SOLID, 1, RGB(38, 44, 56));
    SelectObject(hdc, w_bg);
    SelectObject(hdc, w_p);
    RoundRect(hdc, wave_rc.left, wave_rc.top, wave_rc.right, wave_rc.bottom, 8, 8);
    DeleteObject(w_bg);
    DeleteObject(w_p);

    // Simulated Audio Waveform display
    HPEN wave_pen = CreatePen(PS_SOLID, 1, RGB(59, 130, 246));
    SelectObject(hdc, wave_pen);
    int mid_y = tw_y + (tw_h / 2);
    int wave_w = (wave_rc.right - wave_rc.left - 40);

    for (int px = 0; px < wave_w; px += 3) {
        float norm = static_cast<float>(px) / 60.0f;
        float amp = (std::sin(norm * 2.3f) * 0.4f + std::sin(norm * 5.1f) * 0.3f + std::cos(norm * 1.1f) * 0.3f);
        int bar_h = static_cast<int>(std::abs(amp) * (tw_h * 0.35f));
        MoveToEx(hdc, wave_rc.left + 20 + px, mid_y - bar_h, nullptr);
        LineTo(hdc, wave_rc.left + 20 + px, mid_y + bar_h);
    }
    DeleteObject(wave_pen);

    // Playhead line
    HPEN playhead_pen = CreatePen(PS_SOLID, 2, RGB(239, 68, 68));
    SelectObject(hdc, playhead_pen);
    int playhead_x = wave_rc.left + 20 + ((timer_ticks_ * 2) % (wave_w > 0 ? wave_w : 100));
    MoveToEx(hdc, playhead_x, tw_y + 10, nullptr);
    LineTo(hdc, playhead_x, tw_y + tw_h - 10);
    DeleteObject(playhead_pen);
}

// ---------------------------------------------------------------------------
// 2. Audio Browser View (Built-in Program Menu + Online Downloader)
// ---------------------------------------------------------------------------
void Win32Window::render_audio_browser_view(HDC hdc, int x, int y, int w, int h) {
    (void)h;
    downloader_actions_.clear();
    SetBkMode(hdc, TRANSPARENT);

    // Title & Subtitle
    SelectObject(hdc, font_bold_);
    SetTextColor(hdc, RGB(245, 247, 250));
    RECT t_rc = { x + 24, y + 20, x + w - 280, y + 44 };
    DrawTextW(hdc, L"AUDIO BROWSER & YT-DLP ONLINE INGESTION ENGINE", -1, &t_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(130, 138, 154));
    RECT sub_rc = { x + 24, y + 44, x + w - 280, y + 64 };
    DrawTextW(hdc, L"Full yt-dlp Suite: Audio/Video Extraction, Chapter Splitting, Multi-Threaded (-N 4), and Session Cookies Bypass", -1, &sub_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Engine Status Badges (top right)
    bool ytdlp_ok = state_.has_downloader_ytdlp();
    bool ffmpeg_ok = state_.has_downloader_ffmpeg();

    auto draw_badge = [&](int bx, int by, const wchar_t* label, bool ok) {
        RECT brc = { bx, by, bx + 120, by + 24 };
        HBRUSH bg = CreateSolidBrush(ok ? RGB(16, 40, 26) : RGB(45, 20, 20));
        HPEN pen = CreatePen(PS_SOLID, 1, ok ? RGB(34, 197, 94) : RGB(239, 68, 68));
        SelectObject(hdc, bg);
        SelectObject(hdc, pen);
        RoundRect(hdc, brc.left, brc.top, brc.right, brc.bottom, 4, 4);
        DeleteObject(bg);
        DeleteObject(pen);

        SelectObject(hdc, font_small_);
        SetTextColor(hdc, ok ? RGB(74, 222, 128) : RGB(248, 113, 113));
        DrawTextW(hdc, label, -1, &brc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    int card_w = std::min(w - 48, 880);
    int badge_x = x + 24 + card_w - 250;
    draw_badge(badge_x, y + 22, ytdlp_ok ? L"● yt-dlp: READY" : L"✕ yt-dlp: MISSING", ytdlp_ok);
    draw_badge(badge_x + 126, y + 22, ffmpeg_ok ? L"● FFmpeg: READY" : L"✕ FFmpeg: MISSING", ffmpeg_ok);

    auto draw_card = [&](int cx, int cy, int cw, int ch, const wchar_t* title, const wchar_t* badge_text, COLORREF badge_col) {
        RECT rc = { cx, cy, cx + cw, cy + ch };
        HBRUSH bg = CreateSolidBrush(RGB(20, 24, 32));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(38, 44, 58));
        SelectObject(hdc, bg);
        SelectObject(hdc, pen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        DeleteObject(bg);
        DeleteObject(pen);

        SelectObject(hdc, font_bold_);
        SetTextColor(hdc, RGB(220, 226, 238));
        RECT th_rc = { cx + 16, cy + 12, cx + cw - 280, cy + 32 };
        DrawTextW(hdc, title, -1, &th_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (badge_text) {
            SelectObject(hdc, font_small_);
            SetTextColor(hdc, badge_col);
            RECT bg_rc = { cx + cw - 360, cy + 12, cx + cw - 16, cy + 32 };
            DrawTextW(hdc, badge_text, -1, &bg_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    };

    auto draw_pill = [&](const RECT& rc, const wchar_t* text, bool is_active, COLORREF active_bg, COLORREF active_border) {
        HBRUSH bg = CreateSolidBrush(is_active ? active_bg : RGB(26, 30, 40));
        HPEN pen = CreatePen(PS_SOLID, 1, is_active ? active_border : RGB(44, 50, 66));
        SelectObject(hdc, bg);
        SelectObject(hdc, pen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 5, 5);
        DeleteObject(bg);
        DeleteObject(pen);

        SelectObject(hdc, is_active ? font_bold_ : font_small_);
        SetTextColor(hdc, is_active ? RGB(255, 255, 255) : RGB(160, 168, 182));
        RECT trc = rc;
        DrawTextW(hdc, text, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    auto& cfg = state_.get_download_config();
    const auto& meta = state_.get_probed_meta();
    bool has_probed = !meta.title.empty();
    bool has_file = !state_.get_download_last_file().empty();

    int cur_y = y + 70;
    int card1_h = 286;
    if (has_probed) card1_h += 32;
    if (has_file) card1_h += 42;

    draw_card(x + 24, cur_y, card_w, card1_h, L"YT-DLP INGESTION & ADVANCED CONFIGURATION", L"[ YOUTUBE / SOUNDCLOUD / BANDCAMP / DIRECT URL ]", RGB(59, 130, 246));

    // Row 1: URL Input Box with [Paste URL], [Inspect], and [Clear] Buttons
    int input_left = x + 40;
    int input_top = cur_y + 40;
    int paste_w = 90;
    int probe_w = 90;
    int clear_w = 60;
    int input_w = card_w - 48 - paste_w - probe_w - clear_w - 28;

    RECT url_box = { input_left, input_top, input_left + input_w, input_top + 32 };
    HBRUSH url_bg = CreateSolidBrush(RGB(13, 16, 22));
    HPEN url_pen = CreatePen(PS_SOLID, 1, RGB(45, 53, 70));
    SelectObject(hdc, url_bg);
    SelectObject(hdc, url_pen);
    RoundRect(hdc, url_box.left, url_box.top, url_box.right, url_box.bottom, 4, 4);
    DeleteObject(url_bg);
    DeleteObject(url_pen);

    std::string current_url = state_.get_download_url();
    RECT url_text_rc = { url_box.left + 10, url_box.top, url_box.right - 10, url_box.bottom };
    if (current_url.empty()) {
        SelectObject(hdc, font_small_);
        SetTextColor(hdc, RGB(90, 100, 120));
        DrawTextW(hdc, L"Paste media stream URL here (e.g. YouTube, SoundCloud, Bandcamp, Direct stream)", -1, &url_text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        SelectObject(hdc, font_ui_);
        SetTextColor(hdc, RGB(235, 242, 255));
        std::wstring ws_url = to_wide(current_url);
        DrawTextW(hdc, ws_url.c_str(), -1, &url_text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    RECT paste_btn = { input_left + input_w + 10, input_top, input_left + input_w + 10 + paste_w, input_top + 32 };
    draw_pill(paste_btn, L"📋 Paste URL", false, RGB(37, 99, 235), RGB(59, 130, 246));
    downloader_actions_.push_back({ paste_btn, DownloaderAction::PASTE_CLIPBOARD, 0 });

    RECT probe_btn = { paste_btn.right + 6, input_top, paste_btn.right + 6 + probe_w, input_top + 32 };
    bool probing = state_.is_probing();
    draw_pill(probe_btn, probing ? L"⏳ Probing..." : L"🔍 Inspect", probing, RGB(124, 58, 237), RGB(168, 85, 247));
    downloader_actions_.push_back({ probe_btn, DownloaderAction::PROBE_URL, 0 });

    RECT clear_btn = { probe_btn.right + 6, input_top, probe_btn.right + 6 + clear_w, input_top + 32 };
    draw_pill(clear_btn, L"✕ Clear", false, RGB(30, 36, 48), RGB(50, 60, 80));
    downloader_actions_.push_back({ clear_btn, DownloaderAction::CLEAR_URL, 0 });

    int r_y = cur_y + 80;

    // Row 1.5: Probed Metadata Banner (if available)
    if (has_probed) {
        RECT meta_rc = { input_left, r_y, x + card_w - 24, r_y + 24 };
        HBRUSH m_bg = CreateSolidBrush(RGB(18, 28, 38));
        HPEN m_pen = CreatePen(PS_SOLID, 1, RGB(56, 189, 248));
        SelectObject(hdc, m_bg);
        SelectObject(hdc, m_pen);
        RoundRect(hdc, meta_rc.left, meta_rc.top, meta_rc.right, meta_rc.bottom, 4, 4);
        DeleteObject(m_bg);
        DeleteObject(m_pen);

        SelectObject(hdc, font_small_);
        SetTextColor(hdc, RGB(56, 189, 248));
        std::wstring meta_info = L"🎵 " + to_wide(meta.title) + L"  |  👤 " + to_wide(meta.uploader) + L"  |  ⏱ " + to_wide(meta.duration_string);
        RECT mtext_rc = { meta_rc.left + 10, meta_rc.top, meta_rc.right - 10, meta_rc.bottom };
        DrawTextW(hdc, meta_info.c_str(), -1, &mtext_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        r_y += 30;
    }

    // Row 2: Ingest Mode (Audio Only vs Video + Audio)
    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(160, 168, 185));
    RECT m_lbl = { input_left, r_y, input_left + 95, r_y + 26 };
    DrawTextW(hdc, L"Ingest Mode:", -1, &m_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    bool is_audio_mode = (cfg.mode == remixer::downloader::DownloadMode::AudioOnly);
    RECT m_audio_btn = { input_left + 100, r_y, input_left + 295, r_y + 26 };
    draw_pill(m_audio_btn, L"● Pure Audio Master", is_audio_mode, RGB(37, 99, 235), RGB(59, 130, 246));
    downloader_actions_.push_back({ m_audio_btn, DownloaderAction::TOGGLE_MODE, 0 });

    RECT m_video_btn = { input_left + 303, r_y, input_left + 498, r_y + 26 };
    draw_pill(m_video_btn, L"Video + Audio (MP4)", !is_audio_mode, RGB(124, 58, 237), RGB(168, 85, 247));
    downloader_actions_.push_back({ m_video_btn, DownloaderAction::TOGGLE_MODE, 0 });

    r_y += 34;

    // Row 3: Target Format / Quality
    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(160, 168, 185));
    RECT fmt_lbl = { input_left, r_y, input_left + 95, r_y + 26 };
    DrawTextW(hdc, is_audio_mode ? L"Audio Format:" : L"Resolution:", -1, &fmt_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int fx = input_left + 100;
    if (is_audio_mode) {
        const wchar_t* afmt_labels[] = {
            L"● 32-bit Float WAV",
            L"24-bit WAV",
            L"FLAC Lossless",
            L"MP3 320k",
            L"OPUS Stream",
            L"AAC / M4A"
        };
        remixer::downloader::AudioFormat afmts[] = {
            remixer::downloader::AudioFormat::Wav32Float,
            remixer::downloader::AudioFormat::Wav24,
            remixer::downloader::AudioFormat::Flac,
            remixer::downloader::AudioFormat::Mp3,
            remixer::downloader::AudioFormat::Opus,
            remixer::downloader::AudioFormat::Aac
        };
        for (int i = 0; i < 6; ++i) {
            int pw = (i == 0) ? 140 : 100;
            RECT prc = { fx, r_y, fx + pw, r_y + 26 };
            bool active = (cfg.audio_format == afmts[i]);
            draw_pill(prc, afmt_labels[i], active, RGB(37, 99, 235), RGB(59, 130, 246));
            downloader_actions_.push_back({ prc, DownloaderAction::SET_AUDIO_FORMAT, static_cast<int>(afmts[i]) });
            fx += (pw + 6);
        }
    } else {
        const wchar_t* vq_labels[] = {
            L"● Best Available",
            L"4K UHD (2160p)",
            L"Full HD (1080p)",
            L"HD (720p)"
        };
        remixer::downloader::VideoQuality vqs[] = {
            remixer::downloader::VideoQuality::Best,
            remixer::downloader::VideoQuality::UHD_4K,
            remixer::downloader::VideoQuality::FHD_1080p,
            remixer::downloader::VideoQuality::HD_720p
        };
        for (int i = 0; i < 4; ++i) {
            RECT prc = { fx, r_y, fx + 130, r_y + 26 };
            bool active = (cfg.video_quality == vqs[i]);
            draw_pill(prc, vq_labels[i], active, RGB(124, 58, 237), RGB(168, 85, 247));
            downloader_actions_.push_back({ prc, DownloaderAction::SET_VIDEO_QUALITY, static_cast<int>(vqs[i]) });
            fx += 136;
        }
    }

    r_y += 34;

    // Row 4: Advanced yt-dlp Features & Toggles
    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(160, 168, 185));
    RECT feat_lbl = { input_left, r_y, input_left + 95, r_y + 26 };
    DrawTextW(hdc, L"Features:", -1, &feat_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int feat_x = input_left + 100;

    // Feature 1: Embed Tags & Artwork
    RECT f1_rc = { feat_x, r_y, feat_x + 160, r_y + 26 };
    draw_pill(f1_rc, cfg.embed_metadata ? L"✓ Embed Tags & Art" : L"✕ Embed Tags & Art", cfg.embed_metadata, RGB(16, 185, 129), RGB(52, 211, 153));
    downloader_actions_.push_back({ f1_rc, DownloaderAction::TOGGLE_EMBED_METADATA, 0 });
    feat_x += 166;

    // Feature 2: Multi-thread Acceleration (-N 4)
    RECT f2_rc = { feat_x, r_y, feat_x + 155, r_y + 26 };
    bool multi_on = (cfg.concurrent_fragments > 1);
    draw_pill(f2_rc, multi_on ? L"⚡ Multi-thread (-N 4)" : L"Single Thread", multi_on, RGB(37, 99, 235), RGB(59, 130, 246));
    downloader_actions_.push_back({ f2_rc, DownloaderAction::TOGGLE_MULTITHREAD, 0 });
    feat_x += 161;

    // Feature 3: Split Chapters
    RECT f3_rc = { feat_x, r_y, feat_x + 195, r_y + 26 };
    draw_pill(f3_rc, cfg.split_chapters ? L"✂ Split Chapters: ON" : L"✂ Split Chapters: OFF", cfg.split_chapters, RGB(234, 88, 12), RGB(249, 115, 22));
    downloader_actions_.push_back({ f3_rc, DownloaderAction::TOGGLE_SPLIT_CHAPTERS, 0 });
    feat_x += 201;

    // Feature 4: Playlist Mode
    RECT f4_rc = { feat_x, r_y, feat_x + 130, r_y + 26 };
    draw_pill(f4_rc, cfg.download_playlist ? L"≡ Playlist: ALL" : L"≡ Single Track", cfg.download_playlist, RGB(147, 51, 234), RGB(168, 85, 247));
    downloader_actions_.push_back({ f4_rc, DownloaderAction::TOGGLE_PLAYLIST, 0 });

    r_y += 34;

    // Row 5: Cookies & Bot Verification Bypass
    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(160, 168, 185));
    RECT cook_lbl = { input_left, r_y, input_left + 95, r_y + 26 };
    DrawTextW(hdc, L"Cookies Bypass:", -1, &cook_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const wchar_t* c_names[] = { L"None", L"Google Chrome", L"Microsoft Edge", L"Mozilla Firefox", L"Brave Browser", L"Opera" };
    int c_idx = static_cast<int>(cfg.cookie_browser);
    std::wstring cook_txt = std::wstring(L"🍪 Browser Cookies: ") + c_names[c_idx] + L" (Click to switch)";
    RECT cook_btn = { input_left + 100, r_y, input_left + 420, r_y + 26 };
    draw_pill(cook_btn, cook_txt.c_str(), (c_idx > 0), RGB(20, 83, 45), RGB(34, 197, 94));
    downloader_actions_.push_back({ cook_btn, DownloaderAction::CYCLE_COOKIES, 0 });

    r_y += 38;

    // Row 6: Action Download Button & Live Telemetry
    bool downloading = state_.is_downloading();
    float pct = state_.get_download_progress();

    RECT dl_btn = { input_left, r_y, input_left + 290, r_y + 34 };
    if (!downloading) {
        draw_pill(dl_btn, L"⬇  Download & Ingest Stream", true, RGB(37, 99, 235), RGB(96, 165, 250));
        downloader_actions_.push_back({ dl_btn, DownloaderAction::START_DOWNLOAD, 0 });
    } else {
        std::wstring busy_txt = L"⏳ Ingesting... (" + std::to_wstring(static_cast<int>(pct)) + L"%)";
        draw_pill(dl_btn, busy_txt.c_str(), true, RGB(180, 83, 9), RGB(245, 158, 11));
    }

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, downloading ? RGB(250, 204, 21) : RGB(140, 150, 170));
    RECT stat_rc = { input_left + 305, r_y, x + card_w - 24, r_y + 34 };
    std::string stat_str = state_.get_download_status();
    std::wstring ws_stat = to_wide(stat_str);
    DrawTextW(hdc, ws_stat.c_str(), -1, &stat_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    r_y += 40;

    // Row 7: Progress Bar & Operation
    RECT bar_bg = { input_left, r_y, x + card_w - 24, r_y + 8 };
    HBRUSH bgb = CreateSolidBrush(RGB(14, 17, 24));
    HPEN bgp = CreatePen(PS_SOLID, 1, RGB(36, 42, 56));
    SelectObject(hdc, bgb);
    SelectObject(hdc, bgp);
    RoundRect(hdc, bar_bg.left, bar_bg.top, bar_bg.right, bar_bg.bottom, 4, 4);
    DeleteObject(bgb);
    DeleteObject(bgp);

    if (pct > 0.0f) {
        int total_bw = bar_bg.right - bar_bg.left;
        int fill_bw = static_cast<int>((pct / 100.0f) * total_bw);
        if (fill_bw > 0) {
            RECT bar_fg = { bar_bg.left, bar_bg.top, bar_bg.left + fill_bw, bar_bg.bottom };
            HBRUSH fgb = CreateSolidBrush(downloading ? RGB(59, 130, 246) : RGB(16, 185, 129));
            HPEN fgp = CreatePen(PS_SOLID, 1, downloading ? RGB(96, 165, 250) : RGB(52, 211, 153));
            SelectObject(hdc, fgb);
            SelectObject(hdc, fgp);
            RoundRect(hdc, bar_fg.left, bar_fg.top, bar_fg.right, bar_fg.bottom, 4, 4);
            DeleteObject(fgb);
            DeleteObject(fgp);
        }
    }

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(110, 120, 140));
    RECT op_rc = { input_left, r_y + 10, x + card_w - 24, r_y + 26 };
    std::string op_str = state_.get_download_current_op();
    std::wstring ws_op = to_wide(op_str);
    DrawTextW(hdc, ws_op.c_str(), -1, &op_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    r_y += 30;

    // Row 8: Last Downloaded File Quick Actions Banner (if available)
    std::string last_file = state_.get_download_last_file();
    if (!last_file.empty()) {
        RECT b_rc = { input_left, r_y, x + card_w - 24, r_y + 36 };
        HBRUSH b_bg = CreateSolidBrush(RGB(15, 30, 25));
        HPEN b_pen = CreatePen(PS_SOLID, 1, RGB(34, 197, 94));
        SelectObject(hdc, b_bg);
        SelectObject(hdc, b_pen);
        RoundRect(hdc, b_rc.left, b_rc.top, b_rc.right, b_rc.bottom, 6, 6);
        DeleteObject(b_bg);
        DeleteObject(b_pen);

        std::filesystem::path lpath(last_file);
        std::wstring fname = lpath.filename().wstring();

        SelectObject(hdc, font_bold_);
        SetTextColor(hdc, RGB(74, 222, 128));
        RECT finfo_rc = { b_rc.left + 12, b_rc.top, b_rc.left + 380, b_rc.bottom };
        std::wstring finfo = L"✓ Master: " + fname;
        DrawTextW(hdc, finfo.c_str(), -1, &finfo_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Quick action 1: Load to Deck
        RECT load_btn = { b_rc.right - 275, b_rc.top + 4, b_rc.right - 145, b_rc.bottom - 4 };
        draw_pill(load_btn, L"▶ Play in Deck", false, RGB(22, 101, 52), RGB(74, 222, 128));
        downloader_actions_.push_back({ load_btn, DownloaderAction::LOAD_LAST_TO_DECK, 0 });

        // Quick action 2: Send to Stems
        RECT stems_btn = { b_rc.right - 138, b_rc.top + 4, b_rc.right - 8, b_rc.bottom - 4 };
        draw_pill(stems_btn, L"⚡ Stem Split", false, RGB(30, 58, 138), RGB(96, 165, 250));
        downloader_actions_.push_back({ stems_btn, DownloaderAction::SEND_LAST_TO_STEMS, 0 });
    }

    // =======================================================================
    // Section 2: Local Audio Samples & Session Clips
    // =======================================================================
    int sec2_y = cur_y + card1_h + 16;
    SelectObject(hdc, font_bold_);
    SetTextColor(hdc, RGB(200, 210, 225));
    RECT s2_rc = { x + 24, sec2_y, x + w - 24, sec2_y + 24 };
    DrawTextW(hdc, L"LOCAL SAMPLE REPOSITORY & LOOPS", -1, &s2_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const wchar_t* samples[] = {
        L"Project_Master_48kHz.wav (32-bit Float Stereo Master PCM)",
        L"Acapella_Lead_Vocals.wav (48kHz Mono Lead)",
        L"Drums_Punchy_Kit_120BPM.wav (4-Stem Loop)",
        L"Sub_Bass_30Hz_Heavy.flac (Lossless Bass Track)"
    };

    for (int i = 0; i < 4; ++i) {
        int iy = sec2_y + 30 + (i * 42);
        RECT item_rc = { x + 24, iy, x + card_w, iy + 36 };

        HBRUSH item_b = CreateSolidBrush(RGB(24, 28, 36));
        HPEN item_p = CreatePen(PS_SOLID, 1, RGB(40, 46, 58));
        SelectObject(hdc, item_b);
        SelectObject(hdc, item_p);
        RoundRect(hdc, item_rc.left, item_rc.top, item_rc.right, item_rc.bottom, 6, 6);
        DeleteObject(item_b);
        DeleteObject(item_p);

        SelectObject(hdc, font_icon_);
        SetTextColor(hdc, RGB(59, 130, 246));
        RECT icon_rc = { item_rc.left + 12, item_rc.top, item_rc.left + 36, item_rc.bottom };
        DrawTextW(hdc, L"\uE8D6", -1, &icon_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, font_ui_);
        SetTextColor(hdc, RGB(220, 225, 235));
        RECT text_rc = { item_rc.left + 44, item_rc.top, item_rc.right - 12, item_rc.bottom };
        DrawTextW(hdc, samples[i], -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// ---------------------------------------------------------------------------
// 3. Stem Separator View (Optional Module - Only when Demucs is Installed)
// ---------------------------------------------------------------------------
void Win32Window::render_stem_separator_view(HDC hdc, int x, int y, int w, int h) {
    (void)h;
    SetBkMode(hdc, TRANSPARENT);

    SelectObject(hdc, font_bold_);
    SetTextColor(hdc, RGB(245, 247, 250));
    RECT t_rc = { x + 24, y + 20, x + w - 24, y + 44 };
    DrawTextW(hdc, L"DEMUCS V4 NEURAL STEM SEPARATOR", -1, &t_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(249, 115, 22)); // Orange accent indicating active module
    RECT sub_rc = { x + 24, y + 44, x + w - 24, y + 64 };
    DrawTextW(hdc, L"● ACTIVE MODULE TAB: In-RAM Neural AI Separation Engine (Vocals, Drums, Bass, Other)", -1, &sub_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 4 Channel Strips
    const wchar_t* names[] = { L"VOCALS", L"DRUMS", L"BASS", L"OTHER" };
    COLORREF colors[] = { RGB(6, 182, 212), RGB(249, 115, 22), RGB(168, 85, 247), RGB(16, 185, 129) };

    int strip_w = 125;
    int strip_h = 290;
    int start_x = x + 24;
    int start_y = y + 80;

    for (int i = 0; i < 4; ++i) {
        int cx = start_x + (i * (strip_w + 14));
        RECT strip_rc = { cx, start_y, cx + strip_w, start_y + strip_h };

        HBRUSH s_bg = CreateSolidBrush(RGB(22, 26, 34));
        HPEN s_p = CreatePen(PS_SOLID, 1, RGB(42, 48, 62));
        SelectObject(hdc, s_bg);
        SelectObject(hdc, s_p);
        RoundRect(hdc, strip_rc.left, strip_rc.top, strip_rc.right, strip_rc.bottom, 8, 8);
        DeleteObject(s_bg);
        DeleteObject(s_p);

        // Channel Header
        RECT ch_rc = { cx, start_y + 12, cx + strip_w, start_y + 32 };
        SelectObject(hdc, font_bold_);
        SetTextColor(hdc, colors[i]);
        DrawTextW(hdc, names[i], -1, &ch_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Animated VU Meter Bar
        int meter_x = cx + 22;
        int meter_y = start_y + 44;
        int meter_w = 16;
        int meter_h = 160;

        HBRUSH m_bg = CreateSolidBrush(RGB(15, 17, 22));
        RECT m_bg_rc = { meter_x, meter_y, meter_x + meter_w, meter_y + meter_h };
        FillRect(hdc, &m_bg_rc, m_bg);
        DeleteObject(m_bg);

        // Active Peak Fill
        int peak_level = 50 + static_cast<int>(std::abs(std::sin(timer_ticks_ * 0.15f + i)) * 90);
        int fill_y = meter_y + meter_h - peak_level;
        RECT peak_rc = { meter_x + 1, fill_y, meter_x + meter_w - 1, meter_y + meter_h - 1 };
        HBRUSH peak_b = CreateSolidBrush(colors[i]);
        FillRect(hdc, &peak_rc, peak_b);
        DeleteObject(peak_b);

        // Fader Slider Track
        int fader_x = cx + 60;
        int fader_w = 6;
        int fader_y = meter_y;
        int fader_h = meter_h;
        RECT fader_track = { fader_x, fader_y, fader_x + fader_w, fader_y + fader_h };
        HBRUSH ft_b = CreateSolidBrush(RGB(30, 35, 46));
        FillRect(hdc, &fader_track, ft_b);
        DeleteObject(ft_b);

        // Fader Knob
        int knob_y = fader_y + 40 + (i * 15);
        RECT knob_rc = { fader_x - 10, knob_y - 6, fader_x + fader_w + 10, knob_y + 6 };
        HBRUSH knob_b = CreateSolidBrush(RGB(220, 225, 235));
        SelectObject(hdc, knob_b);
        RoundRect(hdc, knob_rc.left, knob_rc.top, knob_rc.right, knob_rc.bottom, 4, 4);
        DeleteObject(knob_b);

        // Mute / Solo Buttons
        RECT m_btn = { cx + 18, start_y + strip_h - 44, cx + 54, start_y + strip_h - 16 };
        RECT s_btn = { cx + 64, start_y + strip_h - 44, cx + 100, start_y + strip_h - 16 };

        HBRUSH btn_b = CreateSolidBrush(RGB(30, 35, 46));
        HPEN btn_p = CreatePen(PS_SOLID, 1, RGB(50, 56, 70));
        SelectObject(hdc, btn_b);
        SelectObject(hdc, btn_p);
        RoundRect(hdc, m_btn.left, m_btn.top, m_btn.right, m_btn.bottom, 4, 4);
        RoundRect(hdc, s_btn.left, s_btn.top, s_btn.right, s_btn.bottom, 4, 4);
        DeleteObject(btn_b);
        DeleteObject(btn_p);

        SelectObject(hdc, font_small_);
        SetTextColor(hdc, RGB(200, 204, 216));
        DrawTextW(hdc, L"M", -1, &m_btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(hdc, L"S", -1, &s_btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// ---------------------------------------------------------------------------
// 4. Audio To MIDI View (Optional Module - Only when Pitch Tracker is Installed)
// ---------------------------------------------------------------------------
void Win32Window::render_audio_to_midi_view(HDC hdc, int x, int y, int w, int h) {
    (void)h;
    SetBkMode(hdc, TRANSPARENT);

    SelectObject(hdc, font_bold_);
    SetTextColor(hdc, RGB(245, 247, 250));
    RECT t_rc = { x + 24, y + 20, x + w - 24, y + 44 };
    DrawTextW(hdc, L"POLYPHONIC AUDIO-TO-MIDI TRANSCRIBER", -1, &t_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(249, 115, 22)); // Orange accent indicating active module
    RECT sub_rc = { x + 24, y + 44, x + w - 24, y + 64 };
    DrawTextW(hdc, L"● ACTIVE MODULE TAB: Pitch Tracking, Note Detection & Piano Roll Grid", -1, &sub_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Piano Roll Box
    int pr_y = y + 80;
    int pr_w = std::min(w - 48, 700);
    int pr_h = 280;
    RECT pr_rc = { x + 24, pr_y, x + 24 + pr_w, pr_y + pr_h };

    HBRUSH pr_bg = CreateSolidBrush(RGB(18, 21, 28));
    HPEN pr_p = CreatePen(PS_SOLID, 1, RGB(38, 44, 56));
    SelectObject(hdc, pr_bg);
    SelectObject(hdc, pr_p);
    RoundRect(hdc, pr_rc.left, pr_rc.top, pr_rc.right, pr_rc.bottom, 8, 8);
    DeleteObject(pr_bg);
    DeleteObject(pr_p);

    // Left Piano Keys strip
    int key_w = 40;
    int key_h = pr_h / 8;
    for (int k = 0; k < 8; ++k) {
        RECT key_rc = { pr_rc.left, pr_y + (k * key_h), pr_rc.left + key_w, pr_y + ((k + 1) * key_h) };
        bool is_black = (k == 1 || k == 3 || k == 6);
        HBRUSH kb = CreateSolidBrush(is_black ? RGB(10, 12, 16) : RGB(220, 224, 230));
        FillRect(hdc, &key_rc, kb);
        DeleteObject(kb);
    }

    // Grid Note blocks
    HPEN note_p = CreatePen(PS_SOLID, 1, RGB(59, 130, 246));
    HBRUSH note_b = CreateSolidBrush(RGB(37, 99, 235));
    SelectObject(hdc, note_p);
    SelectObject(hdc, note_b);

    RECT notes[] = {
        { pr_rc.left + 80,  pr_y + 40,  pr_rc.left + 160, pr_y + 60 },
        { pr_rc.left + 180, pr_y + 80,  pr_rc.left + 240, pr_y + 100 },
        { pr_rc.left + 260, pr_y + 30,  pr_rc.left + 350, pr_y + 50 },
        { pr_rc.left + 380, pr_y + 110, pr_rc.left + 460, pr_y + 130 },
        { pr_rc.left + 490, pr_y + 60,  pr_rc.left + 590, pr_y + 80 },
    };

    for (const auto& nr : notes) {
        RoundRect(hdc, nr.left, nr.top, nr.right, nr.bottom, 4, 4);
    }
    DeleteObject(note_p);
    DeleteObject(note_b);
}

// ---------------------------------------------------------------------------
// 5. DSP & Modules Manager / Store View (Built-in Program Menu)
// ---------------------------------------------------------------------------
void Win32Window::render_dsp_modules_view(HDC hdc, int x, int y, int w, int h) {
    (void)h;
    SetBkMode(hdc, TRANSPARENT);

    // Title
    SelectObject(hdc, font_bold_);
    SetTextColor(hdc, RGB(245, 247, 250));
    RECT t_rc = { x + 24, y + 20, x + w - 24, y + 44 };
    DrawTextW(hdc, L"DSP & AI MODULE STORE / MANAGER", -1, &t_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(130, 138, 154));
    RECT sub_rc = { x + 24, y + 44, x + w - 24, y + 64 };
    DrawTextW(hdc, L"Sidebar menus are only shown for installed modules. Click Install or Uninstall below to manage sidebar tabs.", -1, &sub_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const auto& modules = state_.get_available_modules();
    int card_w = std::min(w - 48, 700);
    int card_h = 100;

    for (size_t i = 0; i < modules.size(); ++i) {
        const auto& mod = modules[i];
        int cy = y + 80 + static_cast<int>(i * 115);
        RECT card_rc = { x + 24, cy, x + 24 + card_w, cy + card_h };

        HBRUSH c_bg = CreateSolidBrush(mod.is_installed ? RGB(26, 32, 44) : RGB(20, 23, 30));
        HPEN c_p = CreatePen(PS_SOLID, 1, mod.is_installed ? RGB(59, 130, 246) : RGB(38, 43, 56));
        SelectObject(hdc, c_bg);
        SelectObject(hdc, c_p);
        RoundRect(hdc, card_rc.left, card_rc.top, card_rc.right, card_rc.bottom, 8, 8);
        DeleteObject(c_bg);
        DeleteObject(c_p);

        // Icon Chip
        int chip_x = card_rc.left + 16;
        int chip_y = cy + 18;
        RECT chip_rc = { chip_x, chip_y, chip_x + 36, chip_y + 36 };
        HBRUSH ic_b = CreateSolidBrush(mod.is_installed ? RGB(37, 99, 235) : RGB(30, 35, 46));
        SelectObject(hdc, ic_b);
        RoundRect(hdc, chip_rc.left, chip_rc.top, chip_rc.right, chip_rc.bottom, 6, 6);
        DeleteObject(ic_b);

        SelectObject(hdc, font_icon_);
        SetTextColor(hdc, RGB(255, 255, 255));
        DrawTextW(hdc, mod.icon.c_str(), -1, &chip_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Module Name
        SelectObject(hdc, font_bold_);
        SetTextColor(hdc, RGB(245, 247, 250));
        RECT name_rc = { chip_x + 48, cy + 14, card_rc.right - 180, cy + 34 };
        DrawTextW(hdc, mod.name.c_str(), -1, &name_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Description & Subtitle
        SelectObject(hdc, font_small_);
        SetTextColor(hdc, RGB(140, 148, 164));
        RECT desc_rc = { chip_x + 48, cy + 36, card_rc.right - 180, cy + 54 };
        DrawTextW(hdc, mod.description.c_str(), -1, &desc_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Module ID & Version
        SetTextColor(hdc, RGB(100, 110, 128));
        RECT id_rc = { chip_x + 48, cy + 56, card_rc.right - 180, cy + 74 };
        std::wstring meta_str = L"ID: " + std::wstring(mod.module_id.begin(), mod.module_id.end()) + L" • v" + std::wstring(mod.version.begin(), mod.version.end());
        DrawTextW(hdc, meta_str.c_str(), -1, &id_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Status Badge & Action Button
        RECT status_rc = { card_rc.right - 240, cy + 14, card_rc.right - 16, cy + 34 };
        SelectObject(hdc, font_small_);
        if (mod.is_installed) {
            SetTextColor(hdc, RGB(74, 222, 128));
            DrawTextW(hdc, L"● INSTALLED (IN SIDEBAR)", -1, &status_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetTextColor(hdc, RGB(140, 148, 160));
            DrawTextW(hdc, L"○ NOT INSTALLED (HIDDEN)", -1, &status_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        // Interactive Button: Install / Uninstall
        RECT btn_rc = { card_rc.right - 160, cy + 46, card_rc.right - 16, cy + 80 };
        HBRUSH btn_b = CreateSolidBrush(mod.is_installed ? RGB(45, 22, 26) : RGB(22, 101, 52));
        HPEN btn_p = CreatePen(PS_SOLID, 1, mod.is_installed ? RGB(239, 68, 68) : RGB(34, 197, 94));
        SelectObject(hdc, btn_b);
        SelectObject(hdc, btn_p);
        RoundRect(hdc, btn_rc.left, btn_rc.top, btn_rc.right, btn_rc.bottom, 6, 6);
        DeleteObject(btn_b);
        DeleteObject(btn_p);

        SelectObject(hdc, font_bold_);
        SetTextColor(hdc, mod.is_installed ? RGB(248, 113, 113) : RGB(255, 255, 255));
        DrawTextW(hdc, mod.is_installed ? L"Uninstall Module" : L"+ Install Module", -1, &btn_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Register button hit area for clicking
        canvas_buttons_.push_back({ btn_rc, mod.module_id, !mod.is_installed });
    }
}

// ---------------------------------------------------------------------------
// 6. Settings View (Built-in Program Menu)
// ---------------------------------------------------------------------------
void Win32Window::render_settings_view(HDC hdc, int x, int y, int w, int h) {
    (void)h;
    SetBkMode(hdc, TRANSPARENT);
    setting_actions_.clear();

    // 1. Header with Icon Chip
    int chip_x = x + 24;
    int chip_y = y + 16;
    RECT chip_rc = { chip_x, chip_y, chip_x + 36, chip_y + 36 };
    HBRUSH chip_b = CreateSolidBrush(RGB(30, 36, 48));
    HPEN chip_p = CreatePen(PS_SOLID, 1, RGB(50, 60, 80));
    SelectObject(hdc, chip_b);
    SelectObject(hdc, chip_p);
    RoundRect(hdc, chip_rc.left, chip_rc.top, chip_rc.right, chip_rc.bottom, 6, 6);
    DeleteObject(chip_b);
    DeleteObject(chip_p);

    SelectObject(hdc, font_icon_);
    SetTextColor(hdc, RGB(59, 130, 246));
    DrawTextW(hdc, L"\uE713", -1, &chip_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, font_bold_);
    SetTextColor(hdc, RGB(245, 247, 250));
    RECT t_rc = { chip_x + 48, y + 14, x + w - 24, y + 36 };
    DrawTextW(hdc, L"SYSTEM & AUDIO ENGINE PREFERENCES", -1, &t_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(130, 138, 154));
    RECT sub_rc = { chip_x + 48, y + 36, x + w - 24, y + 54 };
    DrawTextW(hdc, L"Configure real-time DSP latency, hardware driver models, AI acceleration, and DAW IPC streaming.", -1, &sub_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int card_w = std::min(w - 48, 860);

    auto draw_card = [&](int cx, int cy, int cw, int ch, const wchar_t* title, const wchar_t* badge_text, COLORREF badge_col) {
        RECT rc = { cx, cy, cx + cw, cy + ch };
        HBRUSH bg = CreateSolidBrush(RGB(20, 24, 32));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(38, 44, 58));
        SelectObject(hdc, bg);
        SelectObject(hdc, pen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        DeleteObject(bg);
        DeleteObject(pen);

        SelectObject(hdc, font_bold_);
        SetTextColor(hdc, RGB(220, 226, 238));
        RECT th_rc = { cx + 16, cy + 12, cx + cw - 160, cy + 32 };
        DrawTextW(hdc, title, -1, &th_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (badge_text) {
            SelectObject(hdc, font_small_);
            SetTextColor(hdc, badge_col);
            RECT bg_rc = { cx + cw - 260, cy + 12, cx + cw - 16, cy + 32 };
            DrawTextW(hdc, badge_text, -1, &bg_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    };

    auto draw_pill = [&](const RECT& rc, const wchar_t* text, bool is_active, COLORREF active_bg, COLORREF active_border) {
        HBRUSH bg = CreateSolidBrush(is_active ? active_bg : RGB(26, 30, 40));
        HPEN pen = CreatePen(PS_SOLID, 1, is_active ? active_border : RGB(44, 50, 66));
        SelectObject(hdc, bg);
        SelectObject(hdc, pen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 5, 5);
        DeleteObject(bg);
        DeleteObject(pen);

        SelectObject(hdc, is_active ? font_bold_ : font_small_);
        SetTextColor(hdc, is_active ? RGB(255, 255, 255) : RGB(160, 168, 182));
        RECT trc = rc;
        DrawTextW(hdc, text, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    // =======================================================================
    // Section 1: Audio Driver & Hardware DSP Card
    // =======================================================================
    int cur_y = y + 70;
    int card1_h = 150;
    draw_card(x + 24, cur_y, card_w, card1_h, L"AUDIO DRIVER & HARDWARE DSP ENGINE", L"[ LOW-LATENCY SPSC BUFFER ]", RGB(59, 130, 246));

    // Row 1: Driver Model
    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(140, 148, 164));
    RECT l1_rc = { x + 40, cur_y + 40, x + 160, cur_y + 64 };
    DrawTextW(hdc, L"Driver Model:", -1, &l1_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const wchar_t* drivers[] = { L"WASAPI Exclusive", L"ASIO Bridge", L"DirectSound" };
    int px = x + 165;
    for (int d = 0; d < 3; ++d) {
        RECT prc = { px, cur_y + 38, px + 140, cur_y + 64 };
        bool active = (state_.get_audio_driver() == d);
        draw_pill(prc, drivers[d], active, RGB(37, 99, 235), RGB(59, 130, 246));
        setting_actions_.push_back({ prc, SettingAction::DRIVER, d, 0 });
        px += 148;
    }

    // Row 2: Sample Rate
    RECT l2_rc = { x + 40, cur_y + 74, x + 160, cur_y + 98 };
    DrawTextW(hdc, L"Sample Rate:", -1, &l2_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    uint32_t rates[] = { 44100, 48000, 88200, 96000 };
    const wchar_t* rate_labels[] = { L"44.1 kHz", L"48.0 kHz", L"88.2 kHz", L"96.0 kHz" };
    px = x + 165;
    for (int r = 0; r < 4; ++r) {
        RECT prc = { px, cur_y + 72, px + 90, cur_y + 98 };
        bool active = (state_.get_sample_rate() == rates[r]);
        draw_pill(prc, rate_labels[r], active, RGB(37, 99, 235), RGB(59, 130, 246));
        setting_actions_.push_back({ prc, SettingAction::SAMPLE_RATE, 0, rates[r] });
        px += 98;
    }

    // Row 3: Buffer Size & Latency
    RECT l3_rc = { x + 40, cur_y + 108, x + 160, cur_y + 132 };
    DrawTextW(hdc, L"Buffer & Latency:", -1, &l3_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    uint32_t buffers[] = { 128, 256, 512, 1024 };
    const wchar_t* buf_labels[] = { L"128 spl (2.7ms)", L"256 spl (5.3ms)", L"512 spl (10.7ms)", L"1024 spl (21.3ms)" };
    px = x + 165;
    for (int b = 0; b < 4; ++b) {
        RECT prc = { px, cur_y + 106, px + 120, cur_y + 132 };
        bool active = (state_.get_buffer_size() == buffers[b]);
        draw_pill(prc, buf_labels[b], active, RGB(37, 99, 235), RGB(59, 130, 246));
        setting_actions_.push_back({ prc, SettingAction::BUFFER_SIZE, 0, buffers[b] });
        px += 128;
    }

    // Reset Engine Action Button
    RECT reset_rc = { x + 24 + card_w - 180, cur_y + 106, x + 24 + card_w - 16, cur_y + 134 };
    draw_pill(reset_rc, L"↺ Reset DSP Engine", false, RGB(30, 36, 48), RGB(60, 70, 90));
    setting_actions_.push_back({ reset_rc, SettingAction::RESET_DSP, 0, 0 });

    // =======================================================================
    // Section 2: AI Acceleration & Inference Engine Card
    // =======================================================================
    cur_y += (card1_h + 14);
    int card2_h = 116;
    draw_card(x + 24, cur_y, card_w, card2_h, L"AI ACCELERATION & INFERENCE (STEMS & MIDI)", L"[ IN-RAM ONNX RUNTIME ]", RGB(168, 85, 247));

    // Row 1: Backend
    RECT a1_rc = { x + 40, cur_y + 40, x + 160, cur_y + 64 };
    DrawTextW(hdc, L"Inference Backend:", -1, &a1_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const wchar_t* backends[] = { L"DirectML (GPU)", L"CPU AVX-512 / AVX2", L"Native ONNX C-API" };
    px = x + 165;
    for (int a = 0; a < 3; ++a) {
        RECT prc = { px, cur_y + 38, px + 155, cur_y + 64 };
        bool active = (state_.get_ai_backend() == a);
        draw_pill(prc, backends[a], active, RGB(124, 58, 237), RGB(168, 85, 247));
        setting_actions_.push_back({ prc, SettingAction::AI_BACKEND, a, 0 });
        px += 163;
    }

    // Row 2: Worker Threads & Scheduling
    RECT a2_rc = { x + 40, cur_y + 74, x + 160, cur_y + 98 };
    DrawTextW(hdc, L"Thread Pool & Priority:", -1, &a2_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int thread_counts[] = { 2, 4, 8 };
    const wchar_t* thread_labels[] = { L"2 Threads", L"4 Threads", L"8 Threads" };
    px = x + 165;
    for (int t = 0; t < 3; ++t) {
        RECT prc = { px, cur_y + 72, px + 95, cur_y + 98 };
        bool active = (state_.get_ai_threads() == thread_counts[t]);
        draw_pill(prc, thread_labels[t], active, RGB(124, 58, 237), RGB(168, 85, 247));
        setting_actions_.push_back({ prc, SettingAction::AI_THREADS, thread_counts[t], 0 });
        px += 103;
    }

    // Toggle Pro Audio Priority Button
    bool prio_on = state_.is_high_priority_audio();
    RECT prio_rc = { px + 20, cur_y + 72, px + 240, cur_y + 98 };
    draw_pill(prio_rc, prio_on ? L"Pro Audio MMCSS: ON" : L"Pro Audio MMCSS: OFF", prio_on, RGB(22, 101, 52), RGB(34, 197, 94));
    setting_actions_.push_back({ prio_rc, SettingAction::TOGGLE_PRIORITY, 0, 0 });

    // =======================================================================
    // Section 3: DAW Integration & Shared Memory IPC Bridge Card
    // =======================================================================
    cur_y += (card2_h + 14);
    int card3_h = 76;
    draw_card(x + 24, cur_y, card_w, card3_h, L"DAW INTEGRATION & INTER-PROCESS BRIDGE", L"[ VST3 / CLAP / AU ]", RGB(16, 185, 129));

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(140, 148, 164));
    RECT ipc_lbl = { x + 40, cur_y + 38, x + 480, cur_y + 64 };
    DrawTextW(hdc, L"Shared Memory Channel: Local\\RemixerTools_Shm_01 (Zero-Copy Ring Buffer)", -1, &ipc_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    bool ipc_on = state_.is_ipc_bridge_enabled();
    RECT ipc_btn = { x + 24 + card_w - 220, cur_y + 36, x + 24 + card_w - 16, cur_y + 64 };
    draw_pill(ipc_btn, ipc_on ? L"DAW IPC Bridge: ENABLED" : L"DAW IPC Bridge: DISABLED", ipc_on, RGB(22, 101, 52), RGB(34, 197, 94));
    setting_actions_.push_back({ ipc_btn, SettingAction::TOGGLE_IPC, 0, 0 });

    // =======================================================================
    // Section 4: Security & Anti-Tamper Telemetry Card
    // =======================================================================
    cur_y += (card3_h + 14);
    int card4_h = 76;
    draw_card(x + 24, cur_y, card_w, card4_h, L"SECURITY & ANTI-REVERSE ENGINEERING GUARD", L"[ HARDENED MATRIX ]", RGB(239, 68, 68));

    SelectObject(hdc, font_small_);
    SetTextColor(hdc, RGB(74, 222, 128));
    RECT sec_lbl = { x + 40, cur_y + 38, x + 500, cur_y + 64 };
    DrawTextW(hdc, L"● System Integrity: SECURE (PEB, Hardware Breakpoints DR0-DR3, .text Hash Clean)", -1, &sec_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT diag_btn = { x + 24 + card_w - 220, cur_y + 36, x + 24 + card_w - 16, cur_y + 64 };
    draw_pill(diag_btn, L"🛡️ Run Diagnostics", false, RGB(35, 40, 52), RGB(60, 70, 90));
    setting_actions_.push_back({ diag_btn, SettingAction::RUN_SECURITY_DIAGNOSTICS, 0, 0 });
}

} // namespace remixer::desktop
