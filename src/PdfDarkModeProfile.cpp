/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#if IS_DEBUG
#include "base/Pixmap.h"
#include "base/tests/UtAssert.h"
#endif

#include "gui/UIModels.h"

#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "Theme.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "PdfDarkMode.h"

static float ColorChannel01(byte v) {
    return (float)v / 255.f;
}

static DarkModePalette BuildPaletteFromColors(Color textCol, Color bgCol, Color linkCol) {
    byte tr, tg, tb, br, bg, bb, lr, lg, lb;
    UnpackColor(textCol, tr, tg, tb);
    UnpackColor(bgCol, br, bg, bb);
    UnpackColor(linkCol, lr, lg, lb);

    DarkModePalette p;
    p.textR = ColorChannel01(tr);
    p.textG = ColorChannel01(tg);
    p.textB = ColorChannel01(tb);
    p.bgR = ColorChannel01(br);
    p.bgG = ColorChannel01(bg);
    p.bgB = ColorChannel01(bb);
    p.linkR = ColorChannel01(lr);
    p.linkG = ColorChannel01(lg);
    p.linkB = ColorChannel01(lb);
    p.diffR = p.bgR - p.textR;
    p.diffG = p.bgG - p.textG;
    p.diffB = p.bgB - p.textB;
    return p;
}

bool DarkModeProfileUsesObjectLevel(const DarkModeProfile* profile) {
    return profile && profile->mode == PageColorMode::SmartDark;
}

bool DarkModeProfileUsesLegacyPostProcess(const DarkModeProfile* profile) {
    if (!profile) {
        return false;
    }
    return profile->mode == PageColorMode::LegacyInvert || profile->mode == PageColorMode::PreserveImages;
}

u32 PdfDarkModeComputeProfileHash(const DarkModeProfile* profile) {
    if (!profile) {
        return 0;
    }
    auto mix = [](u32 h, u32 v) -> u32 { return (h * 31) + v; };
    u32 h = 0;
    h = mix(h, (u32)profile->mode);
    h = mix(h, (u32)profile->foreground);
    h = mix(h, (u32)profile->pageBackground);
    h = mix(h, (u32)profile->linkColor);
    h = mix(h, (u32)profile->preservePdfImages);
    h = mix(h, (u32)profile->preservePdfImagesMinSize);
    h = mix(h, *(u32*)&profile->options.scanImageCoverageThreshold);
    h = mix(h, *(u32*)&profile->options.minScanDominantCoverage);
    h = mix(h, *(u32*)&profile->options.maxScanAspectSkew);
    h = mix(h, (u32)profile->options.maxTextOpsForScanPage);
    h = mix(h, (u32)profile->options.maxVectorOpsForScanPage);
    h = mix(h, *(u32*)&profile->options.preserveImagePaperSoftening);
    h = mix(h, *(u32*)&profile->options.lightFillChromaThreshold);
    h = mix(h, *(u32*)&profile->options.lightFillLuminanceThreshold);
    return h;
}

void BuildViewDarkModeProfile(EngineBase* engine, DarkModeProfile* profile, Color text, Color background) {
    ReportIf(!profile);
    if (!profile) {
        return;
    }
    *profile = DarkModeProfile{};

    // unlike the fork's themes, master's themes never touch page colors:
    // dark pages come from DocumentColorsFollowTheme or custom dark
    // FixedPageUI colors, so key the dark modes off the effective page
    // background rather than the window chrome
    Color bgCol;
    Color textCol = ThemePageRenderColors(bgCol);
    bool custom = text != kColorUnset || background != kColorUnset;
    if (text != kColorUnset) textCol = text;
    if (background != kColorUnset) bgCol = background;
    bool pagesDark = !IsLightColor(bgCol);
    profile->foreground = textCol;
    profile->pageBackground = bgCol;
    profile->linkColor = pagesDark ? ThemeWindowLinkColor() : 0;
    profile->strength = 1.f;
    profile->preservePdfImages = GetPreservePdfImagesInDarkMode();
    profile->preservePdfImagesMinSize = GetPreservePdfImagesMinSize();
    profile->options = PdfDarkModeCurrentOptions();
    profile->palette = BuildPaletteFromColors(textCol, bgCol, profile->linkColor);

    if (!pagesDark && !custom) {
        // mode stays Normal: the render cache's default recolor pass still
        // applies custom (light) page colors from the cache colors
        profile->hash = PdfDarkModeComputeProfileHash(profile);
        return;
    }

    if (EngineUsesReflowThemeCss(engine)) {
        // EPUB/HTML/FB2/MOBI/TXT go through MuPDF's HTML engine: page colors
        // are applied as user CSS (images stay as in the file). Bitmap recolor
        // inverted some of those images (#6050).
        profile->mode = PageColorMode::Normal;
    } else if (EngineUsesDocumentColorsFollowTheme(engine)) {
        if (GetDocumentColorsFollowTheme() == DocumentColorsFollowTheme::Legacy) {
            profile->mode = PageColorMode::LegacyInvert;
        } else {
            // Smart (or Off with pagesDark already handled above): prefer object-level
            if (EngineSupportsSmartDarkMode(engine) && PdfDarkModeUsesObjectLevel()) {
                profile->mode = PageColorMode::SmartDark;
            } else if (profile->preservePdfImages) {
                profile->mode = PageColorMode::PreserveImages;
            } else {
                profile->mode = PageColorMode::LegacyInvert;
            }
        }
    }

    profile->hash = PdfDarkModeComputeProfileHash(profile);
}

bool EngineUsesDocumentColorsFollowTheme(EngineBase* engine) {
    if (!engine || engine->IsImageCollection()) {
        return false;
    }
    if (engine->kind == kindEngineMupdf || engine->kind == kindEngineDjVu) {
        return true;
    }
    // Native HTML-layout engines paint black-on-white pages. Recolor them with
    // FixedPageUI colors the same way as PDF (issue #6030: CHM went white when
    // recolor was narrowed to MuPDF+DjVu).
    return engine->kind == kindEngineChm || engine->kind == kindEngineEpub || engine->kind == kindEngineFb2 ||
           engine->kind == kindEngineMobi || engine->kind == kindEnginePdb || engine->kind == kindEngineHtml ||
           engine->kind == kindEngineTxt;
}

bool EngineUsesReflowThemeCss(EngineBase* engine) {
    return engine && engine->kind == kindEngineMupdf && engine->isReflowable;
}

#if IS_DEBUG
void ReadingColors_UnitTests() {
    Str paint = StrL("0 g 20 20 60 60 re f\n");
    Str objects[] = {StrL("<< /Type /Catalog /Pages 2 0 R >>"), StrL("<< /Type /Pages /Count 1 /Kids [3 0 R] >>"),
                     StrL("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Resources << >> /Contents 4 0 R >>"),
                     fmt("<< /Length %d >>\nstream\n%sendstream", len(paint), paint)};
    str::Builder pdf;
    pdf.Append(StrL("%PDF-1.4\n"));
    Vec<int> offsets;
    for (int i = 0; i < dimofi(objects); i++) {
        VecAppend(offsets, len(pdf));
        pdf.Append(fmt("%d 0 obj\n%s\nendobj\n", i + 1, objects[i]));
    }
    int xref = len(pdf);
    pdf.Append(StrL("xref\n0 5\n0000000000 65535 f \n"));
    for (int offset : offsets) pdf.Append(fmt("%010d 00000 n \n", offset));
    pdf.Append(fmt("trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n", xref));
    auto* engine = CreateEngineMupdfFromData(ToStrTemp(pdf), StrL("reading-colors.pdf"), nullptr);
    utassert(engine != nullptr);
    if (!engine) return;
    Color text[] = {MkRgb(30, 40, 50), MkRgb(210, 220, 230)};
    Color background[] = {MkRgb(240, 230, 220), MkRgb(20, 30, 40)};
    for (int i = 0; i < 2; i++) {
        DarkModeProfile profile;
        BuildViewDarkModeProfile(engine, &profile, text[i], background[i]);
        RectF area{0, 0, 100, 100};
        RenderPageArgs args(1, 1, 0, &area, RenderTarget::View);
        args.darkProfile = &profile;
        args.keepAlpha = false;
        Pixmap* bitmap = engine->RenderPage(args);
        utassert(bitmap != nullptr);
        if (!bitmap) continue;
        if (DarkModeProfileUsesLegacyPostProcess(&profile)) RecolorPixmap(bitmap, text[i], background[i]);
        Pixmap* pixels = PixmapCopyAs32bppDIB(bitmap);
        utassert(pixels && pixels->data && pixels->width == 100 && pixels->height == 100);
        if (pixels && pixels->data) {
            auto pixel = [&](int x, int y) {
                const byte* p = pixels->data + y * pixels->stride + x * 4;
                return MkRgb(p[2], p[1], p[0]);
            };
            utassert(pixel(50, 50) == text[i]);
            utassert(pixel(5, 5) == background[i]);
        }
        FreePixmap(pixels);
        FreePixmap(bitmap);
    }
    engine->Release();
}
#endif
