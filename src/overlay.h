// OLDR Crosshair - the click-through, always-on-top crosshair window.
#pragma once
#include <windows.h>

#include <vector>

#include "renderer.h"
#include "settings.h"

namespace oldr {

struct MonitorInfo {
    RECT rect{};
    bool primary = false;
};
std::vector<MonitorInfo> ListMonitors();

class Overlay {
public:
    bool Create(HINSTANCE instance, const Settings* settings);
    void Destroy();
    void Refresh();                   // call after any settings change

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Tick(bool keepTopmost);
    void UpdateTimers();
    void Present(const Bitmap& bmp);
    POINT ComputeOrigin(int w, int h) const;

    HWND hwnd_ = nullptr;
    const Settings* s_ = nullptr;
    Renderer renderer_;
    HDC memDc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HBITMAP oldBmp_ = nullptr;
    uint32_t* dibBits_ = nullptr;
    int dibW_ = 0, dibH_ = 0;
    POINT origin_{LONG_MIN, LONG_MIN};
    bool shown_ = false;
    bool fastTimer_ = false;
    ULONGLONG startTick_ = 0;
};

}  // namespace oldr
