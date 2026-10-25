// OLDR Crosshair - application entry point: tray icon, hotkeys, single instance.
#include <windows.h>
#include <shellapi.h>

#include <cwchar>

#include "config.h"
#include "overlay.h"
#include "settings.h"
#include "settings_window.h"

#include "oldr_version.h"

namespace {

using namespace oldr;

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr int kHotkeyToggle = 1;
constexpr int kHotkeyOpen = 2;
constexpr UINT_PTR kSaveTimer = 5;
enum : UINT { kMenuOpen = 9001, kMenuToggle = 9002, kMenuExit = 9003 };

class App {
public:
    int Run(HINSTANCE instance, bool startHidden) {
        instance_ = instance;
        LoadSettings(settings_);
        if (settings_.runAtStartup) SetRunAtStartup(true); // keeps the entry valid if the exe moved

        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        showMessage_ = RegisterWindowMessageW(L"OLDR_Crosshair_ShowSettings");

        if (!overlay_.Create(instance, &settings_)) return 1;

        SettingsWindow::Callbacks cb;
        cb.onChanged = [this](unsigned flags) { OnChanged(flags); };
        cb.extra = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return OnMessage(m, w, l, r); };
        if (!window_.Create(instance, &settings_, cb, !(startHidden || settings_.startMinimized))) return 1;

        AddTrayIcon();
        RegisterHotkeys();

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (window_.PreTranslate(&msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

private:
    void OnChanged(unsigned flags) {
        if (flags & kChangedVisual) overlay_.Refresh();
        if (flags & kChangedHotkeys) RegisterHotkeys();
        if (flags & kChangedStartup) SetRunAtStartup(settings_.runAtStartup);
        SetTimer(window_.hwnd(), kSaveTimer, 500, nullptr); // debounced save
    }

    void RegisterHotkeys() {
        HWND hwnd = window_.hwnd();
        UnregisterHotKey(hwnd, kHotkeyToggle);
        UnregisterHotKey(hwnd, kHotkeyOpen);
        const UINT mods = static_cast<UINT>(settings_.hotkeyMods) | MOD_NOREPEAT;
        const bool a = RegisterHotKey(hwnd, kHotkeyToggle, mods, settings_.hotkeyToggle) != FALSE;
        const bool b = RegisterHotKey(hwnd, kHotkeyOpen, mods, settings_.hotkeyOpen) != FALSE;
        window_.SetHotkeyStatus(a, b);
    }

    void AddTrayIcon() {
        NOTIFYICONDATAW nid{sizeof(nid)};
        nid.hWnd = window_.hwnd();
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAY;
        nid.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
        std::wcsncpy(nid.szTip, L"OLDR Crosshair", ARRAYSIZE(nid.szTip) - 1);
        Shell_NotifyIconW(NIM_ADD, &nid);
    }

    void RemoveTrayIcon() {
        NOTIFYICONDATAW nid{sizeof(nid)};
        nid.hWnd = window_.hwnd();
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }

    void ToggleCrosshair() {
        settings_.enabled = !settings_.enabled;
        overlay_.Refresh();
        window_.SyncFromSettings();
        SetTimer(window_.hwnd(), kSaveTimer, 500, nullptr);
    }

    void ShowTrayMenu() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open settings");
        AppendMenuW(menu, MF_STRING | (settings_.enabled ? MF_CHECKED : 0), kMenuToggle, L"Show crosshair");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
        SetMenuDefaultItem(menu, kMenuOpen, FALSE);
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(window_.hwnd());
        const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, window_.hwnd(), nullptr);
        PostMessageW(window_.hwnd(), WM_NULL, 0, 0);
        DestroyMenu(menu);
        if (cmd == kMenuOpen) window_.Show();
        else if (cmd == kMenuToggle) ToggleCrosshair();
        else if (cmd == kMenuExit) DestroyWindow(window_.hwnd());
    }

    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
        result = 0;
        if (msg == WM_HOTKEY) {
            if (wp == kHotkeyToggle) ToggleCrosshair();
            else if (wp == kHotkeyOpen) window_.Show();
            return true;
        }
        if (msg == WM_TRAY) {
            switch (LOWORD(lp)) {
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK: window_.Show(); break;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU: ShowTrayMenu(); break;
            }
            return true;
        }
        if (msg == WM_TIMER && wp == kSaveTimer) {
            KillTimer(window_.hwnd(), kSaveTimer);
            SaveSettings(settings_);
            return true;
        }
        if (msg == taskbarCreated_) { AddTrayIcon(); return true; }
        if (msg == showMessage_) { window_.Show(); return true; }
        if (msg == WM_DESTROY) { // let the window finish its own cleanup afterwards
            SaveSettings(settings_);
            UnregisterHotKey(window_.hwnd(), kHotkeyToggle);
            UnregisterHotKey(window_.hwnd(), kHotkeyOpen);
            RemoveTrayIcon();
            overlay_.Destroy();
            return false;
        }
        return false;
    }

    HINSTANCE instance_ = nullptr;
    Settings settings_;
    Overlay overlay_;
    SettingsWindow window_;
    UINT taskbarCreated_ = 0;
    UINT showMessage_ = 0;
};

bool HasArg(const wchar_t* name) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; argv && i < argc; ++i)
        if (_wcsicmp(argv[i], name) == 0) found = true;
    if (argv) LocalFree(argv);
    return found;
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    // One instance only: wake the running one instead.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"OLDR_Crosshair_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        const UINT showMsg = RegisterWindowMessageW(L"OLDR_Crosshair_ShowSettings");
        if (HWND other = FindWindowW(L"OLDR_Crosshair_Main", nullptr)) PostMessageW(other, showMsg, 0, 0);
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    App app;
    const int code = app.Run(instance, HasArg(L"--minimized"));
    if (mutex) CloseHandle(mutex);
    return code;
}
