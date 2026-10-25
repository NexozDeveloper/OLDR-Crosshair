#include "settings_window.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>

#include "config.h"
#include "lucide_icons.h"
#include "overlay.h"

#include "oldr_version.h"

namespace oldr {
namespace {

// ---------------------------------------------------------------- control tables
enum SliderIdx { SL_SIZE, SL_THICKNESS, SL_OPACITY, SL_ROTATION, SL_OUTLINE_W, SL_DOT_SIZE, SL_GLOW_R,
                 SL_GLOW_S, SL_RAINBOW, SL_PULSE, SL_OFFX, SL_OFFY, SL_COUNT };
enum CheckIdx { CK_ENABLED, CK_OUTLINE, CK_DOT, CK_GLOW, CK_RAINBOW, CK_PULSE, CK_FOLLOW, CK_HIDECUR,
                CK_TRAY, CK_STARTMIN, CK_STARTUP, CK_COUNT };
enum ColorIdx { CO_MAIN, CO_OUTLINE, CO_DOT, CO_GLOW, CO_COUNT };

enum : int {
    ID_TAB = 90,
    ID_PRESET_COMBO = 110, ID_PRESET_APPLY = 111, ID_RESET = 112, ID_OPEN_FOLDER = 113, ID_CENTER = 114,
    ID_MONITOR = 120, ID_MODS = 121, ID_KEY_TOGGLE = 122, ID_KEY_OPEN = 123,
    ID_SLIDER_BASE = 200, ID_CHECK_BASE = 300, ID_COLOR_BASE = 400, ID_HEX_BASE = 450,
};

constexpr UINT_PTR kPreviewTimer = 77;

struct SliderDef {
    int Settings::*field; int lo, hi, div; const wchar_t* label; const wchar_t* unit; bool Settings::*dep;
};
struct CheckDef { bool Settings::*field; const wchar_t* label; };
struct ColorDef { uint32_t Settings::*field; const wchar_t* label; bool Settings::*dep; };

const SliderDef kSliders[SL_COUNT] = {
    {&Settings::size,         8, 400, 1,   L"Size",             L" px",       nullptr},
    {&Settings::thickness,    5, 60,  10,  L"Line thickness",   L"",          nullptr},
    {&Settings::opacity,      10, 100, 1,  L"Opacity",          L" %",        nullptr},
    {&Settings::rotation,    -180, 180, 1, L"Rotation",         L"°",    nullptr},
    {&Settings::outlineWidth, 2, 40,  10,  L"Outline width",    L"",          &Settings::outline},
    {&Settings::dotSize,      1, 24,  1,   L"Dot size",         L" px",       &Settings::dot},
    {&Settings::glowRadius,   1, 60,  1,   L"Glow radius",      L" px",       &Settings::glow},
    {&Settings::glowStrength, 10, 300, 1,  L"Glow strength",    L" %",        &Settings::glow},
    {&Settings::rainbowSpeed, 10, 360, 1,  L"Rainbow speed",    L"°/s",  &Settings::rainbow},
    {&Settings::pulseSpeed,   10, 400, 100, L"Pulse speed",     L" Hz",       &Settings::pulse},
    {&Settings::offsetX,     -1000, 1000, 1, L"Horizontal offset", L" px",    nullptr},
    {&Settings::offsetY,     -1000, 1000, 1, L"Vertical offset",   L" px",    nullptr},
};

const CheckDef kChecks[CK_COUNT] = {
    {&Settings::enabled,       L"Show crosshair"},
    {&Settings::outline,       L"Enable outline"},
    {&Settings::dot,           L"Enable center dot"},
    {&Settings::glow,          L"Enable glow"},
    {&Settings::rainbow,       L"Rainbow color cycle"},
    {&Settings::pulse,         L"Pulse (breathing opacity)"},
    {&Settings::followCursor,  L"Follow the mouse cursor"},
    {&Settings::hideWhenCursorVisible, L"Hide while the mouse cursor is visible (menus)"},
    {&Settings::minimizeToTray, L"Keep running in the tray when the window is closed"},
    {&Settings::startMinimized, L"Start minimized to the tray"},
    {&Settings::runAtStartup,   L"Start with Windows"},
};

const ColorDef kColors[CO_COUNT] = {
    {&Settings::color,        L"Crosshair color", nullptr},
    {&Settings::outlineColor, L"Outline color",   &Settings::outline},
    {&Settings::dotColor,     L"Dot color",       &Settings::dot},
    {&Settings::glowColor,    L"Glow color",      &Settings::glow},
};

struct ModsEntry { const wchar_t* text; int mods; };
const ModsEntry kMods[] = {
    {L"Ctrl + Alt", kModCtrl | kModAlt}, {L"Ctrl + Shift", kModCtrl | kModShift},
    {L"Alt + Shift", kModAlt | kModShift}, {L"Ctrl + Alt + Shift", kModCtrl | kModAlt | kModShift},
    {L"Ctrl", kModCtrl}, {L"Alt", kModAlt}, {L"Shift", kModShift}, {L"None", 0},
};

struct KeyEntry { std::wstring text; int vk; };
const std::vector<KeyEntry>& Keys() {
    static const std::vector<KeyEntry> keys = [] {
        std::vector<KeyEntry> k;
        for (int i = 1; i <= 12; ++i) k.push_back({L"F" + std::to_wstring(i), 0x6F + i});
        for (int c = 'A'; c <= 'Z'; ++c) k.push_back({std::wstring(1, static_cast<wchar_t>(c)), c});
        for (int c = '0'; c <= '9'; ++c) k.push_back({std::wstring(1, static_cast<wchar_t>(c)), c});
        k.push_back({L"Insert", 0x2D}); k.push_back({L"Delete", 0x2E});
        k.push_back({L"Home", 0x24});   k.push_back({L"End", 0x23});
        k.push_back({L"Page Up", 0x21}); k.push_back({L"Page Down", 0x22});
        k.push_back({L"Pause", 0x13});  k.push_back({L"Scroll Lock", 0x91});
        return k;
    }();
    return keys;
}

COLORREF ToColorRef(uint32_t rgb) {
    return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}
uint32_t FromColorRef(COLORREF c) {
    return (static_cast<uint32_t>(GetRValue(c)) << 16) | (static_cast<uint32_t>(GetGValue(c)) << 8) | GetBValue(c);
}

UINT DpiOf(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn && hwnd) {
        const UINT d = fn(hwnd);
        if (d) return d;
    }
    HDC dc = GetDC(nullptr);
    const int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return d > 0 ? static_cast<UINT>(d) : 96;
}

// Alpha-blends premultiplied 0xAARRGGBB pixels onto `dc`.
void BlendPremul(HDC dc, int x, int y, int w, int h, const uint32_t* px) {
    if (w <= 0 || h <= 0) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bm) return;
    std::copy(px, px + static_cast<size_t>(w) * h, static_cast<uint32_t*>(bits));
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bm);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, w, h, mem, 0, 0, w, h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bm);
}

void BlendMask(HDC dc, int x, int y, int w, int h, const std::vector<uint8_t>& mask, COLORREF c) {
    std::vector<uint32_t> px(mask.size());
    const uint32_t r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    for (size_t i = 0; i < mask.size(); ++i) {
        const uint32_t a = mask[i];
        px[i] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
    }
    BlendPremul(dc, x, y, w, h, px.data());
}

void FillRectColor(HDC dc, const RECT& rc, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

const wchar_t* kMainClass = L"OLDR_Crosshair_Main";
const wchar_t* kGalleryClass = L"OLDR_Crosshair_Gallery";
const wchar_t* kPreviewClass = L"OLDR_Crosshair_Preview";

constexpr int kGalleryCols = 4;

}  // namespace

// ---------------------------------------------------------------- helper structs
struct SettingsWindow::SliderCtl { int idx; HWND bar; HWND value; };
struct SettingsWindow::CheckCtl { int idx; HWND hwnd; };
struct SettingsWindow::ColorCtl { int idx; HWND swatch; HWND edit; HWND label; };

// Lays controls out top-to-bottom inside one tab page. All values are 96-dpi units.
struct SettingsWindow::PageBuilder {
    SettingsWindow* w;
    HWND page;
    int width;
    int y = 10;

    HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int x, int yy, int ww, int hh,
              int id = 0, HFONT font = nullptr) {
        HWND h = CreateWindowExW(ex, cls, text, style | WS_CHILD | WS_VISIBLE, w->S(x), w->S(yy), w->S(ww),
                                 w->S(hh), page, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 w->instance_, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : w->font_), TRUE);
        return h;
    }
    HWND Label(const wchar_t* text, int x, int yy, int ww, int hh, DWORD style = SS_LEFT) {
        return Make(L"STATIC", text, style | SS_NOPREFIX, 0, x, yy, ww, hh);
    }
    void Header(const wchar_t* text) {
        if (y > 12) y += 8;
        Make(L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, 12, y, width - 24, 18, 0, w->fontBold_);
        y += 20;
        Make(L"STATIC", L"", SS_ETCHEDHORZ, 0, 12, y, width - 24, 2);
        y += 8;
    }
    void Slider(int idx) {
        const SliderDef& d = kSliders[idx];
        Label(d.label, 12, y + 3, 120, 20);
        HWND bar = Make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, 134, y, width - 134 - 82, 26,
                        ID_SLIDER_BASE + idx);
        SendMessageW(bar, TBM_SETRANGE, TRUE, MAKELONG(d.lo, d.hi));
        SendMessageW(bar, TBM_SETPAGESIZE, 0, std::max(1, (d.hi - d.lo) / 20));
        HWND val = Label(L"", width - 12 - 66, y + 3, 66, 20, SS_RIGHT);
        w->sliders_.push_back({idx, bar, val});
        y += 30;
    }
    void Check(int idx, HWND* out = nullptr) {
        HWND h = Make(L"BUTTON", kChecks[idx].label, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 12, y, width - 24, 22,
                      ID_CHECK_BASE + idx);
        w->checks_.push_back({idx, h});
        if (out) *out = h;
        y += 26;
    }
    void Color(int idx) {
        HWND label = Label(kColors[idx].label, 12, y + 4, 120, 20);
        HWND sw = Make(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 0, 134, y, 54, 24, ID_COLOR_BASE + idx);
        Label(L"#", 198, y + 4, 10, 20);
        HWND ed = Make(L"EDIT", L"", ES_AUTOHSCROLL | ES_UPPERCASE | WS_TABSTOP, WS_EX_CLIENTEDGE, 210, y, 78, 24,
                       ID_HEX_BASE + idx);
        SendMessageW(ed, EM_SETLIMITTEXT, 7, 0);
        w->colors_.push_back({idx, sw, ed, label});
        y += 32;
    }
    HWND Combo(int id, const wchar_t* label, int labelW = 120) {
        Label(label, 12, y + 4, labelW, 20);
        HWND c = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 12 + labelW + 2, y,
                      width - 12 - labelW - 2 - 12, 260, id);
        y += 32;
        return c;
    }
    HWND Button(int id, const wchar_t* text, int bw) {
        HWND b = Make(L"BUTTON", text, BS_PUSHBUTTON | WS_TABSTOP, 0, 12, y, bw, 28, id);
        y += 36;
        return b;
    }
};

SettingsWindow::SettingsWindow() = default;
SettingsWindow::~SettingsWindow() = default;

// ---------------------------------------------------------------- creation
bool SettingsWindow::Create(HINSTANCE instance, Settings* settings, Callbacks callbacks, bool showNow) {
    instance_ = instance;
    s_ = settings;
    cb_ = std::move(callbacks);
    startTick_ = GetTickCount64();

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HICON big = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                              GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    HICON small = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.hIcon = big;
    wc.hIconSm = small;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW gc{sizeof(gc)};
    gc.lpfnWndProc = GalleryProc;
    gc.hInstance = instance;
    gc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    gc.lpszClassName = kGalleryClass;
    RegisterClassExW(&gc);

    WNDCLASSEXW pc{sizeof(pc)};
    pc.lpfnWndProc = PreviewProc;
    pc.hInstance = instance;
    pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pc.lpszClassName = kPreviewClass;
    RegisterClassExW(&pc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    hwnd_ = CreateWindowExW(0, kMainClass, L"OLDR Crosshair", style, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                            nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    dpi_ = DpiOf(hwnd_);
    RECT rc{0, 0, S(860), S(572)};
    using AdjFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static AdjFn adj = reinterpret_cast<AdjFn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi")));
    if (adj) adj(&rc, style, FALSE, 0, dpi_);
    else AdjustWindowRectEx(&rc, style, FALSE, 0);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(hwnd_, nullptr, work.left + ((work.right - work.left) - ww) / 2,
                 work.top + ((work.bottom - work.top) - wh) / 2, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);

    BuildUi();
    if (showNow) Show();
    return true;
}

void SettingsWindow::Show() {
    if (!hwnd_) return;
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    else ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::Hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

bool SettingsWindow::PreTranslate(MSG* msg) {
    if (!hwnd_ || pages_.empty() || currentPage_ < 0 || currentPage_ >= static_cast<int>(pages_.size()))
        return false;
    HWND page = pages_[currentPage_];
    if (msg->hwnd == page || IsChild(page, msg->hwnd)) return IsDialogMessageW(page, msg) != FALSE;
    return false;
}

void SettingsWindow::DestroyUi() {
    KillTimer(hwnd_, kPreviewTimer);
    while (HWND child = GetWindow(hwnd_, GW_CHILD)) DestroyWindow(child);
    pages_.clear(); sliders_.clear(); checks_.clear(); colors_.clear();
    tab_ = gallery_ = preview_ = presetCombo_ = hotkeyStatus_ = nullptr;
    monitorCombo_ = modsCombo_ = toggleCombo_ = openCombo_ = nullptr;
    tiles_.clear();
    if (font_) DeleteObject(font_);
    if (fontBold_) DeleteObject(fontBold_);
    if (fontSmall_) DeleteObject(fontSmall_);
    font_ = fontBold_ = fontSmall_ = nullptr;
}

void SettingsWindow::BuildUi() {
    const int pt = -MulDiv(9, static_cast<int>(dpi_), 72);
    font_ = CreateFontW(pt, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    fontBold_ = CreateFontW(pt, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                            L"Segoe UI");
    fontSmall_ = CreateFontW(-MulDiv(8, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0,
                             0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    // ---- left column: preview, gallery, presets, master switch
    preview_ = CreateWindowExW(0, kPreviewClass, L"", WS_CHILD | WS_VISIBLE, S(16), S(12), S(300), S(200), hwnd_,
                               nullptr, instance_, this);
    gallery_ = CreateWindowExW(0, kGalleryClass, L"", WS_CHILD | WS_VISIBLE, S(16), S(222), S(300), S(266), hwnd_,
                               nullptr, instance_, this);

    auto makeLeft = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
        HWND hh = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, S(x), S(y), S(w), S(h), hwnd_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SendMessageW(hh, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return hh;
    };
    makeLeft(L"STATIC", L"Preset", SS_LEFT, 16, 498, 120, 18, 0);
    presetCombo_ = makeLeft(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 16, 516, 200, 240,
                            ID_PRESET_COMBO);
    for (int i = 0; i < kPresetCount; ++i) {
        wchar_t name[64];
        MultiByteToWideChar(CP_UTF8, 0, kPresets[i].name, -1, name, 64);
        SendMessageW(presetCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    SendMessageW(presetCombo_, CB_SETCURSEL, 0, 0);
    makeLeft(L"BUTTON", L"Apply preset", BS_PUSHBUTTON | WS_TABSTOP, 224, 515, 92, 26, ID_PRESET_APPLY);
    HWND enabled = makeLeft(L"BUTTON", kChecks[CK_ENABLED].label, BS_AUTOCHECKBOX | WS_TABSTOP, 16, 548, 300, 22,
                            ID_CHECK_BASE + CK_ENABLED);
    checks_.push_back({CK_ENABLED, enabled});

    // ---- right column: tabs
    tab_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, S(332),
                           S(12), S(512), S(548), hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TAB)),
                           instance_, nullptr);
    SendMessageW(tab_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    const wchar_t* tabNames[] = {L"Appearance", L"Effects", L"Position", L"Settings"};
    for (int i = 0; i < 4; ++i) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<wchar_t*>(tabNames[i]);
        SendMessageW(tab_, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&ti));
    }

    // pages: modeless DS_CONTROL dialogs get the themed tab background for free
    struct {
        DLGTEMPLATE t;
        WORD menu, cls, title;
    } tmpl{};
    tmpl.t.style = WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | DS_CONTROL;
    for (int i = 0; i < 4; ++i)
        pages_.push_back(CreateDialogIndirectParamW(instance_, &tmpl.t, hwnd_, PageProc, reinterpret_cast<LPARAM>(this)));
    LayoutPages();

    auto build = [&](int i, void (SettingsWindow::*fn)(PageBuilder&)) {
        RECT rc;
        GetClientRect(pages_[i], &rc);
        PageBuilder b{this, pages_[i], MulDiv(rc.right, 96, static_cast<int>(dpi_))};
        (this->*fn)(b);
    };
    build(0, &SettingsWindow::BuildAppearancePage);
    build(1, &SettingsWindow::BuildEffectsPage);
    build(2, &SettingsWindow::BuildPositionPage);
    build(3, &SettingsWindow::BuildSettingsPage);

    currentPage_ = std::min(currentPage_, 3);
    SendMessageW(tab_, TCM_SETCURSEL, currentPage_, 0);
    ShowPage(currentPage_);
    SyncFromSettings();
    SetWindowPos(tab_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void SettingsWindow::LayoutPages() {
    RECT rc;
    GetClientRect(tab_, &rc);
    SendMessageW(tab_, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&rc));
    MapWindowPoints(tab_, hwnd_, reinterpret_cast<POINT*>(&rc), 2);
    for (HWND p : pages_)
        SetWindowPos(p, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE);
}

void SettingsWindow::ShowPage(int index) {
    currentPage_ = index;
    for (int i = 0; i < static_cast<int>(pages_.size()); ++i) ShowWindow(pages_[i], i == index ? SW_SHOW : SW_HIDE);
}

// ---------------------------------------------------------------- pages
void SettingsWindow::BuildAppearancePage(PageBuilder& b) {
    b.Header(L"Color & shape");
    b.Color(CO_MAIN);
    b.Slider(SL_SIZE);
    b.Slider(SL_THICKNESS);
    b.Slider(SL_OPACITY);
    b.Slider(SL_ROTATION);
    b.Header(L"Outline");
    b.Check(CK_OUTLINE);
    b.Color(CO_OUTLINE);
    b.Slider(SL_OUTLINE_W);
    b.Header(L"Center dot");
    b.Check(CK_DOT);
    b.Color(CO_DOT);
    b.Slider(SL_DOT_SIZE);
}

void SettingsWindow::BuildEffectsPage(PageBuilder& b) {
    b.Header(L"Glow");
    b.Check(CK_GLOW);
    b.Color(CO_GLOW);
    b.Slider(SL_GLOW_R);
    b.Slider(SL_GLOW_S);
    b.Header(L"Animation");
    b.Check(CK_RAINBOW);
    b.Slider(SL_RAINBOW);
    b.Check(CK_PULSE);
    b.Slider(SL_PULSE);
    b.Label(L"Rainbow replaces the crosshair and glow colors while it is enabled.", 12, b.y + 6,
            b.width - 24, 36);
}

void SettingsWindow::BuildPositionPage(PageBuilder& b) {
    b.Header(L"Placement");
    monitorCombo_ = b.Combo(ID_MONITOR, L"Monitor");
    FillMonitorCombo();
    b.Slider(SL_OFFX);
    b.Slider(SL_OFFY);
    b.Button(ID_CENTER, L"Center crosshair (reset offsets)", 240);
    b.Header(L"Behavior");
    b.Check(CK_FOLLOW);
    b.Check(CK_HIDECUR);
    b.Label(L"Hiding while the cursor is visible keeps the crosshair out of the way in game menus: most games "
            L"hide the mouse cursor during play and show it in menus.",
            12, b.y + 6, b.width - 24, 50);
}

void SettingsWindow::BuildSettingsPage(PageBuilder& b) {
    b.Header(L"Hotkeys");
    modsCombo_ = b.Combo(ID_MODS, L"Modifier keys", 150);
    for (const auto& m : kMods) SendMessageW(modsCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m.text));
    toggleCombo_ = b.Combo(ID_KEY_TOGGLE, L"Show / hide crosshair", 150);
    openCombo_ = b.Combo(ID_KEY_OPEN, L"Open this window", 150);
    for (const auto& k : Keys()) {
        SendMessageW(toggleCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(k.text.c_str()));
        SendMessageW(openCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(k.text.c_str()));
    }
    hotkeyStatus_ = b.Label(L"", 12, b.y, b.width - 24, 20);
    b.y += 26;

    b.Header(L"Application");
    b.Check(CK_TRAY);
    b.Check(CK_STARTMIN);
    b.Check(CK_STARTUP);

    b.Header(L"Profile");
    b.Button(ID_RESET, L"Reset all settings", 160);
    b.Button(ID_OPEN_FOLDER, L"Open settings folder", 160);

    b.Header(L"About");
    std::wstring about = L"OLDR Crosshair " + std::wstring(L"" OLDR_VERSION) +
                         L"\r\nCrosshair shapes are the official Lucide icons (lucide.dev, ISC License).\r\n"
                         L"Games must run in Borderless / Windowed Fullscreen mode for the overlay to be visible. "
                         L"Some anti-cheat systems may not allow overlays - use at your own risk.";
    b.Label(about.c_str(), 12, b.y, b.width - 24, 90);
}

void SettingsWindow::FillMonitorCombo() {
    SendMessageW(monitorCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(monitorCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Primary monitor"));
    const auto mons = ListMonitors();
    for (size_t i = 0; i < mons.size(); ++i) {
        wchar_t text[96];
        std::swprintf(text, 96, L"Monitor %zu  (%ld x %ld)%ls", i + 1, mons[i].rect.right - mons[i].rect.left,
                      mons[i].rect.bottom - mons[i].rect.top, mons[i].primary ? L"  - primary" : L"");
        SendMessageW(monitorCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
}

// ---------------------------------------------------------------- state <-> controls
void SettingsWindow::UpdateSliderText(SliderCtl& c) {
    const SliderDef& d = kSliders[c.idx];
    const int pos = static_cast<int>(SendMessageW(c.bar, TBM_GETPOS, 0, 0));
    wchar_t text[48];
    if (d.div == 1) std::swprintf(text, 48, L"%d%ls", pos, d.unit);
    else if (d.div == 10) std::swprintf(text, 48, L"%.1f%ls", pos / 10.0, d.unit);
    else std::swprintf(text, 48, L"%.2f%ls", pos / static_cast<double>(d.div), d.unit);
    SetWindowTextW(c.value, text);
}

void SettingsWindow::SetColorText(ColorCtl& c) {
    wchar_t text[16];
    std::swprintf(text, 16, L"%06X", static_cast<unsigned>(s_->*(kColors[c.idx].field) & 0xFFFFFF));
    SetWindowTextW(c.edit, text);
}

void SettingsWindow::UpdateEnabledStates() {
    for (auto& c : sliders_) {
        const SliderDef& d = kSliders[c.idx];
        const BOOL on = d.dep ? (s_->*(d.dep) ? TRUE : FALSE) : TRUE;
        EnableWindow(c.bar, on);
        EnableWindow(c.value, on);
    }
    for (auto& c : colors_) {
        const ColorDef& d = kColors[c.idx];
        const BOOL on = d.dep ? (s_->*(d.dep) ? TRUE : FALSE) : TRUE;
        EnableWindow(c.swatch, on);
        EnableWindow(c.edit, on);
        InvalidateRect(c.swatch, nullptr, TRUE);
    }
    for (auto& c : checks_)
        if (c.idx == CK_HIDECUR) EnableWindow(c.hwnd, s_->followCursor ? FALSE : TRUE);
}

void SettingsWindow::SyncFromSettings() {
    if (!hwnd_ || !tab_) return;
    updating_ = true;
    for (auto& c : sliders_) {
        const SliderDef& d = kSliders[c.idx];
        SendMessageW(c.bar, TBM_SETPOS, TRUE, s_->*(d.field));
        UpdateSliderText(c);
    }
    for (auto& c : checks_)
        SendMessageW(c.hwnd, BM_SETCHECK, s_->*(kChecks[c.idx].field) ? BST_CHECKED : BST_UNCHECKED, 0);
    for (auto& c : colors_) {
        SetColorText(c);
        InvalidateRect(c.swatch, nullptr, TRUE);
    }
    if (monitorCombo_) {
        const auto mons = ListMonitors();
        SendMessageW(monitorCombo_, CB_SETCURSEL,
                     s_->monitor <= static_cast<int>(mons.size()) ? s_->monitor : 0, 0);
    }
    if (modsCombo_) {
        int sel = 0;
        for (int i = 0; i < static_cast<int>(sizeof(kMods) / sizeof(kMods[0])); ++i)
            if (kMods[i].mods == s_->hotkeyMods) sel = i;
        SendMessageW(modsCombo_, CB_SETCURSEL, sel, 0);
        const auto& keys = Keys();
        int t = 0, o = 0;
        for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
            if (keys[i].vk == s_->hotkeyToggle) t = i;
            if (keys[i].vk == s_->hotkeyOpen) o = i;
        }
        SendMessageW(toggleCombo_, CB_SETCURSEL, t, 0);
        SendMessageW(openCombo_, CB_SETCURSEL, o, 0);
    }
    updating_ = false;
    UpdateEnabledStates();
    if (gallery_) InvalidateRect(gallery_, nullptr, FALSE);
    if (preview_) InvalidateRect(preview_, nullptr, FALSE);
    if (Renderer::IsAnimated(*s_)) SetTimer(hwnd_, kPreviewTimer, 33, nullptr);
    else KillTimer(hwnd_, kPreviewTimer);
}

void SettingsWindow::SetHotkeyStatus(bool toggleOk, bool openOk) {
    if (!hotkeyStatus_) return;
    const wchar_t* text = L"Hotkeys are active.";
    if (!toggleOk && !openOk) text = L"Both hotkeys are already used by another program - pick other keys.";
    else if (!toggleOk) text = L"The show / hide hotkey is already used by another program.";
    else if (!openOk) text = L"The open-window hotkey is already used by another program.";
    SetWindowTextW(hotkeyStatus_, text);
}

void SettingsWindow::Changed(unsigned flags) {
    if (preview_) InvalidateRect(preview_, nullptr, FALSE);
    if (Renderer::IsAnimated(*s_)) SetTimer(hwnd_, kPreviewTimer, 33, nullptr);
    else KillTimer(hwnd_, kPreviewTimer);
    if (cb_.onChanged) cb_.onChanged(flags);
}

void SettingsWindow::PickColor(ColorCtl& c) {
    static COLORREF custom[16] = {};
    const ColorDef& d = kColors[c.idx];
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = hwnd_;
    cc.rgbResult = ToColorRef(s_->*(d.field));
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&cc)) return;
    s_->*(d.field) = FromColorRef(cc.rgbResult);
    updating_ = true;
    SetColorText(c);
    updating_ = false;
    InvalidateRect(c.swatch, nullptr, TRUE);
    Changed(kChangedVisual);
}

// ---------------------------------------------------------------- events
void SettingsWindow::OnHScroll(LPARAM lp) {
    HWND bar = reinterpret_cast<HWND>(lp);
    for (auto& c : sliders_) {
        if (c.bar != bar) continue;
        const SliderDef& d = kSliders[c.idx];
        s_->*(d.field) = static_cast<int>(SendMessageW(bar, TBM_GETPOS, 0, 0));
        UpdateSliderText(c);
        Changed(kChangedVisual);
        return;
    }
}

void SettingsWindow::OnCommand(WPARAM wp, LPARAM lp) {
    if (updating_) return;
    const int id = LOWORD(wp), code = HIWORD(wp);
    HWND ctl = reinterpret_cast<HWND>(lp);

    if (id >= ID_CHECK_BASE && id < ID_CHECK_BASE + CK_COUNT && code == BN_CLICKED) {
        const int idx = id - ID_CHECK_BASE;
        s_->*(kChecks[idx].field) = SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED;
        unsigned flags = kChangedVisual;
        if (idx == CK_STARTUP) flags |= kChangedStartup;
        UpdateEnabledStates();
        Changed(flags);
    } else if (id >= ID_COLOR_BASE && id < ID_COLOR_BASE + CO_COUNT && code == BN_CLICKED) {
        for (auto& c : colors_) if (c.idx == id - ID_COLOR_BASE) PickColor(c);
    } else if (id >= ID_HEX_BASE && id < ID_HEX_BASE + CO_COUNT && code == EN_CHANGE) {
        wchar_t text[16] = {};
        GetWindowTextW(ctl, text, 16);
        const wchar_t* p = text[0] == L'#' ? text + 1 : text;
        wchar_t* end = nullptr;
        const unsigned long v = std::wcstoul(p, &end, 16);
        if (std::wcslen(p) == 6 && end && *end == 0) {
            const int idx = id - ID_HEX_BASE;
            s_->*(kColors[idx].field) = static_cast<uint32_t>(v) & 0xFFFFFF;
            for (auto& c : colors_) if (c.idx == idx) InvalidateRect(c.swatch, nullptr, TRUE);
            Changed(kChangedVisual);
        }
    } else if (id == ID_PRESET_APPLY && code == BN_CLICKED) {
        const int sel = static_cast<int>(SendMessageW(presetCombo_, CB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < kPresetCount) {
            kPresets[sel].apply(*s_);
            Sanitize(*s_);
            SyncFromSettings();
            Changed(kChangedVisual);
        }
    } else if (id == ID_MONITOR && code == CBN_SELCHANGE) {
        s_->monitor = static_cast<int>(SendMessageW(monitorCombo_, CB_GETCURSEL, 0, 0));
        Changed(kChangedVisual);
    } else if (id == ID_CENTER && code == BN_CLICKED) {
        s_->offsetX = s_->offsetY = 0;
        SyncFromSettings();
        Changed(kChangedVisual);
    } else if ((id == ID_MODS || id == ID_KEY_TOGGLE || id == ID_KEY_OPEN) && code == CBN_SELCHANGE) {
        const int sel = static_cast<int>(SendMessageW(ctl, CB_GETCURSEL, 0, 0));
        if (sel < 0) return;
        if (id == ID_MODS) s_->hotkeyMods = kMods[sel].mods;
        else if (id == ID_KEY_TOGGLE) s_->hotkeyToggle = Keys()[sel].vk;
        else s_->hotkeyOpen = Keys()[sel].vk;
        Changed(kChangedHotkeys);
    } else if (id == ID_RESET && code == BN_CLICKED) {
        if (MessageBoxW(hwnd_, L"Reset every setting to its default value?", L"OLDR Crosshair",
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
            *s_ = Settings();
            SyncFromSettings();
            Changed(kChangedAny);
        }
    } else if (id == ID_OPEN_FOLDER && code == BN_CLICKED) {
        ShellExecuteW(hwnd_, L"open", ConfigDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void SettingsWindow::OnDrawItem(const DRAWITEMSTRUCT* dis) {
    const int id = static_cast<int>(dis->CtlID);
    if (id < ID_COLOR_BASE || id >= ID_COLOR_BASE + CO_COUNT) return;
    const ColorDef& d = kColors[id - ID_COLOR_BASE];
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    RECT rc = dis->rcItem;
    FillRectColor(dis->hDC, rc, GetSysColor(COLOR_BTNFACE));
    RECT inner = rc;
    InflateRect(&inner, -2, -2);
    COLORREF c = ToColorRef(s_->*(d.field));
    if (disabled) {
        const COLORREF g = GetSysColor(COLOR_BTNFACE);
        c = RGB((GetRValue(c) + GetRValue(g) * 3) / 4, (GetGValue(c) + GetGValue(g) * 3) / 4,
                (GetBValue(c) + GetBValue(g) * 3) / 4);
    }
    FillRectColor(dis->hDC, inner, c);
    HBRUSH frame = CreateSolidBrush(disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(90, 90, 90));
    FrameRect(dis->hDC, &rc, frame);
    DeleteObject(frame);
    if (dis->itemState & ODS_FOCUS) {
        RECT f = rc;
        InflateRect(&f, -3, -3);
        DrawFocusRect(dis->hDC, &f);
    }
}

// ---------------------------------------------------------------- gallery & preview
void SettingsWindow::PaintGallery(HWND wnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(wnd, &ps);
    RECT rc;
    GetClientRect(wnd, &rc);
    const int w = rc.right, h = rc.bottom;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old = SelectObject(mem, bm);
    FillRectColor(mem, rc, RGB(28, 28, 32));

    const int labelH = S(26);
    const int cellW = w / kGalleryCols;
    const int rows = (kIconCount + kGalleryCols - 1) / kGalleryCols;
    const int cellH = (h - labelH) / rows;
    if (tiles_.empty() || tilePx_ != std::min(cellW, cellH)) {
        tilePx_ = std::min(cellW, cellH);
        tiles_.assign(kIconCount, {});
        for (int i = 0; i < kIconCount; ++i) Renderer::RenderTile(i, tilePx_, 2.0f, tiles_[i]);
    }

    for (int i = 0; i < kIconCount; ++i) {
        const int col = i % kGalleryCols, row = i / kGalleryCols;
        RECT cell{col * cellW + S(3), row * cellH + S(3), (col + 1) * cellW - S(3), (row + 1) * cellH - S(3)};
        const bool selected = i == s_->icon, hover = i == hoverIcon_;
        FillRectColor(mem, cell, selected ? RGB(58, 46, 34) : hover ? RGB(56, 56, 66) : RGB(40, 40, 46));
        if (selected) {
            HBRUSH br = CreateSolidBrush(RGB(232, 138, 40));
            for (int t = 0; t < std::max(1, S(2)); ++t) {
                FrameRect(mem, &cell, br);
                InflateRect(&cell, -1, -1);
            }
            DeleteObject(br);
        }
        const int x = col * cellW + (cellW - tilePx_) / 2, y = row * cellH + (cellH - tilePx_) / 2;
        BlendMask(mem, x, y, tilePx_, tilePx_, tiles_[i], selected ? RGB(255, 190, 110) : RGB(205, 205, 215));
    }

    const int shown = hoverIcon_ >= 0 ? hoverIcon_ : s_->icon;
    wchar_t text[96], name[48], id[48];
    MultiByteToWideChar(CP_UTF8, 0, kIcons[shown].name, -1, name, 48);
    MultiByteToWideChar(CP_UTF8, 0, kIcons[shown].id, -1, id, 48);
    std::swprintf(text, 96, L"%ls   ·   lucide:%ls", name, id);
    RECT lr{S(8), h - labelH, w - S(8), h};
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(170, 170, 182));
    HGDIOBJ oldFont = SelectObject(mem, font_);
    DrawTextW(mem, text, -1, &lr, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
    SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(wnd, &ps);
}

void SettingsWindow::GalleryMouse(UINT msg, LPARAM lp) {
    RECT rc;
    GetClientRect(gallery_, &rc);
    const int labelH = S(26);
    const int cellW = rc.right / kGalleryCols;
    const int rows = (kIconCount + kGalleryCols - 1) / kGalleryCols;
    const int cellH = (rc.bottom - labelH) / rows;
    const int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
    int idx = -1;
    if (x >= 0 && y >= 0 && y < cellH * rows) {
        const int i = (y / cellH) * kGalleryCols + x / cellW;
        if (i < kIconCount && x / cellW < kGalleryCols) idx = i;
    }
    if (msg == WM_MOUSEMOVE) {
        if (idx != hoverIcon_) {
            hoverIcon_ = idx;
            InvalidateRect(gallery_, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, gallery_, 0};
        TrackMouseEvent(&tme);
    } else if (msg == WM_MOUSELEAVE) {
        hoverIcon_ = -1;
        InvalidateRect(gallery_, nullptr, FALSE);
    } else if (msg == WM_LBUTTONDOWN && idx >= 0) {
        s_->icon = idx;
        InvalidateRect(gallery_, nullptr, FALSE);
        Changed(kChangedVisual);
    }
}

void SettingsWindow::PaintPreview(HWND wnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(wnd, &ps);
    RECT rc;
    GetClientRect(wnd, &rc);
    const int w = rc.right, h = rc.bottom;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old = SelectObject(mem, bm);
    FillRectColor(mem, rc, RGB(22, 22, 26));

    // subtle grid, brighter center axes
    const int step = S(20);
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(32, 32, 38));
    HPEN axis = CreatePen(PS_SOLID, 1, RGB(48, 48, 58));
    HGDIOBJ oldPen = SelectObject(mem, grid);
    for (int x = w / 2 % step; x < w; x += step) { MoveToEx(mem, x, 0, nullptr); LineTo(mem, x, h); }
    for (int y = h / 2 % step; y < h; y += step) { MoveToEx(mem, 0, y, nullptr); LineTo(mem, w, y); }
    SelectObject(mem, axis);
    MoveToEx(mem, w / 2, 0, nullptr); LineTo(mem, w / 2, h);
    MoveToEx(mem, 0, h / 2, nullptr); LineTo(mem, w, h / 2);
    SelectObject(mem, oldPen);
    DeleteObject(grid);
    DeleteObject(axis);

    // Shrink the settings so the whole crosshair (glow included) fits the panel.
    Settings p = *s_;
    const double canvas = p.size * 1.5 + (p.glow ? p.glowRadius * 4.0 + 4 : 0);
    const double room = std::min(w, h) - S(16);
    if (canvas > room) {
        const double f = room / canvas;
        p.size = std::max(8, static_cast<int>(p.size * f));
        p.glowRadius = std::max(1, static_cast<int>(p.glowRadius * f));
        p.dotSize = std::max(1, static_cast<int>(p.dotSize * f));
    }
    const double seconds = (GetTickCount64() - startTick_) / 1000.0;
    const Bitmap& bmp = previewRenderer_.Compose(p, seconds, nullptr);
    if (s_->enabled) BlendPremul(mem, (w - bmp.w) / 2, (h - bmp.h) / 2, bmp.w, bmp.h, bmp.px.data());

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(110, 110, 124));
    HGDIOBJ oldFont = SelectObject(mem, fontSmall_);
    RECT cap{S(8), S(6), w - S(8), S(24)};
    DrawTextW(mem, s_->enabled ? L"PREVIEW" : L"PREVIEW  (crosshair is hidden)", -1, &cap, DT_SINGLELINE | DT_LEFT);
    SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(wnd, &ps);
}

// ---------------------------------------------------------------- window procedures
LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT result = 0;
    if (cb_.extra && cb_.extra(msg, wp, lp, result)) return result;

    switch (msg) {
        case WM_COMMAND:
            OnCommand(wp, lp);
            return 0;
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lp);
            if (hdr && hdr->hwndFrom == tab_ && hdr->code == TCN_SELCHANGE)
                ShowPage(static_cast<int>(SendMessageW(tab_, TCM_GETCURSEL, 0, 0)));
            return 0;
        }
        case WM_TIMER:
            if (wp == kPreviewTimer && preview_) InvalidateRect(preview_, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            DestroyUi();
            BuildUi();
            return 0;
        }
        case WM_CLOSE:
            if (s_->minimizeToTray) Hide();
            else DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            DestroyUi();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

LRESULT CALLBACK SettingsWindow::MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = static_cast<SettingsWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self || !self->s_) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->HandleMessage(msg, wp, lp);
}

LRESULT CALLBACK SettingsWindow::GalleryProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->s_) {
        switch (msg) {
            case WM_PAINT: self->PaintGallery(hwnd); return 0;
            case WM_ERASEBKGND: return 1;
            case WM_MOUSEMOVE:
            case WM_MOUSELEAVE:
            case WM_LBUTTONDOWN: self->GalleryMouse(msg, lp); return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK SettingsWindow::PreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->s_) {
        if (msg == WM_PAINT) { self->PaintPreview(hwnd); return 0; }
        if (msg == WM_ERASEBKGND) return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

INT_PTR CALLBACK SettingsWindow::PageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, lp);
        return TRUE;
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return FALSE;
    switch (msg) {
        case WM_COMMAND: self->OnCommand(wp, lp); return TRUE;
        case WM_HSCROLL: self->OnHScroll(lp); return TRUE;
        case WM_DRAWITEM:
            self->OnDrawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, TRUE);
            return TRUE;
    }
    return FALSE;
}

}  // namespace oldr
