#pragma once
#include <cstdint>

namespace oldr {

enum HotkeyMod : int { kModAlt = 1, kModCtrl = 2, kModShift = 4 };

struct Settings {
    bool enabled = true;
    int icon = 0;
    uint32_t color = 0x00FF50;
    int size = 64; 
    int thickness = 20; 
    int opacity = 100; 
    int rotation = 0; 

    bool outline = true;
    uint32_t outlineColor = 0x000000;
    int outlineWidth = 10; 

    bool dot = false; 
    uint32_t dotColor = 0xFF3030;
    int dotSize = 4; 

    bool glow = false;
    uint32_t glowColor = 0x00FF50;
    int glowRadius = 8;
    int glowStrength = 100; 

    bool rainbow = false;
    int rainbowSpeed = 60; 
    bool pulse = false;
    int pulseSpeed = 100; 

    int offsetX = 0; 
    int offsetY = 0;
    int monitor = 0; 
    bool followCursor = false;
    bool hideWhenCursorVisible = false;

    int hotkeyMods = kModCtrl | kModAlt;
    int hotkeyToggle = 0x77; 
    int hotkeyOpen = 0x78; 
    bool minimizeToTray = true;
    bool startMinimized = false;
    bool runAtStartup = false;
};

struct Preset {
    const char* name;
    void (*apply)(Settings&); 
};

extern const Preset kPresets[];
extern const int kPresetCount;

void ApplyDefaultLook(Settings& s);

void Sanitize(Settings& s);

} 
