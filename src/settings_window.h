// OLDR Crosshair - the settings window (native Win32 controls, English UI).
#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "renderer.h"
#include "settings.h"

namespace oldr {

enum ChangeFlags : unsigned {
    kChangedVisual = 1, // look / position of the crosshair
    kChangedHotkeys = 2, // hotkeys must be re-registered
    kChangedStartup = 4, // "start with Windows" toggled
    kChangedAny = 7,
};

class SettingsWindow {
public:
    struct Callbacks {
        std::function<void(unsigned flags)> onChanged;
        // Gives the application a first look at window messages (hotkeys, tray icon...).
        std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)> extra;
    };

    SettingsWindow();
    ~SettingsWindow();
    bool Create(HINSTANCE instance, Settings* settings, Callbacks callbacks, bool showNow);
    HWND hwnd() const { return hwnd_; }
    void Show();
    void Hide();
    bool PreTranslate(MSG* msg); // keyboard navigation inside the pages
    void SyncFromSettings(); // refresh every control from the settings
    void SetHotkeyStatus(bool toggleOk, bool openOk);

    // ---- used by the static window procedures ----
    static LRESULT CALLBACK MainProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK GalleryProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK PreviewProc(HWND, UINT, WPARAM, LPARAM);
    static INT_PTR CALLBACK PageProc(HWND, UINT, WPARAM, LPARAM);

private:
    struct SliderCtl; struct CheckCtl; struct ColorCtl;
    struct PageBuilder;

    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void BuildUi();
    void DestroyUi();
    void BuildAppearancePage(PageBuilder& b);
    void BuildEffectsPage(PageBuilder& b);
    void BuildPositionPage(PageBuilder& b);
    void BuildSettingsPage(PageBuilder& b);
    void ShowPage(int index);
    void LayoutPages();

    void OnCommand(WPARAM wp, LPARAM lp);
    void OnHScroll(LPARAM lp);
    void OnDrawItem(const DRAWITEMSTRUCT* dis);
    void Changed(unsigned flags);
    void UpdateEnabledStates();
    void UpdateSliderText(SliderCtl& c);
    void SetColorText(ColorCtl& c);
    void PickColor(ColorCtl& c);
    void FillMonitorCombo();

    void PaintGallery(HWND wnd);
    void PaintPreview(HWND wnd);
    void GalleryMouse(UINT msg, LPARAM lp);

    int S(int v) const { return MulDiv(v, static_cast<int>(dpi_), 96); }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    Settings* s_ = nullptr;
    Callbacks cb_;
    UINT dpi_ = 96;
    bool updating_ = false;

    HFONT font_ = nullptr, fontBold_ = nullptr, fontSmall_ = nullptr;
    HWND tab_ = nullptr, gallery_ = nullptr, preview_ = nullptr;
    HWND presetCombo_ = nullptr, hotkeyStatus_ = nullptr;
    std::vector<HWND> pages_;
    int currentPage_ = 0;

    std::vector<SliderCtl> sliders_;
    std::vector<CheckCtl> checks_;
    std::vector<ColorCtl> colors_;
    HWND monitorCombo_ = nullptr, modsCombo_ = nullptr, toggleCombo_ = nullptr, openCombo_ = nullptr;

    int hoverIcon_ = -1;
    std::vector<std::vector<uint8_t>> tiles_; // cached alpha masks for the gallery
    int tilePx_ = 0;
    Renderer previewRenderer_;
    ULONGLONG startTick_ = 0;
};

}  // namespace OLDR
