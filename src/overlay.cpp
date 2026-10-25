#include "overlay.h"

#include <algorithm>
#include <cstring>

namespace oldr {
namespace {

constexpr UINT_PTR kFastTimer = 1;
constexpr UINT_PTR kSlowTimer = 2;
const wchar_t* kClass = L"OLDR_Crosshair_Overlay";

BOOL CALLBACK EnumProc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(mon, &mi)) {
        MonitorInfo info;
        info.rect = mi.rcMonitor;
        info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        reinterpret_cast<std::vector<MonitorInfo>*>(lp)->push_back(info);
    }
    return TRUE;
}

} // namespace

std::vector<MonitorInfo> ListMonitors() {
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, EnumProc, reinterpret_cast<LPARAM>(&list));
    return list;
}

bool Overlay::Create(HINSTANCE instance, const Settings* settings) {
    s_ = settings;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                                WS_EX_NOACTIVATE,
                            kClass, L"OLDR Crosshair Overlay", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                            instance, this);
    if (!hwnd_) return false;
    startTick_ = GetTickCount64();
    SetTimer(hwnd_, kSlowTimer, 1000, nullptr);
    Refresh();
    return true;
}

void Overlay::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    if (memDc_) {
        if (oldBmp_) SelectObject(memDc_, oldBmp_);
        DeleteDC(memDc_);
        memDc_ = nullptr;
    }
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_TIMER:
            if (self) self->Tick(wp == kSlowTimer);
            return 0;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_DISPLAYCHANGE:
            if (self) self->Refresh();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Overlay::UpdateTimers() {
    const Settings& s = *s_;
    const bool needFast = s.enabled && (Renderer::IsAnimated(s) || s.followCursor || s.hideWhenCursorVisible);
    if (needFast && !fastTimer_) SetTimer(hwnd_, kFastTimer, 16, nullptr);
    if (!needFast && fastTimer_) KillTimer(hwnd_, kFastTimer);
    fastTimer_ = needFast;
}

void Overlay::Refresh() {
    if (!hwnd_) return;
    UpdateTimers();
    Tick(false);
}

POINT Overlay::ComputeOrigin(int w, int h) const {
    const Settings& s = *s_;
    int cx, cy;
    if (s.followCursor) {
        POINT pt{};
        GetCursorPos(&pt);
        cx = pt.x; cy = pt.y;
    } else {
        std::vector<MonitorInfo> mons = ListMonitors();
        RECT r{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        if (!mons.empty()) {
            size_t idx = 0;
            if (s.monitor > 0 && static_cast<size_t>(s.monitor) <= mons.size()) idx = s.monitor - 1;
            else for (size_t i = 0; i < mons.size(); ++i) if (mons[i].primary) idx = i;
            r = mons[idx].rect;
        }
        cx = (r.left + r.right) / 2;
        cy = (r.top + r.bottom) / 2;
    }
    return POINT{cx + s.offsetX - w / 2, cy + s.offsetY - h / 2};
}

void Overlay::Present(const Bitmap& bmp) {
    HDC screen = GetDC(nullptr);
    if (!memDc_) memDc_ = CreateCompatibleDC(screen);
    if (!dib_ || dibW_ != bmp.w || dibH_ != bmp.h) {
        if (dib_) {
            SelectObject(memDc_, oldBmp_);
            DeleteObject(dib_);
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = bmp.w;
        bi.bmiHeader.biHeight = -bmp.h;         // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        dib_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        dibBits_ = static_cast<uint32_t*>(bits);
        oldBmp_ = static_cast<HBITMAP>(SelectObject(memDc_, dib_));
        dibW_ = bmp.w; dibH_ = bmp.h;
    }
    if (dibBits_) std::memcpy(dibBits_, bmp.px.data(), bmp.px.size() * sizeof(uint32_t));

    const POINT dst = origin_;
    SIZE size{bmp.w, bmp.h};
    POINT src{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screen, const_cast<POINT*>(&dst), &size, memDc_, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

void Overlay::Tick(bool keepTopmost) {
    if (!hwnd_ || !s_) return;
    const Settings& s = *s_;

    bool visible = s.enabled;
    if (visible && s.hideWhenCursorVisible && !s.followCursor) {
        CURSORINFO ci{sizeof(ci)};
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) visible = false;
    }
    if (!visible) {
        if (shown_) ShowWindow(hwnd_, SW_HIDE);
        shown_ = false;
        return;
    }

    const double seconds = (GetTickCount64() - startTick_) / 1000.0;
    bool changed = false;
    const Bitmap& bmp = renderer_.Compose(s, seconds, &changed);
    const POINT origin = ComputeOrigin(bmp.w, bmp.h);
    const bool moved = origin.x != origin_.x || origin.y != origin_.y;
    origin_ = origin;

    if (!shown_) {
        ShowWindow(hwnd_, SW_SHOWNA);
        shown_ = true;
        changed = true;
    }
    if (changed || dibW_ != bmp.w || dibH_ != bmp.h) {
        Present(bmp);
    } else if (moved) {
        SetWindowPos(hwnd_, HWND_TOPMOST, origin.x, origin.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (keepTopmost)
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace OLDR
