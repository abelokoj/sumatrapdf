/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Pixmap.h"
#include "base/GuessFileType.h"
#include "base/UITask.h"
#include "base/Win.h"

#include "gui/UIModels.h"

#include "Settings.h"
#include "AppSettings.h"
#include "Theme.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayMode.h"
#include "DisplayModel.h"
#include "PdfDarkMode.h"
#include "RefHover.h"

struct RefHoverRenderJob {
    RefHoverState* s = nullptr;
    RefHoverState::RenderRequest req;
    Pixmap* bmp = nullptr;
    DarkModeProfile profile;
    bool grayscale = false;
};

static DarkModeProfile RefHoverCaptureProfile(RefHoverState* s, EngineBase* engine) {
    Color text = kColorUnset, background = kColorUnset;
    DisplayModel* dm = s->ctrl ? s->ctrl->AsFixed() : nullptr;
    if (dm && dm->GetEngine() == engine) {
        text = dm->pageTextColor;
        background = dm->pageBackgroundColor;
    }
    DarkModeProfile profile;
    BuildViewDarkModeProfile(engine, &profile, text, background);
    return profile;
}

static bool RefHoverGrayscale() {
    return gSettings && gSettings->fixedPageUI.grayscale;
}

static u32 RefHoverProfileKey(const DarkModeProfile& profile, bool grayscale) {
    return profile.hash * 31 + (u32)grayscale;
}

bool RefHoverColorsCurrent(RefHoverState* s) {
    if (!s || !s->hitEngine || !s->bmp) return false;
    DarkModeProfile profile = RefHoverCaptureProfile(s, s->hitEngine);
    return s->colorKey == RefHoverProfileKey(profile, RefHoverGrayscale());
}

static Pixmap* RefHoverGrayPixmap(Pixmap* bmp) {
    Pixmap* converted = PixmapCopyAs32bppDIB(bmp);
    if (!converted || !converted->data) {
        FreePixmap(converted);
        return bmp;
    }
    FreePixmap(bmp);
    for (int y = 0; y < converted->height; y++) {
        byte* p = converted->data + (size_t)y * converted->stride;
        for (int x = 0; x < converted->width; x++, p += 4) {
            byte gray = (byte)((54 * p[2] + 183 * p[1] + 19 * p[0] + 128) >> 8);
            p[0] = p[1] = p[2] = gray;
        }
    }
    return converted;
}

static Pixmap* RefHoverRenderCrop(EngineBase* engine, int page, float zoom, RectF region,
                                  const DarkModeProfile& profile, bool grayscale = false) {
    RenderPageArgs args(page, zoom, 0, &region, RenderTarget::View);
    if (profile.mode != PageColorMode::Normal) args.darkProfile = &profile;
    MaskFpExceptions();
    Pixmap* bmp = engine->RenderPage(args);
    if (!bmp) return nullptr;
    if (grayscale) bmp = RefHoverGrayPixmap(bmp);
    bool recolor = args.darkProfile ? DarkModeProfileUsesLegacyPostProcess(&profile)
                                    : EngineUsesDocumentColorsFollowTheme(engine) && !EngineUsesReflowThemeCss(engine);
    if (!recolor || bmp->hasAlpha) return bmp;
    Vec<Rect> skipRects;
    bool preserve = profile.mode == PageColorMode::PreserveImages && profile.preservePdfImages;
    if (preserve) {
        engine->GetBitmapRecolorSkipRects(page, zoom, 0, region, {bmp->width, bmp->height}, skipRects);
        if (len(skipRects) > 1) {
            Rect largest;
            i64 area = 0;
            for (Rect rect : skipRects) {
                i64 current = (i64)rect.dx * rect.dy;
                if (current > area) {
                    largest = rect;
                    area = current;
                }
            }
            VecClear(skipRects);
            VecAppend(skipRects, largest);
        }
    }
    RecolorPixmap(bmp, profile.foreground, profile.pageBackground, profile.linkColor,
                  len(skipRects) ? &skipRects : nullptr);
    return bmp;
}

static void RefHoverStartRenderJob(RefHoverRenderJob* job);

// Stack independently cropped columns and pad with the page background.
// GDI safely converts palette DIBs. Neither input is consumed.
static Pixmap* StackPixmapsVertically(Pixmap* top, Pixmap* bottom, Color background) {
    int w = top->width > bottom->width ? top->width : bottom->width;
    int h = top->height + bottom->height;
    Pixmap* out = AllocPixmapDIB(w, h);
    if (!out) {
        return nullptr;
    }
    HDC screenDC = GetDC(nullptr);
    HDC outDC = CreateCompatibleDC(screenDC);
    HGDIOBJ oldOut = outDC ? SelectObject(outDC, out->hbmp) : nullptr;
    if (outDC && oldOut) {
        RECT full{0, 0, w, h};
        HBRUSH padding = CreateSolidBrush(background);
        HdcFillRect(outDC, ToRect(full), padding);
        DeleteObject(padding);

        if (top->hbmp) {
            HDC topDC = CreateCompatibleDC(screenDC);
            if (topDC) {
                HGDIOBJ oldTop = SelectObject(topDC, top->hbmp);
                BitBlt(outDC, 0, 0, top->width, top->height, topDC, 0, 0, SRCCOPY);
                SelectObject(topDC, oldTop);
                DeleteDC(topDC);
            }
        }
        if (bottom->hbmp) {
            HDC bottomDC = CreateCompatibleDC(screenDC);
            if (bottomDC) {
                HGDIOBJ oldBottom = SelectObject(bottomDC, bottom->hbmp);
                BitBlt(outDC, 0, top->height, bottom->width, bottom->height, bottomDC, 0, 0, SRCCOPY);
                SelectObject(bottomDC, oldBottom);
                DeleteDC(bottomDC);
            }
        }
        SelectObject(outDC, oldOut);
    }
    if (outDC) {
        DeleteDC(outDC);
    }
    ReleaseDC(nullptr, screenDC);
    return out;
}

static void RefHoverRenderDone(RefHoverRenderJob* job) {
    RefHoverState* s = job->s;
    if (!RefHoverIsLiveState(s)) {
        FreePixmap(job->bmp);
        job->req.engine->Release();
        delete job;
        return;
    }
    s->renderInFlight = false;
    u32 key = RefHoverProfileKey(job->profile, job->grayscale);
    if (job->req.gen == s->renderGen) {
        DarkModeProfile current = RefHoverCaptureProfile(s, job->req.engine);
        if (key != RefHoverProfileKey(current, RefHoverGrayscale())) {
            RefHoverRequestRender(s, job->req.engine, job->req);
            FreePixmap(job->bmp);
            job->req.engine->Release();
            delete job;
            return;
        }
    }
    if (job->bmp && job->req.gen == s->renderGen) {
        FreePixmap(s->bmp);
        s->bmp = job->bmp;
        s->colorKey = key;
        s->pageBackground = job->profile.pageBackground;
        s->displayed.continuationRegion = job->req.continuationRegion;
        if (job->req.showPopup) {
            s->displayed.destPageRaw = job->req.destPageRaw;
            s->displayed.destPage = job->req.pageNo;
            s->displayed.destX = job->req.destXRaw;
            s->displayed.destY = job->req.destYRaw;
            s->displayed.srcPage = job->req.srcPageRaw;
            s->displayed.srcRect = job->req.srcRectRaw;
            s->displayed.region = job->req.region;
            RefHoverShowPopup(s, job->req.screenPt);
        } else {
            HwndInvalidate(s->hwndPopup, true);
        }
    } else {
        FreePixmap(job->bmp);
    }
    job->req.engine->Release();
    delete job;
    if (s->queuedRender.valid) {
        if (s->queuedRender.gen == s->renderGen) {
            auto* next = new RefHoverRenderJob();
            next->s = s;
            next->req = s->queuedRender;
            s->queuedRender.valid = false;
            s->queuedRender.engine = nullptr;
            RefHoverStartRenderJob(next);
        } else {
            RefHoverDropQueuedRender(s);
        }
    }
}

static void RefHoverRenderThread(RefHoverRenderJob* job) {
    job->bmp = RefHoverRenderCrop(job->req.engine, job->req.pageNo, job->req.zoom, job->req.region, job->profile,
                                  job->grayscale);
    RectF cont = job->req.continuationRegion;
    if (job->bmp && cont.dx > 0.f && cont.dy > 0.f) {
        Pixmap* contBmp =
            RefHoverRenderCrop(job->req.engine, job->req.pageNo, job->req.zoom, cont, job->profile, job->grayscale);
        if (contBmp) {
            Pixmap* stacked = StackPixmapsVertically(job->bmp, contBmp, job->profile.pageBackground);
            FreePixmap(contBmp);
            if (stacked) {
                FreePixmap(job->bmp);
                job->bmp = stacked;
            }
        }
    }
    auto fn = MkFunc0<RefHoverRenderJob>(RefHoverRenderDone, job);
    uitask::Post(fn, "RefHoverRenderDone");
}

static void RefHoverStartRenderJob(RefHoverRenderJob* job) {
    // Capture settings on the UI thread; the render worker only uses this snapshot.
    job->profile = RefHoverCaptureProfile(job->s, job->req.engine);
    job->grayscale = RefHoverGrayscale();
    job->s->renderInFlight = true;
    auto fn = MkFunc0<RefHoverRenderJob>(RefHoverRenderThread, job);
    RunAsync(fn, StrL("RefHoverRender"));
}

void RefHoverRequestRender(RefHoverState* s, EngineBase* engine, RefHoverState::RenderRequest req) {
    if (!s || !engine) return;
    s->renderGen++;
    req.valid = true;
    req.gen = s->renderGen;
    engine->AddRef();
    req.engine = engine;
    if (s->renderInFlight) {
        RefHoverDropQueuedRender(s);
        s->queuedRender = req;
        return;
    }
    auto* job = new RefHoverRenderJob();
    job->s = s;
    job->req = req;
    RefHoverStartRenderJob(job);
}

void RefHoverRefreshColors(RefHoverState* s) {
    if (!s || !s->hitEngine || s->displayed.destPage <= 0 || !HwndIsVisible(s->hwndPopup) || RefHoverColorsCurrent(s))
        return;
    RefHoverState::RenderRequest req;
    req.pageNo = s->displayed.destPage;
    req.zoom = s->displayed.baseZoom * s->displayed.userZoom;
    req.region = s->displayed.region;
    req.continuationRegion = s->displayed.continuationRegion;
    RefHoverRequestRender(s, s->hitEngine, req);
}

#if IS_DEBUG
#include "base/tests/UtAssert.h"
void RefHoverRender_UnitTests() {
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
    auto* engine = CreateEngineMupdfFromData(ToStrTemp(pdf), StrL("preview-colors.pdf"), nullptr);
    utassert(engine != nullptr);
    if (!engine) return;
    Color text[] = {MkRgb(30, 40, 50), MkRgb(210, 220, 230)};
    Color background[] = {MkRgb(240, 230, 220), MkRgb(20, 30, 40)};
    PageColorMode modes[] = {PageColorMode::Normal, PageColorMode::LegacyInvert, PageColorMode::PreserveImages,
                             PageColorMode::SmartDark};
    for (PageColorMode mode : modes) {
        for (int i = 0; i < 2; i++) {
            DarkModeProfile profile;
            profile.mode = mode;
            profile.foreground = text[i];
            profile.pageBackground = background[i];
            profile.preservePdfImages = mode == PageColorMode::PreserveImages;
            profile.palette.textR = GetRValue(text[i]) / 255.f;
            profile.palette.textG = GetGValue(text[i]) / 255.f;
            profile.palette.textB = GetBValue(text[i]) / 255.f;
            profile.palette.bgR = GetRValue(background[i]) / 255.f;
            profile.palette.bgG = GetGValue(background[i]) / 255.f;
            profile.palette.bgB = GetBValue(background[i]) / 255.f;
            profile.palette.diffR = profile.palette.bgR - profile.palette.textR;
            profile.palette.diffG = profile.palette.bgG - profile.palette.textG;
            profile.palette.diffB = profile.palette.bgB - profile.palette.textB;
            profile.hash = PdfDarkModeComputeProfileHash(&profile);
            utassert(RefHoverProfileKey(profile, false) != RefHoverProfileKey(profile, true));
            RectF area{0, 0, 100, 100};
            Pixmap* bitmap = RefHoverRenderCrop(engine, 1, 1, area, profile);
            utassert(bitmap != nullptr);
            if (!bitmap) continue;
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
            RectF narrowArea{0, 0, 40, 100};
            Pixmap* narrow = RefHoverRenderCrop(engine, 1, 1, narrowArea, profile);
            utassert(narrow != nullptr);
            if (narrow) {
                Pixmap* stacked = StackPixmapsVertically(bitmap, narrow, background[i]);
                utassert(stacked != nullptr);
                Pixmap* composed = stacked ? PixmapCopyAs32bppDIB(stacked) : nullptr;
                utassert(composed && composed->data && composed->width == 100 && composed->height == 200);
                if (composed && composed->data) {
                    const byte* p = composed->data + 150 * composed->stride + 90 * 4;
                    utassert(MkRgb(p[2], p[1], p[0]) == background[i]);
                }
                FreePixmap(composed);
                FreePixmap(stacked);
                FreePixmap(narrow);
            }
            FreePixmap(bitmap);
        }
    }
    engine->Release();
}

#endif
