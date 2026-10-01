/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/PlatformWindow.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"

#include "Settings.h"
#include "DisplayMode.h"
#include "AppSettings.h"
#include "MainWindow.h"
#include "FileHistory.h"
#include "FileThumbnails.h"
#include "Tabs.h"
#include "Theme.h"
#include "SumatraConfig.h"
#include "SumatraPDF.h"
#include "AppTools.h"
#include "Translations.h"
#include "DarkMode.h"
#include "SumatraDialogs.h"

// Section headers, labels and OK/Cancel are VirtCtrl; layout/zoom/command
// combos and the checkboxes are HWNDs. Same WindowBase layout as Inverse Search.
struct SettingsWnd : WindowBase {
    ~SettingsWnd() override = default;

    MainWindow* win = nullptr;
    ScrollBox* scroll = nullptr;
    Vec<float> zoomLevels;
    float startZoom = 0;
    bool showInverseSearch = false;

    VirtText* labelView = nullptr;
    VirtText* labelLayout = nullptr;
    VirtText* labelZoom = nullptr;
    VirtText* labelAdvanced = nullptr;
    VirtText* labelInverse = nullptr;
    VirtText* labelCmdLine = nullptr;

    DropDown* dropLayout = nullptr;
    DropDown* dropZoom = nullptr;
    DropDown* dropInverse = nullptr;
    DropDown* dropUiFamily = nullptr;
    DropDown* dropUiSize = nullptr;
    DropDown* dropTreeSize = nullptr;
    DropDown* dropThumbnailSize = nullptr;
    DropDown* dropToolbarSize = nullptr;
    DropDown* dropRecentCount = nullptr;
    DropDown* dropMinTabWidth = nullptr;
    DropDown* dropHoverDelay = nullptr;
    DropDown* dropPenMin = nullptr;
    DropDown* dropPenMax = nullptr;
    DropDown* dropPenStep = nullptr;
    Vec<int> uiSizes;
    Vec<int> treeSizes;
    Vec<int> thumbnailSizes;

    Checkbox* chkReferenceHover = nullptr;
    Checkbox* chkShowToc = nullptr;
    Checkbox* chkRememberState = nullptr;
    Checkbox* chkUseTabs = nullptr;
    Checkbox* chkCheckUpdates = nullptr;
    Checkbox* chkRememberOpened = nullptr;

    VirtButton* btnCancel = nullptr;
    VirtButton* btnOk = nullptr;

    bool Create(MainWindow* win);
    void FillLayout();
    void FillZoom();
    void FillInverse();
    float SelectedZoom();
    void OnRememberOpenedChanged();
    void OnReferenceHoverChanged();

    void OnCancel(VirtMouseEvent* ev = nullptr);
    void OnOk(VirtMouseEvent* ev = nullptr);
};

static void FillSizeChoices(DropDown* drop, Vec<int>& sizes, int current, bool percentage) {
    const int fontSizes[] = {0, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48};
    const int thumbnailSizes[] = {75, 100, 125, 150, 175, 200, 250};
    if (percentage) {
        for (int value : thumbnailSizes) {
            VecAppend(sizes, value);
        }
    } else {
        for (int value : fontSizes) {
            VecAppend(sizes, value);
        }
    }
    if (VecFind(sizes, current) < 0) {
        VecAppend(sizes, current);
    }
    StrVec labels;
    for (int value : sizes) {
        labels.Append(value == 0 ? Tr("Automatic (Windows)") : fmt(percentage ? "%d%%" : "%d px", value));
    }
    drop->SetItems(labels);
    CbSetCurrentSelection(drop, VecFind(sizes, current));
}

static int SelectedSize(DropDown* drop, const Vec<int>& sizes, int fallback) {
    int index = CbGetCurrentSelection(drop);
    return index >= 0 && index < len(sizes) ? sizes[index] : fallback;
}

static void FillNumberChoices(DropDown* drop, Str options, double current) {
    StrVec labels;
    Split(&labels, options, StrL("|"));
    drop->SetItems(labels);
    drop->SetText(fmt("%g", current));
}

static double SelectedNumber(DropDown* drop, double fallback, double minimum, double maximum) {
    TempStr text = drop->GetTextTemp();
    char* end = nullptr;
    char* input = CStrTemp(text);
    double value = strtod(input, &end);
    if (end == input || !isfinite(value)) {
        return fallback;
    }
    return limitValue(value, minimum, maximum);
}

static SettingsWnd* gSettingsWnd = nullptr;

static void ClearSettingsWnd() {
    gSettingsWnd = nullptr;
}

void SettingsWnd::FillLayout() {
    if (!dropLayout) {
        return;
    }
    StrVec items;
    items.Append(Tr("Automatic"));
    items.Append(Tr("Single Page"));
    items.Append(Tr("Facing"));
    items.Append(Tr("Book View"));
    items.Append(Tr("Continuous"));
    items.Append(Tr("Continuous Facing"));
    items.Append(Tr("Continuous Book View"));
    items.Append(Tr("Page Aspect"));
    dropLayout->SetItems(items);
    int sel = 0;
    if (gSettings && IsPageAspectDisplayMode(gSettings->defaultDisplayMode)) {
        sel = len(items) - 1;
    } else if (gSettings) {
        sel = (int)gSettings->defaultDisplayModeEnum - (int)DisplayMode::Automatic;
    }
    if (sel < 0 || sel >= len(items)) {
        sel = 0;
    }
    CbSetCurrentSelection(dropLayout, sel);
}

void SettingsWnd::FillZoom() {
    if (!dropZoom) {
        return;
    }
    startZoom = gSettings ? gSettings->defaultZoomFloat : 0;
    CollectZoomLevels(zoomLevels, false);
    StrVec items;
    for (float z : zoomLevels) {
        items.Append(ZoomLevelStrExact(z));
    }
    dropZoom->SetItems(items);
    int sel = -1;
    for (int i = 0; i < len(zoomLevels); i++) {
        if (zoomLevels[i] == startZoom) {
            sel = i;
            break;
        }
    }
    if (sel >= 0) {
        CbSetCurrentSelection(dropZoom, sel);
    } else {
        dropZoom->SetText(fmt("%.0f%%", startZoom));
    }
}

void SettingsWnd::FillInverse() {
    if (!dropInverse) {
        return;
    }
    StrVec items;
    Str cmdLine = gSettings ? gSettings->inverseSearchCmdLine : Str{};
    CollectInverseSearchCommands(items, cmdLine);
    if (len(cmdLine) == 0 && len(items) > 0) {
        cmdLine = items[0];
    }
    dropInverse->SetItems(items);
    if (len(cmdLine) == 0) {
        return;
    }
    int idx = items.Find(cmdLine);
    if (idx >= 0) {
        CbSetCurrentSelection(dropInverse, idx);
    } else {
        dropInverse->SetText(cmdLine);
    }
}

// Selected list entry, or a typed number (empty / non-numeric keeps startZoom).
float SettingsWnd::SelectedZoom() {
    int idx = CbGetCurrentSelection(dropZoom);
    if (idx >= 0 && idx < len(zoomLevels)) {
        float z = zoomLevels[idx];
        return z == 0 ? startZoom : z;
    }
    TempStr text = dropZoom ? dropZoom->GetTextTemp() : Str{};
    if (len(text) == 0) {
        return startZoom;
    }
    float zoom = (float)atof(CStrTemp(text));
    if (zoom == 0) {
        return startZoom;
    }
    return limitValue(zoom, kZoomMin, kZoomMax);
}

void SettingsWnd::OnReferenceHoverChanged() {
    if (dropHoverDelay) {
        dropHoverDelay->SetIsEnabled(chkReferenceHover && chkReferenceHover->IsChecked());
    }
}

void SettingsWnd::OnRememberOpenedChanged() {
    if (!chkRememberState) {
        return;
    }
    bool on = chkRememberOpened && chkRememberOpened->IsChecked();
    chkRememberState->SetIsEnabled(on);
}

void SettingsWnd::OnCancel(VirtMouseEvent*) {
    ScheduleDelete();
}

void SettingsWnd::OnOk(VirtMouseEvent*) {
    if (!gSettings) {
        ScheduleDelete();
        return;
    }
    int layoutIdx = CbGetCurrentSelection(dropLayout);
    int nLayout = dropLayout ? len(dropLayout->items) : 0;
    if (layoutIdx >= 0 && nLayout > 0 && layoutIdx == nLayout - 1) {
        str::ReplaceWithCopy(&gSettings->defaultDisplayMode, StrL("page aspect"));
        gSettings->defaultDisplayModeEnum = DisplayMode::Automatic;
    } else if (layoutIdx >= 0) {
        gSettings->defaultDisplayModeEnum = (DisplayMode)(layoutIdx + (int)DisplayMode::Automatic);
        str::ReplaceWithCopy(&gSettings->defaultDisplayMode, DisplayModeToString(gSettings->defaultDisplayModeEnum));
    }
    gSettings->defaultZoomFloat = SelectedZoom();
    int uiSize = SelectedSize(dropUiSize, uiSizes, gSettings->uIFontSize);
    int treeSize = SelectedSize(dropTreeSize, treeSizes, gSettings->treeFontSize);
    static const Str families[] = {StrL("system"), StrL("Manrope"), StrL("Pretendard Std"), StrL("Public Sans")};
    int familyIndex = CbGetCurrentSelection(dropUiFamily);
    Str family = familyIndex >= 0 && familyIndex < dimofi(families) ? families[familyIndex] : gSettings->uIFontFamily;
    bool fontsChanged = uiSize != gSettings->uIFontSize || treeSize != gSettings->treeFontSize ||
                        !str::EqI(family, gSettings->uIFontFamily);
    str::ReplaceWithCopy(&gSettings->uIFontFamily, family);
    gSettings->uIFontSize = uiSize;
    gSettings->treeFontSize = treeSize;
    gSettings->homePageThumbnailSize =
        SelectedSize(dropThumbnailSize, thumbnailSizes, gSettings->homePageThumbnailSize);
    gSettings->citationHoverDelay =
        chkReferenceHover->IsChecked() ? (int)SelectedNumber(dropHoverDelay, 300, 0, 2000) : -1;
    gSettings->toolbarSize = (int)SelectedNumber(dropToolbarSize, gSettings->toolbarSize, 8, 64);
    gSettings->homePageMaxRecentItems = (int)SelectedNumber(dropRecentCount, gSettings->homePageMaxRecentItems, 1, 200);
    gSettings->minTabWidth = (int)SelectedNumber(dropMinTabWidth, gSettings->minTabWidth, 60, 400);
    float penMin = (float)SelectedNumber(dropPenMin, gSettings->penMinWidth, 0.1, 64);
    float penMax = (float)SelectedNumber(dropPenMax, gSettings->penMaxWidth, penMin, 64);
    gSettings->penMinWidth = penMin;
    gSettings->penMaxWidth = penMax;
    gSettings->penWidthStep = (float)SelectedNumber(dropPenStep, gSettings->penWidthStep, 0.1, 16);
    if (fontsChanged) {
        RefreshUiFonts();
    }
    for (MainWindow* window : gWindows) {
        UpdateTabWidth(window);
    }
    if (chkShowToc) {
        gSettings->showToc = chkShowToc->IsChecked();
    }
    if (chkRememberState) {
        gSettings->rememberStatePerDocument = chkRememberState->IsChecked();
    }
    if (chkUseTabs) {
        gSettings->useTabs = chkUseTabs->IsChecked();
    }
    if (chkCheckUpdates) {
        gSettings->checkForUpdates = chkCheckUpdates->IsChecked();
    }
    if (chkRememberOpened) {
        gSettings->rememberOpenedFiles = chkRememberOpened->IsChecked();
    }
    if (showInverseSearch && dropInverse) {
        TempStr tmp = dropInverse->GetTextTemp();
        str::ReplaceWithCopy(&gSettings->inverseSearchCmdLine, tmp);
    }

    if (!SettingsRememberOpenedFiles()) {
        FileHistoryClear(true);
        EmptyThumbnailCacheDirectory();
    }
    UpdateDocumentColors();
    // note: ideally we would also update state for useTabs changes but that's complicated since
    // to do it right we would have to convert tabs to windows. When moving no tabs -> tabs,
    // there's no problem. When moving tabs -> no tabs, a half solution would be to only
    // call SetTabsInTitlebar() for windows that have only one tab, but that's somewhat inconsistent
    ApplySettingsToOpenWindows();
    ScheduleSaveSettings();
    MaybeRedrawHomePage();
    ScheduleDelete();
}

static void OnClose(WindowBase::CloseEvent* /*ev*/) {
    if (gSettingsWnd) {
        gSettingsWnd->OnCancel();
    }
}

static void OnDestroy(WindowBase::DestroyEvent* /*ev*/) {
    if (gSettingsWnd) {
        gSettingsWnd->ScheduleDelete();
    }
}

static DropDown* MakeDropDown(HWND parent, PlatformFont* font, bool isRtl, bool editable) {
    DropDown::CreateArgs args;
    args.parent = parent;
    args.font = font;
    args.isRtl = isRtl;
    args.isEditable = editable;
    auto* c = new DropDown();
    c->Create(args);
    return c;
}

static Checkbox* MakeCheckbox(HWND parent, Str text, bool isRtl, bool checked, int topPt) {
    Checkbox::CreateArgs args;
    args.parent = parent;
    args.text = text;
    args.isRtl = isRtl;
    if (checked) {
        args.initialState = Checkbox::State::Checked;
    }
    auto* c = new Checkbox();
    c->SetInsetsPt(topPt, 0, 0, 0);
    c->Create(args);
    return c;
}

bool SettingsWnd::Create(MainWindow* mainWin) {
    win = mainWin;
    showInverseSearch = gSettings && gSettings->enableTeXEnhancements && CanAccessDisk();

    {
        CreateCustomArgs args;
        args.title = Tr("Settings");
        args.visible = false;
        args.style = WS_POPUPWINDOW | WS_CAPTION | WS_THICKFRAME | WS_VSCROLL;
        args.font = GetFont();
        args.icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
        CreateCustom(args);
    }
    if (!hwnd) {
        return false;
    }
    bool isRtl = IsUIRtl();

    auto* vbox = new VBox();
    vbox->alignMain = MainAxisAlign::MainStart;
    vbox->alignCross = CrossAxisAlign::Stretch;

    //[ ACCESSKEY_GROUP Settings Dialog
    {
        auto* c = NewVirtText({
            .s = Tr("View"),
            .font = font,
            .isRtl = isRtl,
            .padding = DpiScaledInsets(0, 0, 4, 0),
        });
        labelView = c;
        vbox->AddChild(c);
    }

    {
        // Default Layout / Default Zoom in a 2x2 table so the labels share a
        // column and the two drop-downs line up at the same left edge
        auto* labLayout = NewVirtText({
            .s = Tr("Default &Layout:"),
            .font = font,
            .isRtl = isRtl,
            .prefix = true,
        });
        labelLayout = labLayout;
        dropLayout = MakeDropDown(hwnd, GetFont(), isRtl, false);

        auto* labZoom = NewVirtText({
            .s = Tr("Default &Zoom:"),
            .font = font,
            .isRtl = isRtl,
            .prefix = true,
        });
        labelZoom = labZoom;
        dropZoom = MakeDropDown(hwnd, GetFont(), isRtl, true);

        auto* table = new Table();
        table->SetSize(2, 2);
        table->colGap = DpiScale(8);
        table->rowGap = DpiScale(4);
        auto& lc = table->SetCell(0, 0, labLayout);
        lc.alignV = CrossAxisAlign::CrossCenter;
        auto& ld = table->SetCell(0, 1, dropLayout);
        ld.alignH = CrossAxisAlign::Stretch;
        ld.alignV = CrossAxisAlign::CrossCenter;
        auto& zc = table->SetCell(1, 0, labZoom);
        zc.alignV = CrossAxisAlign::CrossCenter;
        auto& zd = table->SetCell(1, 1, dropZoom);
        zd.alignH = CrossAxisAlign::Stretch;
        zd.alignV = CrossAxisAlign::CrossCenter;
        vbox->AddChild(table);
        FillLayout();
        FillZoom();
    }

    {
        vbox->AddChild(NewVirtText({
            .s = Tr("Appearance"),
            .font = font,
            .isRtl = isRtl,
            .padding = DpiScaledInsets(12, 0, 4, 0),
        }));
        auto* table = new Table();
        table->SetSize(8, 2);
        table->colGap = DpiScale(8);
        table->rowGap = DpiScale(4);
        const Str names[] = {Tr("Interface font:"),         Tr("&Interface text size:"),
                             Tr("&Sidebar text size:"),     Tr("Home &thumbnail size:"),
                             Tr("UI icon size (px):"),      Tr("Recent documents shown:"),
                             Tr("Minimum tab width (px):"), Tr("Reference preview delay (ms):")};
        DropDown** controls[] = {&dropUiFamily,    &dropUiSize,      &dropTreeSize,    &dropThumbnailSize,
                                 &dropToolbarSize, &dropRecentCount, &dropMinTabWidth, &dropHoverDelay};
        for (int row = 0; row < dimofi(names); row++) {
            auto* label = NewVirtText({.s = names[row], .font = font, .isRtl = isRtl, .prefix = true});
            auto* drop = MakeDropDown(hwnd, GetFont(), isRtl, row >= 4);
            *controls[row] = drop;
            table->SetCell(row, 0, label).alignV = CrossAxisAlign::CrossCenter;
            auto& cell = table->SetCell(row, 1, drop);
            cell.alignH = CrossAxisAlign::Stretch;
            cell.alignV = CrossAxisAlign::CrossCenter;
        }
        StrVec families;
        families.Append(Tr("System (Windows)"));
        families.Append(StrL("Manrope"));
        families.Append(StrL("Pretendard Std"));
        families.Append(StrL("Public Sans"));
        dropUiFamily->SetItems(families);
        int familyIndex = 0;
        for (int i = 1; i < len(families); i++) {
            if (str::EqI(families[i], gSettings->uIFontFamily)) {
                familyIndex = i;
            }
        }
        CbSetCurrentSelection(dropUiFamily, familyIndex);
        FillSizeChoices(dropUiSize, uiSizes, gSettings ? gSettings->uIFontSize : 0, false);
        FillSizeChoices(dropTreeSize, treeSizes, gSettings ? gSettings->treeFontSize : 0, false);
        FillSizeChoices(dropThumbnailSize, thumbnailSizes, gSettings ? gSettings->homePageThumbnailSize : 100, true);
        FillNumberChoices(dropToolbarSize, StrL("12|16|18|24|28|32|40|48|64"), gSettings->toolbarSize);
        FillNumberChoices(dropRecentCount, StrL("10|20|30|50|100|200"), gSettings->homePageMaxRecentItems);
        FillNumberChoices(dropHoverDelay, StrL("0|150|300|500|750|1000|2000"),
                          std::max(0, gSettings->citationHoverDelay));
        FillNumberChoices(dropMinTabWidth, StrL("60|100|120|150|180|200|250|300|400"), gSettings->minTabWidth);
        vbox->AddChild(table);
        vbox->AddChild(NewVirtText({
            .s = Tr("Interface: menus, toolbar and dialogs. Sidebar: bookmarks."),
            .font = font,
            .isRtl = isRtl,
            .padding = DpiScaledInsets(4, 0, 0, 0),
        }));
    }

    {
        vbox->AddChild(
            NewVirtText({.s = Tr("Pen"), .font = font, .isRtl = isRtl, .padding = DpiScaledInsets(12, 0, 4, 0)}));
        auto* table = new Table();
        table->SetSize(3, 2);
        table->colGap = DpiScale(8);
        table->rowGap = DpiScale(4);
        const Str names[] = {Tr("Minimum width (pt):"), Tr("Maximum width (pt):"), Tr("Width adjustment step (pt):")};
        DropDown** controls[] = {&dropPenMin, &dropPenMax, &dropPenStep};
        for (int row = 0; row < 3; row++) {
            auto* label = NewVirtText({.s = names[row], .font = font, .isRtl = isRtl});
            auto* drop = MakeDropDown(hwnd, GetFont(), isRtl, true);
            *controls[row] = drop;
            table->SetCell(row, 0, label).alignV = CrossAxisAlign::CrossCenter;
            auto& cell = table->SetCell(row, 1, drop);
            cell.alignH = CrossAxisAlign::Stretch;
            cell.alignV = CrossAxisAlign::CrossCenter;
        }
        FillNumberChoices(dropPenMin, StrL("0.1|0.2|0.5|1|2"), gSettings->penMinWidth);
        FillNumberChoices(dropPenMax, StrL("4|8|12|16|24|32|64"), gSettings->penMaxWidth);
        FillNumberChoices(dropPenStep, StrL("0.1|0.2|0.5|1|2"), gSettings->penWidthStep);
        vbox->AddChild(table);
    }

    chkReferenceHover = MakeCheckbox(hwnd, Tr("Show reference previews on hover"), isRtl,
                                     gSettings && gSettings->citationHoverDelay >= 0, 8);
    chkReferenceHover->onStateChanged = MkMethod0<SettingsWnd, &SettingsWnd::OnReferenceHoverChanged>(this);
    OnReferenceHoverChanged();
    vbox->AddChild(chkReferenceHover);

    chkShowToc =
        MakeCheckbox(hwnd, Tr("Show the &bookmarks sidebar when available"), isRtl, gSettings && gSettings->showToc, 8);
    vbox->AddChild(chkShowToc);

    chkRememberState = MakeCheckbox(hwnd, Tr("&Remember these settings for each document"), isRtl,
                                    gSettings && gSettings->rememberStatePerDocument, 4);
    if (gSettings && !gSettings->rememberOpenedFiles) {
        chkRememberState->SetIsEnabled(false);
    }
    vbox->AddChild(chkRememberState);

    {
        auto* c = NewVirtText({
            .s = Tr("Advanced"),
            .font = font,
            .isRtl = isRtl,
            .padding = DpiScaledInsets(12, 0, 4, 0),
        });
        labelAdvanced = c;
        vbox->AddChild(c);
    }

    chkUseTabs = MakeCheckbox(hwnd, Tr("Use &tabs"), isRtl, gSettings && gSettings->useTabs, 0);
    vbox->AddChild(chkUseTabs);

    chkCheckUpdates =
        MakeCheckbox(hwnd, Tr("Automatically check for &updates"), isRtl, gSettings && gSettings->checkForUpdates, 4);
    if (!HasPermission(Perm::InternetAccess)) {
        chkCheckUpdates->SetIsEnabled(false);
    }
    vbox->AddChild(chkCheckUpdates);

    chkRememberOpened =
        MakeCheckbox(hwnd, Tr("Remember &opened files"), isRtl, gSettings && gSettings->rememberOpenedFiles, 4);
    chkRememberOpened->onStateChanged = MkMethod0<SettingsWnd, &SettingsWnd::OnRememberOpenedChanged>(this);
    vbox->AddChild(chkRememberOpened);

    if (showInverseSearch) {
        auto* hdr = NewVirtText({
            .s = Tr("Set inverse search command line"),
            .font = font,
            .isRtl = isRtl,
            .padding = DpiScaledInsets(12, 0, 4, 0),
        });
        labelInverse = hdr;
        vbox->AddChild(hdr);

        auto* lab = NewVirtText({
            .s = Tr("Enter the command line to invoke when you double-click on the PDF document:"),
            .font = font,
            .isRtl = isRtl,
            .padding = DpiScaledInsets(0, 0, 4, 0),
        });
        labelCmdLine = lab;
        vbox->AddChild(lab);

        dropInverse = MakeDropDown(hwnd, GetFont(), isRtl, true);
        vbox->AddChild(dropInverse);
        FillInverse();
    }
    //] ACCESSKEY_GROUP Settings Dialog

    {
        auto* hbox = new HBox();
        hbox->alignMain = MainAxisAlign::MainEnd;
        hbox->alignCross = CrossAxisAlign::CrossCenter;
        hbox->gap = font->averageCharWidth;
        auto pad = Insets{4, 0, 4, 0};

        btnCancel = NewThemedButton(hwnd, Tr("Cancel"), font, false);
        btnCancel->onClick = MkMethod1<SettingsWnd, VirtMouseEvent*, &SettingsWnd::OnCancel>(this);
        hbox->AddChild(new Padding(btnCancel, pad));
        btnOk = NewThemedButton(hwnd, Tr("OK"), font, true);
        btnOk->onClick = MkMethod1<SettingsWnd, VirtMouseEvent*, &SettingsWnd::OnOk>(this);
        hbox->AddChild(new Padding(btnOk, pad));
        vbox->AddChild(hbox);
    }

    scroll = new ScrollBox(vbox);
    scroll->lineDy = PlatformFontLineHeight(font) + DpiScale(8);
    auto* padding = new Padding(scroll, DpiScaledInsets(4, 8));
    layout = padding;

    int dx = DpiScale(480);
    LayoutAndSizeToContent(layout, dx, 0, hwnd);
    Rect workArea = PlatformWindowWorkArea(win ? win->hwndFrame : hwnd);
    Size client = HwndClientRect(hwnd).Size();
    if (!workArea.IsEmpty()) {
        client.dy = std::min(client.dy, std::max(DpiScale(240), workArea.dy - DpiScale(100)));
        ResizeHwndToClientArea(hwnd, client.dx, client.dy, false);
    }
    DoLayout(HwndClientRect(hwnd).Size());
    HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
    UpdateTheme();

    SetIsVisible(true);
    if (dropLayout) {
        HwndSetFocus(dropLayout->hwnd);
    }
    return true;
}

static void OnSettingsMessage(WindowBase::WndProcEvent* ev) {
    auto* window = (SettingsWnd*)ev->w;
    if (!window || !window->scroll) {
        return;
    }
    if (ev->msg == WM_VSCROLL && ev->lparam == 0) {
        window->scroll->OnVScroll(ev->wparam);
    } else if (ev->msg == WM_MOUSEWHEEL) {
        VirtMouseEvent mouse;
        mouse.wheelDelta = GET_WHEEL_DELTA_WPARAM(ev->wparam);
        window->scroll->OnMouseWheel(&mouse);
    } else {
        return;
    }
    ev->result = 0;
    ev->didHandle = true;
}

void ShowSettingsDialog(MainWindow* win) {
    if (!HasPermission(Perm::SavePreferences)) {
        return;
    }
    if (gSettingsWnd) {
        HwndSetFocus(gSettingsWnd->hwnd);
        if (gSettingsWnd->dropLayout) {
            HwndSetFocus(gSettingsWnd->dropLayout->hwnd);
        }
        return;
    }
    auto* wnd = new SettingsWnd();
    wnd->closeOnEsc = true;
    wnd->onWndProc = MkFunc1Void<WindowBase::WndProcEvent*>(OnSettingsMessage);
    wnd->onBeforeDelete = MkFunc0Void(ClearSettingsWnd);
    wnd->onClose = MkFunc1Void<WindowBase::CloseEvent*>(OnClose);
    wnd->onDestroy = MkFunc1Void<WindowBase::DestroyEvent*>(OnDestroy);
    wnd->SetFont(GetAppFont());
    bool ok = wnd->Create(win);
    if (!ok) {
        delete wnd;
        return;
    }
    gSettingsWnd = wnd;
}
