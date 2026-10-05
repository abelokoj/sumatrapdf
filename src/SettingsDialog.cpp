/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "base/File.h"
#include "base/Timer.h"

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
#include "UiFonts.h"
#include "DocController.h"
#include "EngineBase.h"
#include "DisplayModel.h"
#include "WindowTab.h"
#include "RenderCache.h"
#include "PdfDarkMode.h"
#include "RefHover.h"
#include <commdlg.h>

#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "SumatraDialogs.h"

static constexpr int kSettingsMaxWidth = 900;
static constexpr int kSettingsMinWidth = 560;

enum class SettingsView {
    Visible,
    Hidden
};

struct SettingsLabel : VirtText {
    Str display;
    bool wrapped = false;
    PlatformFont* measuredFont = nullptr;
    Size textSize, wrappedSize;
    int wrappedWidth = -1;

    SettingsLabel(const VirtTextArgs& args) : VirtText(args.s, args.font) {
        prefix = args.prefix;
        mnemonic = prefix ? MnemonicCharInStr(s) : 0;
        isRtl = args.isRtl;
        padding = args.padding;
        str::Builder text;
        for (int i = 0; i < len(s); i++) {
            if (prefix && s.s[i] == '&') {
                if (i + 1 < len(s) && s.s[i + 1] == '&')
                    i++;
                else
                    continue;
            }
            text.AppendChar(s.s[i]);
        }
        display = str::Dup(ToStrTemp(text));
    }
    ~SettingsLabel() override { str::Free(display); }

    Size Layout(Constraints bc) override {
        if (measuredFont != font) {
            textSize = PlatformFontMeasureText(font, display);
            measuredFont = font;
            wrappedWidth = -1;
        }
        int padX = padding.left + padding.right;
        int width = bc.HasBoundedWidth() ? std::max(1, bc.max.dx - padX) : -1;
        wrapped = width > 0 && textSize.dx > width;
        Size size = textSize;
        if (wrapped) {
            if (wrappedWidth != width) {
                wrappedSize = PlatformFontMeasureText(font, display, width);
                wrappedWidth = width;
            }
            size = wrappedSize;
        }
        return bc.Constrain({size.dx + padX, size.dy + padding.top + padding.bottom});
    }
    int MinIntrinsicWidth(int) override { return Layout(ExpandInf()).dx; }
    int MinIntrinsicHeight(int width) override { return Layout(ExpandHeight(width)).dy; }
    void Paint(VirtPaintCtx& ctx) override {
        if (!wrapped) {
            VirtText::Paint(ctx);
            return;
        }
        ctx.gfx->DrawText(display, ctx.content, gfxTextWrap | (isRtl ? gfxTextRtl : 0), font, ThemeWindowTextColor());
    }
};

static VirtText* NewSettingsLabel(const VirtTextArgs& args) {
    return new SettingsLabel(args);
}

struct SettingsDropDownMetric {
    PlatformFont* font;
    StrVec items;
    Size size;
    int dpi, idealDx, maxDx, height;
    DWORD margins;
};
struct SettingsMetrics {
    Vec<SettingsDropDownMetric*> entries;
#if IS_DEBUG
    int measured = 0, reused = 0;
#endif
    ~SettingsMetrics() {
        for (auto* entry : entries) delete entry;
    }
};
static SettingsMetrics settingsMetrics;

struct SettingsDropDown : DropDown {
    PlatformFont* measuredFont = nullptr;
    StrVec measuredItems;
    Size measuredSize;
    int measuredDpi = 0;
    int measuredIdealDx = 0;
    int measuredMaxDx = 0;
    int measuredHeight = 0;
    DWORD measuredMargins = 0;

    Size GetIdealSize() override {
        bool sameItems = len(items) == len(measuredItems);
        for (int i = 0; sameItems && i < len(items); i++) sameItems = str::Eq(items[i], measuredItems[i]);
        int height = HwndWindowRect(hwnd).dy;
        HWND edit = CbEditHwnd(hwnd);
        DWORD margins = edit ? (DWORD)SendMessageW(edit, EM_GETMARGINS, 0, 0) : 0;
        if (sameItems && measuredFont == font && measuredDpi == DpiGet() && measuredIdealDx == idealDx &&
            measuredMaxDx == maxDx && measuredHeight == height && measuredMargins == margins)
            return measuredSize;
        SettingsDropDownMetric* found = nullptr;
        for (auto* entry : settingsMetrics.entries) {
            if (entry->font != font || entry->dpi != DpiGet() || entry->idealDx != idealDx || entry->maxDx != maxDx ||
                entry->height != height || entry->margins != margins || len(entry->items) != len(items))
                continue;
            bool same = true;
            for (int i = 0; same && i < len(items); i++) same = str::Eq(entry->items[i], items[i]);
            if (same) {
                found = entry;
                break;
            }
        }
        if (found) {
            measuredSize = found->size;
#if IS_DEBUG
            settingsMetrics.reused++;
#endif
        } else {
            measuredSize = DropDown::GetIdealSize();
            // Reopening Settings keeps text metrics, with a bounded cache for changing fonts and values.
            constexpr int kSettingsMetricLimit = 64;
            if (len(settingsMetrics.entries) == kSettingsMetricLimit) {
                delete settingsMetrics.entries[0];
                VecRemoveAt(settingsMetrics.entries, 0);
            }
            auto* entry = new SettingsDropDownMetric{font, {}, measuredSize, DpiGet(), idealDx, maxDx, height, margins};
            for (Str item : items) entry->items.Append(item);
            VecAppend(settingsMetrics.entries, entry);
#if IS_DEBUG
            settingsMetrics.measured++;
#endif
        }
        measuredFont = font;
        measuredDpi = DpiGet();
        measuredIdealDx = idealDx;
        measuredMaxDx = maxDx;
        measuredHeight = height;
        measuredMargins = margins;
        measuredItems.Reset();
        for (Str item : items) measuredItems.Append(item);
        return measuredSize;
    }
};

struct SettingsForm : Table {
    Vec<Size> labelSizes;
    Vec<Size> valueSizes;
    int labelWidth = 0;
    int contentHeight = 0;
    bool stacked = false;
    bool rtl = false;
    int measuredWidth = -1;
    int measuredDpi = 0;
    PlatformFont* measuredFont = nullptr;
    Size naturalSize;

    Size NaturalSize() {
        if (measuredDpi != DpiGet() || measuredFont != GetAppFont()) {
            naturalSize = Table::Layout(ExpandInf());
            labelWidth = Table::ColWidth(0);
            measuredWidth = -1;
            measuredDpi = DpiGet();
            measuredFont = GetAppFont();
        }
        return naturalSize;
    }
    int MinIntrinsicWidth(int) override { return NaturalSize().dx; }

    Size Layout(Constraints bc) override {
        Size natural = NaturalSize();
        int width = bc.HasBoundedWidth() ? bc.max.dx : natural.dx;
        if (width == measuredWidth) return bc.Constrain({width, contentHeight});
        measuredWidth = width;
        stacked = width < natural.dx;
        VecClear(labelSizes);
        VecClear(valueSizes);
        contentHeight = 0;
        for (int row = 0; row < rows; row++) {
            int labelDx = stacked ? width : labelWidth;
            int valueDx = stacked ? width : std::max(1, width - labelWidth - colGap);
            Size label = GetCell(row, 0)->Layout(ExpandHeight(labelDx));
            Size value = GetCell(row, 1)->Layout(ExpandHeight(valueDx));
            VecAppend(labelSizes, label);
            VecAppend(valueSizes, value);
            contentHeight += stacked ? label.dy + rowGap + value.dy : std::max(label.dy, value.dy);
            if (row + 1 < rows) contentHeight += rowGap;
        }
        return bc.Constrain({width, contentHeight});
    }
    int MinIntrinsicHeight(int width) override { return Layout(ExpandHeight(width)).dy; }
    void SetBounds(Rect bounds) override {
        lastBounds = bounds;
        int y = bounds.y;
        for (int row = 0; row < rows; row++) {
            Size label = labelSizes[row], value = valueSizes[row];
            if (stacked) {
                GetCell(row, 0)->SetBounds({bounds.x, y, bounds.dx, label.dy});
                y += label.dy + rowGap;
                GetCell(row, 1)->SetBounds({bounds.x, y, bounds.dx, value.dy});
                y += value.dy + rowGap;
                continue;
            }
            int height = std::max(label.dy, value.dy);
            int valueWidth = std::max(1, bounds.dx - labelWidth - colGap);
            int labelX = rtl ? bounds.x + valueWidth + colGap : bounds.x;
            int valueX = rtl ? bounds.x : bounds.x + labelWidth + colGap;
            GetCell(row, 0)->SetBounds({labelX, y + (height - label.dy) / 2, labelWidth, label.dy});
            GetCell(row, 1)->SetBounds({valueX, y + (height - value.dy) / 2, valueWidth, value.dy});
            y += height + rowGap;
        }
    }
};

struct SettingsCheckbox : Checkbox {
    Size Layout(Constraints bc) override {
        Size size = GetIdealSize();
        int padX = insets.left + insets.right;
        int padY = insets.top + insets.bottom;
        if (bc.HasBoundedWidth() && size.dx > bc.max.dx - padX) {
            int glyph = DpiGetSystemMetrics(SM_CXMENUCHECK) + UiScalePx(8);
            Size text = PlatformFontMeasureText(font, GetTextTemp(), std::max(1, bc.max.dx - padX - glyph));
            size.dy = std::max(size.dy, text.dy + DpiScale(4));
        }
        childSize = bc.Inset(padX, padY).Constrain(size);
        return {childSize.dx + padX, childSize.dy + padY};
    }
};

struct SettingsViewport : ScrollBox {
    int wheelRemainder = 0;
    explicit SettingsViewport(ILayout* child) : ScrollBox(child) {}
    void ClipControls(ILayout* node) {
        if (auto* control = node->AsControl()) {
            Rect bounds = ChildPosWithinParent(control->hwnd);
            Rect clip = bounds.Intersect(lastBounds);
            clip.x -= bounds.x;
            clip.y -= bounds.y;
            HRGN region = CreateRectRgn(clip.x, clip.y, clip.x + clip.dx, clip.y + clip.dy);
            HRGN rounded = RoundedControlRegion(control->hwnd, bounds.Size());
            if (rounded) {
                CombineRgn(region, region, rounded, RGN_AND);
                DeleteObject(rounded);
            }
            HRGN current = CreateRectRgn(0, 0, 0, 0);
            bool unchanged = GetWindowRgn(control->hwnd, current) != ERROR && EqualRgn(current, region);
            DeleteObject(current);
            if (unchanged || !SetWindowRgn(control->hwnd, region, FALSE))
                DeleteObject(region);
            else
                InvalidateRect(control->hwnd, nullptr, FALSE);
        }
        for (int i = 0; i < node->LayoutChildCount(); i++) ClipControls(node->LayoutChildAt(i));
    }
    void SetBounds(Rect bounds) override {
        ScrollBox::SetBounds(bounds);
        ClipControls(child);
    }
    void Wheel(int delta) {
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (!lines) return;
        // Accumulate fractional pixels instead of discarding small touchpad deltas.
        int step = lines == WHEEL_PAGESCROLL ? lastBounds.dy
                                             : (int)std::min(lines, (UINT)INT_MAX / std::max(1, lineDy)) * lineDy;
        int64_t pixels = (int64_t)wheelRemainder - (int64_t)delta * step;
        int dy = (int)std::clamp<int64_t>(pixels / WHEEL_DELTA, INT_MIN, INT_MAX);
        wheelRemainder = (int)(pixels % WHEEL_DELTA);
        if (ScrollBy(dy)) ClipControls(child);
    }
};

// Section headers, labels and OK/Cancel are VirtCtrl; layout/zoom/command
// combos and the checkboxes are HWNDs. Same WindowBase layout as Inverse Search.
struct SettingsWnd : WindowBase {
    ~SettingsWnd() override { str::Free(colorFile); }

    MainWindow* win = nullptr;
    Str colorFile;
    DropDown* dropPageText = nullptr;
    DropDown* dropPageBg = nullptr;
    void PickPageColor(DropDown*);
    void PickPageText(VirtMouseEvent*) { PickPageColor(dropPageText); }
    void PickPageBg(VirtMouseEvent*) { PickPageColor(dropPageBg); }
    SettingsViewport* scroll = nullptr;
    Vec<SettingsForm*> forms;
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
    DropDown* dropInterfaceScale = nullptr;
    DropDown* dropUiSize = nullptr;
    DropDown* dropTreeSize = nullptr;
    DropDown* dropThumbnailSize = nullptr;
    DropDown* dropToolbarSize = nullptr;
    DropDown* dropRecentCount = nullptr;
    DropDown* dropTabListCount = nullptr;
    DropDown* dropScrollbarWidth = nullptr;
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

    VirtRichText* dataLocation = nullptr;
    void UpdateDataLocation();
    void ChooseDataFolder(VirtMouseEvent*);
    void RestoreDataFolder(VirtMouseEvent*);
    void ApplyDataFolder(Str folder);

    VirtButton* btnCancel = nullptr;
    VirtButton* btnOk = nullptr;

    bool Create(MainWindow* win, SettingsView view = SettingsView::Visible);
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

static bool ParseSettingNumber(Str text, Str unit, double& value) {
    char* input = CStrTemp(text);
    char* end = nullptr;
    value = strtod(input, &end);
    if (end == input || !isfinite(value)) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    Str suffix(end);
    while (len(suffix) && isspace((unsigned char)suffix.s[len(suffix) - 1])) suffix.len--;
    return !len(suffix) || (len(unit) && str::EqI(suffix, unit));
}

static bool ReadSettingNumber(DropDown* drop, Str unit, double& value, bool automatic = false) {
    Str text = drop->GetTextTemp();
    str::TrimWSInPlace(text, str::TrimOpt::Both);
    if (automatic && (str::EqI(text, Tr("Automatic (Windows)")) || str::EqI(text, StrL("automatic")))) {
        value = 0;
        return true;
    }
    return ParseSettingNumber(text, unit, value);
}

static int SelectedSize(DropDown* drop, const Vec<int>& sizes, int fallback) {
    int index = CbGetCurrentSelection(drop);
    if (index >= 0 && index < len(sizes)) return sizes[index];
    double value;
    bool percentage = len(sizes) && sizes[0] != 0;
    if (!ReadSettingNumber(drop, percentage ? StrL("%") : StrL("px"), value, !percentage)) return fallback;
    if (value != floor(value)) return fallback;
    return (int)limitValue(value, percentage ? 75.0 : 0.0, percentage ? 250.0 : 96.0);
}

static void FillNumberChoices(DropDown* drop, Str options, double current) {
    StrVec labels;
    Split(&labels, options, StrL("|"));
    drop->SetItems(labels);
    drop->SetText(fmt("%g", current));
}

static double SelectedNumber(DropDown* drop, double fallback, double minimum, double maximum, Str unit = {}) {
    double value;
    if (!ReadSettingNumber(drop, unit, value)) return fallback;
    return limitValue(value, minimum, maximum);
}

struct SettingsFontMatch {
    Str name;
    bool found = false;
};

static int CALLBACK FindSettingsFont(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM data) {
    auto* match = (SettingsFontMatch*)data;
    match->found = str::EqI(ToUtf8Temp(font->lfFaceName), match->name);
    return match->found ? 0 : 1;
}

static bool IsSettingsFont(Str name) {
    if (str::EqI(name, StrL("Manrope")) || str::EqI(name, StrL("Pretendard Std")) ||
        str::EqI(name, StrL("Public Sans")))
        return true;
    WStr wide = ToWStrTemp(name);
    if (!len(wide) || len(wide) >= LF_FACESIZE || wide.s[0] == L'@') return false;
    LOGFONTW font{};
    font.lfCharSet = DEFAULT_CHARSET;
    wstr::BufSet(WStr(font.lfFaceName, dimof(font.lfFaceName)), wide);
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    SettingsFontMatch match{name};
    EnumFontFamiliesExW(dc, &font, FindSettingsFont, (LPARAM)&match, 0);
    ReleaseDC(nullptr, dc);
    return match.found;
}

void SettingsWnd::UpdateDataLocation() {
    dataLocation->Reset();
    dataLocation->AddPlainText(fmt("%s %s", Tr("Current folder:"), GetAppDataDirTemp()));
    if (GetPendingDataDirTemp()) {
        dataLocation->AddPlainText(fmt("%s %s", Tr("After restart:"), GetPendingDataDirTemp()));
    }
    DoLayout();
    HwndInvalidate(hwnd);
}

void SettingsWnd::ApplyDataFolder(Str folder) {
    Str message =
        Tr("Copy current settings, dictionaries, vocabulary and reading data to this folder on the next "
           "restart?\n\nYes: copy into an empty folder and keep the originals.\nNo: use data already in the selected "
           "folder; your current data stays where it is.\nCancel: keep the current location.\n\nPDF files and "
           "annotations saved inside PDFs are not moved.");
    int choice =
        MessageBoxW(hwnd, CWStrTemp(message), CWStrTemp(Tr("Change data folder")), MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return;
    ScheduleSaveSettings();
    FlushScheduledSaveSettings();
    Str error;
    bool ok =
        RequestDataFolder(folder, choice == IDYES ? DataFolderMode::CopyCurrent : DataFolderMode::UseExisting, error);
    if (!ok) {
        MessageBoxW(hwnd, CWStrTemp(error), CWStrTemp(Tr("Data folder unchanged")), MB_OK | MB_ICONWARNING);
        str::Free(error);
        return;
    }
    UpdateDataLocation();
    MessageBoxW(hwnd,
                CWStrTemp(Tr("The folder choice is saved. Close all SumatraPDF Enhanced windows and restart to apply "
                             "it. The app continues using its current data until then.")),
                CWStrTemp(Tr("Restart required")), MB_OK | MB_ICONINFORMATION);
}

void SettingsWnd::ChooseDataFolder(VirtMouseEvent*) {
    BROWSEINFOW args{};
    args.hwndOwner = hwnd;
    args.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    args.lpszTitle = CWStrTemp(Tr("Choose the folder for SumatraPDF Enhanced data"));
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&args);
    if (!item) return;
    WCHAR folder[MAX_PATH];
    bool ok = SHGetPathFromIDListW(item, folder) != FALSE;
    CoTaskMemFree(item);
    if (ok) ApplyDataFolder(ToUtf8Temp(folder));
}

void SettingsWnd::RestoreDataFolder(VirtMouseEvent*) {
    ApplyDataFolder(GetDefaultDataDirTemp());
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
    CollectZoomPickerLevels(zoomLevels);
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
        dropZoom->SetText(ZoomLevelStrExact(startZoom));
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
    double zoom;
    if (!ParseSettingNumber(text, StrL("%"), zoom) || zoom <= 0) return startZoom;
    return (float)limitValue(zoom, (double)kZoomMin, (double)kZoomMax);
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
    Color colors[2] = {kColorUnset, kColorUnset};
    DropDown* colorDrops[] = {dropPageText, dropPageBg};
    for (int i = 0; i < 2 && len(colorFile); i++) {
        Str value = colorDrops[i]->GetTextTemp();
        str::TrimWSInPlace(value, str::TrimOpt::Both);
        if (str::EqI(value, Tr("Use theme"))) continue;
        ParsedColor parsed{};
        SetColorText(parsed, value);
        ParseColor(parsed);
        bool valid = parsed.parsedOk;
        colors[i] = parsed.col;
        FreeColorText(parsed);
        if (valid) continue;
        MessageBoxW(hwnd, CWStrTemp(Tr("Enter a color such as #202020, choose a color, or select Use theme.")),
                    CWStrTemp(Tr("Check color")), MB_OK | MB_ICONWARNING);
        SetFocus(colorDrops[i]->hwnd);
        return;
    }
    struct NumberField {
        DropDown* drop;
        Str label;
        Str unit;
        double minimum;
        double maximum;
        bool automatic = false;
        bool fractional = false;
    };
    NumberField fields[] = {
        {dropInterfaceScale, Tr("Overall interface scale"), StrL("%"), 50, 250},
        {dropUiSize, Tr("Interface text size"), StrL("px"), 9, 96, true},
        {dropTreeSize, Tr("Sidebar text size"), StrL("px"), 9, 96, true},
        {dropThumbnailSize, Tr("Home thumbnail size"), StrL("%"), 75, 250},
        {dropToolbarSize, Tr("UI icon size"), StrL("px"), 8, 64},
        {dropRecentCount, Tr("Recent documents shown"), {}, 1, 200},
        {dropTabListCount, Tr("Visible open-file list rows"), {}, 1, 50},
        {dropScrollbarWidth, Tr("Scrollbar width"), StrL("px"), 8, 40},
        {dropMinTabWidth, Tr("Minimum tab width"), StrL("px"), 60, 400},
        {dropHoverDelay, Tr("Reference preview delay"), StrL("ms"), 0, 2000},
        {dropPenMin, Tr("Minimum width"), StrL("pt"), 0.1, 64, false, true},
        {dropPenMax, Tr("Maximum width"), StrL("pt"), 0.1, 64, false, true},
        {dropPenStep, Tr("Width adjustment step"), StrL("pt"), 0.1, 16, false, true},
    };
    for (const auto& field : fields) {
        if (!field.drop->IsEnabled()) continue;
        double value;
        bool valid = ReadSettingNumber(field.drop, field.unit, value, field.automatic);
        valid = valid && ((value >= field.minimum && value <= field.maximum) || (field.automatic && value == 0));
        valid = valid && (field.fractional || value == floor(value));
        if (valid) continue;
        Str message =
            fmt("%s: %s %g–%g %s.%s", field.label, Tr("Enter a value in the range"), field.minimum, field.maximum,
                field.unit, field.automatic ? fmt(" %s", Tr("Use Automatic (Windows) for the default size.")) : Str{});
        MessageBoxW(hwnd, CWStrTemp(message), CWStrTemp(Tr("Check setting")), MB_OK | MB_ICONWARNING);
        SetFocus(field.drop->hwnd);
        return;
    }
    Str family = dropUiFamily->GetTextTemp();
    str::TrimWSInPlace(family, str::TrimOpt::Both);
    if (CbGetCurrentSelection(dropUiFamily) == 0 || str::EqI(family, Tr("System (Windows)")) ||
        str::EqI(family, StrL("system")))
        family = StrL("system");
    if (!str::EqI(family, StrL("system")) && !IsSettingsFont(family)) {
        MessageBoxW(hwnd, CWStrTemp(Tr("Choose a bundled font or enter the name of an installed Windows font.")),
                    CWStrTemp(Tr("Font unavailable")), MB_OK | MB_ICONWARNING);
        SetFocus(dropUiFamily->hwnd);
        return;
    }
    if (CbGetCurrentSelection(dropZoom) < 0) {
        double zoom;
        if (!ReadSettingNumber(dropZoom, StrL("%"), zoom) || zoom < kZoomMin || zoom > kZoomMax) {
            Str message =
                fmt("%s: %s %g–%g%%.", Tr("Default Zoom"), Tr("Enter a value in the range"), kZoomMin, kZoomMax);
            MessageBoxW(hwnd, CWStrTemp(message), CWStrTemp(Tr("Check setting")), MB_OK | MB_ICONWARNING);
            SetFocus(dropZoom->hwnd);
            return;
        }
    }
    double minimum = SelectedNumber(dropPenMin, gSettings->penMinWidth, 0.1, 64, StrL("pt"));
    double maximum = SelectedNumber(dropPenMax, gSettings->penMaxWidth, 0.1, 64, StrL("pt"));
    if (maximum < minimum) {
        MessageBoxW(hwnd, CWStrTemp(Tr("Maximum pen width must be at least the minimum pen width.")),
                    CWStrTemp(Tr("Check setting")), MB_OK | MB_ICONWARNING);
        SetFocus(dropPenMax->hwnd);
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
    int interfaceScale = (int)SelectedNumber(dropInterfaceScale, gSettings->interfaceScale, 50, 250, StrL("%"));
    bool fontsChanged = interfaceScale != gSettings->interfaceScale || uiSize != gSettings->uIFontSize ||
                        treeSize != gSettings->treeFontSize || !str::EqI(family, gSettings->uIFontFamily);
    str::ReplaceWithCopy(&gSettings->uIFontFamily, family);
    gSettings->uIFontSize = uiSize;
    gSettings->interfaceScale = interfaceScale;
    gSettings->treeFontSize = treeSize;
    gSettings->homePageThumbnailSize =
        SelectedSize(dropThumbnailSize, thumbnailSizes, gSettings->homePageThumbnailSize);
    gSettings->citationHoverDelay =
        chkReferenceHover->IsChecked() ? (int)SelectedNumber(dropHoverDelay, 300, 0, 2000, StrL("ms")) : -1;
    gSettings->toolbarSize = (int)SelectedNumber(dropToolbarSize, gSettings->toolbarSize, 8, 64, StrL("px"));
    gSettings->homePageMaxRecentItems = (int)SelectedNumber(dropRecentCount, gSettings->homePageMaxRecentItems, 1, 200);
    gSettings->tabListVisibleItems = (int)SelectedNumber(dropTabListCount, gSettings->tabListVisibleItems, 1, 50);
    gSettings->scrollbarWidth = (int)SelectedNumber(dropScrollbarWidth, gSettings->scrollbarWidth, 8, 40, StrL("px"));
    gSettings->minTabWidth = (int)SelectedNumber(dropMinTabWidth, gSettings->minTabWidth, 60, 400, StrL("px"));
    float penMin = (float)SelectedNumber(dropPenMin, gSettings->penMinWidth, 0.1, 64, StrL("pt"));
    float penMax = (float)SelectedNumber(dropPenMax, gSettings->penMaxWidth, penMin, 64, StrL("pt"));
    gSettings->penMinWidth = penMin;
    gSettings->penMaxWidth = penMax;
    gSettings->penWidthStep = (float)SelectedNumber(dropPenStep, gSettings->penWidthStep, 0.1, 16, StrL("pt"));
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
    if (len(colorFile)) {
        FileState* fs = FileHistoryFindByPath(colorFile);
        if (fs) {
            SetColorText(fs->pageTextColor, colors[0] == kColorUnset ? Str{} : SerializeColorTemp(colors[0]));
            SetColorText(fs->pageBackgroundColor, colors[1] == kColorUnset ? Str{} : SerializeColorTemp(colors[1]));
        }
        for (MainWindow* window : gWindows) {
            for (WindowTab* tab : window->Tabs()) {
                auto* dm = tab->ctrl ? tab->ctrl->AsFixed() : nullptr;
                if (!dm || !str::EqI(dm->GetFilePath(), colorFile)) continue;
                gRenderCache->CancelRenderingBlocking(dm);
                gRenderCache->FreeForDisplayModel(dm);
                dm->pageTextColor = colors[0];
                dm->pageBackgroundColor = colors[1];
                dm->RepaintDisplay();
                if (window->AsFixed() == dm) RefHoverRefreshColors(window->refHover);
            }
        }
    }
    // note: ideally we would also update state for useTabs changes but that's complicated since
    // to do it right we would have to convert tabs to windows. When moving no tabs -> tabs,
    // there's no problem. When moving tabs -> no tabs, a half solution would be to only
    // call SetTabsInTitlebar() for windows that have only one tab, but that's somewhat inconsistent
    ApplySettingsToOpenWindows();
    ScheduleSaveSettings();
    MaybeRedrawHomePage();
    ScheduleDelete();
}

void SettingsWnd::PickPageColor(DropDown* drop) {
    Color selected = kColWhite;
    if (drop == dropPageText) selected = kColBlack;
    ParseColor(&selected, drop->GetTextTemp());
    static COLORREF customColors[16]{};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.rgbResult = selected;
    cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (DarkModeChooseColor(&cc)) drop->SetText(SerializeColorTemp(cc.rgbResult));
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
    args.deferItems = true;
    auto* c = new SettingsDropDown();
    c->Create(args);
    return c;
}

static Checkbox* MakeCheckbox(HWND parent, PlatformFont* font, Str text, bool isRtl, bool checked, int topPt) {
    Checkbox::CreateArgs args;
    args.parent = parent;
    args.text = text;
    args.font = font;
    args.isRtl = isRtl;
    if (checked) {
        args.initialState = Checkbox::State::Checked;
    }
    auto* c = new SettingsCheckbox();
    c->SetInsetsPt(topPt, 0, 0, 0);
    c->Create(args);
    SetWindowLongPtrW(c->hwnd, GWL_STYLE, GetWindowLongPtrW(c->hwnd, GWL_STYLE) | BS_MULTILINE);
    return c;
}

static LRESULT CALLBACK SettingsFocusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, SettingsFocusProc, id);
    if (msg == WM_SETFOCUS) {
        auto* wnd = (SettingsWnd*)data;
        auto* scroll = wnd->scroll;
        Rect bounds = HwndMapRectToWindow(HwndClientRect(hwnd), hwnd, wnd->hwnd);
        Rect view = scroll->lastBounds;
        int delta = bounds.y < view.y ? bounds.y - view.y : std::max(0, bounds.y + bounds.dy - view.y - view.dy);
        if (delta && scroll->ScrollBy(delta)) scroll->ClipControls(scroll->child);
    }
    if (msg == WM_MOUSEWHEEL) {
        auto* wnd = (SettingsWnd*)data;
        HWND combo = GetParent(hwnd) == wnd->hwnd ? hwnd : GetParent(hwnd);
        if (!SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) return SendMessageW(wnd->hwnd, msg, wp, lp);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void SettingsFocusStops(SettingsWnd* wnd, ILayout* node) {
    if (auto* control = node->AsControl()) {
        SetWindowSubclass(control->hwnd, SettingsFocusProc, 1, (DWORD_PTR)wnd);
        if (HWND edit = CbEditHwnd(control->hwnd)) SetWindowSubclass(edit, SettingsFocusProc, 1, (DWORD_PTR)wnd);
    }
    for (int i = 0; i < node->LayoutChildCount(); i++) SettingsFocusStops(wnd, node->LayoutChildAt(i));
}

static void OnSettingsMessage(WindowBase::WndProcEvent* ev);

bool SettingsWnd::Create(MainWindow* mainWin, SettingsView view) {
#if IS_DEBUG
    TimeStamp openingStart = TimeGet();
    int measurementsBefore = settingsMetrics.measured, reuseBefore = settingsMetrics.reused;
#endif
    autoLayout = false;
    bool visible = view == SettingsView::Visible;
    onWndProc = MkFunc1Void<WindowBase::WndProcEvent*>(OnSettingsMessage);
    win = mainWin;
    auto* colorModel = win ? win->AsFixed() : nullptr;
    if (colorModel && EngineUsesDocumentColorsFollowTheme(colorModel->GetEngine()) &&
        !EngineUsesReflowThemeCss(colorModel->GetEngine())) {
        colorFile = str::Dup(colorModel->GetFilePath());
    }
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
        autoLayout = true;
        return false;
    }
    DpiScope dpi(hwnd);
    SetFont(GetAppFont());
    bool isRtl = IsUIRtl();

    auto* vbox = new VBox();
    vbox->alignMain = MainAxisAlign::MainStart;
    vbox->alignCross = CrossAxisAlign::Stretch;
    vbox->gap = UiScalePx(4);
    PlatformFont* headingFont = GetBoldPlatformFont(font);

    //[ ACCESSKEY_GROUP Settings Dialog
    {
        auto* c = NewSettingsLabel({
            .s = Tr("View"),
            .font = headingFont,
            .isRtl = isRtl,
            .padding = Insets{UiScalePx(0), UiScalePx(0), UiScalePx(4), UiScalePx(0)},
        });
        labelView = c;
        vbox->AddChild(c);
    }

    {
        // Default Layout / Default Zoom in a 2x2 table so the labels share a
        // column and the two drop-downs line up at the same left edge
        auto* labLayout = NewSettingsLabel({
            .s = Tr("Default &Layout:"),
            .font = font,
            .isRtl = isRtl,
            .prefix = true,
        });
        labelLayout = labLayout;
        dropLayout = MakeDropDown(hwnd, GetFont(), isRtl, false);

        auto* labZoom = NewSettingsLabel({
            .s = Tr("Default &Zoom:"),
            .font = font,
            .isRtl = isRtl,
            .prefix = true,
        });
        labelZoom = labZoom;
        dropZoom = MakeDropDown(hwnd, GetFont(), isRtl, true);

        auto* table = new SettingsForm();
        table->rtl = isRtl;
        VecAppend(forms, table);
        table->SetSize(2, 2);
        table->colGap = UiScalePx(20);
        table->rowGap = UiScalePx(6);
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

    if (len(colorFile)) {
        vbox->AddChild(NewSettingsLabel({.s = Tr("Current file colors"),
                                         .font = headingFont,
                                         .isRtl = isRtl,
                                         .padding = Insets{UiScalePx(12), 0, UiScalePx(4), 0}}));
        vbox->AddChild(NewSettingsLabel({.s = path::GetBaseNameTemp(colorFile), .font = font, .isRtl = isRtl}));
        auto* table = new SettingsForm();
        table->rtl = isRtl;
        table->SetSize(2, 2);
        table->colGap = UiScalePx(20);
        table->rowGap = UiScalePx(6);
        VecAppend(forms, table);
        DropDown** drops[] = {&dropPageText, &dropPageBg};
        Str labels[] = {Tr("Foreground:"), Tr("Page background:")};
        Color colors[] = {colorModel->pageTextColor, colorModel->pageBackgroundColor};
        for (int i = 0; i < 2; i++) {
            table->SetCell(i, 0, NewSettingsLabel({.s = labels[i], .font = font, .isRtl = isRtl})).alignV =
                CrossAxisAlign::CrossCenter;
            auto* drop = MakeDropDown(hwnd, font, isRtl, true);
            *drops[i] = drop;
            StrVec presets;
            presets.Append(Tr("Use theme"));
            for (Str color : {StrL("#000000"), StrL("#ffffff"), StrL("#202020"), StrL("#f5efdf"), StrL("#dceadf")})
                presets.Append(color);
            drop->SetItems(presets);
            drop->SetText(colors[i] == kColorUnset ? Tr("Use theme") : SerializeColorTemp(colors[i]));
            auto* choose = NewThemedButton(hwnd, Tr("Choose..."), font, false);
            choose->onClick = i == 0 ? MkMethod1<SettingsWnd, VirtMouseEvent*, &SettingsWnd::PickPageText>(this)
                                     : MkMethod1<SettingsWnd, VirtMouseEvent*, &SettingsWnd::PickPageBg>(this);
            auto* row = new HBox();
            row->gap = UiScalePx(8);
            row->alignCross = CrossAxisAlign::CrossCenter;
            row->AddChild(drop, 1);
            row->AddChild(choose);
            table->SetCell(i, 1, row).alignH = CrossAxisAlign::Stretch;
        }
        vbox->AddChild(table);
        vbox->AddChild(NewSettingsLabel(
            {.s = Tr("Colors apply to this file only. Select Use theme to reset."), .font = font, .isRtl = isRtl}));
    }

    {
        vbox->AddChild(NewSettingsLabel({
            .s = Tr("Appearance"),
            .font = headingFont,
            .isRtl = isRtl,
            .padding = Insets{UiScalePx(12), UiScalePx(0), UiScalePx(4), UiScalePx(0)},
        }));
        auto* table = new SettingsForm();
        table->rtl = isRtl;
        VecAppend(forms, table);
        table->SetSize(11, 2);
        table->colGap = UiScalePx(20);
        table->rowGap = UiScalePx(6);
        const Str names[] = {
            Tr("Overall interface scale (%):"), Tr("Interface font:"),         Tr("&Interface text size:"),
            Tr("&Sidebar text size:"),          Tr("Home &thumbnail size:"),   Tr("UI icon size (px):"),
            Tr("Recent documents shown:"),      Tr("Minimum tab width (px):"), Tr("Reference preview delay (ms):"),
            Tr("Visible open-file list rows:"), Tr("Scrollbar width (px):")};
        DropDown** controls[] = {&dropInterfaceScale, &dropUiFamily,     &dropUiSize,        &dropTreeSize,
                                 &dropThumbnailSize,  &dropToolbarSize,  &dropRecentCount,   &dropMinTabWidth,
                                 &dropHoverDelay,     &dropTabListCount, &dropScrollbarWidth};
        for (int row = 0; row < dimofi(names); row++) {
            auto* label = NewSettingsLabel({.s = names[row], .font = font, .isRtl = isRtl, .prefix = true});
            auto* drop = MakeDropDown(hwnd, GetFont(), isRtl, true);
            *controls[row] = drop;
            table->SetCell(row, 0, label).alignV = CrossAxisAlign::CrossCenter;
            auto& cell = table->SetCell(row, 1, drop);
            cell.alignH = CrossAxisAlign::Stretch;
            cell.alignV = CrossAxisAlign::CrossCenter;
        }
        FillNumberChoices(dropInterfaceScale, StrL("50|75|100|125|150|175|200|225|250"), gSettings->interfaceScale);
        StrVec families;
        families.Append(Tr("System (Windows)"));
        families.Append(StrL("Manrope"));
        families.Append(StrL("Pretendard Std"));
        families.Append(StrL("Public Sans"));
        int familyIndex = 0;
        for (int i = 1; i < len(families); i++) {
            if (str::EqI(families[i], gSettings->uIFontFamily)) {
                familyIndex = i;
            }
        }
        if (!familyIndex && len(gSettings->uIFontFamily) && !str::EqI(gSettings->uIFontFamily, StrL("system"))) {
            familyIndex = len(families);
            families.Append(gSettings->uIFontFamily);
        }
        dropUiFamily->SetItems(families);
        CbSetCurrentSelection(dropUiFamily, familyIndex);
        FillSizeChoices(dropUiSize, uiSizes, gSettings ? gSettings->uIFontSize : 0, false);
        FillSizeChoices(dropTreeSize, treeSizes, gSettings ? gSettings->treeFontSize : 0, false);
        FillSizeChoices(dropThumbnailSize, thumbnailSizes, gSettings ? gSettings->homePageThumbnailSize : 100, true);
        FillNumberChoices(dropToolbarSize, StrL("12|16|18|24|28|32|40|48|64"), gSettings->toolbarSize);
        FillNumberChoices(dropRecentCount, StrL("10|20|30|50|100|200"), gSettings->homePageMaxRecentItems);
        FillNumberChoices(dropTabListCount, StrL("5|10|15|20|30|50"), gSettings->tabListVisibleItems);
        FillNumberChoices(dropScrollbarWidth, StrL("8|12|16|20|24|28|32|40"), gSettings->scrollbarWidth);
        FillNumberChoices(dropHoverDelay, StrL("0|150|300|500|750|1000|2000"),
                          std::max(0, gSettings->citationHoverDelay));
        FillNumberChoices(dropMinTabWidth, StrL("60|100|120|150|180|200|250|300|400"), gSettings->minTabWidth);
        vbox->AddChild(table);
        vbox->AddChild(NewSettingsLabel({
            .s = Tr("Overall scale changes the interface, not document zoom. Individual font and icon sizes remain "
                    "available. Choose a preset or type a custom value. You can also enter an installed font name."),
            .font = font,
            .isRtl = isRtl,
            .padding = Insets{UiScalePx(4), UiScalePx(0), UiScalePx(0), UiScalePx(0)},
        }));
    }

    {
        vbox->AddChild(NewSettingsLabel({.s = Tr("Data storage"),
                                         .font = headingFont,
                                         .isRtl = isRtl,
                                         .padding = Insets{UiScalePx(12), UiScalePx(0), UiScalePx(4), UiScalePx(0)}}));
        dataLocation = new VirtRichText();
        dataLocation->font = font;
        dataLocation->AddPlainText(fmt("%s %s", Tr("Current folder:"), GetAppDataDirTemp()));
        if (GetPendingDataDirTemp()) {
            dataLocation->AddPlainText(fmt("%s %s", Tr("After restart:"), GetPendingDataDirTemp()));
        }
        vbox->AddChild(dataLocation);
        auto* details = new VirtRichText();
        details->font = font;
        details->AddPlainText(
            Tr("Settings, dictionaries, saved vocabulary, practice progress and reading history use this folder. PDF "
               "annotations remain in their PDF files. Folder changes apply after restart; originals are kept."));
        vbox->AddChild(details);
        auto* choose = NewThemedButton(hwnd, Tr("Choose data folder..."), font, false);
        choose->onClick = MkMethod1<SettingsWnd, VirtMouseEvent*, &SettingsWnd::ChooseDataFolder>(this);
        auto* reset = NewThemedButton(hwnd, Tr("Use default folder"), font, false);
        reset->onClick = MkMethod1<SettingsWnd, VirtMouseEvent*, &SettingsWnd::RestoreDataFolder>(this);
        choose->SetIsEnabled(!IsDataFolderOverridden() && !gForTesting);
        reset->SetIsEnabled(!IsDataFolderOverridden() && !gForTesting);
        auto* folderActions = new Wrap();
        folderActions->rtl = isRtl;
        folderActions->colGap = UiScalePx(8);
        folderActions->rowGap = UiScalePx(4);
        folderActions->AddChild(choose);
        folderActions->AddChild(reset);
        vbox->AddChild(folderActions);
        if (IsDataFolderOverridden() || gForTesting) {
            auto* explanation = new VirtRichText();
            explanation->font = font;
            explanation->AddPlainText(
                Tr("This session uses a command-line or testing data folder. Restart normally to change the persistent "
                   "location."));
            vbox->AddChild(explanation);
        }
    }

    {
        vbox->AddChild(NewSettingsLabel({.s = Tr("Pen"),
                                         .font = headingFont,
                                         .isRtl = isRtl,
                                         .padding = Insets{UiScalePx(12), UiScalePx(0), UiScalePx(4), UiScalePx(0)}}));
        auto* table = new SettingsForm();
        table->rtl = isRtl;
        VecAppend(forms, table);
        table->SetSize(3, 2);
        table->colGap = UiScalePx(20);
        table->rowGap = UiScalePx(6);
        const Str names[] = {Tr("Minimum width (pt):"), Tr("Maximum width (pt):"), Tr("Width adjustment step (pt):")};
        DropDown** controls[] = {&dropPenMin, &dropPenMax, &dropPenStep};
        for (int row = 0; row < 3; row++) {
            auto* label = NewSettingsLabel({.s = names[row], .font = font, .isRtl = isRtl});
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

    chkReferenceHover = MakeCheckbox(hwnd, GetFont(), Tr("Show reference previews on hover"), isRtl,
                                     gSettings && gSettings->citationHoverDelay >= 0, 8);
    chkReferenceHover->onStateChanged = MkMethod0<SettingsWnd, &SettingsWnd::OnReferenceHoverChanged>(this);
    OnReferenceHoverChanged();
    vbox->AddChild(chkReferenceHover);

    chkShowToc = MakeCheckbox(hwnd, GetFont(), Tr("Show the &bookmarks sidebar when available"), isRtl,
                              gSettings && gSettings->showToc, 8);
    vbox->AddChild(chkShowToc);

    chkRememberState = MakeCheckbox(hwnd, GetFont(), Tr("&Remember these settings for each document"), isRtl,
                                    gSettings && gSettings->rememberStatePerDocument, 4);
    if (gSettings && !gSettings->rememberOpenedFiles) {
        chkRememberState->SetIsEnabled(false);
    }
    vbox->AddChild(chkRememberState);

    {
        auto* c = NewSettingsLabel({
            .s = Tr("Advanced"),
            .font = headingFont,
            .isRtl = isRtl,
            .padding = Insets{UiScalePx(12), UiScalePx(0), UiScalePx(4), UiScalePx(0)},
        });
        labelAdvanced = c;
        vbox->AddChild(c);
    }

    chkUseTabs = MakeCheckbox(hwnd, GetFont(), Tr("Use &tabs"), isRtl, gSettings && gSettings->useTabs, 0);
    vbox->AddChild(chkUseTabs);

    chkCheckUpdates = MakeCheckbox(hwnd, GetFont(), Tr("Check for Enhanced releases automatically"), isRtl,
                                   gSettings && gSettings->checkForUpdates, 0);
    vbox->AddChild(chkCheckUpdates);
    auto* updateHelp = new VirtRichText();
    updateHelp->font = font;
    updateHelp->AddPlainText(
        Tr("Checks GitHub at most once a day. Downloads start only when you choose them. Use Help > Check for updates "
           "at any time."));
    vbox->AddChild(updateHelp);

    chkRememberOpened = MakeCheckbox(hwnd, GetFont(), Tr("Remember &opened files"), isRtl,
                                     gSettings && gSettings->rememberOpenedFiles, 4);
    chkRememberOpened->onStateChanged = MkMethod0<SettingsWnd, &SettingsWnd::OnRememberOpenedChanged>(this);
    vbox->AddChild(chkRememberOpened);

    if (showInverseSearch) {
        auto* hdr = NewSettingsLabel({
            .s = Tr("Set inverse search command line"),
            .font = headingFont,
            .isRtl = isRtl,
            .padding = Insets{UiScalePx(12), UiScalePx(0), UiScalePx(4), UiScalePx(0)},
        });
        labelInverse = hdr;
        vbox->AddChild(hdr);

        auto* lab = NewSettingsLabel({
            .s = Tr("Enter the command line to invoke when you double-click on the PDF document:"),
            .font = font,
            .isRtl = isRtl,
            .padding = Insets{UiScalePx(0), UiScalePx(0), UiScalePx(4), UiScalePx(0)},
        });
        labelCmdLine = lab;
        vbox->AddChild(lab);

        dropInverse = MakeDropDown(hwnd, GetFont(), isRtl, true);
        vbox->AddChild(dropInverse);
        FillInverse();
    }
    //] ACCESSKEY_GROUP Settings Dialog

    auto* root = new VBox();
    root->alignCross = CrossAxisAlign::Stretch;
    root->gap = UiScalePx(12);
    scroll = new SettingsViewport(vbox);
    scroll->lineDy = PlatformFontLineHeight(font) + UiScalePx(8);
    root->AddChild(scroll, 1);
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
        root->AddChild(hbox);
    }

    int pad = UiScalePx(16);
    layout = new Padding(root, Insets{pad, pad, pad, pad});
#if IS_DEBUG
    double controlsMs = TimeSinceInMs(openingStart);
    TimeStamp layoutStart = TimeGet();
#endif
    int naturalWidth = 0;
    for (auto* form : forms) naturalWidth = std::max(naturalWidth, form->MinIntrinsicWidth(0));
    int width = limitValue(naturalWidth + pad * 2, DpiScale(kSettingsMinWidth), DpiScale(kSettingsMaxWidth));
    Rect workArea = PlatformWindowWorkArea(win ? win->hwndFrame : hwnd);
    if (!workArea.IsEmpty()) width = std::min(width, std::max(1, workArea.dx - DpiScale(48)));
    Size desired = layout->Layout(ExpandHeight(width));
    int maxHeight = DpiScale(760);
    if (!workArea.IsEmpty()) maxHeight = std::min(maxHeight, std::max(1, workArea.dy - DpiScale(80)));
    ResizeHwndToClientArea(hwnd, width, std::min(desired.dy, maxHeight), false);
    DoLayout(HwndClientRect(hwnd).Size());
    autoLayout = true;
    HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
#if IS_DEBUG
    double layoutMs = TimeSinceInMs(layoutStart);
    TimeStamp themeStart = TimeGet();
#endif
    UpdateTheme();

    SettingsFocusStops(this, scroll->child);
    SetIsVisible(visible);
    if (visible && dropLayout) {
        HwndSetFocus(dropLayout->hwnd);
    }
#if IS_DEBUG
    logf(
        "Settings opening: %.3f ms controls, %.3f ms layout, %.3f ms theme/show; %d dropdown measurements, %d "
        "reused; %.3f ms total\n",
        controlsMs, layoutMs, TimeSinceInMs(themeStart), settingsMetrics.measured - measurementsBefore,
        settingsMetrics.reused - reuseBefore, TimeSinceInMs(openingStart));
#endif
    return true;
}

static void OnSettingsMessage(WindowBase::WndProcEvent* ev) {
    auto* window = (SettingsWnd*)ev->w;
    if (!window || !window->scroll) {
        return;
    }
    if (ev->msg == WM_PRINTCLIENT && ev->wparam && window->vroot) {
        PaintVirtTree(window->vroot, (HDC)ev->wparam, HwndClientRect(window->hwnd), ThemeWindowBackgroundColor());
        ev->result = 0;
        ev->didHandle = true;
        return;
    }
    if (ev->msg == WM_VSCROLL && ev->lparam == 0) {
        window->scroll->OnVScroll(ev->wparam);
        window->scroll->ClipControls(window->scroll->child);
    } else if (ev->msg == WM_MOUSEWHEEL) {
        window->scroll->Wheel(GET_WHEEL_DELTA_WPARAM(ev->wparam));
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

#if IS_DEBUG

struct SettingsMeasureProbe : SettingsLabel {
    int layouts = 0;
    SettingsMeasureProbe() : SettingsLabel({.s = StrL("Measured label"), .font = GetAppFont()}) {}
    Size Layout(Constraints bc) override {
        layouts++;
        return SettingsLabel::Layout(bc);
    }
};

static void SettingsResponsivenessTests(SettingsWnd* wnd) {
    SettingsForm form;
    form.SetSize(1, 2);
    auto* probe = new SettingsMeasureProbe();
    form.SetCell(0, 0, probe);
    form.SetCell(0, 1, NewSettingsLabel({.s = StrL("Value"), .font = GetAppFont()}));
    form.Layout(ExpandHeight(DpiScale(500)));
    int measured = probe->layouts;
    for (int i = 0; i < 20; i++) form.Layout(ExpandHeight(DpiScale(500)));
    utassert(probe->layouts == measured);
    form.Layout(ExpandHeight(DpiScale(200)));
    utassert(probe->layouts > measured);
    form.Layout(ExpandHeight(DpiScale(50)));
    utassert(probe->wrapped);
    measured = probe->layouts;
    form.MinIntrinsicWidth(0);
    form.Layout(ExpandHeight(DpiScale(50)));
    utassert(probe->layouts == measured && probe->wrapped);

    wnd->scroll->ScrollTo(0);
    wnd->scroll->ClipControls(wnd->scroll->child);
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    for (int i = 0; i < WHEEL_DELTA; i++) SendMessageW(wnd->hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-1), 0);
    utassert(lines ? wnd->scroll->scrollY > 0 : wnd->scroll->scrollY == 0);
    wnd->scroll->ScrollTo(0);
    wnd->scroll->ClipControls(wnd->scroll->child);
    int selection = CbGetCurrentSelection(wnd->dropLayout);
    SendMessageW(wnd->dropLayout->hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), 0);
    utassert(lines ? wnd->scroll->scrollY > 0 : wnd->scroll->scrollY == 0);
    utassert(CbGetCurrentSelection(wnd->dropLayout) == selection);
    wnd->scroll->ScrollTo(0);
    wnd->scroll->ClipControls(wnd->scroll->child);
    HWND edit = CbEditHwnd(wnd->dropZoom);
    utassert(edit);
    SendMessageW(edit, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), 0);
    utassert(lines ? wnd->scroll->scrollY > 0 : wnd->scroll->scrollY == 0);
    wnd->scroll->ScrollTo(0);
    wnd->scroll->ClipControls(wnd->scroll->child);

    auto* drop = (SettingsDropDown*)wnd->dropLayout;
    Size original = drop->GetIdealSize();
    StrVec choices;
    choices.Append(StrL("A much longer replacement setting choice for measurement invalidation"));
    drop->SetItems(choices);
    utassert(drop->GetIdealSize().dx > original.dx);
    wnd->FillLayout();
    CbSetCurrentSelection(drop, selection);
}

static void SettingsCustomValueTests(SettingsWnd* wnd) {
    wnd->dropTabListCount->SetText(StrL("17"));
    utassert(SelectedNumber(wnd->dropTabListCount, 10, 1, 50) == 17);
    wnd->dropScrollbarWidth->SetText(StrL("26 px"));
    utassert(SelectedNumber(wnd->dropScrollbarWidth, 20, 8, 40, StrL("px")) == 26);
    Color parsed;
    utassert(ParseColor(&parsed, StrL("#123456")) && parsed == MkRgb(0x12, 0x34, 0x56));
    utassert(!ParseColor(&parsed, StrL("#not-a-color")));
    DarkModeProfile first, second, theme;
    BuildViewDarkModeProfile(nullptr, &first, MkRgb(20, 30, 40), MkRgb(230, 240, 250));
    BuildViewDarkModeProfile(nullptr, &second, MkRgb(200, 210, 220), MkRgb(10, 20, 30));
    BuildViewDarkModeProfile(nullptr, &theme);
    utassert(first.foreground == MkRgb(20, 30, 40) && first.pageBackground == MkRgb(230, 240, 250));
    utassert(second.foreground == MkRgb(200, 210, 220) && second.pageBackground == MkRgb(10, 20, 30));
    utassert(first.hash != second.hash && first.hash != theme.hash);
    Settings* sample = NewSettings({});
    FileState* state = NewFileState(StrL("reading-colors.pdf"));
    state->useDefaultState = false;
    SetColorText(state->pageTextColor, StrL("#123456"));
    SetColorText(state->pageBackgroundColor, StrL("#f5efdf"));
    VecAppend(*sample->fileStates, state);
    Str serialized = SerializeSettings(sample, {});
    Settings* restored = NewSettings(serialized);
    utassert(len(*restored->fileStates) == 1);
    if (len(*restored->fileStates)) {
        utassert(GetParsedColor((*restored->fileStates)[0]->pageTextColor, kColorUnset) == MkRgb(0x12, 0x34, 0x56));
        utassert(GetParsedColor((*restored->fileStates)[0]->pageBackgroundColor, kColorUnset) ==
                 MkRgb(0xf5, 0xef, 0xdf));
    }
    str::Free(serialized);
    DeleteSettings(sample);
    DeleteSettings(restored);
    DropDown* fields[] = {wnd->dropUiSize, wnd->dropTreeSize, wnd->dropThumbnailSize};
    Vec<int>* choices[] = {&wnd->uiSizes, &wnd->treeSizes, &wnd->thumbnailSizes};
    const int values[] = {23, 37, 137};
    for (int i = 0; i < dimofi(fields); i++) {
        utassert(CbEditHwnd(fields[i]));
        CbSetCurrentSelection(fields[i], -1);
        fields[i]->SetText(fmt("%d", values[i]));
        utassert(SelectedSize(fields[i], *choices[i], 100) == values[i]);
    }
    wnd->dropUiSize->SetText(StrL("23 px"));
    utassert(SelectedSize(wnd->dropUiSize, wnd->uiSizes, 100) == 23);
    wnd->dropUiSize->SetText(StrL(" Automatic (Windows) "));
    utassert(SelectedSize(wnd->dropUiSize, wnd->uiSizes, 100) == 0);
    wnd->dropThumbnailSize->SetText(StrL("137%"));
    utassert(SelectedSize(wnd->dropThumbnailSize, wnd->thumbnailSizes, 100) == 137);
    double value;
    utassert(!ParseSettingNumber(StrL("nan"), {}, value));
    utassert(!ParseSettingNumber(StrL("1e999"), {}, value));
    utassert(!ParseSettingNumber(StrL("24oops"), StrL("px"), value));
    utassert(!ParseSettingNumber(StrL("24%"), StrL("px"), value));
    utassert(ParseSettingNumber(StrL(" 0.75 pt "), StrL("pt"), value) && value == 0.75);
    VecClear(wnd->uiSizes);
    VecClear(wnd->treeSizes);
    VecClear(wnd->thumbnailSizes);
    FillSizeChoices(wnd->dropUiSize, wnd->uiSizes, 0, false);
    FillSizeChoices(wnd->dropTreeSize, wnd->treeSizes, 0, false);
    FillSizeChoices(wnd->dropThumbnailSize, wnd->thumbnailSizes, 100, true);
    wnd->dropToolbarSize->SetText(StrL("24oops"));
    utassert(SelectedNumber(wnd->dropToolbarSize, 16, 8, 64) == 16);
    wnd->dropToolbarSize->SetText(StrL("24 px"));
    utassert(SelectedNumber(wnd->dropToolbarSize, 16, 8, 64, StrL("px")) == 24);
    utassert(CbEditHwnd(wnd->dropUiFamily));
    utassert(IsSettingsFont(StrL("Manrope")));
    utassert(IsSettingsFont(StrL("Segoe UI")));
    utassert(!IsSettingsFont(StrL("Nonexistent enhanced settings font")));
    utassert(!IsSettingsFont(StrL("NoSuchEnhancedFont")));
    utassert(str::Eq(ResolveUiFontName(StrL("Segoe UI")), StrL("Segoe UI")));
    utassert(!len(ResolveUiFontName(StrL("system"))));
    CbSetCurrentSelection(wnd->dropZoom, -1);
    wnd->dropZoom->SetText(StrL("137.5%"));
    utassert(wnd->SelectedZoom() == 137.5f);
    wnd->dropZoom->SetText(StrL("137oops"));
    utassert(wnd->SelectedZoom() == wnd->startZoom);
    wnd->FillZoom();
}

struct SettingsPrintCtx {
    HDC dc;
    HWND hwnd;
    Rect viewport;
};

static BOOL CALLBACK PrintSettingsChild(HWND hwnd, LPARAM data) {
    if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE)) return TRUE;
    auto* args = (SettingsPrintCtx*)data;
    if (GetParent(hwnd) != args->hwnd) return TRUE;
    HDC dc = args->dc;
    Rect bounds = ChildPosWithinParent(hwnd);
    int saved = SaveDC(dc);
    SetViewportOrgEx(dc, bounds.x, bounds.y, nullptr);
    IntersectClipRect(dc, 0, 0, bounds.dx, bounds.dy);
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (GetWindowRgn(hwnd, region) != ERROR) {
        Rect expected = bounds.Intersect(args->viewport);
        RECT regionBounds{};
        GetRgnBox(region, &regionBounds);
        utassert(regionBounds.left >= 0 && regionBounds.top >= 0);
        utassert(regionBounds.right <= bounds.dx && regionBounds.bottom <= bounds.dy);
        if (regionBounds.right > regionBounds.left && regionBounds.bottom > regionBounds.top) {
            RECT actual = regionBounds;
            OffsetRect(&actual, bounds.x, bounds.y);
            utassert(actual.left >= expected.x && actual.top >= expected.y);
            utassert(actual.right <= expected.Right() && actual.bottom <= expected.Bottom());
        }
        OffsetRgn(region, bounds.x, bounds.y);
        ExtSelectClipRgn(dc, region, RGN_AND);
    }
    DeleteObject(region);
    SendMessageW(hwnd, WM_PRINT, (WPARAM)dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
    RestoreDC(dc, saved);
    return TRUE;
}

static void CaptureSettings(SettingsWnd* wnd, Str path) {
    Size size = HwndClientRect(wnd->hwnd).Size();
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size.dx, -size.dy, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    utassert(bitmap && pixels);
    if (!bitmap || !pixels) {
        DeleteDC(dc);
        return;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    SendMessageW(wnd->hwnd, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    SettingsPrintCtx args{dc, wnd->hwnd, wnd->scroll->lastBounds};
    EnumChildWindows(wnd->hwnd, PrintSettingsChild, (LPARAM)&args);
    GdiFlush();
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + size.dx * size.dy * 4;
    str::Builder bytes;
    bytes.Append(Str((const char*)&header, sizeof(header)));
    bytes.Append(Str((const char*)&info.bmiHeader, sizeof(info.bmiHeader)));
    bytes.Append(Str((const char*)pixels, size.dx * size.dy * 4));
    utassert(file::WriteFile(path, ToStrTemp(bytes)));
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

static void SettingsSnapshots() {
    WCHAR folder[1024]{};
    DWORD length = GetEnvironmentVariableW(L"SUMATRA_SETTINGS_SNAPSHOTS", folder, dimof(folder));
    if (!length || length >= dimof(folder)) return;
    Str output = ToUtf8Temp(folder);
    utassert(dir::CreateAll(output));
    // Unit tests run before normal startup initializes native control theming.
    // Captures must use the same subclasses as the actual Settings window.
    auto* oldDarkModeHook = gWindowBaseApplyDarkMode;
    DarkModeInit();
    defer {
        gWindowBaseApplyDarkMode = oldDarkModeHook;
    };
    RenderCache* previousCache = gRenderCache;
    if (!previousCache) gRenderCache = new RenderCache();
    int originalTheme = ThemeGetCurrentIndex();
    defer {
        SetThemeByIndex(originalTheme);
        if (!previousCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    for (int variant = 0; variant < 3; variant++) {
        gSettings->uIFontSize = variant == 2 ? 28 : 0;
        gSettings->interfaceScale = variant == 2 ? 150 : 100;
        str::ReplaceWithCopy(&gSettings->theme, variant == 0 ? StrL("Sumatra Light") : StrL("Modern Green Dark"));
        SetCurrentThemeFromSettings();
        RefreshUiFonts();
        auto* wnd = new SettingsWnd();
        wnd->SetFont(GetAppFont());
        utassert(wnd->Create(nullptr, SettingsView::Hidden));
        if (variant == 2) {
            ResizeHwndToClientArea(wnd->hwnd, DpiScale(640), DpiScale(760), false);
            wnd->DoLayout();
        }
        utassert(!IsWindowVisible(wnd->hwnd));
        CaptureSettings(wnd, path::JoinTemp(output, fmt("settings-%d.bmp", variant)));
        wnd->scroll->ScrollTo(INT_MAX);
        wnd->scroll->ClipControls(wnd->scroll->child);
        CaptureSettings(wnd, path::JoinTemp(output, fmt("settings-%d-bottom.bmp", variant)));
        DestroyWindow(wnd->hwnd);
        delete wnd;
    }
}

static void SettingsOpeningTests() {
    for (auto* entry : settingsMetrics.entries) delete entry;
    VecReset(settingsMetrics.entries);
    int before = settingsMetrics.measured;
    auto* first = new SettingsWnd();
    first->SetFont(GetAppFont());
    utassert(first->Create(nullptr, SettingsView::Hidden));
    int coldMeasurements = settingsMetrics.measured - before;
    int width = HwndClientRect(first->hwnd).dx;
    utassert(!IsWindowVisible(first->hwnd) && first->autoLayout);
    DestroyWindow(first->hwnd);
    delete first;

    before = settingsMetrics.measured;
    int reused = settingsMetrics.reused;
    auto* second = new SettingsWnd();
    second->SetFont(GetAppFont());
    utassert(second->Create(nullptr, SettingsView::Hidden));
    utassert(!IsWindowVisible(second->hwnd) && second->autoLayout);
    utassert(HwndClientRect(second->hwnd).dx == width);
    utassert(settingsMetrics.measured - before < coldMeasurements);
    utassert(settingsMetrics.reused > reused);
    utassert(len(settingsMetrics.entries) <= 64);
    DestroyWindow(second->hwnd);
    delete second;
}

bool SettingsDialog_UnitTestsSizing() {
    RenderCache* savedCache = gRenderCache;
    if (!savedCache) gRenderCache = new RenderCache();
    defer {
        if (!savedCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    Settings* saved = gSettings;
    gSettings = NewSettings({});
    RefreshUiFonts();
    if (!ThemeGetCount()) CreateThemeCommands();
    SetCurrentThemeFromSettings();
    defer {
        DeleteSettings(gSettings);
        gSettings = saved;
        if (gSettings) SetCurrentThemeFromSettings();
        RefreshUiFonts();
    };
    utassert(gSettings);
    SettingsOpeningTests();
    auto* wnd = new SettingsWnd();
    wnd->SetFont(GetAppFont());
    utassert(wnd->Create(nullptr, SettingsView::Hidden));
    Rect work = PlatformWindowWorkArea(wnd->hwnd);
    int maxWidth = std::min(DpiScale(900), std::max(1, work.dx - DpiScale(32)));
    utassert(HwndClientRect(wnd->hwnd).dx <= maxWidth);
    utassert(!IsWindowVisible(wnd->hwnd));
    SettingsResponsivenessTests(wnd);
    SettingsCustomValueTests(wnd);
    Rect footer = wnd->btnOk->lastBounds;
    utassert(footer.y >= wnd->scroll->lastBounds.y + wnd->scroll->lastBounds.dy);
    wnd->scroll->ScrollTo(INT_MAX);
    wnd->scroll->ClipControls(wnd->scroll->child);
    utassert(wnd->btnOk->lastBounds == footer);
    wnd->scroll->ScrollTo(0);
    wnd->scroll->ClipControls(wnd->scroll->child);
    SendMessageW(wnd->chkRememberOpened->hwnd, WM_SETFOCUS, 0, 0);
    Rect focused = ChildPosWithinParent(wnd->chkRememberOpened->hwnd);
    Rect view = wnd->scroll->lastBounds;
    utassert(focused.y >= view.y && focused.y + focused.dy <= view.y + view.dy);
    ResizeHwndToClientArea(wnd->hwnd, DpiScale(360), DpiScale(500), false);
    wnd->DoLayout();
    bool stacked = false;
    for (auto* form : wnd->forms) {
        stacked |= form->stacked;
        for (int row = 0; row < form->rows; row++) {
            Rect label = form->GetCell(row, 0)->lastBounds;
            Rect value = form->GetCell(row, 1)->lastBounds;
            if (form->stacked)
                utassert(label.y + label.dy <= value.y);
            else
                utassert(label.x + label.dx <= value.x);
            utassert(value.x + value.dx <= form->lastBounds.x + form->lastBounds.dx);
        }
    }
    utassert(stacked);
    DestroyWindow(wnd->hwnd);
    delete wnd;
    SettingsSnapshots();
    return true;
}
#endif
