#include "app/app_state.hpp"
#include <iostream>

#if defined(_WIN32)
#include "app/win32_window.hpp"

#include "core/security_guard.hpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;

    // 1. Apply Process Mitigation policies (Strict handle checking, DLL image policies)
    remixer::core::SecurityGuard::apply_process_mitigation();

    // 2. Initialize baseline checksum for .text memory section integrity
    remixer::core::SecurityGuard::initialize_integrity_baseline();

    remixer::desktop::Win32Window window;
    if (!window.create(hInstance, nCmdShow, 1200, 750)) {
        DWORD err = GetLastError();
        wchar_t err_msg[256];
        swprintf_s(err_msg, L"Failed to create Remixer Tools Window. (Error code: %lu)", err);
        MessageBoxW(nullptr, err_msg, L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Anti-Reverse Engineering: Erase in-memory compiler metadata & hide UI thread
    remixer::core::SecurityGuard::erase_pe_header_from_memory();
    remixer::core::SecurityGuard::hide_current_thread();

    return window.run_message_loop();
}

// Fallback entry point when compiled with standard main console subsystem
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return wWinMain(GetModuleHandle(nullptr), nullptr, nullptr, SW_SHOW);
}

#else

int main(int argc, char* argv[]) {
    std::cout << "Remixer Tools Desktop App: Non-Windows platform placeholder.\n";
    return 0;
}

#endif
