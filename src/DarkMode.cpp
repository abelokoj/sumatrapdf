/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// Everything that talks to darkmodelib. The rest of the app calls the functions
// in DarkMode.h and never names DarkMode:: itself, so the conditions that
// used to be repeated at ~40 call sites - is the library compiled in, is it
// enabled, is the current theme the default one - live here instead.

#include "base/Base.h"
#include "base/Win.h"

#include <commdlg.h>
#include <dwmapi.h>
#include "base/WinDynCalls.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "gui/VirtHost.h"

#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "DisplayModel.h"
#include "RenderCache.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "AppTools.h"
#include "Theme.h"
#include "gui/win/TabsCtrl.h"

#include "DarkModeSubclass.h" // IWYU pragma: keep
#include "DarkMode.h"
static void RoundPopupMenu(HWND hwnd);
#include "ScaledWindowCaption.h"

// darkmodelib only supports the architectures we still ship it for; older
// 32-bit builds run without it
#if !defined(_DARKMODELIB_NOT_USED) && \
    (defined(__x86_64__) || defined(_M_X64) || defined(__arm64__) || defined(__arm64) || defined(_M_ARM64))
static bool gUseDarkModeLib = true;
#else
static bool gUseDarkModeLib = false;
#endif

bool DarkModeIsActive() {
    return gUseDarkModeLib && DarkMode::isEnabled();
}

Color DarkModeDialogBgColor() {
    if (DarkModeIsActive()) {
        return ThemeWindowControlBackgroundColor();
    }
    return MkGray(0xee);
}

static void RoundPopupMenu(HWND hwnd);

void WindowApplyScaledCaption(HWND hwnd) {
    ApplyScaledWindowCaption(hwnd);
}

bool WindowApplyRoundedCorners(HWND hwnd) {
    if (!hwnd) {
        return false;
    }
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (style & WS_CHILD) return false;
    if ((style & WS_CAPTION) != WS_CAPTION) {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (!(style & WS_POPUP) || !WindowBaseFromHwnd(hwnd) || (exStyle & (WS_EX_LAYERED | WS_EX_TRANSPARENT)))
            return false;
        RoundPopupMenu(hwnd);
        return true;
    }
    // DWM handles maximized and snapped windows; older Windows ignores this attribute.
    DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference)));
}

static void StyleWindowScrollbars(HWND hwnd);

#if IS_DEBUG
static int nativePopupProbeCount = 0;
static int menuRegionBuildCount = 0;
#endif

static bool IsNativeCornerPopup(HWND hwnd) {
    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) return false;
#if IS_DEBUG
    nativePopupProbeCount++;
#endif
    WCHAR name[48]{};
    GetClassNameW(hwnd, name, dimof(name));
    return wcscmp(name, L"#32768") == 0 || _wcsicmp(name, L"ComboLBox") == 0 || _wcsicmp(name, TOOLTIPS_CLASSW) == 0;
}

static LRESULT CALLBACK WindowCornersHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND) {
        if (IsNativeCornerPopup((HWND)wp)) RoundPopupMenu((HWND)wp);
    }
    if (code == HCBT_ACTIVATE) {
        WindowApplyRoundedCorners((HWND)wp);
        ApplyScaledWindowCaption((HWND)wp);
        RoundChildControls((HWND)wp);
        StyleWindowScrollbars((HWND)wp);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

struct MenuRegionCache {
    Size size{};
    int diameter = 0;
    HRGN rounded = nullptr;
    HRGN previous = CreateRectRgn(0, 0, 0, 0);
    ~MenuRegionCache() {
        if (rounded) DeleteObject(rounded);
        if (previous) DeleteObject(previous);
    }
};

static void ApplyMenuRegion(HWND hwnd, MenuRegionCache* cache) {
    if (IsZoomed(hwnd)) {
        if (GetWindowRgn(hwnd, cache->previous) != ERROR) SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    Size size = HwndWindowRect(hwnd).Size();
    if (size.dx <= 0 || size.dy <= 0) return;
    int diameter = std::min(2 * GetAppCornerRadius(DpiGetForHwnd(hwnd), 6), std::min(size.dx, size.dy));
    if (!cache->rounded || cache->size != size || cache->diameter != diameter) {
        HRGN rounded = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
#if IS_DEBUG
        menuRegionBuildCount++;
#endif
        if (!rounded) return;
        if (cache->rounded) DeleteObject(cache->rounded);
        cache->rounded = rounded;
        cache->size = size;
        cache->diameter = diameter;
    }
    if (!cache->previous) return;
    if (GetWindowRgn(hwnd, cache->previous) != ERROR && EqualRgn(cache->previous, cache->rounded)) return;
    HRGN applied = CreateRectRgn(0, 0, 0, 0);
    if (!applied) return;
    if (CombineRgn(applied, cache->rounded, nullptr, RGN_COPY) == ERROR ||
        !SetWindowRgn(hwnd, applied, IsWindowVisible(hwnd)))
        DeleteObject(applied);
}

static void DrawMenuBorder(HDC dc, Size size, int diameter, Rect client, Color background) {
    int saved = SaveDC(dc);
    if (!saved) return;
    ExcludeClipRect(dc, client.x, client.y, client.Right(), client.Bottom());
    GfxHdc gfx(dc);
    // Remove the native rectangular frame before drawing the rounded perimeter.
    gfx.FillRect({0, 0, size.dx, size.dy}, background);
    RestoreDC(dc, saved);
    gfx.FillRoundedRect({0, 0, size.dx, size.dy}, diameter, kColorTransparent, ThemeEdgeColor());
}

static void PaintMenuBorder(HWND hwnd) {
    if (!gSettings || !IsWindowVisible(hwnd)) return;
    Size size = HwndWindowRect(hwnd).Size();
    HDC dc = GetWindowDC(hwnd);
    if (!dc) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, nullptr, (POINT*)&client, 2);
    Rect bounds = HwndWindowRect(hwnd);
    OffsetRect(&client, -bounds.x, -bounds.y);
    Color background = ThemeMainWindowBackgroundColor();
    if (WindowBase* window = WindowBaseFromHwnd(hwnd))
        background = window->GetColor(kColWinBg);
    else {
        WCHAR name[48]{};
        GetClassNameW(hwnd, name, dimof(name));
        if (_wcsicmp(name, L"ComboLBox") == 0)
            background = ThemeWindowControlBackgroundColor();
        else if (_wcsicmp(name, TOOLTIPS_CLASSW) == 0)
            background = (Color)SendMessageW(hwnd, TTM_GETTIPBKCOLOR, 0, 0);
    }
    DrawMenuBorder(dc, size, 2 * GetAppCornerRadius(DpiGetForHwnd(hwnd), 6), ToRect(client), background);
    ReleaseDC(hwnd, dc);
}

static LRESULT CALLBACK MenuRoundSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* cache = (MenuRegionCache*)data;
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, MenuRoundSubclass, id);
        delete cache;
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    static thread_local bool applying = false;
    if (!applying && (msg == WM_WINDOWPOSCHANGED || msg == WM_NCPAINT || msg == WM_PAINT || msg == WM_SHOWWINDOW ||
                      msg == WM_DPICHANGED || msg == WM_SETFONT || msg == WM_THEMECHANGED)) {
        applying = true;
        ApplyMenuRegion(hwnd, cache);
        if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) != WS_CAPTION &&
            (msg == WM_NCPAINT || msg == WM_PAINT || msg == WM_THEMECHANGED))
            PaintMenuBorder(hwnd);
        applying = false;
    }
    return result;
}

static void RoundPopupMenu(HWND hwnd) {
    constexpr UINT_PTR kMenuRoundSubclassId = 1;
    DWORD_PTR data = 0;
    if (!GetWindowSubclass(hwnd, MenuRoundSubclass, kMenuRoundSubclassId, &data)) {
        auto* cache = new MenuRegionCache;
        if (!SetWindowSubclass(hwnd, MenuRoundSubclass, kMenuRoundSubclassId, (DWORD_PTR)cache)) {
            delete cache;
            return;
        }
        data = (DWORD_PTR)cache;
    }
    ApplyMenuRegion(hwnd, (MenuRegionCache*)data);
}

static LRESULT CALLBACK MenuCornersHook(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        auto* message = (CWPRETSTRUCT*)lp;
        if (message->message == WM_NCCREATE || message->message == WM_WINDOWPOSCHANGED ||
            message->message == WM_SHOWWINDOW) {
            if (IsNativeCornerPopup(message->hwnd)) RoundPopupMenu(message->hwnd);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void WindowCornersInit() {
    gUiCornerRadius = GetAppCornerRadius;
    // Common dialogs and message boxes also pass through the owning UI thread.
    static thread_local HHOOK hook = nullptr;
    if (!hook) {
        hook = SetWindowsHookExW(WH_CBT, WindowCornersHook, nullptr, GetCurrentThreadId());
    }
    static thread_local HHOOK menuHook = nullptr;
    if (!menuHook) menuHook = SetWindowsHookExW(WH_CALLWNDPROCRET, MenuCornersHook, nullptr, GetCurrentThreadId());
}

void DarkModeInit() {
    WindowCornersInit();
    gUiInstallScrollbar = InstallAppScrollbar;
    gUiScrollbarTrackPos = AppScrollbarTrackPos;
    gUiScrollbarInset = AppScrollbarInset;
    gUiScrollbarWidth = GetAppScrollbarWidth;
    // WindowBase::UpdateTheme() re-applies dark mode through this hook, so
    // gui/ never names darkmodelib. Installed even when the lib isn't used:
    // DarkModeApplyToWindow() no-ops then
    gWindowBaseApplyDarkMode = DarkModeApplyToWindow;
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::initDarkMode();
    DarkMode::setColorizeTitleBarConfig(true);
}

// push the current palette (which may be the system's, in high contrast mode)
// to darkmodelib, which draws the controls we don't draw ourselves
void DarkModeApplyThemeColors() {
    if (!gUseDarkModeLib) {
        return;
    }
    // TODO: we should apply themes to every theme other than 0
    // but in Solarized Light in Find dialog's input field text is invisible i.e. black
    // UINT mode = themeIdx == 0 ? kModeClassic : kModeDark;
    const bool isDarkCol = DarkMode::isColorDark(ThemeWindowControlBackgroundColor());
    DarkMode::DarkModeType modeType = DarkMode::DarkModeType::light;
    if (isDarkCol) {
        modeType = DarkMode::DarkModeType::dark;
    } else if (IsCurrentThemeDefault()) {
        modeType = DarkMode::DarkModeType::classic;
    }
    const UINT mode = static_cast<UINT>(modeType);
    DarkMode::setDarkModeConfigEx(mode);
    DarkMode::setDefaultColors(false);

    DarkMode::setBackgroundColor(ThemeWindowBackgroundColor());
    DarkMode::setCtrlBackgroundColor(ThemeWindowControlBackgroundColor());
    Color ctrlBg = ThemeWindowControlBackgroundColor();
    DarkMode::setHotBackgroundColor(ThemeHotBackgroundColor());
    DarkMode::setTextColor(ThemeWindowTextColor());
    DarkMode::setDarkerTextColor(ThemeWindowDarkerTextColor());
    DarkMode::setDisabledTextColor(ThemeWindowTextDisabledColor());
    DarkMode::setDlgBackgroundColor(ctrlBg);
    DarkMode::setLinkTextColor(ThemeWindowLinkColor());
    DarkMode::setEdgeColor(ThemeEdgeColor());
    DarkMode::setHotEdgeColor(ThemeHotEdgeColor());
    DarkMode::setDisabledEdgeColor(ThemeDisabledEdgeColor());
    DarkMode::setErrorBackgroundColor(ThemeErrorBackgroundColor());
    DarkMode::updateThemeBrushesAndPens();
    DarkMode::updateCommonDlgsBrushes();

    DarkMode::setViewTextColor(ThemeWindowTextColor());
    DarkMode::setViewBackgroundColor(ThemeWindowControlBackgroundColor());
    DarkMode::calculateTreeViewStyle();
}

void DarkModeRememberTreeViewStyle() {
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::setPrevTreeViewStyle();
}

static BOOL CALLBACK StyleChildScrollbar(HWND hwnd, LPARAM) {
    WCHAR klass[64]{};
    GetClassNameW(hwnd, klass, dimofi(klass));
    // Document canvases select their own hidden/overlay mode.
    if (_wcsicmp(klass, L"SUMATRA_PDF_CANVAS") == 0) return TRUE;
    if (_wcsicmp(klass, L"COMBOBOX") == 0) {
        COMBOBOXINFO info{sizeof(info)};
        if (GetComboBoxInfo(hwnd, &info)) {
            RoundPopupMenu(info.hwndList);
            ControlBase* control = ControlFromHwnd(hwnd);
            if (!control || !str::Eq(Str(control->GetKind()), StrL("dropdown")) || IsWindowVisible(info.hwndList))
                InstallAppScrollbar(info.hwndList);
        }
        return TRUE;
    }
    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL)) InstallAppScrollbar(hwnd);
    return TRUE;
}

static void StyleWindowScrollbars(HWND hwnd) {
    StyleChildScrollbar(hwnd, 0);
    EnumChildWindows(hwnd, StyleChildScrollbar, 0);
}

static constexpr WCHAR kCustomCheckboxPaint[] = L"SumatraCustomCheckboxPaint";

void DarkModeUseCustomCheckboxPaint(HWND hwnd) {
    SetPropW(hwnd, kCustomCheckboxPaint, (HANDLE)1);
    if (gUseDarkModeLib) DarkMode::removeCheckboxOrRadioBtnCtrlSubclass(hwnd);
}

static BOOL CALLBACK PreserveCustomCheckboxPaint(HWND hwnd, LPARAM) {
    if (GetPropW(hwnd, kCustomCheckboxPaint)) DarkModeUseCustomCheckboxPaint(hwnd);
    return TRUE;
}

void DarkModeApplyToWindow(HWND hwnd) {
    WindowApplyRoundedCorners(hwnd);
    RoundChildControls(hwnd);
    StyleWindowScrollbars(hwnd);
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::setDarkWndSafe(hwnd);
    EnumChildWindows(hwnd, PreserveCustomCheckboxPaint, 0);
}

void DarkModeApplyToWindowAndEraseBg(HWND hwnd) {
    WindowApplyRoundedCorners(hwnd);
    RoundChildControls(hwnd);
    StyleWindowScrollbars(hwnd);
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::setDarkWndSafe(hwnd);
    DarkMode::setWindowEraseBgSubclass(hwnd);
}

void DarkModeApplyToNotifyWindowAndEraseBg(HWND hwnd) {
    WindowApplyRoundedCorners(hwnd);
    RoundChildControls(hwnd);
    StyleWindowScrollbars(hwnd);
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::setDarkWndNotifySafe(hwnd);
    DarkMode::setWindowEraseBgSubclass(hwnd);
}

void DarkModeApplyToTitleBar(HWND hwnd) {
    WindowApplyRoundedCorners(hwnd);
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::setDarkTitleBarEx(hwnd, true);
}

// Some of our popup windows create their children after the window itself, and
// darkmodelib only themes the children that exist when it is called - so they
// call this once the children are there (issues #5894, #5895).
void DarkModeApplyToPopupWindow(HWND hwnd) {
    WindowApplyRoundedCorners(hwnd);
    StyleWindowScrollbars(hwnd);
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::setDarkTitleBarEx(hwnd, true);
    if (IsCurrentThemeDefault()) {
        return;
    }
    // darkmodelib answers WM_CTLCOLORLISTBOX for a combo's drop-down here (the
    // combo forwards it to us); without this subclass the list keeps the
    // system colors and is white in a dark theme (issue #6083)
    DarkMode::setWindowCtlColorSubclass(hwnd);
    DarkMode::setChildCtrlsSubclassAndTheme(hwnd);
    DarkMode::setWindowNotifyCustomDrawSubclass(hwnd);
}

void DarkModeApplyToMenuWindow(HWND hwnd) {
    if (hwnd && GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId()) RoundPopupMenu(hwnd);
    if (!DarkModeIsActive() || !hwnd) {
        return;
    }
    DarkMode::setDarkTitleBarEx(hwnd, false);
}

void DarkModeApplyToMenuBar(HWND hwndRebar) {
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::setWindowNotifyCustomDrawSubclass(hwndRebar);
    DarkMode::setChildCtrlsSubclassAndTheme(hwndRebar);
}

void DarkModeApplyToChildControls(HWND hwnd) {
    StyleWindowScrollbars(hwnd);
    if (!gUseDarkModeLib || IsCurrentThemeDefault()) {
        return;
    }
    DarkMode::setChildCtrlsSubclassAndTheme(hwnd);
}

// Infotip colors come from TooltipApplyColors (unthemed TTM_SETTIP*).
// DarkMode_Explorer would ignore those colors and, with Windows in light
// mode, leave a white bubble (issue #6000).
static void ApplyToInfotip(MainWindow* win) {
    if (!win || !win->infotip || !win->infotip->hwnd) {
        return;
    }
    win->infotip->SetFont(GetAppFont());
}

void DarkModeApplyToNewFrame(MainWindow* win) {
    StyleWindowScrollbars(win->hwndFrame);
    WindowApplyRoundedCorners(win->hwndFrame);
    if (!gUseDarkModeLib || IsCurrentThemeDefault()) {
        return;
    }
    DarkMode::setDarkTitleBarEx(win->hwndFrame, true);
    DarkMode::setChildCtrlsSubclassAndTheme(win->hwndFrame);
    DarkMode::removeTabCtrlSubclass(win->tabsCtrl->hwnd);
    DarkMode::setDarkScrollBar(win->hwndCanvas);
    DarkMode::setWindowMenuBarSubclass(win->hwndFrame);
    ApplyToInfotip(win);
}

// ChooseColorW with darkmodelib's hook so the system color dialog follows the
// current theme (darkmodelib 0.76).
bool DarkModeChooseColor(tagCHOOSECOLORW* cc) {
    if (!cc) {
        return false;
    }
    if (gUseDarkModeLib) {
        return DarkMode::darkChooseColorW(cc);
    }
    return ChooseColorW(cc);
}

void DarkModeApplyToFrameAfterThemeChange(MainWindow* win) {
    StyleWindowScrollbars(win->hwndFrame);
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::setDarkTitleBarEx(win->hwndFrame, true);
    DarkMode::setChildCtrlsTheme(win->hwndFrame);
    if (win->tabsCtrl) {
        DarkMode::removeTabCtrlSubclass(win->tabsCtrl->hwnd);
    }
    DarkMode::setDarkScrollBar(win->hwndCanvas);
    DarkMode::setWindowMenuBarSubclass(win->hwndFrame);
    ApplyToInfotip(win);
}

#if IS_DEBUG
#include "base/tests/UtAssert.h"

static void MenuBorderPixelTests() {
    RenderCache* savedCache = gRenderCache;
    if (!savedCache) gRenderCache = new RenderCache();
    defer {
        if (!savedCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    Size size(240, 160);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size.dx, -size.dy, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    utassert(dc && bitmap && pixels);
    if (!dc || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    int theme = ThemeGetCurrentIndex();
    if (!ThemeGetCount()) CreateThemeCommands();
    for (Str name : {StrL("Sumatra Light"), StrL("Modern Green Dark")}) {
        str::ReplaceWithCopy(&gSettings->theme, name);
        SetCurrentThemeFromSettings();
        for (int diameter : {12, 30, 60}) {
            GfxHdc gfx(dc);
            Color nativeFrame = RGB(253, 0, 253), content = RGB(11, 73, 119);
            gfx.FillRect({0, 0, size.dx, size.dy}, nativeFrame);
            gfx.FillRect({3, 3, size.dx - 6, size.dy - 6}, content);
            DrawMenuBorder(dc, size, diameter, {3, 3, size.dx - 6, size.dy - 6}, ThemeMainWindowBackgroundColor());
            GdiFlush();
            utassert(GetPixel(dc, 1, diameter) != nativeFrame);
            utassert(GetPixel(dc, diameter, 1) != nativeFrame);
            utassert(GetPixel(dc, size.dx - 2, size.dy - diameter - 1) != nativeFrame);
            utassert(GetPixel(dc, size.dx - diameter - 1, size.dy - 2) != nativeFrame);
            utassert(GetPixel(dc, size.dx / 2, size.dy / 2) == content);
        }
    }
    SetThemeByIndex(theme);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

struct MenuCornerProbe {
    int phase = 0;
    int popupCount = 0;
    int visibleCount = 0;
    HWND menuWindow = nullptr;
    HMENU menu = nullptr;
    ULONGLONG started = 0;
    bool clipped = true;
    bool restored = true;
};

static BOOL CALLBACK ProbeMenuCorners(HWND hwnd, LPARAM arg) {
    WCHAR name[32]{};
    GetClassNameW(hwnd, name, dimof(name));
    if (wcscmp(name, L"#32768") != 0 || !IsWindowVisible(hwnd)) return TRUE;
    auto* probe = (MenuCornerProbe*)arg;
    probe->popupCount++;
    Size size = HwndWindowRect(hwnd).Size();
    HRGN clip = CreateRectRgn(0, 0, 0, 0);
    probe->clipped &=
        GetWindowRgn(hwnd, clip) != ERROR && !PtInRegion(clip, 0, 0) && PtInRegion(clip, size.dx / 2, size.dy / 2);
    int diameter = std::min(2 * GetAppCornerRadius(DpiGetForHwnd(hwnd), 6), std::min(size.dx, size.dy));
    HRGN expected = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
    probe->clipped &= EqualRgn(clip, expected) != FALSE;
    DeleteObject(expected);
    SetWindowRgn(hwnd, nullptr, FALSE);
    SendMessageW(hwnd, WM_NCPAINT, 1, 0);
    probe->restored &= GetWindowRgn(hwnd, clip) != ERROR && !PtInRegion(clip, 0, 0);
    DeleteObject(clip);
    return TRUE;
}

static BOOL CALLBACK FindTestMenu(HWND hwnd, LPARAM arg) {
    WCHAR name[32]{};
    GetClassNameW(hwnd, name, dimof(name));
    if (wcscmp(name, L"#32768") != 0 || !IsWindowVisible(hwnd)) return TRUE;
    auto* probe = (MenuCornerProbe*)arg;
    if (!probe->menuWindow) probe->menuWindow = hwnd;
    probe->visibleCount++;
    return TRUE;
}

static void CALLBACK ProbeMenuTimer(HWND owner, UINT, UINT_PTR id, DWORD) {
    auto* probe = (MenuCornerProbe*)GetWindowLongPtrW(owner, GWLP_USERDATA);
    probe->visibleCount = 0;
    EnumThreadWindows(GetCurrentThreadId(), FindTestMenu, (LPARAM)probe);
    if (GetTickCount64() - probe->started < 2000) {
        // Open both real native popup windows without depending on keyboard focus
        // or the user's cursor selecting the parent item during this region test.
        if (probe->phase == 0 && probe->menuWindow) {
            probe->phase = 1;
            Rect root = HwndWindowRect(probe->menuWindow);
            TrackPopupMenuEx(GetSubMenu(probe->menu, 0), TPM_RECURSE | TPM_RETURNCMD | TPM_NONOTIFY | TPM_NOANIMATION,
                             root.Right(), root.y, owner, nullptr);
            KillTimer(owner, id);
            EndMenu();
            return;
        }
        if (probe->visibleCount < 2) return;
    }
    EnumThreadWindows(GetCurrentThreadId(), ProbeMenuCorners, (LPARAM)probe);
    KillTimer(owner, id);
    EndMenu();
}

static void NativeMenuCornerTest() {
    WindowCornersInit();
    HWND owner = CreateWindowExW(0, L"STATIC", L"Native menu test", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
    utassert(owner != nullptr);
    if (!owner) return;
    HMENU menu = CreatePopupMenu();
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING, 1, L"Command");
    AppendMenuW(menu, MF_POPUP | MF_STRING, (UINT_PTR)sub, L"Submenu");
    MenuCornerProbe probe;
    probe.menu = menu;
    probe.started = GetTickCount64();
    SetWindowLongPtrW(owner, GWLP_USERDATA, (LONG_PTR)&probe);
    UINT_PTR timer = SetTimer(owner, 1, 40, ProbeMenuTimer);
    utassert(timer != 0);
    if (timer) {
        TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_NOANIMATION, 10, 10, owner, nullptr);
        KillTimer(owner, timer);
        utassert(probe.popupCount >= 2);
        utassert(probe.clipped);
        utassert(probe.restored);
    }
    DestroyMenu(menu);
    DestroyWindow(owner);
}

void WindowCorners_UnitTests() {
    Settings* savedSettings = gSettings;
    if (!gSettings) gSettings = NewSettings({});
    defer {
        if (!savedSettings) {
            DeleteSettings(gSettings);
            gSettings = nullptr;
        }
    };
    utassert(!WindowApplyRoundedCorners(nullptr));
    WindowCornersInit();
    {
        HWND parent = CreateWindowExW(0, WC_STATICW, L"Relayout test", WS_POPUP, -10000, -10000, 300, 200, nullptr,
                                      nullptr, GetInstance(), nullptr);
        HWND child =
            CreateWindowExW(0, WC_EDITW, L"", WS_CHILD, 0, 0, 100, 30, parent, nullptr, GetInstance(), nullptr);
        utassert(parent && child);
        if (parent && child) {
            int probes = nativePopupProbeCount;
            for (int i = 0; i < 100; i++) {
                SetWindowPos(child, nullptr, 0, i & 1, 100, 30, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            }
            utassert(nativePopupProbeCount == probes);
            RoundPopupMenu(parent);
            int builds = menuRegionBuildCount;
            for (int i = 0; i < 100; i++) {
                SetWindowPos(parent, nullptr, -10000, -10000 + (i & 1), 300, 200,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            }
            utassert(menuRegionBuildCount == builds);
        }
        if (parent) DestroyWindow(parent);
    }
    {
        int oldScale = gSettings->interfaceScale;
        int oldFontSize = gSettings->uIFontSize;
        defer {
            gSettings->interfaceScale = oldScale;
            gSettings->uIFontSize = oldFontSize;
            RefreshUiFonts();
        };
        WindowBase dialog;
        dialog.CreateCustom({.title = StrL("Scaled caption test"),
                             .style = WS_POPUPWINDOW | WS_CAPTION | WS_THICKFRAME,
                             .pos = {-10000, -10000, 400, 300},
                             .visible = false});
        utassert(dialog.hwnd != nullptr);
        if (dialog.hwnd) {
            gSettings->interfaceScale = 200;
            gSettings->uIFontSize = 22;
            RefreshUiFonts();
            Rect before = HwndClientRect(dialog.hwnd);
            ApplyScaledWindowCaption(dialog.hwnd);
            Rect after = HwndClientRect(dialog.hwnd);
            utassert(before.dx == after.dx && before.dy == after.dy);
            HRGN shape = CreateRectRgn(0, 0, 0, 0);
            utassert(GetWindowRgn(dialog.hwnd, shape) != ERROR);
            utassert(!PtInRegion(shape, 0, 0));
            SetWindowPos(dialog.hwnd, nullptr, 0, 0, 520, 420, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
            GetWindowRgn(dialog.hwnd, shape);
            utassert(!PtInRegion(shape, 519, 419) && PtInRegion(shape, 500, 200));
            utassert(!PtInRegion(shape, 519, 0) && !PtInRegion(shape, 0, 419));
            LONG_PTR normalStyle = GetWindowLongPtrW(dialog.hwnd, GWL_STYLE);
            SetWindowLongPtrW(dialog.hwnd, GWL_STYLE, normalStyle | WS_MAXIMIZE);
            RoundPopupMenu(dialog.hwnd);
            utassert(GetWindowRgn(dialog.hwnd, shape) == ERROR);
            SetWindowLongPtrW(dialog.hwnd, GWL_STYLE, normalStyle);
            RoundPopupMenu(dialog.hwnd);
            utassert(GetWindowRgn(dialog.hwnd, shape) != ERROR && !PtInRegion(shape, 0, 0));
            DeleteObject(shape);
            BOOL nativeCaption = TRUE;
            if (SUCCEEDED(DwmGetWindowAttribute(dialog.hwnd, DWMWA_NCRENDERING_ENABLED, &nativeCaption,
                                                sizeof(nativeCaption))))
                utassert(!nativeCaption);
            DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_AUTO;
            if (SUCCEEDED(DwmGetWindowAttribute(dialog.hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop))))
                utassert(backdrop == DWMSBT_NONE);
            Rect caption = AppCaptionRect(dialog.hwnd);
            utassert(caption.dy >= PlatformFontLineHeight(GetAppFontForDpi(dialog.GetDpi())));
            Rect window = HwndWindowRect(dialog.hwnd);
            HDC frameDc = CreateCompatibleDC(nullptr);
            HDC desktop = GetDC(nullptr);
            HBITMAP frameBitmap = CreateCompatibleBitmap(desktop, window.dx, window.dy);
            ReleaseDC(nullptr, desktop);
            HGDIOBJ previousBitmap = SelectObject(frameDc, frameBitmap);
            GfxHdc frameGfx(frameDc);
            Color stale = RGB(253, 0, 253), content = RGB(11, 73, 119);
            frameGfx.FillRect({0, 0, window.dx, window.dy}, stale);
            frameGfx.FillRect({window.dx / 2, window.dy / 2, 10, 10}, content);
            ScaledWindowCaption state;
            PaintAppCaption(dialog.hwnd, &state, frameDc);
            GdiFlush();
            utassert(GetPixel(frameDc, 3, window.dy / 2) != stale);
            utassert(GetPixel(frameDc, window.dx / 2, window.dy - 3) != stale);
            utassert(GetPixel(frameDc, window.dx / 2, window.dy / 2) == content);
            state.hot = HTCLOSE;
            PaintAppCaption(dialog.hwnd, &state, frameDc);
            GdiFlush();
            Rect hoveredClose = AppCaptionButton(dialog.hwnd, HTCLOSE);
            utassert(GetPixel(frameDc, hoveredClose.x + hoveredClose.dx / 2, hoveredClose.y + hoveredClose.dy / 4) ==
                     (gColsCloseBtn[kColCloseCircleHover] & 0x00ffffff));
            SelectObject(frameDc, previousBitmap);
            DeleteObject(frameBitmap);
            DeleteDC(frameDc);
            Point title = {caption.x + caption.dx / 2, caption.y + caption.dy / 2};
            utassert(SendMessageW(dialog.hwnd, WM_NCHITTEST, 0, MAKELPARAM(window.x + title.x, window.y + title.y)) ==
                     HTCAPTION);
            Rect close = AppCaptionButton(dialog.hwnd, HTCLOSE);
            utassert(close.dy == caption.dy);
            utassert(SendMessageW(dialog.hwnd, WM_NCHITTEST, 0,
                                  MAKELPARAM(window.x + close.x + close.dx / 2, window.y + close.y + close.dy / 2)) ==
                     HTCLOSE);
            utassert(str::Eq(HwndGetTextTemp(dialog.hwnd), StrL("Scaled caption test")));
            const Size clientSizes[] = {{320, 240}, {640, 480}};
            for (Size expected : clientSizes) {
                ResizeHwndToClientArea(dialog.hwnd, expected.dx, expected.dy, false);
                Rect client = HwndClientRect(dialog.hwnd);
                utassert(client.dx == expected.dx);
                utassert(client.dy == expected.dy);
            }
            dialog.Destroy();
        }
    }
    MenuBorderPixelTests();
    HWND frame = CreateWindowExW(0, L"STATIC", L"Corner test", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    utassert(frame != nullptr);
    if (!frame) {
        return;
    }

    DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_DEFAULT;
    HRESULT supported = DwmGetWindowAttribute(frame, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    bool applied = WindowApplyRoundedCorners(frame);
    if (SUCCEEDED(supported)) {
        utassert(applied);
        utassert(
            SUCCEEDED(DwmGetWindowAttribute(frame, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference))));
        utassert(preference == DWMWCP_ROUND);
    }
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    utassert(GetWindowRgn(frame, region) == ERROR);
    DeleteObject(region);

    HWND child = CreateWindowExW(0, L"STATIC", L"Child", WS_CHILD | WS_CAPTION, 0, 0, 100, 30, frame, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    utassert(child != nullptr);
    utassert(!WindowApplyRoundedCorners(child));
    DestroyWindow(child);

    // Fullscreen and overlay styles must retain the caller's square-corner preference.
    SetWindowLongPtrW(frame, GWL_STYLE, WS_POPUP);
    preference = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(frame, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    utassert(!WindowApplyRoundedCorners(frame));
    if (SUCCEEDED(supported)) {
        DwmGetWindowAttribute(frame, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
        utassert(preference == DWMWCP_DONOTROUND);
    }
    DestroyWindow(frame);

    WindowBase appPopup;
    appPopup.CreateCustom({.style = WS_POPUPWINDOW,
                           .exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                           .pos = {-10000, -10000, 240, 160},
                           .visible = false});
    utassert(appPopup.hwnd != nullptr);
    if (appPopup.hwnd) {
        DarkModeApplyToWindow(appPopup.hwnd);
        HRGN popupClip = CreateRectRgn(0, 0, 0, 0);
        utassert(GetWindowRgn(appPopup.hwnd, popupClip) != ERROR);
        utassert(!PtInRegion(popupClip, 0, 0) && PtInRegion(popupClip, 120, 80));
        SetWindowPos(appPopup.hwnd, nullptr, -10000, -10000, 320, 200, SWP_NOZORDER | SWP_NOACTIVATE);
        GetWindowRgn(appPopup.hwnd, popupClip);
        utassert(!PtInRegion(popupClip, 0, 0) && PtInRegion(popupClip, 300, 180));
        DropDown drop;
        HWND combo = drop.Create({.parent = appPopup.hwnd, .deferItems = true, .visible = false});
        SetWindowPos(combo, nullptr, 10, 10, 180, 120, SWP_NOZORDER | SWP_NOACTIVATE);
        utassert(combo != nullptr);
        COMBOBOXINFO info{sizeof(info)};
        utassert(GetComboBoxInfo(combo, &info));
        DarkModeApplyToWindow(appPopup.hwnd);
        utassert(GetWindowRgn(info.hwndList, popupClip) != ERROR);
        utassert(!PtInRegion(popupClip, 0, 0));
        utassert(!GetPropW(info.hwndList, L"SumatraAppScrollbar"));
        StrVec items;
        for (int i = 0; i < 40; i++) items.Append(StrL("Scrollbar first-open test"));
        drop.SetItems(items);
        auto previousScrollbarInstaller = gUiInstallScrollbar;
        gUiInstallScrollbar = InstallAppScrollbar;
        ShowWindow(appPopup.hwnd, SW_SHOWNOACTIVATE);
        ShowWindow(combo, SW_SHOWNOACTIVATE);
        SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
        utassert(GetPropW(info.hwndList, L"SumatraAppScrollbar"));
        utassert(IsWindowVisible(info.hwndList));
        SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
        gUiInstallScrollbar = previousScrollbarInstaller;
        DeleteObject(popupClip);
        appPopup.Destroy();
    }
    WindowBase overlay;
    overlay.CreateCustom({.style = WS_POPUP,
                          .exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT,
                          .pos = {-10000, -10000, 240, 160},
                          .visible = false});
    utassert(overlay.hwnd != nullptr);
    if (overlay.hwnd) {
        utassert(!WindowApplyRoundedCorners(overlay.hwnd));
        HRGN overlayClip = CreateRectRgn(0, 0, 0, 0);
        utassert(GetWindowRgn(overlay.hwnd, overlayClip) == ERROR);
        DeleteObject(overlayClip);
        overlay.Destroy();
    }

    HWND popup = CreateWindowExW(0, L"STATIC", L"Menu shape test", WS_POPUP, 0, 0, 300, 180, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    utassert(popup != nullptr);
    if (popup) {
        int savedScale = gSettings->interfaceScale;
        int savedFontSize = gSettings->uIFontSize;
        gSettings->interfaceScale = 100;
        gSettings->uIFontSize = 18;
        RoundPopupMenu(popup);
        // Native menus can replace their window region during non-client painting.
        SetWindowRgn(popup, nullptr, FALSE);
        SendMessageW(popup, WM_NCPAINT, 1, 0);
        HRGN clip = CreateRectRgn(0, 0, 0, 0);
        utassert(GetWindowRgn(popup, clip) != ERROR);
        utassert(!PtInRegion(clip, 0, 0) && PtInRegion(clip, 150, 90));
        int popupDpi = DpiGetForHwnd(popup);
        int normalRadius = GetAppCornerRadius(popupDpi, 6);
        gSettings->interfaceScale = 150;
        gSettings->uIFontSize = 28;
        RoundPopupMenu(popup);
        GetWindowRgn(popup, clip);
        int enlargedRadius = GetAppCornerRadius(popupDpi, 6);
        utassert(enlargedRadius >= normalRadius * 2);
        Size popupSize = HwndWindowRect(popup).Size();
        int expectedDiameter = std::min(2 * enlargedRadius, std::min(popupSize.dx, popupSize.dy));
        HRGN expected =
            CreateRoundRectRgn(0, 0, popupSize.dx + 1, popupSize.dy + 1, expectedDiameter, expectedDiameter);
        utassert(EqualRgn(clip, expected));
        DeleteObject(expected);
        SetWindowPos(popup, nullptr, 0, 0, 18, 14, SWP_NOACTIVATE | SWP_NOZORDER);
        RoundPopupMenu(popup);
        GetWindowRgn(popup, clip);
        expectedDiameter = std::min(2 * enlargedRadius, 14);
        expected = CreateRoundRectRgn(0, 0, 19, 15, expectedDiameter, expectedDiameter);
        utassert(EqualRgn(clip, expected));
        DeleteObject(expected);
        SetWindowPos(popup, nullptr, 0, 0, 440, 260, SWP_NOACTIVATE | SWP_NOZORDER);
        RoundPopupMenu(popup);
        GetWindowRgn(popup, clip);
        utassert(PtInRegion(clip, 430, 250));
        utassert(!PtInRegion(clip, 0, 0));
        DeleteObject(clip);
        DestroyWindow(popup);
        gSettings->interfaceScale = savedScale;
        gSettings->uIFontSize = savedFontSize;
    }
    NativeMenuCornerTest();
}
#endif
