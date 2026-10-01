/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
License: GPLv3 */

#include "base/Base.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"

#include "Settings.h"
#include "AppSettings.h"
#include "Commands.h"
#include "DarkMode.h"
#include "Translations.h"
#include "PdfDarkMode.h"
#include "base/Win.h"

#include "Theme.h"

// The installer and uninstaller never load settings, so CreateThemeCommands()
// doesn't run and there is no current theme - every Theme*Color() accessor
// dereferences gCurrentTheme and would crash. They still create buttons and
// edits; those paint in gui/'s own defaults, which start out as the system
// colors, so nothing here has to answer without a theme. Defined next to
// gCurrentTheme.
static bool HasCurrentTheme();

// A button is a virtual control, so it takes its look from the gui/ color
// defaults, which SumatraUpdateTheme() fills in from the theme. All this adds
// is the dpi-scaled padding and the default-button shade
VirtButton* NewThemedButton(HWND hwndForDpi, Str text, PlatformFont* font, bool isDefault) {
    DpiSetFromHwnd(hwndForDpi);
    auto* b = new VirtButton(text, font);
    b->SetIsDefault(isDefault);
    b->cornerRadius = DpiScale(8);
    b->textPadding = DpiScaledInsets(5, 12);
    return b;
}

/*
preserve those translations:
TrN("Dark")
TrN("Light")
TrN("Charcoal")
*/

// Optional colors (DisabledTextColor … NotificationHighlightTextColor) fix
// muddy derived hues when TextColor is not neutral gray (e.g. Dracula #f8f8f2).
// Empty optional fields still fall back to AccentColor / blend of the four base colors.
static Str themesTxt = StrL(R"(Themes [
    [
        Name = Sumatra Light
        TextColor = #1f2937
        BackgroundColor = #f4f6f8
        ControlBackgroundColor = #ffffff
        LinkColor = #eab308
        ColorizeControls = true
    ]
    [
        Name = Sumatra Dark
        TextColor = #f8fafc
        BackgroundColor = #000000
        ControlBackgroundColor = #222222
        LinkColor = #eab308
        ColorizeControls = true
    ]
    [
        Name = Modern Slate Light
        TextColor = #243041
        BackgroundColor = #f4f6f8
        ControlBackgroundColor = #e0f0ee
        LinkColor = #0f766e
        ColorizeControls = true
    ]
    [
        Name = Modern Slate Dark
        TextColor = #dbe4ee
        BackgroundColor = #0f172a
        ControlBackgroundColor = #05557a
        LinkColor = #38bdf8
        ColorizeControls = true
    ]
    [
        Name = Modern Blue Light
        TextColor = #1e293b
        BackgroundColor = #f8fafc
        ControlBackgroundColor = #daebff
        LinkColor = #0f62fe
        ColorizeControls = true
    ]
    [
        Name = Modern Blue Dark
        TextColor = #e2e8f0
        BackgroundColor = #0b1120
        ControlBackgroundColor = #1c3d74
        LinkColor = #60a5fa
        ColorizeControls = true
    ]
    [
        Name = Modern Green Light
        TextColor = #1f2937
        BackgroundColor = #f4f6f8
        ControlBackgroundColor = #d1fae5
        LinkColor = #16a34a
        ColorizeControls = true
    ]
    [
        Name = Modern Green Dark
        TextColor = #e5f3ea
        BackgroundColor = #10251b
        ControlBackgroundColor = #0f5529
        LinkColor = #4ade80
        ColorizeControls = true
    ]
    [
        Name = Modern Purple Light
        TextColor = #29223a
        BackgroundColor = #f4f6f8
        ControlBackgroundColor = #f3e8ff
        LinkColor = #7c3aed
        ColorizeControls = true
    ]
    [
        Name = Modern Purple Dark
        TextColor = #ede9fe
        BackgroundColor = #1c122b
        ControlBackgroundColor = #4e4558
        LinkColor = #c084fc
        ColorizeControls = true
    ]
    [
        Name = Modern Amber Light
        TextColor = #31251a
        BackgroundColor = #f4f6f8
        ControlBackgroundColor = #fff7ed
        LinkColor = #d97706
        ColorizeControls = true
    ]
    [
        Name = Modern Amber Dark
        TextColor = #f9ede1
        BackgroundColor = #23160d
        ControlBackgroundColor = #634106
        LinkColor = #fbbf24
        ColorizeControls = true
    ]
]
)");

extern void UpdateAfterThemeChange();
static void UpdateGuiColorsFromTheme();

int gFirstSetThemeCmdId;
int gLastSetThemeCmdId;
int gCurrSetThemeCmdId;

static Vec<Theme*>* gThemes = nullptr;
static int gThemeCount;
static int gCurrThemeIndex = 0;
static Theme* gCurrentTheme = nullptr;
static Theme* gThemeLight = nullptr;
static Themes* gParsedThemes = nullptr;

static bool HasCurrentTheme() {
    return gCurrentTheme != nullptr;
}

bool IsCurrentThemeDefault() {
    return gCurrThemeIndex == 0;
}

// Windows high contrast mode. The user picked a system-wide palette because
// they need it to read the screen, so an app is expected to use those colors
// instead of its own. We only do that for the default theme: choosing any
// other theme is an explicit decision about colors and it wins (issue #2124).
static bool gIsHighContrast = false;

// true when the UI colors have to come from the system palette instead of the
// theme. Every color accessor tests this, so it's computed once by
// RecalcUseHighContrast() instead of on every call. Page rendering deliberately
// does not consult it: recoloring the document is not part of high contrast
// mode and inverting images was the original complaint in #2124.
static bool gUseHighContrast = false;

static void DetectHighContrastMode() {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) {
        gIsHighContrast = false;
        return;
    }
    gIsHighContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

// call whenever the OS high contrast setting or the current theme changes
static void RecalcUseHighContrast() {
    gUseHighContrast = gIsHighContrast && HasCurrentTheme() && IsCurrentThemeDefault();
}

// exported so the few places that hardcode a color for the default theme (a
// white page box on the toolbar, say) can defer to the palette instead
bool ThemeUsesHighContrastColors() {
    return gUseHighContrast;
}

void FreeThemes() {
    delete gThemes; // no need to free members, they are owned by gParsedThemes
    gThemes = nullptr;
    FreeParsedThemes(gParsedThemes);
    gParsedThemes = nullptr;
}

void CreateThemeCommands() {
    FreeThemes();
    DetectHighContrastMode();

    gThemes = new Vec<Theme*>();
    gParsedThemes = ParseThemes(themesTxt);
    for (Theme* theme : *gParsedThemes->themes) {
        VecAppend(*gThemes, theme);
    }

    for (Theme* theme : *gSettings->themes) {
        VecAppend(*gThemes, theme);
    }

    gThemeCount = len(*gThemes);
    if (gCurrThemeIndex >= gThemeCount) {
        gCurrThemeIndex = 0;
    }
    gCurrentTheme = (*gThemes)[gCurrThemeIndex];
    gThemeLight = (*gThemes)[0];
    RecalcUseHighContrast();

    CustomCommand* cmd;
    for (int i = 0; i < gThemeCount; i++) {
        Theme* theme = (*gThemes)[i];
        Str themeName = theme->name;
        auto* args = NewStringArg(kCmdArgTheme, themeName);
        cmd = CreateCustomCommand(themeName, CmdSetTheme, args, fmt(Tr("Set theme '%s'").s, themeName));
        if (i == 0) {
            gFirstSetThemeCmdId = cmd->id;
        } else if (i == gThemeCount - 1) {
            gLastSetThemeCmdId = cmd->id;
        }
    }
    gCurrSetThemeCmdId = gFirstSetThemeCmdId + gCurrThemeIndex;
}

// when true, the user picked "System" as the theme: we resolve it to the
// preferred light/dark theme from the OS setting and re-resolve when Windows
// switches modes; gSettings->theme stays "System"
static bool gThemeFollowsSystem = false;

// remember the last explicitly used light and dark theme so the light/dark
// toggle and the System theme know what to switch to
static void RememberLastLightDarkTheme() {
    if (!gSettings || !gCurrentTheme) {
        return;
    }
    if (gUseHighContrast) {
        // the theme's own colors are not in use, so they say nothing about
        // whether the user's light or dark preference is this theme
        return;
    }
    if (IsLightColor(ThemeWindowBackgroundColor())) {
        str::ReplaceWithCopy(&gSettings->lastLightTheme, gCurrentTheme->name);
    } else {
        str::ReplaceWithCopy(&gSettings->lastDarkTheme, gCurrentTheme->name);
    }
}

int ThemeGetCount() {
    return gThemeCount;
}

Str ThemeGetNameAt(int idx) {
    if (idx < 0 || idx >= gThemeCount) {
        return {};
    }
    return (*gThemes)[idx]->name;
}

int ThemeGetCurrentIndex() {
    return gCurrThemeIndex;
}

void SetThemeByIndex(int themeIdx) {
    ReportIf((themeIdx < 0) || (themeIdx >= gThemeCount));
    if (themeIdx >= gThemeCount) {
        themeIdx = 0;
    }
    gThemeFollowsSystem = false;
    bool themeChanged = (gCurrThemeIndex != themeIdx);
    gCurrThemeIndex = themeIdx;
    gCurrSetThemeCmdId = gFirstSetThemeCmdId + themeIdx;
    gCurrentTheme = (*gThemes)[gCurrThemeIndex];
    RecalcUseHighContrast(); // it depends on which theme is current
    str::ReplaceWithCopy(&gSettings->theme, gCurrentTheme->name);
    RememberLastLightDarkTheme();
    DarkModeApplyThemeColors();
    // always, not only when the theme changed: the same theme can resolve to
    // different colors (the System theme, high contrast, a settings edit)
    UpdateGuiColorsFromTheme();
    if (themeChanged) {
        UpdateAfterThemeChange();
    }
    DarkModeRememberTreeViewStyle();
};

// Map removed / renamed themes so existing settings keep working.
static Str ResolveThemeAlias(Str name) {
    if (str::EqI(name, StrL("Light"))) {
        return StrL("Sumatra Light");
    }
    if (str::EqI(name, StrL("Dark")) || str::EqI(name, StrL("Charcoal"))) {
        return StrL("Sumatra Dark");
    }
    if (str::EqI(name, StrL("Darker")) || str::EqI(name, StrL("Dark background Bright text"))) {
        return StrL("Sumatra Dark");
    }
    return name;
}

// a Themes[] entry the user wrote themselves; that theme does exist, whatever
// we once shipped under the same name, so it must not be migrated away
static bool HasCustomThemeNamed(Str name) {
    if (!gSettings || !gSettings->themes) {
        return false;
    }
    for (Theme* theme : *gSettings->themes) {
        if (str::EqI(theme->name, name)) {
            return true;
        }
    }
    return false;
}

// Rewrite the theme names dropped in 971623174 to what replaced them.
// ResolveThemeAlias() already makes them work, but only in memory: the settings
// file keeps naming a theme that no longer exists, is saved back that way every
// time, and shows a stale name to anyone who looks (#5887). Returns true if
// anything changed, so the caller can save.
bool MigrateRenamedThemeNames() {
    if (!gSettings) {
        return false;
    }
    Str* names[] = {&gSettings->theme, &gSettings->lastLightTheme, &gSettings->lastDarkTheme};
    bool changed = false;
    for (Str* name : names) {
        if (str::IsEmptyOrWhiteSpace(*name)) {
            continue;
        }
        Str newName = ResolveThemeAlias(*name);
        if (str::EqI(*name, newName) || HasCustomThemeNamed(*name)) {
            continue;
        }
        logf("MigrateRenamedThemeNames: '%s' -> '%s'\n", *name, newName);
        str::ReplaceWithCopy(name, newName);
        changed = true;
    }
    return changed;
}

// not case sensitive
static int GetThemeByName(Str name) {
    name = ResolveThemeAlias(name);
    for (int i = 0; i < gThemeCount; i++) {
        Theme* theme = (*gThemes)[i];
        if (str::EqI(theme->name, name)) {
            return i;
        }
    }
    return -1;
}

// this is the default aggressive yellow that we suppress
constexpr Color kMainWinBgColDefault = (MkRgb(0xff, 0xf2, 0) - 0x80000000);

static bool IsDefaultMainWinColor(ParsedColor* col) {
    return col->parsedOk && col->col == kMainWinBgColDefault;
}

// true if Windows "choose your default app mode" is set to dark
static bool OsAppsUseDarkMode() {
    DWORD val = 1; // AppsUseLightTheme defaults to 1 (light)
    DWORD cb = sizeof(val);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &val, &cb);
    return val == 0;
}

static int GetPreferredLightThemeIndex() {
    int idx = GetThemeByName(gSettings->lastLightTheme);
    if (idx >= 0) {
        return idx;
    }
    return 0; // gThemeLight
}

static int GetPreferredDarkThemeIndex() {
    int idx = GetThemeByName(gSettings->lastDarkTheme);
    if (idx >= 0) {
        return idx;
    }
    idx = GetThemeByName(StrL("Dark"));
    return idx >= 0 ? idx : 0;
}

void SetTheme(Str name) {
    if (str::EqI(name, StrL("System"))) {
        // resolve to the preferred light/dark theme from the OS setting; keep
        // "System" in prefs so it persists and keeps following the OS
        int idx = OsAppsUseDarkMode() ? GetPreferredDarkThemeIndex() : GetPreferredLightThemeIndex();
        SetThemeByIndex(idx);
        gThemeFollowsSystem = true;
        str::ReplaceWithCopy(&gSettings->theme, StrL("System"));
        return;
    }
    name = ResolveThemeAlias(name);
    int idx = GetThemeByName(name);
    if (idx < 0) {
        // invalid name, reset to light theme
        str::ReplaceWithCopy(&gSettings->theme, gThemeLight->name);
        idx = 0;
    }
    SetThemeByIndex(idx);
}

// switch between the last used light and dark theme (CmdToggleLightDarkTheme)
void ToggleLightDarkTheme() {
    bool isDark = !IsLightColor(ThemeWindowBackgroundColor());
    int idx = isDark ? GetPreferredLightThemeIndex() : GetPreferredDarkThemeIndex();
    SetThemeByIndex(idx);
}

void SetLightDarkTheme(bool dark) {
    SetThemeByIndex(dark ? GetPreferredDarkThemeIndex() : GetPreferredLightThemeIndex());
}

// name of the theme ToggleLightDarkTheme() would switch to, for the command
// palette: ": set to true" says nothing useful when a toggle picks a theme
Str ToggleLightDarkThemeTargetName() {
    bool isDark = !IsLightColor(ThemeWindowBackgroundColor());
    int idx = isDark ? GetPreferredLightThemeIndex() : GetPreferredDarkThemeIndex();
    return ThemeGetNameAt(idx);
}

// Spreads the current theme over gui/'s per-control color defaults. This is the
// whole of the app -> gui coupling: the controls never ask us for a color, they
// paint in the defaults, and an individual control that wants something else
// (the toolbar's palette, a notification's) overrides its own slots.
static void UpdateGuiColorsFromTheme() {
    if (!HasCurrentTheme()) {
        // the installer and uninstaller: gui/ keeps the system colors
        return;
    }
    // fill in the system defaults first, so a control created later doesn't do
    // it on our behalf and undo what we are about to write. Clearing the flag
    // is what stops WindowBase::OnThemeChange() from doing the same
    GuiColorsInitIfNeeded();
    gGuiColorsFromSystem = false;

    Color text = ThemeWindowTextColor();
    Color disabled = ThemeWindowTextDisabledColor();
    Color link = ThemeWindowLinkColor();
    // what dialogs, side panels and the chrome around the document put their
    // controls on
    Color ctlBg = ThemeWindowControlBackgroundColor();
    Color edge = ThemeEdgeColor();
    Color hotEdge = ThemeHotEdgeColor();

    gColsText[kColText] = text;
    gColsLink[kColText] = link;

    gColsBtn[kColBtnText] = text;
    gColsBtn[kColBtnBg] = AccentColor(ctlBg, 14);
    gColsBtn[kColBtnBgHover] = AccentColor(ctlBg, 28);
    gColsBtn[kColBtnBorder] = edge;
    gColsBtn[kColBtnTextDisabled] = disabled;

    gColsBtnDefault[kColBtnText] = ThemeBrandTextColor();
    gColsBtnDefault[kColBtnBg] = ThemeBrandColor();
    gColsBtnDefault[kColBtnBgHover] = AccentColor(ThemeBrandColor(), 12);
    gColsBtnDefault[kColBtnBorder] = ThemeBrandColor();
    gColsBtnDefault[kColBtnTextDisabled] = disabled;

    gColsIconBtn[kColIconBtnBgHover] = AccentColor(ctlBg, 20);
    gColsIconBtn[kColIconBtnBgSelected] = AccentColor(ctlBg, 36);
    gColsIconBtn[kColIconBtnChevron] = text;
    gColsIconBtn[kColIconBtnChevronDisabled] = disabled;

    // the ✕ keeps its own look in every theme: a tab sets the circle to its own
    // background, and a withCircle one sits on content we don't own

    gColsListBox[kColListText] = text;
    gColsListBox[kColListBg] = ctlBg;
    gColsListBox[kColListSel] = AccentColor(ctlBg, 25);
    gColsListBox[kColListSelFocused] = AccentColor(ctlBg, 45);
    gColsListBox[kColListScrollbar] = AccentColor(ctlBg, 60);

    gColsSplitter[kColSplitterBg] = ctlBg;
    gColsFill[kColFillBg] = ctlBg;
    gColsLine[kColLineFg] = edge;

    gColsSlider[kColSliderTrack] = AccentColor(ctlBg, 40);
    gColsSlider[kColSliderFill] = text;
    gColsSlider[kColSliderThumb] = text;
    gColsSlider[kColSliderThumbHover] = AccentColor(text, 20);

    gColsRichText[kColRichText] = text;
    gColsRichText[kColRichLink] = link;
    gColsRichText[kColRichBg] = ctlBg;

    gColsTab[kColTabText] = text;
    gColsTab[kColTabBg] = ThemeActiveTabBackgroundColor();
    gColsTab[kColTabInactiveBg] = ThemeInactiveTabBackgroundColor();

    // custom top-level windows (dialogs, popups) sit their content on ctlBg,
    // like the side panels; a window that wants something else (the toolbar's
    // palette) sets its own textColor / bgColor
    gColsWin[kColWinText] = text;
    gColsWin[kColWinBg] = ctlBg;

    // the underline under a borderless Edit is a separator, so it takes the edge
    // color like every other border and divider. Blending the control's own text
    // color toward its background instead lands wherever those two happen to be:
    // on the Dark theme's black sidebar that gave a bright #535353 line instead
    // of the theme's #374151 (issue #5893)
    gColsEdit[kColEditBottomBorder] = edge;
}

// The app's theme, or the system palette it follows, changed: push our colors
// into gui/'s defaults, then rebuild and repaint everything that shows them.
void SumatraUpdateTheme() {
    UpdateGuiColorsFromTheme();
    UpdateAfterThemeChange();
}

// call on WM_SETTINGCHANGE "ImmersiveColorSet": re-resolves the System theme
// when the user switches Windows between light and dark mode
void UpdateThemeAfterSystemColorChange() {
    if (!gThemeFollowsSystem) {
        return;
    }
    SetTheme(StrL("System")); // no-op unless the resolved theme changed
}

// call on WM_SETTINGCHANGE: the user can turn high contrast on and off at any
// time (Alt+Shift+PrtScr), which swaps the whole palette out from under us
void UpdateThemeAfterHighContrastChange() {
    bool wasUsingHighContrast = gUseHighContrast;
    DetectHighContrastMode();
    RecalcUseHighContrast();
    if (wasUsingHighContrast == gUseHighContrast) {
        // also the common case of toggling it while a custom theme is current,
        // which changes nothing we draw
        return;
    }
    logf("UpdateThemeAfterHighContrastChange: using high contrast colors: %d\n", (int)gUseHighContrast);
    DarkModeApplyThemeColors();
    SumatraUpdateTheme();
    DarkModeRememberTreeViewStyle();
}

// call after loading settings
void SetCurrentThemeFromSettings() {
    SetTheme(gSettings->theme);
    ParsedColor* bgParsed = GetPrefsColor(gSettings->mainWindowBackground);
    bool isDefault = IsDefaultMainWinColor(bgParsed);
    if (isDefault) {
        gThemeLight->colorizeControls = true;
        gThemeLight->controlBackgroundColor.wasParsed = true;
        gThemeLight->controlBackgroundColor.parsedOk = true;
        gThemeLight->controlBackgroundColor.col = kColWhite;
    } else if (bgParsed->parsedOk) {
        gThemeLight->colorizeControls = true;
        gThemeLight->controlBackgroundColor.wasParsed = true;
        gThemeLight->controlBackgroundColor.parsedOk = true;
        gThemeLight->controlBackgroundColor.col = bgParsed->col;
    }
    // SetTheme() above ran before we adjusted the Light theme, so re-push
    UpdateGuiColorsFromTheme();
}

#define GetThemeCol(name, def) GetParsedColor(name, def)

// canvas/window background color around the document pages
// not affected by FixedPageUI.TextColor/BackgroundColor (those affect page rendering)
Color ThemeDocumentColors(Color& bg) {
    bg = ThemeMainWindowBackgroundColor();

    if (!DocumentColorsFollowThemeEnabled()) {
        return ThemeWindowTextColor();
    }

    Color text = ThemeWindowTextColor();
    bg = ThemeMainWindowBackgroundColor();

    // the system palette is exact: tinting it away from COLOR_WINDOW is the
    // kind of "close enough" color high contrast mode exists to avoid
    if (gCurrThemeIndex < 3 && !gUseHighContrast) {
        bg = AccentColor(bg, 8);
    }
    return text;
}

// CmdInvertColors: session-only, not a setting. Before 3.7 it swapped the page
// colors outright and had nothing to do with theming; 37f920ff0 reduced it to a
// DocumentColorsFollowTheme toggle, which does nothing at all when the page
// colors don't come from the theme - custom FixedPageUI colors, for instance,
// are used as-is in every mode, so pressing it only repainted the same pixels
// (issue #5887). Swapping the effective page colors works whatever they are.
static bool gInvertPageColors = false;

bool GetInvertPageColors() {
    return gInvertPageColors;
}

void SetInvertPageColors(bool invert) {
    gInvertPageColors = invert;
}

// colors for page bitmap recoloring (render cache)
// TextColor substitutes black, BackgroundColor substitutes white in rendered pages
static Color ThemePageRenderColorsNoInvert(Color& bg) {
    Color text = kColBlack;
    bg = kColWhite;

    // Headless harnesses such as -extract-text run before LoadSettings.
    if (!gSettings) {
        return text;
    }

    ParsedColor* parsedCol;
    parsedCol = GetPrefsColor(gSettings->fixedPageUI.textColor);
    if (parsedCol->parsedOk) {
        text = parsedCol->col;
    }

    parsedCol = GetPrefsColor(gSettings->fixedPageUI.backgroundColor);
    if (parsedCol->parsedOk) {
        bg = parsedCol->col;
    }

    if (!DocumentColorsFollowThemeEnabled()) {
        return text;
    }

    // Custom FixedPageUI colors: use them as recolor targets (no invert).
    // DocumentColorsFollowTheme means "match the UI theme", not "swap black/white"
    // — the old InvertColors path swapped here and forced white-on-black for the
    // Light theme (#5821).
    bool userDidChange = text != kColBlack || bg != kColWhite;
    if (userDidChange) {
        return text;
    }

    if (gUseHighContrast) {
        // High contrast mode is about the app's own UI. Recoloring the document
        // is not part of it - that's what made images come out inverted for no
        // reason (#2124) - so the page keeps its black-on-white default.
        return text;
    }

    // Defaults: page colors follow the window theme (light theme → dark text on
    // light paper; dark theme → light text on dark paper).
    text = ThemeWindowTextColor();
    bg = ThemeMainWindowBackgroundColor();

    if (gCurrThemeIndex < 3) {
        bg = AccentColor(bg, 8);
    }
    return text;
}

Color ThemePageRenderColors(Color& bg) {
    Color text = ThemePageRenderColorsNoInvert(bg);
    if (!gInvertPageColors) {
        return text;
    }
    std::swap(text, bg);
    return text;
}

Color ThemeControlBackgroundColor() {
    if (gUseHighContrast) {
        return SysWindowBgColor();
    }
    // note: we can change it in ThemeUpdateAfterLoadSettings()
    auto col = GetThemeCol(gCurrentTheme->controlBackgroundColor, kColRed);
    return col;
}

Color ThemeMainWindowBackgroundColor() {
    if (gUseHighContrast) {
        return SysWindowBgColor();
    }
    Color bgColor = GetThemeCol(gCurrentTheme->backgroundColor, kColRed);
    if (gCurrThemeIndex == 0) {
        // Special behavior for light theme.
        ParsedColor* bgParsed = GetPrefsColor(gSettings->mainWindowBackground);
        if (bgParsed->parsedOk && !IsDefaultMainWinColor(bgParsed)) {
            bgColor = bgParsed->col;
        }
    }
    return bgColor;
}

Color ThemeWindowBackgroundColor() {
    if (gUseHighContrast) {
        return SysWindowBgColor();
    }
    auto col = GetThemeCol(gCurrentTheme->backgroundColor, kColRed);
    return col;
}

Color ThemeWindowTextColor() {
    if (gUseHighContrast) {
        return SysWindowTextColor();
    }
    auto col = GetThemeCol(gCurrentTheme->textColor, kColRed);
    return col;
}

static Color BlendTextAndBgHalfway() {
    // fallback when DisabledTextColor is unset: mute text toward background
    Color txt = ThemeWindowTextColor();
    Color bg = ThemeMainWindowBackgroundColor();
    u8 r = (u8)((GetRValue(txt) + GetRValue(bg)) / 2);
    u8 g = (u8)((GetGValue(txt) + GetGValue(bg)) / 2);
    u8 b = (u8)((GetBValue(txt) + GetBValue(bg)) / 2);
    return MkRgb(r, g, b);
}

Color ThemeWindowTextDisabledColor() {
    if (gUseHighContrast) {
        return SysDisabledTextColor();
    }
    return GetThemeCol(gCurrentTheme->disabledTextColor, BlendTextAndBgHalfway());
}

Color ThemeWindowDarkerTextColor() {
    if (gUseHighContrast) {
        // high contrast has no muted text: muting it is the opposite of the point
        return SysWindowTextColor();
    }
    // fallback: slightly muted primary text (not as flat as disabled)
    Color fallback = AccentColor(ThemeWindowTextColor(), 40);
    return GetThemeCol(gCurrentTheme->darkerTextColor, fallback);
}

Color ThemeWindowControlBackgroundColor() {
    if (gUseHighContrast) {
        return SysWindowBgColor();
    }
    auto col = GetThemeCol(gCurrentTheme->controlBackgroundColor, kColRed);
    return col;
}

Color ThemeActiveTabBackgroundColor() {
    if (gUseHighContrast) {
        return SysWindowBgColor();
    }
    return GetThemeCol(gCurrentTheme->activeTabBackgroundColor, ThemeWindowControlBackgroundColor());
}

Color ThemeInactiveTabBackgroundColor() {
    if (gUseHighContrast) {
        return SysWindowBgColor();
    }
    Color fallback = AccentColor(ThemeWindowControlBackgroundColor(), 25);
    return GetThemeCol(gCurrentTheme->inactiveTabBackgroundColor, fallback);
}

Color ThemeWindowLinkColor() {
    if (gUseHighContrast) {
        return SysLinkColor();
    }
    auto col = GetThemeCol(gCurrentTheme->linkColor, kColRed);
    return col;
}

Color ThemeHotBackgroundColor() {
    if (gUseHighContrast) {
        return SysHighlightBgColor();
    }
    Color fallback = AccentColor(ThemeWindowControlBackgroundColor(), 20);
    return GetThemeCol(gCurrentTheme->hotBackgroundColor, fallback);
}

Color ThemeEdgeColor() {
    if (gUseHighContrast) {
        // borders have to stay visible, so they take the text color
        return SysWindowTextColor();
    }
    Color fallback = AccentColor(ThemeWindowControlBackgroundColor(), 40);
    return GetThemeCol(gCurrentTheme->edgeColor, fallback);
}

Color ThemeHotEdgeColor() {
    if (gUseHighContrast) {
        return SysHighlightBgColor();
    }
    Color fallback = AccentColor(ThemeEdgeColor(), 30);
    return GetThemeCol(gCurrentTheme->hotEdgeColor, fallback);
}

Color ThemeDisabledEdgeColor() {
    if (gUseHighContrast) {
        return SysDisabledTextColor();
    }
    Color fallback = AccentColor(ThemeWindowControlBackgroundColor(), 15);
    return GetThemeCol(gCurrentTheme->disabledEdgeColor, fallback);
}

Color ThemeErrorBackgroundColor() {
    if (gUseHighContrast) {
        // no error color in the system palette; the text carries the message
        return SysWindowBgColor();
    }
    // soft red tint of control background when unset
    Color fallback = RgbToColor(0x5c1a1a);
    if (IsLightColor(ThemeWindowControlBackgroundColor())) {
        fallback = RgbToColor(0xffe0e0);
    }
    return GetThemeCol(gCurrentTheme->errorBackgroundColor, fallback);
}

Color ThemeNotificationsBackgroundColor() {
    if (gUseHighContrast) {
        return SysWindowBgColor();
    }
    Color fallback = AdjustLightness2(ThemeWindowBackgroundColor(), 10);
    return GetThemeCol(gCurrentTheme->notificationBackgroundColor, fallback);
}

Color ThemeNotificationsTextColor() {
    return ThemeWindowTextColor();
}

// Warning notifications are meant to read as warnings, so the fallback is amber
// in both directions: light themes get the classic yellow, dark themes a muted
// dark amber. Deriving it from the theme's own accent (as we used to) produced
// saturated, unrelated hues -- Dracula's warnings came out bright purple.
Color ThemeNotificationsHighlightColor() {
    if (gUseHighContrast) {
        return SysHighlightBgColor();
    }
    Color fallback;
    if (IsLightColor(ThemeNotificationsBackgroundColor())) {
        fallback = RgbToColor(0xFFEE70); // yellowish
    } else {
        fallback = RgbToColor(0x422006); // dark amber
    }
    return GetThemeCol(gCurrentTheme->notificationHighlightColor, fallback);
}

Color ThemeNotificationsHighlightTextColor() {
    if (gUseHighContrast) {
        return SysHighlightTextColor();
    }
    Color fallback;
    if (IsLightColor(ThemeNotificationsBackgroundColor())) {
        fallback = RgbToColor(0x8d0801); // reddish
    } else {
        fallback = RgbToColor(0xFDE68A); // light amber
    }
    return GetThemeCol(gCurrentTheme->notificationHighlightTextColor, fallback);
}

// Links inside a warning notification. The theme's link color is picked to sit on
// the window background and can vanish on the amber warning background (Dracula's
// cyan, Choco's yellow). Links are underlined, so reusing the warning text color
// stays legible and still reads as a link.
Color ThemeNotificationsHighlightLinkColor() {
    return ThemeNotificationsHighlightTextColor();
}

Color ThemeNotificationsProgressColor() {
    return ThemeWindowLinkColor();
}

bool ThemeColorizeControls() {
    if (gCurrentTheme->colorizeControls) {
        return true;
    }
    return !IsMenuFontSizeDefault();
}

// The colors Windows draws its own UI in. The default theme defers to them so
// the app follows the system appearance, and every theme defers to them in high
// contrast mode, where the user's palette is the whole point. The rest of the
// file asks for them by name so it doesn't have to know the OS palette itself.

Color SysWindowBgColor() {
    return GetSysColor(COLOR_WINDOW);
}

Color SysWindowTextColor() {
    return GetSysColor(COLOR_WINDOWTEXT);
}

Color SysControlTextColor() {
    return GetSysColor(COLOR_BTNTEXT);
}

Color SysDisabledTextColor() {
    return GetSysColor(COLOR_GRAYTEXT);
}

Color SysLinkColor() {
    return GetSysColor(COLOR_HOTLIGHT);
}

Color SysHighlightBgColor() {
    return GetSysColor(COLOR_HIGHLIGHT);
}

Color SysHighlightTextColor() {
    return GetSysColor(COLOR_HIGHLIGHTTEXT);
}

Color ThemeBrandColor() {
    if (ThemeUsesHighContrastColors()) {
        return SysHighlightBgColor();
    }
    static const struct {
        Str name;
        Color color;
    } brands[] = {
        {StrL("Sumatra Light"), MkRgb(0xfa, 0xcc, 0x15)},       {StrL("Sumatra Dark"), MkRgb(0xfa, 0xcc, 0x15)},
        {StrL("Modern Slate Light"), MkRgb(0x14, 0xb8, 0xa6)},  {StrL("Modern Slate Dark"), MkRgb(0x0e, 0xa5, 0xe9)},
        {StrL("Modern Blue Light"), MkRgb(0x25, 0x63, 0xeb)},   {StrL("Modern Blue Dark"), MkRgb(0x3b, 0x82, 0xf6)},
        {StrL("Modern Green Light"), MkRgb(0x22, 0xc5, 0x5e)},  {StrL("Modern Green Dark"), MkRgb(0x22, 0xc5, 0x5e)},
        {StrL("Modern Purple Light"), MkRgb(0x8b, 0x5c, 0xf6)}, {StrL("Modern Purple Dark"), MkRgb(0xa8, 0x55, 0xf7)},
        {StrL("Modern Amber Light"), MkRgb(0xf5, 0x9e, 0x0b)},  {StrL("Modern Amber Dark"), MkRgb(0xf5, 0x9e, 0x0b)},
    };
    for (auto& brand : brands) {
        if (str::Eq(gCurrentTheme->name, brand.name)) {
            return brand.color;
        }
    }
    return IsLightColor(ThemeWindowBackgroundColor()) ? MkRgb(0xea, 0xb3, 0x08) : MkRgb(0xfa, 0xcc, 0x15);
}

Color ThemeBrandTextColor() {
    if (ThemeUsesHighContrastColors()) {
        return SysHighlightTextColor();
    }
    return IsLightColor(ThemeBrandColor()) ? MkRgb(0x1d, 0x1d, 0x1f) : kColWhite;
}
