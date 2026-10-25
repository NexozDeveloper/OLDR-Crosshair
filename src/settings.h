// OLDR Crosshair - user settings (plain data, no Windows dependency).
#pragma once
#include <cstdint>

namespace oldr {

// Modifier bits match the Win32 MOD_* values on purpose.
enum HotkeyMod : int { kModAlt = 1, kModCtrl = 2, kModShift = 4 };

struct Settings {
    // ---- Crosshair --------------------------------------------------------
    bool enabled = true;
    int icon = 0; // index into kIcons
    uint32_t color = 0x00FF50; // 0xRRGGBB
    int size = 64; // px, 8..400
    int thickness = 20; // Lucide stroke width x10 (5..60)
    int opacity = 100; // %, 10..100
    int rotation = 0; // degrees, -180..180

    bool outline = true;
    uint32_t outlineColor = 0x000000;
    int outlineWidth = 10; // Lucide units x10 (2..40)

    bool dot = false; // extra Lucide "dot" in the center
    uint32_t dotColor = 0xFF3030;
    int dotSize = 4; // px, 1..24

    // ---- Effects ----------------------------------------------------------
    bool glow = false;
    uint32_t glowColor = 0x00FF50;
    int glowRadius = 8; // px, 1..60
    int glowStrength = 100; // %, 10..300

    bool rainbow = false;
    int rainbowSpeed = 60; // degrees / second, 10..360
    bool pulse = false;
    int pulseSpeed = 100; // Hz x100, 10..400

    // ---- Position ---------------------------------------------------------
    int offsetX = 0; // px
    int offsetY = 0; // px
    int monitor = 0; // 0 = primary, n = n-th monitor
    bool followCursor = false;
    bool hideWhenCursorVisible = false;

    // ---- Application ------------------------------------------------------
    int hotkeyMods = kModCtrl | kModAlt;
    int hotkeyToggle = 0x77; // VK_F8
    int hotkeyOpen = 0x78; // VK_F9
    bool minimizeToTray = true;
    bool startMinimized = false;
    bool runAtStartup = false;
};

struct Preset {
    const char* name;
    void (*apply)(Settings&); // only touches the look, never position/app options
};

extern const Preset kPresets[];
extern const int kPresetCount;

// Resets only the "look" of the crosshair (icon, colors, effects).
void ApplyDefaultLook(Settings& s);

// Clamps every value to the range supported by the UI / renderer.
void Sanitize(Settings& s);

} // namespace OLDR
