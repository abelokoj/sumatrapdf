/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "gui/Dpi.h"
#include "base/Pixmap.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"
#include "gui/win/TabsCtrl.h"

#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "Annotation.h"
#include "WindowTab.h"
#include "MainWindow.h"
#include "FileHistory.h"
#include "Tabs.h"
#include "Theme.h"
#include "SumatraConfig.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "DarkMode.h"
#include "SumatraDialogs.h"

static const int kMaxCustomColors = 13;
static const int kNumPresets = 3;
static const Color kBgPresetColors[] = {
    kColorUnset,
    kColBlack,
    kColWhite,
};

// custom swatches are laid out in 2 rows
static const int kCustomInRow1 = 5;

static const Color kColCheckerDark = MkRgb(204, 204, 204);

static const int kIdPreview = 1;
static const int kIdPreset0 = 10;
static const int kIdCustom0 = 20;

enum class CloseAction {
    Cancel,
    Select
};

enum class ColorModel {
    Rgb,
    Hex,
    Cmyk,
    Hsv,
    Hsl
};

static constexpr int kMaxColorChannels = 4;
static const SeqStrings kColorModels = "RGB\0HEX\0CMYK\0HSV\0HSL\0";

// HSV picker, numeric color edits, swatches and Cancel/OK. Same WindowBase layout as
// Settings. Used for Change Background Color and, when colorsArgs is set, as
// the generic color picker (ShowChangeColorsDialog).
struct ChangeColorWnd : WindowBase {
    ~ChangeColorWnd() override;

    MainWindow* win = nullptr;
    WindowTab* tab = nullptr;
    Str filePath;
    // non-null: generic color picker mode, owned
    ChangeColorsArgs* colorsArgs = nullptr;
    // the picked color carries an opacity byte the user can change
    bool withOpacity = false;
    bool isCbx = false;
    bool isImage = false;
    bool isEbook = false;

    Color currentColor = 0;
    bool isCheckered = false;
    u8 opacity = 0xff;
    Color customColors[kMaxCustomColors]{};
    // how many of customColors are defined; the swatch at this index is the
    // single empty slot for defining the next color
    int nCustom = 0;
    bool customColorsChanged = false;
    int selectedCustomIdx = -1;
    bool previewSelected = true;
    bool updatingEdit = false;
    bool colorInputValid = true;
    ColorModel colorModel = ColorModel::Hex;

    Pixmap* hsvPx = nullptr;
    VirtCustom* colorArea = nullptr;
    DropDown* dropColorModel = nullptr;
    VirtText* labelRgb = nullptr;
    Edit* editRgb = nullptr;
    ILayout* hexRow = nullptr;
    ILayout* channelRow = nullptr;
    ILayout* channelCells[kMaxColorChannels]{};
    VirtText* channelLabels[kMaxColorChannels]{};
    Edit* channelEdits[kMaxColorChannels]{};
    VirtCustom* swatchPreview = nullptr;
    VirtCustom* swatchPreset[kNumPresets]{};
    VirtCustom* swatchCustom[kMaxCustomColors]{};
    ILayout* swatchRow2 = nullptr;
    ILayout* opacityRow = nullptr;
    VirtText* opacityLabel = nullptr;
    VirtSlider* opacitySlider = nullptr;
    VirtText* opacityValue = nullptr;
    Checkbox* radioThisFile = nullptr;
    Checkbox* radioAllFiles = nullptr;
    VirtButton* btnRemove = nullptr;
    VirtButton* btnCancel = nullptr;
    VirtButton* btnOk = nullptr;

    bool Create(MainWindow* win);
    void SetTargetBackground(MainWindow* win);
    void SetTargetColors(ChangeColorsArgs* args);
    void ClassifyTab(WindowTab* tab);
    void LoadCurrentColor();
    void LoadColors();
    void SaveCustomColorsIfChanged();
    void UpdateEditFromColor();
    bool TryParseEdit();
    void SelectPreview();
    void SelectCustom(int idx);
    void InvalidateSwatches();
    void UpdateSwatchVis();
    void UpdateRemoveBtn();
    void UpdateOpacityVis();
    void UpdateOpacityValue();
    void SyncOpacityFromColor();
    void OnOpacityChanged();
    void SetCustomColor(int idx, Color);
    void RemoveCustom(int idx);
    void PickFromArea(Point ptLocal);
    void OnAreaMouse(VirtMouseEvent* ev);
    void OnSwatchClick(VirtMouseEvent* ev);
    void OnSwatchContext(VirtMouseEvent* ev);
    void OnEditChanged();
    void OnColorModelChanged();
    void UpdateColorModelVis();
    void FocusColorEdit();
    void Relayout();
    int PreferredWidth();
    void RelayoutRadios();

    void OnRemove(VirtMouseEvent* ev = nullptr);
    void OnCancel(VirtMouseEvent* ev = nullptr);
    void OnOk(VirtMouseEvent* ev = nullptr);
    void Finish(CloseAction);
    void NotifyColorsArgs(CloseAction);
    void ApplyBackground();
    WindowTab* TargetTab();
};

static ChangeColorWnd* gChangeColorWnd = nullptr;

ChangeColorWnd::~ChangeColorWnd() {
    // a window destroyed without going through Finish() still owes the caller a reply
    NotifyColorsArgs(CloseAction::Cancel);
    str::Free(filePath);
    FreePixmap(hsvPx);
}

static void ClearChangeColorWnd() {
    gChangeColorWnd = nullptr;
}

static void HsvToRgb(float h, float s, float v, u8& r, u8& g, u8& b) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;
    if (h < 60) {
        rf = c;
        gf = x;
        bf = 0;
    } else if (h < 120) {
        rf = x;
        gf = c;
        bf = 0;
    } else if (h < 180) {
        rf = 0;
        gf = c;
        bf = x;
    } else if (h < 240) {
        rf = 0;
        gf = x;
        bf = c;
    } else if (h < 300) {
        rf = x;
        gf = 0;
        bf = c;
    } else {
        rf = c;
        gf = 0;
        bf = x;
    }
    r = (u8)((rf + m) * 255.0f);
    g = (u8)((gf + m) * 255.0f);
    b = (u8)((bf + m) * 255.0f);
}

static Pixmap* MakeHsvPixmap(int w, int h) {
    Pixmap* px = AllocPixmap(w, h, PixmapFormat::BGRA8, false);
    if (!px) {
        return nullptr;
    }
    for (int y = 0; y < h; y++) {
        float val = 1.0f - ((float)y / (float)h);
        u8* row = px->data + ((size_t)y * (size_t)px->stride);
        for (int x = 0; x < w; x++) {
            float hue = (float)x / (float)w * 360.0f;
            u8 r, g, b;
            HsvToRgb(hue, 1.0f, val, r, g, b);
            row[(size_t)x * 4] = b;
            row[(x * 4) + 1] = g;
            row[(x * 4) + 2] = r;
            row[(x * 4) + 3] = 255;
        }
    }
    return px;
}

static Color WithAlpha(Color c, u8 a) {
    return (c & 0xffffff) | ((Color)a << 24);
}

// an alpha of 0 means "no alpha given" everywhere else, so it reads as opaque
static u8 OpacityOf(Color c) {
    u8 a = GetAlpha(c);
    return a == 0 ? 0xff : a;
}

static int ColorChannelCount(ColorModel model) {
    return model == ColorModel::Hex ? 0 : model == ColorModel::Cmyk ? 4 : 3;
}

static double ColorChannelMax(ColorModel model, int channel) {
    if (model == ColorModel::Rgb) return 255;
    if (model != ColorModel::Cmyk && channel == 0) return 360;
    return 100;
}

static bool ParseColorChannel(Str text, ColorModel model, int channel, double& value) {
    char* input = CStrTemp(text);
    char* end = nullptr;
    value = strtod(input, &end);
    if (end == input || !isfinite(value)) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end == '%' && model != ColorModel::Rgb && (model == ColorModel::Cmyk || channel > 0)) {
        end++;
        while (*end && isspace((unsigned char)*end)) end++;
    }
    if (*end || value < 0 || value > ColorChannelMax(model, channel)) return false;
    return model != ColorModel::Rgb || value == floor(value);
}

static void ColorToChannels(Color color, ColorModel model, double values[kMaxColorChannels]) {
    u8 red, green, blue;
    UnpackColor(color, red, green, blue);
    values[0] = red;
    values[1] = green;
    values[2] = blue;
    values[3] = 0;
    if (model == ColorModel::Rgb || model == ColorModel::Hex) return;

    double r = red / 255.0, g = green / 255.0, b = blue / 255.0;
    double hi = fmax(r, fmax(g, b));
    double lo = fmin(r, fmin(g, b));
    double delta = hi - lo;
    if (model == ColorModel::Cmyk) {
        values[0] = hi > 0 ? (hi - r) / hi * 100 : 0;
        values[1] = hi > 0 ? (hi - g) / hi * 100 : 0;
        values[2] = hi > 0 ? (hi - b) / hi * 100 : 0;
        values[3] = (1 - hi) * 100;
        return;
    }

    double hue = 0;
    if (delta > 0) {
        if (hi == r)
            hue = (g - b) / delta;
        else if (hi == g)
            hue = (b - r) / delta + 2;
        else
            hue = (r - g) / delta + 4;
        hue *= 60;
        if (hue < 0) hue += 360;
    }
    values[0] = hue;
    if (model == ColorModel::Hsv) {
        values[1] = hi > 0 ? delta / hi * 100 : 0;
        values[2] = hi * 100;
        return;
    }
    double light = (hi + lo) / 2;
    values[1] = delta > 0 ? delta / (1 - fabs(2 * light - 1)) * 100 : 0;
    values[2] = light * 100;
}

static u8 ColorByte(double value) {
    return (u8)limitValue((int)floor(value * 255 + 0.5), 0, 255);
}

static bool ChannelsToColor(ColorModel model, const double values[kMaxColorChannels], u8 alpha, Color& color) {
    if (model == ColorModel::Hex) return false;
    for (int i = 0; i < ColorChannelCount(model); i++) {
        if (!isfinite(values[i]) || values[i] < 0 || values[i] > ColorChannelMax(model, i)) return false;
        if (model == ColorModel::Rgb && values[i] != floor(values[i])) return false;
    }
    double r, g, b;
    if (model == ColorModel::Rgb) {
        r = values[0] / 255;
        g = values[1] / 255;
        b = values[2] / 255;
    } else if (model == ColorModel::Cmyk) {
        double white = 1 - values[3] / 100;
        r = (1 - values[0] / 100) * white;
        g = (1 - values[1] / 100) * white;
        b = (1 - values[2] / 100) * white;
    } else {
        double hue = fmod(values[0], 360.0) / 60;
        double saturation = values[1] / 100;
        double level = values[2] / 100;
        double chroma = model == ColorModel::Hsv ? level * saturation : (1 - fabs(2 * level - 1)) * saturation;
        double x = chroma * (1 - fabs(fmod(hue, 2.0) - 1));
        double m = model == ColorModel::Hsv ? level - chroma : level - chroma / 2;
        r = g = b = 0;
        if (hue < 1) {
            r = chroma;
            g = x;
        } else if (hue < 2) {
            r = x;
            g = chroma;
        } else if (hue < 3) {
            g = chroma;
            b = x;
        } else if (hue < 4) {
            g = x;
            b = chroma;
        } else if (hue < 5) {
            r = x;
            b = chroma;
        } else {
            r = chroma;
            b = x;
        }
        r += m;
        g += m;
        b += m;
    }
    color = MkRgba(ColorByte(r), ColorByte(g), ColorByte(b), alpha);
    return true;
}

static bool ParseHexColor(Str text, u8 alpha, Color& color) {
    TempStr value = str::DupTemp(text);
    str::TrimWSInPlace(value, str::TrimOpt::Both);
    if (str::EqI(value, StrL("unset")) || str::EqI(value, StrL("checkered"))) {
        color = kColorUnset;
        return true;
    }
    if (!str::TrimPrefix(value, StrL("0x"))) str::TrimPrefix(value, StrL("#"));
    if (len(value) != 6) return false;
    for (int i = 0; i < len(value); i++) {
        if (!isxdigit((unsigned char)value.s[i])) return false;
    }
    ParsedColor parsed;
    ParseColor(parsed, value);
    if (!parsed.parsedOk) return false;
    color = WithAlpha(parsed.col, alpha);
    return true;
}

static u8 BlendChannel(u8 fg, u8 bg, u8 a) {
    return (u8)((((int)fg * (int)a) + ((int)bg * (255 - (int)a))) / 255);
}

static Color BlendOver(Color col, Color bg, u8 a) {
    u8 r, g, b, br, bg2, bb;
    UnpackColor(col, r, g, b);
    UnpackColor(bg, br, bg2, bb);
    return MkRgb(BlendChannel(r, br, a), BlendChannel(g, bg2, a), BlendChannel(b, bb, a));
}

static void PaintCheckerboard(Gfx* gfx, Rect rc, Color light, Color dark) {
    constexpr int kCheckerSize = 8;
    for (int cy = 0; cy < rc.dy; cy += kCheckerSize) {
        for (int cx = 0; cx < rc.dx; cx += kCheckerSize) {
            int cellW = kCheckerSize;
            if (cellW > rc.dx - cx) {
                cellW = rc.dx - cx;
            }
            int cellH = kCheckerSize;
            if (cellH > rc.dy - cy) {
                cellH = rc.dy - cy;
            }
            bool isDark = ((cx / kCheckerSize) + (cy / kCheckerSize)) % 2 != 0;
            gfx->FillRect({rc.x + cx, rc.y + cy, cellW, cellH}, isDark ? dark : light);
        }
    }
}

static void SaveCustomColors(const Vec<Color>& colors) {
    if (!gSettings) {
        return;
    }
    str::ReplaceWithCopy(&gSettings->customColors, SerializeColorList(colors));
    ScheduleSaveSettings();
}

void ChangeColorWnd::LoadColors() {
    Vec<Color> colors;
    if (colorsArgs) {
        colors = colorsArgs->colors;
    } else if (gSettings) {
        ParseColorList(gSettings->customColors, colors, kMaxCustomColors);
    }
    nCustom = 0;
    customColorsChanged = false;
    for (Color col : colors) {
        if (nCustom >= kMaxCustomColors) {
            break;
        }
        customColors[nCustom++] = col;
    }
}

void ChangeColorWnd::SaveCustomColorsIfChanged() {
    if (!customColorsChanged) {
        return;
    }
    Vec<Color> colors;
    for (int i = 0; i < nCustom; i++) {
        VecAppend(colors, customColors[i]);
    }
    SaveCustomColors(colors);
}

void ChangeColorWnd::InvalidateSwatches() {
    if (swatchPreview) {
        swatchPreview->Invalidate();
    }
    for (VirtCustom* sw : swatchCustom) {
        if (sw) {
            sw->Invalidate();
        }
    }
}

// only the defined colors plus a single empty slot are shown
void ChangeColorWnd::UpdateSwatchVis() {
    int nVisible = nCustom < kMaxCustomColors ? nCustom + 1 : kMaxCustomColors;
    for (int i = 0; i < kMaxCustomColors; i++) {
        if (swatchCustom[i]) {
            swatchCustom[i]->SetIsVisible(i < nVisible);
        }
    }
    if (swatchRow2) {
        bool show = nVisible > kCustomInRow1;
        swatchRow2->SetVisibility(show ? Visibility::Visible : Visibility::Collapse);
    }
    UpdateRemoveBtn();
    Relayout();
}

void ChangeColorWnd::UpdateRemoveBtn() {
    if (!btnRemove) {
        return;
    }
    btnRemove->SetIsEnabled(selectedCustomIdx >= 0 && selectedCustomIdx < nCustom);
}

// Visibility on the row only takes it out of the layout, so the controls in it
// are hidden as well
void ChangeColorWnd::UpdateOpacityVis() {
    if (!opacityRow) {
        return;
    }
    Visibility vis = withOpacity ? Visibility::Visible : Visibility::Collapse;
    opacityRow->SetVisibility(vis);
    opacityLabel->SetVisibility(vis);
    opacitySlider->SetVisibility(vis);
    opacityValue->SetVisibility(vis);
}

void ChangeColorWnd::UpdateOpacityValue() {
    if (!opacityValue) {
        return;
    }
    opacityValue->SetText(fmt("%d", (int)opacity));
    if (hwnd && layout) {
        DoLayout(HwndClientRect(hwnd).Size());
        HwndInvalidate(hwnd);
    }
}

void ChangeColorWnd::SyncOpacityFromColor() {
    if (!withOpacity) {
        return;
    }
    opacity = isCheckered ? 0xff : OpacityOf(currentColor);
    if (opacitySlider) {
        opacitySlider->SetValue(opacity, false);
    }
    UpdateOpacityValue();
}

void ChangeColorWnd::OnOpacityChanged() {
    if (!opacitySlider) {
        return;
    }
    opacity = (u8)limitValue(opacitySlider->value, 0, 255);
    if (!isCheckered) {
        currentColor = WithAlpha(currentColor, opacity);
        UpdateEditFromColor();
    }
    UpdateOpacityValue();
}

// setting a color on the empty slot defines it, which opens a new empty slot
void ChangeColorWnd::SetCustomColor(int idx, Color col) {
    if (idx < 0 || idx >= kMaxCustomColors) {
        return;
    }
    if (idx < nCustom && customColors[idx] == col) return;
    customColors[idx] = col;
    customColorsChanged = true;
    if (idx < nCustom) {
        return;
    }
    nCustom = idx + 1;
    UpdateSwatchVis();
}

void ChangeColorWnd::RemoveCustom(int idx) {
    if (idx < 0 || idx >= nCustom) {
        return;
    }
    for (int i = idx; i < nCustom - 1; i++) {
        customColors[i] = customColors[i + 1];
    }
    nCustom--;
    customColorsChanged = true;
    SelectPreview();
    UpdateSwatchVis();
}

void ChangeColorWnd::SelectPreview() {
    selectedCustomIdx = -1;
    previewSelected = true;
    UpdateRemoveBtn();
    InvalidateSwatches();
}

void ChangeColorWnd::SelectCustom(int idx) {
    selectedCustomIdx = idx;
    previewSelected = false;
    UpdateRemoveBtn();
    InvalidateSwatches();
}

void ChangeColorWnd::UpdateEditFromColor() {
    updatingEdit = true;
    if (editRgb) {
        if (isCheckered) {
            editRgb->SetText(colorsArgs ? StrL("unset") : StrL("checkered"));
        } else {
            editRgb->SetText(SerializeColorTemp(currentColor & 0xffffff));
        }
    }
    double values[kMaxColorChannels];
    ColorToChannels(currentColor, colorModel, values);
    for (int i = 0; i < kMaxColorChannels; i++) {
        if (!channelEdits[i]) continue;
        channelEdits[i]->SetText(isCheckered                     ? Str{}
                                 : colorModel == ColorModel::Rgb ? fmt("%d", (int)values[i])
                                                                 : fmt("%.2f", values[i]));
    }
    updatingEdit = false;
    colorInputValid = true;
    if (btnOk) btnOk->SetIsEnabled(true);
    if (selectedCustomIdx >= 0 && !isCheckered) {
        SetCustomColor(selectedCustomIdx, currentColor);
    }
    InvalidateSwatches();
}

bool ChangeColorWnd::TryParseEdit() {
    Color parsed;
    bool unset = false;
    u8 alpha = withOpacity ? opacity : GetAlpha(currentColor);
    if (colorModel == ColorModel::Hex) {
        if (!editRgb) return false;
        TempStr text = editRgb->GetTextTemp();
        if (!ParseHexColor(text, alpha, parsed)) return false;
        str::TrimWSInPlace(text, str::TrimOpt::Both);
        unset = str::EqI(text, StrL("unset")) || str::EqI(text, StrL("checkered"));
    } else {
        double values[kMaxColorChannels]{};
        for (int i = 0; i < ColorChannelCount(colorModel); i++) {
            if (!channelEdits[i] || !ParseColorChannel(channelEdits[i]->GetTextTemp(), colorModel, i, values[i])) {
                return false;
            }
        }
        if (!ChannelsToColor(colorModel, values, alpha, parsed)) return false;
    }
    if (unset) {
        isCheckered = true;
    } else {
        isCheckered = false;
        currentColor = parsed;
    }
    return true;
}

void ChangeColorWnd::OnEditChanged() {
    if (updatingEdit) {
        return;
    }
    bool valid = TryParseEdit();
    colorInputValid = valid;
    if (btnOk) btnOk->SetIsEnabled(valid);
    if (!valid) {
        return;
    }
    if (selectedCustomIdx >= 0 && !isCheckered) {
        SetCustomColor(selectedCustomIdx, currentColor);
    }
    InvalidateSwatches();
}

void ChangeColorWnd::UpdateColorModelVis() {
    bool hex = colorModel == ColorModel::Hex;
    Visibility hexVis = hex ? Visibility::Visible : Visibility::Collapse;
    if (hexRow) hexRow->SetVisibility(hexVis);
    if (labelRgb) labelRgb->SetVisibility(hexVis);
    if (editRgb) editRgb->SetVisibility(hexVis);
    if (channelRow) channelRow->SetVisibility(hex ? Visibility::Collapse : Visibility::Visible);
    const char* rgb[] = {"R (0-255)", "G (0-255)", "B (0-255)", ""};
    const char* cmyk[] = {"C (0-100%)", "M (0-100%)", "Y (0-100%)", "K (0-100%)"};
    const char* hsv[] = {"H (0-360)", "S (0-100%)", "V (0-100%)", ""};
    const char* hsl[] = {"H (0-360)", "S (0-100%)", "L (0-100%)", ""};
    const char** labels = colorModel == ColorModel::Rgb    ? rgb
                          : colorModel == ColorModel::Cmyk ? cmyk
                          : colorModel == ColorModel::Hsv  ? hsv
                                                           : hsl;
    for (int i = 0; i < kMaxColorChannels; i++) {
        Visibility vis = i < ColorChannelCount(colorModel) ? Visibility::Visible : Visibility::Collapse;
        if (channelCells[i]) channelCells[i]->SetVisibility(vis);
        if (channelLabels[i]) {
            channelLabels[i]->SetText(Str(labels[i]));
            channelLabels[i]->SetVisibility(vis);
        }
        if (channelEdits[i]) {
            channelEdits[i]->SetVisibility(vis);
            EditSetCueText(channelEdits[i], Str(labels[i]));
        }
    }
}

void ChangeColorWnd::FocusColorEdit() {
    Edit* edit = colorModel == ColorModel::Hex ? editRgb : channelEdits[0];
    EditSetFocus(edit);
    EditSelectAll(edit);
}

void ChangeColorWnd::OnColorModelChanged() {
    int index = CbGetCurrentSelection(dropColorModel);
    if (index < 0 || index > (int)ColorModel::Hsl) return;
    colorModel = (ColorModel)index;
    UpdateColorModelVis();
    UpdateEditFromColor();
    Relayout();
}

void ChangeColorWnd::PickFromArea(Point ptLocal) {
    if (!colorArea) {
        return;
    }
    Size sz = colorArea->bounds.Size();
    int x = ptLocal.x - 1;
    int y = ptLocal.y - 1;
    int dx = sz.dx - 2;
    int dy = sz.dy - 2;
    if (dx <= 0 || dy <= 0) {
        return;
    }
    x = ClampI(x, 0, dx - 1);
    y = ClampI(y, 0, dy - 1);
    float hue = (float)x / (float)dx * 360.0f;
    float val = 1.0f - ((float)y / (float)dy);
    u8 cr, cg, cb;
    HsvToRgb(hue, 1.0f, val, cr, cg, cb);
    isCheckered = false;
    currentColor = withOpacity ? MkRgba(cr, cg, cb, opacity) : MkRgb(cr, cg, cb);
    UpdateEditFromColor();
}

void ChangeColorWnd::OnAreaMouse(VirtMouseEvent* ev) {
    Point local = ev->pt;
    if (colorArea) {
        Rect b = colorArea->BoundsInWindow();
        local = {ev->ptWindow.x - b.x, ev->ptWindow.y - b.y};
    }
    PickFromArea(local);
    ev->didHandle = true;
}

static void OnAreaMouseMove(ChangeColorWnd* wnd, VirtMouseEvent* ev) {
    if (wnd->colorArea && wnd->colorArea->HasFlag(vwfPressed)) {
        wnd->OnAreaMouse(ev);
    }
}

void ChangeColorWnd::OnSwatchClick(VirtMouseEvent* ev) {
    VirtCtrl* hit = ev->hit;
    if (!hit) {
        return;
    }
    int id = hit->id;
    if (id == kIdPreview) {
        SelectPreview();
        ev->didHandle = true;
        return;
    }
    if (id >= kIdPreset0 && id < kIdPreset0 + kNumPresets) {
        Color col = kBgPresetColors[id - kIdPreset0];
        if (col == kColorUnset) {
            isCheckered = true;
        } else {
            isCheckered = false;
            currentColor = col;
        }
        SelectPreview();
        SyncOpacityFromColor();
        UpdateEditFromColor();
        ev->didHandle = true;
        return;
    }
    if (id >= kIdCustom0 && id < kIdCustom0 + kMaxCustomColors) {
        int idx = id - kIdCustom0;
        if (idx > nCustom) {
            return;
        }
        if (selectedCustomIdx == idx) {
            SelectPreview();
        } else {
            SelectCustom(idx);
            if (idx < nCustom) {
                isCheckered = false;
                currentColor = customColors[idx];
                SyncOpacityFromColor();
                UpdateEditFromColor();
            }
        }
        ev->didHandle = true;
    }
}

void ChangeColorWnd::OnSwatchContext(VirtMouseEvent* ev) {
    VirtCtrl* hit = ev->hit;
    if (!hit) {
        return;
    }
    int id = hit->id;
    if (id < kIdCustom0 || id >= kIdCustom0 + kMaxCustomColors) {
        return;
    }
    int idx = id - kIdCustom0;
    if (idx >= nCustom) {
        return;
    }
    RemoveCustom(idx);
    ev->didHandle = true;
}

static void PaintColorArea(ChangeColorWnd* wnd, VirtPaintCtx* ctx) {
    Rect r = ctx->content;
    if (r.dx < 4 || r.dy < 4) {
        return;
    }
    Rect inner = r;
    inner.Inflate(-1, -1);
    if (!wnd->hsvPx || wnd->hsvPx->width != inner.dx || wnd->hsvPx->height != inner.dy) {
        FreePixmap(wnd->hsvPx);
        wnd->hsvPx = MakeHsvPixmap(inner.dx, inner.dy);
    }
    if (wnd->hsvPx) {
        ctx->gfx->DrawPixmap(wnd->hsvPx, inner);
    }
    ctx->gfx->DrawRect(r, ThemeWindowTextColor());
}

static void PaintSwatch(VirtCustom* sw, VirtPaintCtx* ctx) {
    auto* wnd = (ChangeColorWnd*)sw->userData;
    if (!wnd) {
        return;
    }
    Rect rc = ctx->content;
    bool selected = false;
    bool checkered = false;
    Color col = 0;
    bool empty = false;
    int id = sw->id;
    if (id == kIdPreview) {
        selected = wnd->previewSelected;
        checkered = wnd->isCheckered;
        col = wnd->currentColor;
    } else if (id >= kIdPreset0 && id < kIdPreset0 + kNumPresets) {
        col = kBgPresetColors[id - kIdPreset0];
        checkered = (col == kColorUnset);
    } else if (id >= kIdCustom0 && id < kIdCustom0 + kMaxCustomColors) {
        int idx = id - kIdCustom0;
        selected = (idx == wnd->selectedCustomIdx);
        if (idx < wnd->nCustom) {
            col = wnd->customColors[idx];
        } else {
            empty = true;
        }
    }

    if (selected) {
        ctx->gfx->FillRect(rc, GetSysColor(COLOR_HIGHLIGHT));
        rc.Inflate(-3, -3);
    }
    if (empty) {
        ctx->gfx->FillRect(rc, ThemeWindowControlBackgroundColor());
        Color edge = ThemeEdgeColor();
        ctx->gfx->DrawRect(rc, edge);
        ctx->gfx->DrawLineAA({rc.x, rc.y}, {rc.x + rc.dx - 1, rc.y + rc.dy - 1}, edge);
        ctx->gfx->DrawLineAA({rc.x + rc.dx - 1, rc.y}, {rc.x, rc.y + rc.dy - 1}, edge);
        return;
    }
    if (checkered) {
        PaintCheckerboard(ctx->gfx, rc, kColWhite, kColCheckerDark);
    } else {
        u8 a = GetAlpha(col);
        bool blend = wnd->withOpacity && a != 0 && a != 0xff;
        if (blend) {
            // show the color over a checkerboard, so opacity is visible
            PaintCheckerboard(ctx->gfx, rc, BlendOver(col, kColWhite, a), BlendOver(col, kColCheckerDark, a));
        } else {
            ctx->gfx->FillRect(rc, col & 0xffffff);
        }
    }
    if (sw->HasFlag(vwfFocused) && id >= kIdPreset0 && id < kIdPreset0 + kNumPresets) {
        ctx->gfx->DrawFocusRect(ctx->content);
    }
}

// hands the picked color and the edited set of colors back to the caller
void ChangeColorWnd::NotifyColorsArgs(CloseAction action) {
    if (!colorsArgs) {
        return;
    }
    ChangeColorsArgs* args = colorsArgs;
    colorsArgs = nullptr;
    VecClear(args->colors);
    for (int i = 0; i < nCustom; i++) {
        VecAppend(args->colors, customColors[i]);
    }
    args->color = isCheckered ? kColorUnset : currentColor;
    args->didSelect = (action == CloseAction::Select);
    args->colorsChanged = customColorsChanged;
    args->onClose.Call(args);
    delete args;
}

void ChangeColorWnd::Finish(CloseAction action) {
    if (colorsArgs) {
        NotifyColorsArgs(action);
    } else {
        SaveCustomColorsIfChanged();
        if (action == CloseAction::Select) {
            ApplyBackground();
        }
    }
    ScheduleDelete();
}

void ChangeColorWnd::OnRemove(VirtMouseEvent*) {
    RemoveCustom(selectedCustomIdx);
}

void ChangeColorWnd::OnCancel(VirtMouseEvent*) {
    Finish(CloseAction::Cancel);
}

WindowTab* ChangeColorWnd::TargetTab() {
    if (!IsMainWindowValidAndNotClosing(win)) {
        return nullptr;
    }
    WindowTab* t = FindTabByFilePath(filePath);
    if (!t || t->win != win) {
        return nullptr;
    }
    return t;
}

void ChangeColorWnd::ApplyBackground() {
    WindowTab* t = TargetTab();
    if (!t || !t->ctrl) {
        return;
    }
    Str colorStr;
    if (isCheckered) {
        colorStr = StrL("checkered");
    } else {
        colorStr = SerializeColorTemp(currentColor);
    }
    Color newColor = isCheckered ? kColorUnset : currentColor;
    bool applyToAll = radioAllFiles && radioAllFiles->IsChecked();

    if (applyToAll) {
        if (isCbx) {
            SetColorText(gSettings->comicBookUI.windowBgCol, colorStr);
        } else if (isImage) {
            SetColorText(gSettings->imageUI.windowBgCol, colorStr);
        } else if (isEbook) {
            SetColorText(gSettings->eBookUI.windowBgCol, colorStr);
        } else {
            SetColorText(gSettings->fixedPageUI.windowBgCol, colorStr);
        }
        FileState* fs = FileHistoryFindByPath(t->filePath);
        if (fs) {
            SetColorText(fs->bgCol, StrL(""));
        }
        t->bgColor = kColorUnset;
        t->bgColorCheckered = false;
    } else {
        FileState* fs = FileHistoryFindByPath(t->filePath);
        if (fs) {
            SetColorText(fs->bgCol, colorStr);
        }
        t->bgColor = newColor;
        t->bgColorCheckered = isCheckered;
    }
    ScheduleSaveSettings();
    HwndInvalidate(win->hwndCanvas, true);
}

void ChangeColorWnd::OnOk(VirtMouseEvent*) {
    if (!colorInputValid) return;
    Finish(CloseAction::Select);
}

static void OnClose(WindowBase::CloseEvent* /*ev*/) {
    if (gChangeColorWnd) {
        gChangeColorWnd->OnCancel();
    }
}

static void OnDestroy(WindowBase::DestroyEvent* /*ev*/) {
    if (gChangeColorWnd) {
        gChangeColorWnd->ScheduleDelete();
    }
}

static void SwatchClicked(ChangeColorWnd* wnd, VirtMouseEvent* ev) {
    wnd->OnSwatchClick(ev);
}

static void SwatchContext(ChangeColorWnd* wnd, VirtMouseEvent* ev) {
    wnd->OnSwatchContext(ev);
}

void ChangeColorWnd::ClassifyTab(WindowTab* t) {
    isCbx = false;
    isImage = false;
    isEbook = false;
    if (!t) {
        return;
    }
    auto* engine = t->GetEngine();
    if (!engine) {
        return;
    }
    isImage = engine->IsImageCollection();
    isCbx = engine->kind == kindEngineComicBooks;
    isEbook = engine->kind == kindEngineMupdf && !str::EqI(engine->defaultExt, StrL(".pdf"));
}

void ChangeColorWnd::LoadCurrentColor() {
    if (colorsArgs) {
        currentColor = colorsArgs->color;
        isCheckered = (currentColor == kColorUnset);
        if (isCheckered) {
            currentColor = ThemeControlBackgroundColor();
        }
        return;
    }
    WindowTab* t = tab;
    if (!t) {
        currentColor = kColWhite;
        isCheckered = false;
        return;
    }
    if (t->bgColorCheckered) {
        currentColor = kColorUnset;
        isCheckered = true;
        return;
    }
    if (t->bgColor != kColorUnset) {
        currentColor = t->bgColor;
        isCheckered = false;
        return;
    }
    ParsedColor* bgOverride = nullptr;
    if (isCbx) {
        bgOverride = GetPrefsColor(gSettings->comicBookUI.windowBgCol);
    } else if (isImage) {
        bgOverride = GetPrefsColor(gSettings->imageUI.windowBgCol);
    } else if (isEbook) {
        bgOverride = GetPrefsColor(gSettings->eBookUI.windowBgCol);
    } else {
        bgOverride = GetPrefsColor(gSettings->fixedPageUI.windowBgCol);
    }
    if (bgOverride && bgOverride->parsedOk) {
        currentColor = bgOverride->col;
        isCheckered = (bgOverride->col == kColorUnset);
        return;
    }
    Color bg;
    ThemeDocumentColors(bg);
    currentColor = bg;
    isCheckered = false;
}

void ChangeColorWnd::Relayout() {
    if (!hwnd || !layout) {
        return;
    }
    int dx = PreferredWidth();
    LayoutAndSizeToContent(layout, dx, 0, hwnd);
    DoLayout(HwndClientRect(hwnd).Size());
}

int ChangeColorWnd::PreferredWidth() {
    int width = DpiScale(400);
    if (font) {
        int fieldWidth = font->averageCharWidth * 44 + DpiScale(32);
        if (fieldWidth > width) width = fieldWidth;
    }
    return width;
}

void ChangeColorWnd::RelayoutRadios() {
    bool show = !colorsArgs;
    Visibility vis = show ? Visibility::Visible : Visibility::Collapse;
    if (radioThisFile) {
        radioThisFile->SetVisibility(vis);
    }
    if (radioAllFiles) {
        radioAllFiles->SetVisibility(vis);
    }
    if (show && radioAllFiles) {
        Str label = Tr("For all &PDF files");
        if (isCbx) {
            label = Tr("For all &comic books");
        } else if (isImage) {
            label = Tr("For all &images");
        } else if (isEbook) {
            label = Tr("For all &ebooks");
        }
        radioAllFiles->SetText(label);
        radioThisFile->SetIsChecked(true);
        radioAllFiles->SetIsChecked(false);
    }
}

void ChangeColorWnd::SetTargetBackground(MainWindow* mainWin) {
    NotifyColorsArgs(CloseAction::Cancel);
    withOpacity = false;
    win = mainWin;
    tab = (IsMainWindowValidAndNotClosing(win) && win->CurrentTab() && win->CurrentTab()->ctrl) ? win->CurrentTab()
                                                                                                : nullptr;
    str::ReplaceWithCopy(&filePath, tab ? tab->filePath : Str{});
    ClassifyTab(tab);
    LoadCurrentColor();
    selectedCustomIdx = -1;
    previewSelected = true;
    if (hwnd) {
        HwndSetText(hwnd, Tr("Change Background Color"));
        btnOk->SetText(Tr("OK"));
        RelayoutRadios();
        LoadColors();
        UpdateSwatchVis();
        UpdateOpacityVis();
        SyncOpacityFromColor();
        UpdateEditFromColor();
        Relayout();
        UpdateTheme();
    }
}

void ChangeColorWnd::SetTargetColors(ChangeColorsArgs* args) {
    NotifyColorsArgs(CloseAction::Cancel);
    colorsArgs = args;
    withOpacity = args->withOpacity;
    win = args->win;
    tab = nullptr;
    str::ReplaceWithCopy(&filePath, Str{});
    ClassifyTab(nullptr);
    LoadCurrentColor();
    selectedCustomIdx = -1;
    previewSelected = true;
    if (hwnd) {
        HwndSetText(hwnd, args->title);
        btnOk->SetText(Tr("Select"));
        RelayoutRadios();
        LoadColors();
        UpdateSwatchVis();
        UpdateOpacityVis();
        SyncOpacityFromColor();
        UpdateEditFromColor();
        Relayout();
        UpdateTheme();
    }
}

static VirtCustom* MakeSwatch(ChangeColorWnd* wnd, int id, Size sz, bool contextMenu) {
    auto* c = new VirtCustom();
    c->idealSize = sz;
    c->id = id;
    c->userData = (uintptr_t)wnd;
    c->SetFlag(vwfFocusable, true);
    c->cursor = CursorId::Hand;
    c->onPaint = MkFunc1(PaintSwatch, c);
    c->onClick = MkFunc1(SwatchClicked, wnd);
    if (contextMenu) {
        c->onContextMenu = MkFunc1(SwatchContext, wnd);
    }
    return c;
}

// HBox::gap, not spacers, so that hiding a swatch doesn't leave a hole
static HBox* SwatchRow(VirtCustom** items, int n, int gap) {
    auto* row = new HBox();
    row->alignMain = MainAxisAlign::MainStart;
    row->alignCross = CrossAxisAlign::CrossCenter;
    row->gap = gap;
    for (int i = 0; i < n; i++) {
        row->AddChild(items[i]);
    }
    return row;
}

bool ChangeColorWnd::Create(MainWindow* mainWin) {
    win = mainWin;

    {
        CreateCustomArgs args;
        // owned by the main window, so it can't end up behind it
        args.owner = mainWin ? mainWin->hwndFrame : nullptr;
        args.title = colorsArgs ? colorsArgs->title : Tr("Change Background Color");
        args.visible = false;
        args.style = WS_POPUPWINDOW | WS_CAPTION;
        args.font = GetFont();
        args.icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
        CreateCustom(args);
    }
    if (!hwnd) {
        return false;
    }
    bool isRtl = IsUIRtl();
    LoadColors();

    auto* vbox = new VBox();
    vbox->alignMain = MainAxisAlign::MainStart;
    vbox->alignCross = CrossAxisAlign::Stretch;

    {
        auto* c = new VirtCustom();
        c->idealSize = {DpiScale(380), DpiScale(150)};
        c->SetFlag(vwfCapturesMouse, true);
        c->cursor = CursorId::Cross;
        c->onPaint = MkFunc1(PaintColorArea, this);
        c->onMouseDown = MkMethod1<ChangeColorWnd, VirtMouseEvent*, &ChangeColorWnd::OnAreaMouse>(this);
        c->onMouseMove = MkFunc1(OnAreaMouseMove, this);
        colorArea = c;
        vbox->AddChild(c);
    }

    {
        auto* row = new HBox();
        row->alignMain = MainAxisAlign::MainStart;
        row->alignCross = CrossAxisAlign::CrossCenter;
        row->gap = DpiScale(4);

        opacityLabel = NewVirtText({
            .s = Tr("Opacity:"),
            .font = font,
            .isRtl = isRtl,
        });
        row->AddChild(opacityLabel);

        auto* sl = new VirtSlider();
        sl->minVal = 0;
        sl->maxVal = 255;
        sl->value = opacity;
        sl->idealDx = DpiScale(200);
        sl->onValueChanged = MkMethod0<ChangeColorWnd, &ChangeColorWnd::OnOpacityChanged>(this);
        opacitySlider = sl;
        row->AddChild(sl);

        opacityValue = NewVirtText({
            .s = StrL("255"),
            .font = font,
            .isRtl = isRtl,
        });
        row->AddChild(opacityValue);
        opacityRow = new Padding(row, DpiScaledInsets(4, 0, 0, 0));
        vbox->AddChild(opacityRow);
    }

    {
        auto* row = new HBox();
        row->alignMain = MainAxisAlign::MainStart;
        row->alignCross = CrossAxisAlign::CrossCenter;
        row->gap = DpiScale(8);
        row->AddChild(NewVirtText({
            .s = Tr("Color model:"),
            .font = font,
            .isRtl = isRtl,
        }));
        DropDown::CreateArgs args;
        args.parent = hwnd;
        args.font = GetFont();
        args.isRtl = isRtl;
        dropColorModel = new DropDown();
        dropColorModel->Create(args);
        dropColorModel->SetItemsSeqStrings(kColorModels);
        CbSetCurrentSelection(dropColorModel, (int)colorModel);
        dropColorModel->onSelectionChanged = MkMethod0<ChangeColorWnd, &ChangeColorWnd::OnColorModelChanged>(this);
        dropColorModel->onCloseUp = MkMethod0<ChangeColorWnd, &ChangeColorWnd::FocusColorEdit>(this);
        row->AddChild(dropColorModel);

        Size previewSz{DpiScale(42), DpiScale(20)};
        swatchPreview = MakeSwatch(this, kIdPreview, previewSz, false);
        row->AddChild(swatchPreview);
        vbox->AddChild(new Padding(row, DpiScaledInsets(6, 0, 0, 0)));
    }

    {
        auto* row = new HBox();
        row->alignMain = MainAxisAlign::MainStart;
        row->alignCross = CrossAxisAlign::CrossCenter;
        auto* lab = NewVirtText({
            .s = StrL("HEX (#RRGGBB):"),
            .font = font,
            .isRtl = isRtl,
            .padding = DpiScaledInsets(0, 8, 0, 0),
        });
        labelRgb = lab;
        row->AddChild(lab);

        Edit::CreateArgs args;
        args.parent = hwnd;
        args.font = GetFont();
        args.withBorder = true;
        args.isRtl = isRtl;
        args.idealWidthChars = 14;
        auto* e = new Edit();
        e->SetInsetsPt(8, 0, 0, 0);
        e->Create(args);
        e->onTextChanged = MkMethod0<ChangeColorWnd, &ChangeColorWnd::OnEditChanged>(this);
        editRgb = e;
        row->AddChild(e);

        hexRow = row;
        vbox->AddChild(row);
    }

    {
        auto* row = new HBox();
        row->alignMain = MainAxisAlign::MainStart;
        row->alignCross = CrossAxisAlign::CrossStart;
        row->gap = DpiScale(8);
        for (int i = 0; i < kMaxColorChannels; i++) {
            auto* cell = new VBox();
            cell->alignCross = CrossAxisAlign::Stretch;
            auto* label = NewVirtText({.font = font, .isRtl = isRtl});
            channelLabels[i] = label;
            cell->AddChild(label);
            Edit::CreateArgs args;
            args.parent = hwnd;
            args.font = GetFont();
            args.withBorder = true;
            args.isRtl = isRtl;
            args.idealWidthChars = 7;
            auto* edit = new Edit();
            edit->Create(args);
            edit->onTextChanged = MkMethod0<ChangeColorWnd, &ChangeColorWnd::OnEditChanged>(this);
            channelEdits[i] = edit;
            cell->AddChild(edit);
            channelCells[i] = cell;
            row->AddChild(cell);
        }
        channelRow = new Padding(row, DpiScaledInsets(4, 0, 0, 0));
        vbox->AddChild(channelRow);
    }

    Size swSz{DpiScale(36), DpiScale(22)};
    int gap = DpiScale(4);
    const int kInRow1 = kNumPresets + kCustomInRow1;
    VirtCustom* row1[kInRow1]{};
    for (int i = 0; i < kNumPresets; i++) {
        swatchPreset[i] = MakeSwatch(this, kIdPreset0 + i, swSz, false);
        row1[i] = swatchPreset[i];
    }
    for (int i = 0; i < kCustomInRow1; i++) {
        swatchCustom[i] = MakeSwatch(this, kIdCustom0 + i, swSz, true);
        row1[kNumPresets + i] = swatchCustom[i];
    }
    auto* swatches1 = SwatchRow(row1, kInRow1, gap);
    vbox->AddChild(new Padding(swatches1, DpiScaledInsets(8, 0, 0, 0)));

    const int kInRow2 = kMaxCustomColors - kCustomInRow1;
    VirtCustom* row2[kInRow2]{};
    for (int i = 0; i < kInRow2; i++) {
        swatchCustom[kCustomInRow1 + i] = MakeSwatch(this, kIdCustom0 + kCustomInRow1 + i, swSz, true);
        row2[i] = swatchCustom[kCustomInRow1 + i];
    }
    auto* swatches2 = SwatchRow(row2, kInRow2, gap);
    swatchRow2 = new Padding(swatches2, DpiScaledInsets(4, 0, 0, 0));
    vbox->AddChild(swatchRow2);

    {
        auto* row = new HBox();
        row->alignMain = MainAxisAlign::MainStart;
        row->alignCross = CrossAxisAlign::CrossCenter;

        Checkbox::CreateArgs args;
        args.parent = hwnd;
        args.text = Tr("&This file");
        args.isRtl = isRtl;
        args.isRadio = true;
        args.isGroupStart = true;
        args.initialState = Checkbox::State::Checked;
        auto* r1 = new Checkbox();
        r1->SetInsetsPt(10, 0, 0, 0);
        r1->Create(args);
        radioThisFile = r1;
        row->AddChild(r1);

        args.text = Tr("For all &PDF files");
        args.isGroupStart = false;
        args.initialState = Checkbox::State::Unchecked;
        auto* r2 = new Checkbox();
        r2->SetInsetsPt(10, 0, 0, 12);
        r2->Create(args);
        radioAllFiles = r2;
        row->AddChild(r2);
        vbox->AddChild(row);
    }

    {
        auto* hbox = new HBox();
        hbox->alignMain = MainAxisAlign::MainEnd;
        hbox->alignCross = CrossAxisAlign::CrossCenter;
        hbox->gap = font->averageCharWidth;
        auto pad = Insets{4, 0, 4, 0};

        btnRemove = NewThemedButton(hwnd, Tr("Remove"), font, false);
        btnRemove->onClick = MkMethod1<ChangeColorWnd, VirtMouseEvent*, &ChangeColorWnd::OnRemove>(this);
        hbox->AddChild(new Padding(btnRemove, pad));
        btnCancel = NewThemedButton(hwnd, Tr("Cancel"), font, false);
        btnCancel->onClick = MkMethod1<ChangeColorWnd, VirtMouseEvent*, &ChangeColorWnd::OnCancel>(this);
        hbox->AddChild(new Padding(btnCancel, pad));
        btnOk = NewThemedButton(hwnd, colorsArgs ? Tr("Select") : Tr("OK"), font, true);
        btnOk->onClick = MkMethod1<ChangeColorWnd, VirtMouseEvent*, &ChangeColorWnd::OnOk>(this);
        hbox->AddChild(new Padding(btnOk, pad));
        // same space above the buttons as below them
        vbox->AddChild(new Padding(hbox, DpiScaledInsets(4, 0, 0, 0)));
    }

    auto* padding = new Padding(vbox, DpiScaledInsets(4, 8));
    layout = padding;

    RelayoutRadios();
    UpdateSwatchVis();
    UpdateOpacityVis();
    SyncOpacityFromColor();
    UpdateColorModelVis();
    UpdateEditFromColor();

    int dx = PreferredWidth();
    LayoutAndSizeToContent(layout, dx, 0, hwnd);
    DoLayout(HwndClientRect(hwnd).Size());
    HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
    UpdateTheme();

    SetIsVisible(true);
    FocusColorEdit();
    return true;
}

void ShowChangeBackgroundColorDialog(MainWindow* win) {
    if (!IsMainWindowValidAndNotClosing(win) || !win->CurrentTab() || !win->CurrentTab()->ctrl) {
        return;
    }
    if (gChangeColorWnd) {
        gChangeColorWnd->SetTargetBackground(win);
        HwndSetFocus(gChangeColorWnd->hwnd);
        gChangeColorWnd->FocusColorEdit();
        return;
    }
    auto* wnd = new ChangeColorWnd();
    wnd->SetTargetBackground(win);
    wnd->closeOnEsc = true;
    wnd->onBeforeDelete = MkFunc0Void(ClearChangeColorWnd);
    wnd->onClose = MkFunc1Void<WindowBase::CloseEvent*>(OnClose);
    wnd->onDestroy = MkFunc1Void<WindowBase::DestroyEvent*>(OnDestroy);
    wnd->SetFont(GetAppFont());
    bool ok = wnd->Create(win);
    if (!ok) {
        delete wnd;
        return;
    }
    gChangeColorWnd = wnd;
}

void ShowChangeColorsDialog(ChangeColorsArgs* args) {
    if (!IsMainWindowValidAndNotClosing(args->win)) {
        delete args;
        return;
    }
    if (gChangeColorWnd) {
        gChangeColorWnd->SetTargetColors(args);
        HwndSetFocus(gChangeColorWnd->hwnd);
        gChangeColorWnd->FocusColorEdit();
        return;
    }
    auto* wnd = new ChangeColorWnd();
    wnd->SetTargetColors(args);
    wnd->closeOnEsc = true;
    wnd->onBeforeDelete = MkFunc0Void(ClearChangeColorWnd);
    wnd->onClose = MkFunc1Void<WindowBase::CloseEvent*>(OnClose);
    wnd->onDestroy = MkFunc1Void<WindowBase::DestroyEvent*>(OnDestroy);
    wnd->SetFont(GetAppFont());
    bool ok = wnd->Create(args->win);
    if (!ok) {
        delete wnd;
        return;
    }
    gChangeColorWnd = wnd;
}

#if defined(DEBUG)
bool ChangeColor_UnitTests() {
    Color color;
    double values[kMaxColorChannels] = {255, 128, 0, 0};
    if (!ChannelsToColor(ColorModel::Rgb, values, 37, color) || color != MkRgba(255, 128, 0, 37)) return false;
    values[0] = 256;
    if (ChannelsToColor(ColorModel::Rgb, values, 37, color)) return false;
    values[0] = 0.5;
    if (ChannelsToColor(ColorModel::Rgb, values, 37, color)) return false;

    double cmyk[kMaxColorChannels] = {0, 100, 100, 0};
    if (!ChannelsToColor(ColorModel::Cmyk, cmyk, 0, color) || color != kColRed) return false;
    cmyk[0] = cmyk[1] = cmyk[2] = 0;
    cmyk[3] = 100;
    if (!ChannelsToColor(ColorModel::Cmyk, cmyk, 93, color) || color != MkRgba(0, 0, 0, 93)) return false;

    double hsv[kMaxColorChannels] = {120, 100, 100, 0};
    if (!ChannelsToColor(ColorModel::Hsv, hsv, 255, color) || color != MkRgba(0, 255, 0, 255)) return false;
    hsv[0] = 360;
    if (!ChannelsToColor(ColorModel::Hsv, hsv, 0, color) || color != kColRed) return false;
    hsv[0] = 240;
    hsv[2] = 50;
    if (!ChannelsToColor(ColorModel::Hsv, hsv, 0, color) || color != MkRgb(0, 0, 128)) return false;

    double hsl[kMaxColorChannels] = {240, 100, 50, 0};
    if (!ChannelsToColor(ColorModel::Hsl, hsl, 41, color) || color != MkRgba(0, 0, 255, 41)) return false;
    hsl[1] = 0;
    if (!ChannelsToColor(ColorModel::Hsl, hsl, 41, color) || color != MkRgba(128, 128, 128, 41)) return false;
    hsl[2] = 100;
    if (!ChannelsToColor(ColorModel::Hsl, hsl, 41, color) || color != MkRgba(255, 255, 255, 41)) return false;

    if (!ParseHexColor(StrL(" #12aBef "), 0, color) || color != MkRgb(18, 171, 239)) return false;
    if (!ParseHexColor(StrL("0x123456"), 79, color) || color != MkRgba(18, 52, 86, 79)) return false;
    if (!ParseHexColor(StrL("#ffffff"), 0, color) || GetAlpha(color) != 0) return false;
    if (!ParseHexColor(StrL("unset"), 79, color) || color != kColorUnset) return false;
    if (ParseHexColor(StrL("#12345g"), 79, color) || ParseHexColor(StrL("#80123456"), 79, color)) return false;

    double component;
    if (!ParseColorChannel(StrL("255"), ColorModel::Rgb, 0, component) || component != 255) return false;
    if (!ParseColorChannel(StrL(" 42.5 % "), ColorModel::Cmyk, 1, component) || component != 42.5) return false;
    if (!ParseColorChannel(StrL("360"), ColorModel::Hsl, 0, component) || component != 360) return false;
    if (ParseColorChannel(StrL("256"), ColorModel::Rgb, 0, component) ||
        ParseColorChannel(StrL("0.5"), ColorModel::Rgb, 0, component) ||
        ParseColorChannel(StrL("-1"), ColorModel::Hsv, 0, component) ||
        ParseColorChannel(StrL("101%"), ColorModel::Hsl, 2, component) ||
        ParseColorChannel(StrL("50%"), ColorModel::Hsv, 0, component) ||
        ParseColorChannel(StrL("nan"), ColorModel::Cmyk, 0, component) ||
        ParseColorChannel(StrL("inf"), ColorModel::Rgb, 0, component) ||
        ParseColorChannel(StrL("12x"), ColorModel::Rgb, 0, component) ||
        ParseColorChannel(StrL(""), ColorModel::Rgb, 0, component))
        return false;

    // Include black/white/gray and colors across all hue sectors. Display rounding
    // must still reproduce the original RGB bytes after a numeric channel edit.
    const Color samples[] = {kColBlack,        kColWhite,          MkGray(128),        kColRed,           kColBlue,
                             MkRgb(0, 255, 0), MkRgb(17, 83, 229), MkRgb(254, 1, 128), MkRgb(19, 200, 91)};
    const ColorModel models[] = {ColorModel::Rgb, ColorModel::Cmyk, ColorModel::Hsv, ColorModel::Hsl};
    for (Color original : samples) {
        for (ColorModel model : models) {
            ColorToChannels(original, model, values);
            for (int i = 0; i < ColorChannelCount(model); i++) {
                if (!ParseColorChannel(model == ColorModel::Rgb ? fmt("%d", (int)values[i]) : fmt("%.2f", values[i]),
                                       model, i, values[i]))
                    return false;
            }
            if (!ChannelsToColor(model, values, 113, color) || color != WithAlpha(original, 113)) return false;
        }
    }

    ChangeColorWnd wnd;
    wnd.currentColor = MkRgba(19, 200, 91, 0);
    wnd.withOpacity = true;
    wnd.opacity = 0;
    wnd.nCustom = 1;
    wnd.customColors[0] = wnd.currentColor;
    wnd.selectedCustomIdx = 0;
    for (int i = 0; i <= (int)ColorModel::Hsl; i++) {
        wnd.colorModel = (ColorModel)i;
        wnd.UpdateEditFromColor();
        if (wnd.currentColor != MkRgba(19, 200, 91, 0) || wnd.opacity != 0 || wnd.customColorsChanged) return false;
    }
    return true;
}
#endif

// which tab the color picked in the generic dialog applies to
struct TabColorTarget {
    MainWindow* win = nullptr;
    Str filePath;
};

static void TabColorPicked(TabColorTarget* target, ChangeColorsArgs* args) {
    if (args->colorsChanged) {
        SaveCustomColors(args->colors);
    }
    WindowTab* tab = FindTabByFilePath(target->filePath);
    bool tabValid = IsMainWindowValidAndNotClosing(target->win) && tab && tab->win == target->win && tab->ctrl;
    if (args->didSelect && tabValid) {
        tab->tabColor = args->color;
        SetTabInfoColor(tab);
        FileState* fs = FileHistoryFindByPath(tab->filePath);
        if (fs) {
            bool isUnset = (args->color == kColorUnset);
            SetColorText(fs->tabCol, isUnset ? StrL("") : SerializeColorTemp(args->color));
        }
        ScheduleSaveSettings();
        if (target->win->tabsCtrl) {
            target->win->tabsCtrl->ScheduleRepaint();
        }
    }
    str::Free(target->filePath);
    delete target;
}

void ShowSetTabColorDialog(MainWindow* win, WindowTab* tab) {
    if (!IsMainWindowValidAndNotClosing(win) || !tab || !tab->ctrl) {
        return;
    }
    auto* target = new TabColorTarget();
    target->win = win;
    str::ReplaceWithCopy(&target->filePath, tab->filePath);

    auto* args = new ChangeColorsArgs();
    args->win = win;
    args->title = Tr("Change Tab Color");
    args->color = tab->tabColor;
    if (gSettings) {
        ParseColorList(gSettings->customColors, args->colors, kMaxCustomColors);
    }
    args->onClose = MkFunc1(TabColorPicked, target);
    ShowChangeColorsDialog(args);
}
