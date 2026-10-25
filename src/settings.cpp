#include "settings.h"

#include <algorithm>

#include "lucide_icons.h"

namespace oldr {
namespace {

int IconIndex(const char* id) {
    for (int i = 0; i < kIconCount; ++i) {
        const char* a = kIcons[i].id;
        const char* b = id;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) return i;
    }
    return 0;
}

template <typename T>
void Clamp(T& v, T lo, T hi) { v = std::min(std::max(v, lo), hi); }

}  // namespace

void ApplyDefaultLook(Settings& s) {
    const Settings d;
    s.icon = d.icon;       s.color = d.color;        s.size = d.size;
    s.thickness = d.thickness; s.opacity = d.opacity; s.rotation = d.rotation;
    s.outline = d.outline; s.outlineColor = d.outlineColor; s.outlineWidth = d.outlineWidth;
    s.dot = d.dot;         s.dotColor = d.dotColor;  s.dotSize = d.dotSize;
    s.glow = d.glow;       s.glowColor = d.glowColor; s.glowRadius = d.glowRadius;
    s.glowStrength = d.glowStrength;
    s.rainbow = d.rainbow; s.rainbowSpeed = d.rainbowSpeed;
    s.pulse = d.pulse;     s.pulseSpeed = d.pulseSpeed;
}

static void PresetClassic(Settings& s) {
    ApplyDefaultLook(s);
}

static void PresetRedDot(Settings& s) {
    ApplyDefaultLook(s);
    s.icon = IconIndex("circle-dot"); s.color = 0xFF2A2A; s.size = 40; s.thickness = 16;
}

static void PresetNeonCyan(Settings& s) {
    ApplyDefaultLook(s);
    s.icon = IconIndex("locate-fixed"); s.color = 0x00F0FF; s.size = 56; s.thickness = 18;
    s.outline = false; s.glow = true; s.glowColor = 0x00B4FF; s.glowRadius = 10; s.glowStrength = 140;
}

static void PresetTactical(Settings& s) {
    ApplyDefaultLook(s);
    s.icon = IconIndex("plus"); s.color = 0xFFA000; s.size = 44; s.thickness = 30;
    s.outline = true; s.outlineWidth = 14;
}

static void PresetMinimal(Settings& s) {
    ApplyDefaultLook(s);
    s.icon = IconIndex("plus"); s.color = 0xFFFFFF; s.size = 24; s.thickness = 14;
    s.outline = false; s.opacity = 90;
}

static void PresetPrecision(Settings& s) {
    ApplyDefaultLook(s);
    s.icon = IconIndex("locate"); s.color = 0xFFFFFF; s.size = 72; s.thickness = 12;
    s.dot = true; s.dotColor = 0xFF2A2A; s.dotSize = 4;
}

static void PresetRainbow(Settings& s) {
    ApplyDefaultLook(s);
    s.icon = IconIndex("crosshair"); s.size = 72; s.thickness = 22;
    s.rainbow = true; s.rainbowSpeed = 90; s.glow = true; s.glowRadius = 12; s.glowStrength = 120;
    s.outline = false;
}

const Preset kPresets[] = {
    {"Classic Green",   PresetClassic},
    {"Red Dot",         PresetRedDot},
    {"Neon Cyan",       PresetNeonCyan},
    {"Tactical Orange", PresetTactical},
    {"Minimal White",   PresetMinimal},
    {"Precision",       PresetPrecision},
    {"Rainbow Glow",    PresetRainbow},
};
const int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

void Sanitize(Settings& s) {
    Clamp(s.icon, 0, kIconCount - 1);
    Clamp(s.size, 8, 400);
    Clamp(s.thickness, 5, 60);
    Clamp(s.opacity, 10, 100);
    Clamp(s.rotation, -180, 180);
    Clamp(s.outlineWidth, 2, 40);
    Clamp(s.dotSize, 1, 24);
    Clamp(s.glowRadius, 1, 60);
    Clamp(s.glowStrength, 10, 300);
    Clamp(s.rainbowSpeed, 10, 360);
    Clamp(s.pulseSpeed, 10, 400);
    Clamp(s.offsetX, -4000, 4000);
    Clamp(s.offsetY, -4000, 4000);
    Clamp(s.monitor, 0, 15);
    s.hotkeyMods &= (kModAlt | kModCtrl | kModShift);
    s.color &= 0xFFFFFF; s.outlineColor &= 0xFFFFFF;
    s.dotColor &= 0xFFFFFF; s.glowColor &= 0xFFFFFF;
}

}  // namespace oldr
