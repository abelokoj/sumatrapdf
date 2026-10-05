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
#include "gui/VirtCtrl.h"
#include "gui/VirtHost.h"

#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "DisplayModel.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "AppTools.h"
#include "Theme.h"
#include "gui/win/TabsCtrl.h"

#include "DarkModeSubclass.h" // IWYU pragma: keep
#include "DarkMode.h"

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

bool WindowApplyRoundedCorners(HWND hwnd) {
    if (!hwnd) {
        return false;
    }
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_CHILD) || (style & WS_CAPTION) != WS_CAPTION) {
        return false;
    }
    // DWM handles maximized and snapped windows; older Windows ignores this attribute.
    DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference)));
}

static void RoundPopupMenu(HWND hwnd);
static void StyleWindowScrollbars(HWND hwnd);

static LRESULT CALLBACK WindowCornersHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND) {
        WCHAR name[32]{};
        GetClassNameW((HWND)wp, name, dimof(name));
        if (wcscmp(name, L"#32768") == 0) RoundPopupMenu((HWND)wp);
    }
    if (code == HCBT_ACTIVATE) {
        WindowApplyRoundedCorners((HWND)wp);
        RoundChildControls((HWND)wp);
        StyleWindowScrollbars((HWND)wp);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static void ApplyMenuRegion(HWND hwnd) {
    Size size = HwndWindowRect(hwnd).Size();
    if (size.dx <= 0 || size.dy <= 0) return;
    int diameter = std::min(2 * GetAppCornerRadius(DpiGetForHwnd(hwnd), 6), std::min(size.dx, size.dy));
    HRGN rounded = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
    if (!rounded) return;
    HRGN previous = CreateRectRgn(0, 0, 0, 0);
    bool same = GetWindowRgn(hwnd, previous) != ERROR && EqualRgn(previous, rounded);
    DeleteObject(previous);
    if (same || !SetWindowRgn(hwnd, rounded, TRUE)) DeleteObject(rounded);
}

static void PaintMenuBorder(HWND hwnd) {
    Size size = HwndWindowRect(hwnd).Size();
    HDC dc = GetWindowDC(hwnd);
    if (!dc) return;
    GfxHdc gfx(dc);
    gfx.FillRoundedRect({0, 0, size.dx, size.dy}, 2 * GetAppCornerRadius(DpiGetForHwnd(hwnd), 6), kColorTransparent,
                        ThemeEdgeColor());
    ReleaseDC(hwnd, dc);
}

static LRESULT CALLBACK MenuRoundSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, MenuRoundSubclass, id);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    static thread_local bool applying = false;
    if (!applying && (msg == WM_WINDOWPOSCHANGED || msg == WM_NCPAINT || msg == WM_PAINT || msg == WM_SHOWWINDOW ||
                      msg == WM_DPICHANGED)) {
        applying = true;
        ApplyMenuRegion(hwnd);
        if (msg == WM_NCPAINT || msg == WM_PAINT) PaintMenuBorder(hwnd);
        applying = false;
    }
    return result;
}

static void RoundPopupMenu(HWND hwnd) {
    constexpr UINT_PTR kMenuRoundSubclassId = 1;
    SetWindowSubclass(hwnd, MenuRoundSubclass, kMenuRoundSubclassId, 0);
    ApplyMenuRegion(hwnd);
}

static LRESULT CALLBACK MenuCornersHook(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        auto* message = (CWPRETSTRUCT*)lp;
        if (message->message == WM_NCCREATE || message->message == WM_WINDOWPOSCHANGED ||
            message->message == WM_SHOWWINDOW) {
            WCHAR name[32]{};
            GetClassNameW(message->hwnd, name, dimof(name));
            if (wcscmp(name, L"#32768") == 0) RoundPopupMenu(message->hwnd);
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
        if (GetComboBoxInfo(hwnd, &info)) InstallAppScrollbar(info.hwndList);
        return TRUE;
    }
    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL)) InstallAppScrollbar(hwnd);
    return TRUE;
}

static void StyleWindowScrollbars(HWND hwnd) {
    StyleChildScrollbar(hwnd, 0);
    EnumChildWindows(hwnd, StyleChildScrollbar, 0);
}

void DarkModeApplyToWindow(HWND hwnd) {
    WindowApplyRoundedCorners(hwnd);
    RoundChildControls(hwnd);
    StyleWindowScrollbars(hwnd);
    if (!gUseDarkModeLib) {
        return;
    }
    DarkMode::setDarkWndSafe(hwnd);
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

struct MenuCornerProbe {
    int phase = 0;
    int popupCount = 0;
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
    *(HWND*)arg = hwnd;
    return FALSE;
}

static void CALLBACK ProbeMenuTimer(HWND owner, UINT, UINT_PTR id, DWORD) {
    auto* probe = (MenuCornerProbe*)GetWindowLongPtrW(owner, GWLP_USERDATA);
    if (probe->phase < 2) {
        HWND menu = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), FindTestMenu, (LPARAM)&menu);
        if (menu) {
            PostMessageW(menu, WM_KEYDOWN, probe->phase == 0 ? VK_DOWN : VK_RIGHT, 0);
        }
        probe->phase++;
        return;
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
    SetWindowLongPtrW(owner, GWLP_USERDATA, (LONG_PTR)&probe);
    UINT_PTR timer = SetTimer(owner, 1, 40, ProbeMenuTimer);
    utassert(timer != 0);
    if (timer) {
        TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY, 10, 10, owner, nullptr);
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
        ApplyMenuRegion(popup);
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
        ApplyMenuRegion(popup);
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
