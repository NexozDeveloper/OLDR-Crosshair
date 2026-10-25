#include "config.h"

#include <windows.h>

#include <cstdlib>
#include <cwchar>

namespace oldr {
namespace {

const wchar_t* kSection = L"OLDR Crosshair";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"OLDR Crosshair";

std::wstring IniPath() { return ConfigDirectory() + L"\\settings.ini"; }

struct Io {
    const std::wstring path;
    bool writing;

    void Int(const wchar_t* key, int& v) {
        if (writing) {
            wchar_t b[32];
            std::swprintf(b, 32, L"%d", v);
            WritePrivateProfileStringW(kSection, key, b, path.c_str());
        } else {
            v = static_cast<int>(GetPrivateProfileIntW(kSection, key, v, path.c_str()));
            // GetPrivateProfileInt reads unsigned; negative numbers need manual parsing.
            wchar_t b[32] = {};
            GetPrivateProfileStringW(kSection, key, L"", b, 32, path.c_str());
            if (b[0] == L'-') v = static_cast<int>(std::wcstol(b, nullptr, 10));
        }
    }
    void Bool(const wchar_t* key, bool& v) {
        int i = v ? 1 : 0;
        Int(key, i);
        v = i != 0;
    }
    void Color(const wchar_t* key, uint32_t& v) {
        if (writing) {
            wchar_t b[16];
            std::swprintf(b, 16, L"#%06X", static_cast<unsigned>(v & 0xFFFFFF));
            WritePrivateProfileStringW(kSection, key, b, path.c_str());
        } else {
            wchar_t b[32] = {};
            GetPrivateProfileStringW(kSection, key, L"", b, 32, path.c_str());
            if (b[0] == L'#' && std::wcslen(b) == 7)
                v = static_cast<uint32_t>(std::wcstoul(b + 1, nullptr, 16)) & 0xFFFFFF;
        }
    }
    void All(Settings& s) {
        Bool(L"Enabled", s.enabled);
        Int(L"Icon", s.icon);
        Color(L"Color", s.color);
        Int(L"Size", s.size);
        Int(L"Thickness", s.thickness);
        Int(L"Opacity", s.opacity);
        Int(L"Rotation", s.rotation);
        Bool(L"Outline", s.outline);
        Color(L"OutlineColor", s.outlineColor);
        Int(L"OutlineWidth", s.outlineWidth);
        Bool(L"CenterDot", s.dot);
        Color(L"CenterDotColor", s.dotColor);
        Int(L"CenterDotSize", s.dotSize);
        Bool(L"Glow", s.glow);
        Color(L"GlowColor", s.glowColor);
        Int(L"GlowRadius", s.glowRadius);
        Int(L"GlowStrength", s.glowStrength);
        Bool(L"Rainbow", s.rainbow);
        Int(L"RainbowSpeed", s.rainbowSpeed);
        Bool(L"Pulse", s.pulse);
        Int(L"PulseSpeed", s.pulseSpeed);
        Int(L"OffsetX", s.offsetX);
        Int(L"OffsetY", s.offsetY);
        Int(L"Monitor", s.monitor);
        Bool(L"FollowCursor", s.followCursor);
        Bool(L"HideWhenCursorVisible", s.hideWhenCursorVisible);
        Int(L"HotkeyModifiers", s.hotkeyMods);
        Int(L"HotkeyToggle", s.hotkeyToggle);
        Int(L"HotkeyOpenSettings", s.hotkeyOpen);
        Bool(L"MinimizeToTray", s.minimizeToTray);
        Bool(L"StartMinimized", s.startMinimized);
        Bool(L"RunAtStartup", s.runAtStartup);
    }
};

} // namespace

std::wstring ConfigDirectory() {
    std::wstring dir;
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    dir = appdata ? appdata : L".";
    dir += L"\\OLDR Crosshair";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void LoadSettings(Settings& s) {
    Io io{IniPath(), false};
    io.All(s);
    Sanitize(s);
}

void SaveSettings(const Settings& s) {
    Settings copy = s;
    Io io{IniPath(), true};
    io.All(copy);
}

bool SetRunAtStartup(bool enable) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LONG rc;
    if (enable) {
        wchar_t exe[MAX_PATH * 2] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --minimized";
        rc = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kRunValue);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

} // namespace OLDR
