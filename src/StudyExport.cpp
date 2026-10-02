/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3 */
#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"
#include "base/Zip.h"
#include "base/GuessFileType.h"
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/PlatformFont.h"
#include <commdlg.h>
#include "Settings.h"
#include "Annotation.h"
#include "DocController.h"
#include "DocProperties.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "SumatraPDF.h"
#include "AppSettings.h"
#include "Theme.h"
#include "Translations.h"
#include "StudyExport.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif

enum class StudyFormat {
    Markdown,
    Text,
    Typst,
    Html,
    Word,
    Json,
    Csv
};
static SeqStrings kStudyFormats = "Markdown\0Plain text\0Typst\0HTML\0Word document\0JSON\0CSV\0";
static SeqStrings kStudyExtensions = "md\0txt\0typ\0html\0docx\0json\0csv\0";

struct StudyNote {
    int page = 0, ordinal = 0;
    AnnotationType type = AnnotationType::Unknown;
    PdfColor color = 0;
    time_t created = 0, modified = 0;
    Str pageLabel, quote, comment, author;
    bool selected = false, drawing = false;
};
struct StudyDocument {
    Str title, source;
    int pages = 0;
    Vec<StudyNote> notes;
    ~StudyDocument() {
        str::Free(title);
        str::Free(source);
        for (auto& n : notes) {
            str::Free(n.pageLabel);
            str::Free(n.quote);
            str::Free(n.comment);
            str::Free(n.author);
        }
    }
};
struct StudyFilter {
    int from = 1, to = INT_MAX;
    AnnotationType type = AnnotationType::Unknown;
    int colorIndex = 0;
    PdfColor color = 0;
    bool selection = false, creationOrder = false;
};

static TempStr StudyDate(time_t date) {
    if (!date) return {};
    tm utc{};
    if (gmtime_s(&utc, &date)) return {};
    char text[32]{};
    strftime(text, dimof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return str::DupTemp(Str(text));
}
static TempStr StudyColor(PdfColor color) {
    if (!color || color == kColorUnset) return StrL("none");
    u8 r, g, b, a;
    UnpackPdfColor(color, r, g, b, a);
    return fmt("#%02x%02x%02x%02x", (int)r, (int)g, (int)b, (int)a);
}
static TempStr StudyLink(Str source, int page) {
    str::Builder out;
    out.Append(str::StartsWith(source, StrL("\\\\")) ? StrL("file:") : StrL("file:///"));
    for (int i = 0; i < len(source); i++) {
        u8 c = (u8)source.s[i];
        if (c == '\\') c = '/';
        if (isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~')
            out.AppendChar(c);
        else
            out.Append(fmt("%%%02X", (int)c));
    }
    out.Append(fmt("#page=%d", page));
    return ToStrTemp(out);
}
static TempStr StudyQuote(EngineBase* engine, Annotation* annot) {
    Vec<RectF> areas = GetQuadPointsAsRect(annot);
    if (!len(areas) || !engine->AllowsCopyingText()) return {};
    PageText page;
    page.text = engine->GetTextForPage(annot->pageNo, &page.nCodepoints, &page.coords);
    str::Builder text;
    bool inSpan = false;
    int codepoint = 0;
    for (int byte = 0; byte < len(page.text) && codepoint < page.nCodepoints; codepoint++) {
        int start = byte;
        int c = Utf8CodepointNext(page.text, byte);
        Rect glyph = page.coords ? page.coords[codepoint] : Rect{};
        PointF center(glyph.x + glyph.dx / 2.f, glyph.y + glyph.dy / 2.f);
        bool inside = false;
        for (const RectF& area : areas) inside |= area.Contains(center);
        if (inside && glyph.dx > 0 && glyph.dy > 0) {
            if (!inSpan && len(text) && text.LastChar() != '\n') text.AppendChar('\n');
            text.Append(Str(page.text.s + start, byte - start));
            inSpan = true;
        } else if (inSpan && (c == '\n' || c == '\r')) {
            if (text.LastChar() != '\n') text.AppendChar('\n');
            inSpan = false;
        } else if (c != ' ' && c != '\t') {
            inSpan = false;
        }
    }
    return ToStrTemp(text);
}
static void CollectStudyNotes(EngineBase* engine, Str source, Annotation* selected, StudyDocument& doc) {
    doc.source = str::Dup(source);
    Str title = engine->GetPropertyTemp(DocProp::Title);
    doc.title = str::Dup(title ? title : path::GetBaseNameTemp(source));
    doc.pages = engine->pageCount;
    Vec<Annotation*> annots;
    EngineMupdfGetAnnotations(engine, annots);
    for (Annotation* annot : annots) {
        AnnotationType type = Type(annot);
        if (!AnnotationIsLive(annot) || type == AnnotationType::Link || type == AnnotationType::Popup ||
            type == AnnotationType::Widget || type == AnnotationType::Unknown)
            continue;
        StudyNote note;
        note.page = annot->pageNo;
        note.ordinal = len(doc.notes);
        note.type = type;
        note.color = type == AnnotationType::FreeText ? DefaultAppearanceTextColor(annot) : GetColor(annot);
        note.pageLabel = str::Dup(engine->GetPageLabeTemp(note.page));
        note.created = CreationDate(annot);
        note.modified = ModificationDate(annot);
        note.author = str::Dup(Author(annot));
        note.comment = str::Dup(Contents(annot));
        note.selected = selected == annot;
        note.quote = str::Dup(StudyQuote(engine, annot));
        note.drawing = type != AnnotationType::Text && type != AnnotationType::FreeText &&
                       type != AnnotationType::Highlight && type != AnnotationType::Underline &&
                       type != AnnotationType::Squiggly && type != AnnotationType::StrikeOut &&
                       type != AnnotationType::Caret;
        VecAppend(doc.notes, note);
    }
}
static void SelectStudyNotes(StudyDocument& doc, const StudyFilter& filter, Vec<StudyNote*>& out) {
    VecReset(out);
    for (StudyNote& note : doc.notes) {
        if (note.page < filter.from || note.page > filter.to || (filter.selection && !note.selected) ||
            (filter.type != AnnotationType::Unknown && note.type != filter.type) ||
            (filter.colorIndex && note.color != filter.color))
            continue;
        VecAppend(out, &note);
    }
    VecSort(out, [](StudyNote* const* a, StudyNote* const* b) {
        if ((*a)->page != (*b)->page) return (*a)->page < (*b)->page ? -1 : 1;
        return (*a)->ordinal - (*b)->ordinal;
    });
    if (filter.creationOrder)
        VecSort(out, [](StudyNote* const* a, StudyNote* const* b) {
            time_t x = (*a)->created, y = (*b)->created;
            if (x != y) return !x ? 1 : !y ? -1 : x < y ? -1 : 1;
            return (*a)->ordinal - (*b)->ordinal;
        });
}

static TempStr JsonText(Str value) {
    str::Builder out;
    out.AppendChar('"');
    for (int i = 0; i < len(value); i++) {
        u8 c = (u8)value.s[i];
        switch (c) {
            case '"':
                out.Append(StrL("\\\""));
                break;
            case '\\':
                out.Append(StrL("\\\\"));
                break;
            case '\n':
                out.Append(StrL("\\n"));
                break;
            case '\r':
                out.Append(StrL("\\r"));
                break;
            case '\t':
                out.Append(StrL("\\t"));
                break;
            default:
                if (c < 32)
                    out.Append(fmt("\\u%04x", (int)c));
                else
                    out.AppendChar(c);
        }
    }
    out.AppendChar('"');
    return ToStrTemp(out);
}
static TempStr XmlText(Str value) {
    str::Builder out;
    for (int byte = 0; byte < len(value);) {
        int start = byte;
        int c = Utf8CodepointNext(value, byte);
        switch (c) {
            case '&':
                out.Append(StrL("&amp;"));
                break;
            case '<':
                out.Append(StrL("&lt;"));
                break;
            case '>':
                out.Append(StrL("&gt;"));
                break;
            case '"':
                out.Append(StrL("&quot;"));
                break;
            case '\'':
                out.Append(StrL("&apos;"));
                break;
            default:
                if (c >= 32 || c == '\n' || c == '\r' || c == '\t') out.Append(Str(value.s + start, byte - start));
        }
    }
    return ToStrTemp(out);
}
static TempStr TypstText(Str value) {
    str::Builder out;
    out.AppendChar('"');
    for (int i = 0; i < len(value); i++) {
        u8 c = (u8)value.s[i];
        if (c == '"' || c == '\\') {
            out.AppendChar('\\');
            out.AppendChar(c);
        } else if (c == '\n')
            out.Append(StrL("\\n"));
        else if (c == '\r')
            out.Append(StrL("\\r"));
        else if (c == '\t')
            out.Append(StrL("\\t"));
        else if (c < 32)
            out.Append(fmt("\\u{%x}", (int)c));
        else
            out.AppendChar(c);
    }
    out.AppendChar('"');
    return ToStrTemp(out);
}
static TempStr MarkdownText(Str value) {
    str::Builder out;
    for (int i = 0; i < len(value); i++) {
        char c = value.s[i];
        if (c == '\r') continue;
        if (c == '\n') {
            out.Append(StrL("  \n"));
            continue;
        }
        if (strchr("\\`*_{}[]()<>#+-.!|~", c)) out.AppendChar('\\');
        out.AppendChar(c);
    }
    return ToStrTemp(out);
}
static TempStr CsvText(Str value) {
    str::Builder out;
    out.AppendChar('"');
    // Keep text safe when opened as a spreadsheet, without changing JSON or document exports.
    if (value && strchr("=+-@\t\r", value.s[0])) out.AppendChar('\'');
    for (int i = 0; i < len(value); i++) {
        if (value.s[i] == '"') out.AppendChar('"');
        out.AppendChar(value.s[i]);
    }
    out.AppendChar('"');
    return ToStrTemp(out);
}
static TempStr NoteHeading(const StudyNote& n) {
    return fmt("Page %s (%d) · %s", n.pageLabel, n.page, SeqStrByIndex(AnnotationTypeNames(), (int)n.type));
}
static TempStr NoteDetails(const StudyNote& n) {
    str::Builder out;
    out.Append(fmt("Color: %s", StudyColor(n.color)));
    if (n.author) out.Append(fmt(" | Author: %s", n.author));
    if (n.created) out.Append(fmt(" | Created: %s", StudyDate(n.created)));
    if (n.modified) out.Append(fmt(" | Modified: %s", StudyDate(n.modified)));
    return ToStrTemp(out);
}
static constexpr char kDrawingNotice[] =
    "Non-text annotation. Its drawing remains in the source PDF; no handwriting recognition was performed.";
static constexpr char kNoQuoteNotice[] =
    "No underlying text could be extracted. Scanned pages require OCR; no text was invented.";
static TempStr NoteBody(const StudyNote& n) {
    str::Builder out;
    if (n.quote) out.Append(fmt("Selected text:\n%s\n\n", n.quote));
    if (n.comment) out.Append(fmt("Note:\n%s\n\n", n.comment));
    if (n.drawing)
        out.Append(StrL(kDrawingNotice));
    else if (len(n.quote) == 0 && len(n.comment) == 0)
        out.Append(StrL(kNoQuoteNotice));
    return ToStrTemp(out);
}

static bool WriteStudyDocx(str::Builder& out, StudyDocument& doc, const Vec<StudyNote*>& notes) {
    str::Builder xml;
    xml.Append(
        StrL("<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:document "
             "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
             "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><w:body>"));
    auto paragraph = [&](Str text, bool bold) {
        xml.Append(StrL("<w:p><w:r>"));
        if (bold) xml.Append(StrL("<w:rPr><w:b/></w:rPr>"));
        xml.Append(StrL("<w:t xml:space=\"preserve\">"));
        int start = 0;
        for (int i = 0; i <= len(text); i++) {
            if (i < len(text) && text.s[i] != '\n' && text.s[i] != '\r') continue;
            xml.Append(XmlText(Str(text.s + start, i - start)));
            if (i < len(text)) {
                xml.Append(StrL("</w:t><w:br/><w:t xml:space=\"preserve\">"));
                if (text.s[i] == '\r' && i + 1 < len(text) && text.s[i + 1] == '\n') i++;
            }
            start = i + 1;
        }
        xml.Append(StrL("</w:t></w:r></w:p>"));
    };
    paragraph(doc.title, true);
    paragraph(fmt("Source: %s", doc.source), false);
    str::Builder rels;
    rels.Append(
        StrL("<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships "
             "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"));
    for (int i = 0; i < len(notes); i++) {
        const StudyNote& n = *notes[i];
        paragraph(NoteHeading(n), true);
        paragraph(NoteDetails(n), false);
        paragraph(NoteBody(n), false);
        xml.Append(
            fmt("<w:p><w:hyperlink r:id=\"p%d\"><w:r><w:rPr><w:color w:val=\"166534\"/><w:u "
                "w:val=\"single\"/></w:rPr><w:t>Open source page</w:t></w:r></w:hyperlink></w:p>",
                i));
        rels.Append(
            fmt("<Relationship Id=\"p%d\" "
                "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"%s\" "
                "TargetMode=\"External\"/>",
                i, XmlText(StudyLink(doc.source, n.page))));
    }
    if (!len(notes)) paragraph(StrL("No annotations match the export filters."), false);
    xml.Append(
        StrL("<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1134\" w:right=\"1134\" "
             "w:bottom=\"1134\" w:left=\"1134\"/></w:sectPr></w:body></w:document>"));
    rels.Append(StrL("</Relationships>"));
    ZipCreator zip(out);
    return zip.AddFileData(
               StrL("[Content_Types].xml"),
               StrL(
                   "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types "
                   "xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" "
                   "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default "
                   "Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\" "
                   "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/"
                   "></Types>")) &&
           zip.AddFileData(
               StrL("_rels/.rels"),
               StrL("<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships "
                    "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"main\" "
                    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
                    "Target=\"word/document.xml\"/></Relationships>")) &&
           zip.AddFileData(StrL("word/document.xml"), ToStr(xml)) &&
           zip.AddFileData(StrL("word/_rels/document.xml.rels"), ToStr(rels)) && zip.Finish();
}

static bool RenderStudy(str::Builder& out, StudyDocument& doc, const Vec<StudyNote*>& notes, StudyFormat format) {
    if (format == StudyFormat::Word) return WriteStudyDocx(out, doc, notes);
    if (format == StudyFormat::Json) {
        out.Append(fmt("{\"schema\":1,\"title\":%s,\"source\":%s,\"annotations\":[\n", JsonText(doc.title),
                       JsonText(doc.source)));
        for (int i = 0; i < len(notes); i++) {
            const StudyNote& n = *notes[i];
            out.Append(
                fmt("%s{\"page\":%d,\"page_label\":%s,\"type\":%s,\"color\":%s,\"created\":%s,\"modified\":%s,"
                    "\"author\":%s,\"quote\":%s,\"note\":%s,\"non_text\":%s,\"source_link\":%s}\n",
                    i ? StrL(",") : Str{}, n.page, JsonText(n.pageLabel),
                    JsonText(SeqStrByIndex(AnnotationTypeNames(), (int)n.type)), JsonText(StudyColor(n.color)),
                    n.created ? JsonText(StudyDate(n.created)) : StrL("null"),
                    n.modified ? JsonText(StudyDate(n.modified)) : StrL("null"), JsonText(n.author), JsonText(n.quote),
                    JsonText(n.comment), n.drawing ? StrL("true") : StrL("false"),
                    JsonText(StudyLink(doc.source, n.page))));
        }
        out.Append(StrL("]}\n"));
        return true;
    }
    if (format == StudyFormat::Csv) {
        out.Append(StrL(
            "document,source,page,page_label,type,color,created,modified,author,quote,note,non_text,source_link\r\n"));
        for (const StudyNote* n : notes) {
            out.Append(fmt("%s,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\r\n", CsvText(doc.title), CsvText(doc.source),
                           n->page, CsvText(n->pageLabel), CsvText(SeqStrByIndex(AnnotationTypeNames(), (int)n->type)),
                           CsvText(StudyColor(n->color)), CsvText(StudyDate(n->created)),
                           CsvText(StudyDate(n->modified)), CsvText(n->author), CsvText(n->quote), CsvText(n->comment),
                           n->drawing ? StrL("true") : StrL("false"), CsvText(StudyLink(doc.source, n->page))));
        }
        return true;
    }
    if (format == StudyFormat::Html)
        out.Append(
            fmt("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
                "content=\"width=device-width\"><title>%s</title><style>body{font:1.05rem/1.65 "
                "system-ui,sans-serif;max-width:54rem;margin:3rem auto;padding:0 "
                "1.25rem;color:#17251d}article{border-top:1px solid #bccbbf;padding:1.5rem "
                "0}p{white-space:pre-wrap;overflow-wrap:anywhere}small{color:#405548}a{color:#166534}</style></"
                "head><body><h1>%s</h1><p>Source: %s</p>\n",
                XmlText(doc.title), XmlText(doc.title), XmlText(doc.source)));
    else if (format == StudyFormat::Typst)
        out.Append(
            fmt("#set page(paper: \"a4\", margin: 2cm)\n#set text(size: 11pt)\n#let notes(s) = { for (i, line) in "
                "s.split(\"\\n\").enumerate() { if i > 0 { linebreak() }; text(line) } }\n#heading(level: 1, "
                "%s)\n#notes(%s)\n\n",
                TypstText(doc.title), TypstText(fmt("Source: %s", doc.source))));
    else if (format == StudyFormat::Markdown)
        out.Append(fmt("# %s\n\nSource: %s\n\n", MarkdownText(doc.title), MarkdownText(doc.source)));
    else
        out.Append(fmt("%s\nSource: %s\n\n", doc.title, doc.source));
    if (!len(notes))
        out.Append(format == StudyFormat::Typst ? StrL("#par(\"No annotations match the export filters.\")\n")
                                                : StrL("No annotations match the export filters.\n"));
    for (const StudyNote* n : notes) {
        Str heading = NoteHeading(*n), details = NoteDetails(*n), body = NoteBody(*n),
            link = StudyLink(doc.source, n->page);
        switch (format) {
            case StudyFormat::Html:
                out.Append(
                    fmt("<article><h2>%s</h2><small>%s</small><p>%s</p><a href=\"%s\">Open source page</a></article>\n",
                        XmlText(heading), XmlText(details), XmlText(body), XmlText(link)));
                break;
            case StudyFormat::Typst:
                out.Append(fmt("#heading(level: 2, %s)\n#notes(%s)\n\n#notes(%s)\n\n#link(%s)[Open source page]\n\n",
                               TypstText(heading), TypstText(details), TypstText(body), TypstText(link)));
                break;
            case StudyFormat::Markdown:
                out.Append(fmt("## %s\n\n%s\n\n%s\n\n[Open source page](%s)\n\n", MarkdownText(heading),
                               MarkdownText(details), MarkdownText(body), link));
                break;
            default:
                out.Append(fmt("%s\n%s\n\n%s\n\nSource page: %s\n\n", heading, details, body, link));
        }
    }
    if (format == StudyFormat::Html) out.Append(StrL("</body></html>\n"));
    return true;
}

enum StudyControl {
    scScope = 101,
    scFrom,
    scTo,
    scType,
    scColor,
    scOrder,
    scFormat,
    scPreview,
    scSave,
    scStatus
};
struct StudyWindow {
    HWND hwnd = nullptr, controls[scStatus + 1]{};
    StudyDocument* doc = nullptr;
    Vec<PdfColor> colors;
    Vec<AnnotationType> types;
    HBRUSH background = nullptr, field = nullptr;
    int scroll = 0;
    bool layoutBusy = false;
    ~StudyWindow() {
        DeleteObject(background);
        DeleteObject(field);
    }
};
static HWND StudyControlAt(StudyWindow* w, int id) {
    return w->controls[id];
}
static TempStr StudyControlText(StudyWindow* w, int id) {
    HWND h = StudyControlAt(w, id);
    int size = GetWindowTextLengthW(h);
    WCHAR* text = AllocArray<WCHAR>(size + 1);
    GetWindowTextW(h, text, size + 1);
    TempStr utf8 = ToUtf8Temp(WStr(text));
    free(text);
    return utf8;
}
static void StudyLayout(StudyWindow* w);
static void StudySetText(StudyWindow* w, int id, Str text) {
    SetWindowTextW(StudyControlAt(w, id), CWStrTemp(text));
    if (id == scStatus && w->controls[scSave]) StudyLayout(w);
}
static int StudyChoice(StudyWindow* w, int id) {
    return (int)SendMessageW(StudyControlAt(w, id), CB_GETCURSEL, 0, 0);
}
static bool StudyPage(Str text, int max, int& out) {
    if (len(text) == 0) return false;
    int value = 0;
    for (int i = 0; i < len(text); i++) {
        char c = text.s[i];
        if (c < '0' || c > '9' || value > (INT_MAX - (c - '0')) / 10) return false;
        value = value * 10 + c - '0';
    }
    if (value < 1 || value > max) return false;
    out = value;
    return true;
}
static bool StudySelection(StudyWindow* w, Vec<StudyNote*>& notes) {
    StudyFilter f;
    f.selection = StudyChoice(w, scScope) == 1;
    if (!StudyPage(StudyControlText(w, scFrom), w->doc->pages, f.from) ||
        !StudyPage(StudyControlText(w, scTo), w->doc->pages, f.to) || f.from > f.to)
        return false;
    int type = StudyChoice(w, scType), color = StudyChoice(w, scColor);
    if (type > 0 && type <= len(w->types)) f.type = w->types[type - 1];
    f.colorIndex = color;
    if (color > 0 && color <= len(w->colors)) f.color = w->colors[color - 1];
    f.creationOrder = StudyChoice(w, scOrder) == 1;
    SelectStudyNotes(*w->doc, f, notes);
    return true;
}
static void StudyPreview(StudyWindow* w) {
    if (!w->controls[scSave]) return;
    Vec<StudyNote*> notes;
    bool valid = StudySelection(w, notes);
    EnableWindow(StudyControlAt(w, scSave), valid);
    if (!valid) {
        StudySetText(w, scStatus, Tr("Enter a valid page range within this document."));
        StudySetText(w, scPreview, {});
        return;
    }
    str::Builder out;
    RenderStudy(out, *w->doc, notes, StudyFormat::Text);
    StudySetText(w, scPreview, ToStr(out));
    StudySetText(w, scStatus,
                 fmt("%d annotations. Preview shows the same content saved in your chosen format. Local source links "
                     "need the PDF at its current location.",
                     len(notes)));
}
static void StudySave(StudyWindow* w) {
    Vec<StudyNote*> notes;
    if (!StudySelection(w, notes)) return;
    int choice = StudyChoice(w, scFormat);
    if (choice < 0 || choice > (int)StudyFormat::Csv) return;
    Str extension = SeqStrByIndex(kStudyExtensions, choice);
    WCHAR dest[32768]{};
    WStr proposed =
        ToWStrTemp(fmt("%s-notes.%s", path::GetBaseNameTemp(path::GetPathNoExtTemp(w->doc->source)), extension));
    lstrcpynW(dest, proposed.s, dimof(dest));
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = w->hwnd;
    ofn.lpstrFile = dest;
    ofn.nMaxFile = dimof(dest);
    ofn.lpstrFilter = L"Export file\0*.*\0";
    ofn.lpstrDefExt = ToWStrTemp(extension).s;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;
    Str target = ToUtf8Temp(WStr(dest));
    if (path::IsSame(target, w->doc->source)) {
        StudySetText(w, scStatus, Tr("Choose a different filename to preserve your source PDF."));
        return;
    }
    str::Builder out;
    bool ok = RenderStudy(out, *w->doc, notes, (StudyFormat)choice);
    Str temp = str::Dup(MakeUniqueFilePathTemp(fmt("%s.export-tmp", target)));
    ok = ok && file::WriteFile(temp, ToStr(out));
    if (ok) ok = MoveFileExW(ToWStrTemp(temp).s, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    file::Delete(temp);
    str::Free(temp);
    StudySetText(w, scStatus,
                 ok ? fmt("Export saved: %s", target)
                    : Tr("The export could not be saved. Choose a writable folder and try again."));
}
static int StudyTextHeight(HWND hwnd, int width) {
    HDC dc = GetDC(hwnd);
    HGDIOBJ font = SelectObject(dc, GetAppFontForDpi(DpiGet())->GetHFont());
    RECT rc{0, 0, std::max(width, 1), 0};
    DrawTextW(dc, CWStrTemp(HwndGetTextTemp(hwnd)), -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, font);
    ReleaseDC(hwnd, dc);
    return std::max((int)rc.bottom, GetAppFontSizeForDpi(DpiGet()));
}
static void StudyLayout(StudyWindow* w) {
    if (w->layoutBusy || !w->controls[scSave]) return;
    w->layoutBusy = true;
    defer {
        w->layoutBusy = false;
    };
    RECT rc;
    GetClientRect(w->hwnd, &rc);
    DpiSetFromHwnd(w->hwnd);
    int pad = DpiScale(16), gap = DpiScale(8);
    int row = std::max(DpiScale(34), GetAppFontSizeForDpi(DpiGet()) + DpiScale(18));
    int width = std::max(1, (int)rc.right - pad * 2);
    bool stacked = width < row * 13;
    int labelWidth = std::min(std::max(DpiScale(190), row * 4), width / 3);
    int fieldWidth = stacked ? width : width - labelWidth - gap;
    int heights[7]{};
    int formHeight = 0;
    for (int id = scScope; id <= scFormat; id++) {
        int labelHeight = StudyTextHeight(GetDlgItem(w->hwnd, id + 200), stacked ? width : labelWidth);
        int height = stacked ? labelHeight + gap + row : std::max(row, labelHeight);
        heights[id - scScope] = height;
        formHeight += height + gap;
    }
    int statusHeight = StudyTextHeight(StudyControlAt(w, scStatus), width);
    int buttonWidth = (width - gap) / 2;
    int buttonHeight = std::max(row, std::max(StudyTextHeight(StudyControlAt(w, scSave), buttonWidth - gap * 2),
                                              StudyTextHeight(GetDlgItem(w->hwnd, IDCANCEL), buttonWidth - gap * 2)) +
                                         gap * 2);
    int previewDy =
        std::max(DpiScale(150), (int)rc.bottom - pad * 2 - formHeight - statusHeight - buttonHeight - gap * 2);
    int height = pad * 2 + formHeight + previewDy + statusHeight + buttonHeight + gap * 2;
    w->scroll = std::clamp(w->scroll, 0, std::max(0, height - (int)rc.bottom));
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMax = height - 1;
    si.nPage = rc.bottom;
    si.nPos = w->scroll;
    SetScrollInfo(w->hwnd, SB_VERT, &si, TRUE);
    auto place = [&](HWND child, int x, int y, int dx, int dy) {
        SetWindowPos(child, nullptr, x, y - w->scroll, dx, dy, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    int y = pad;
    for (int id = scScope; id <= scFormat; id++) {
        HWND label = GetDlgItem(w->hwnd, id + 200), control = StudyControlAt(w, id);
        int labelHeight = StudyTextHeight(label, stacked ? width : labelWidth);
        place(label, pad, y, stacked ? width : labelWidth, labelHeight);
        int controlY = stacked ? y + labelHeight + gap : y;
        int controlDy = id == scFrom || id == scTo ? row : row * 10;
        place(control, stacked ? pad : pad + labelWidth + gap, controlY, fieldWidth, controlDy);
        y += heights[id - scScope] + gap;
    }
    place(StudyControlAt(w, scPreview), pad, y, width, previewDy);
    y += previewDy + gap;
    place(StudyControlAt(w, scStatus), pad, y, width, statusHeight);
    y += statusHeight + gap;
    place(StudyControlAt(w, scSave), pad, y, buttonWidth, buttonHeight);
    place(GetDlgItem(w->hwnd, IDCANCEL), pad + buttonWidth + gap, y, buttonWidth, buttonHeight);
}
static void StudyRevealFocus(StudyWindow* w, HWND child) {
    if (!child || !IsChild(w->hwnd, child)) return;
    RECT item, client;
    GetWindowRect(child, &item);
    GetClientRect(w->hwnd, &client);
    MapWindowPoints(nullptr, w->hwnd, (POINT*)&item, 2);
    WCHAR klass[32]{};
    GetClassNameW(child, klass, dimof(klass));
    if (wcscmp(klass, L"COMBOBOX") == 0) {
        item.bottom = item.top + std::max(DpiScale(34), GetAppFontSizeForDpi(DpiGet()) + DpiScale(18));
    }
    int margin = DpiScale(8);
    int viewport = client.bottom - margin * 2;
    if (item.bottom - item.top > viewport || item.top < margin)
        w->scroll += item.top - margin;
    else if (item.bottom > client.bottom - margin)
        w->scroll += item.bottom - client.bottom + margin;
    else
        return;
    StudyLayout(w);
}
static void StudyRefreshStyle(StudyWindow* w) {
    DpiSetFromHwnd(w->hwnd);
    DeleteObject(w->background);
    DeleteObject(w->field);
    w->background = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    w->field = CreateSolidBrush(ThemeControlBackgroundColor());
    for (HWND child = GetWindow(w->hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        SendMessageW(child, WM_SETFONT, (WPARAM)GetAppFontForDpi(DpiGet())->GetHFont(), TRUE);
        WCHAR klass[32]{};
        GetClassNameW(child, klass, dimof(klass));
        if (wcscmp(klass, L"COMBOBOX") == 0) {
            HDC dc = GetDC(child);
            HGDIOBJ old = SelectObject(dc, GetAppFontForDpi(DpiGet())->GetHFont());
            int width = 0;
            int count = (int)SendMessageW(child, CB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++) {
                int size = (int)SendMessageW(child, CB_GETLBTEXTLEN, i, 0);
                if (size < 0) continue;
                WCHAR* text = AllocArray<WCHAR>(size + 1);
                SendMessageW(child, CB_GETLBTEXT, i, (LPARAM)text);
                SIZE extent{};
                GetTextExtentPoint32W(dc, text, size, &extent);
                width = std::max(width, (int)extent.cx + DpiScale(40));
                free(text);
            }
            SelectObject(dc, old);
            ReleaseDC(child, dc);
            SendMessageW(child, CB_SETDROPPEDWIDTH, width, 0);
        }
    }
    StudyLayout(w);
    InvalidateRect(w->hwnd, nullptr, TRUE);
}
static LRESULT CALLBACK StudyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DpiScope dpi(hwnd);
    auto* w = (StudyWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        w = (StudyWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        w->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
    }
    if (!w) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
        case WM_GETMINMAXINFO: {
            auto* info = (MINMAXINFO*)lp;
            info->ptMinTrackSize = {DpiScale(520), DpiScale(420)};
            return 0;
        }
        case WM_COMMAND:
            if (lp && (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == CBN_SETFOCUS || HIWORD(wp) == BN_SETFOCUS)) {
                StudyRevealFocus(w, (HWND)lp);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == scSave && HIWORD(wp) == BN_CLICKED) {
                StudySave(w);
                return 0;
            }
            if ((HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) >= scScope && LOWORD(wp) <= scFormat) ||
                (HIWORD(wp) == EN_CHANGE && (LOWORD(wp) == scFrom || LOWORD(wp) == scTo)))
                StudyPreview(w);
            return 0;
        case WM_SIZE:
            if (w->controls[scSave]) StudyLayout(w);
            return 0;
        case WM_MOUSEWHEEL:
            w->scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * DpiScale(80);
            StudyLayout(w);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof(si), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &si);
            int action = LOWORD(wp);
            if (action == SB_TOP)
                w->scroll = 0;
            else if (action == SB_BOTTOM)
                w->scroll = si.nMax;
            else if (action == SB_THUMBTRACK || action == SB_THUMBPOSITION)
                w->scroll = si.nTrackPos;
            else if (action == SB_LINEUP)
                w->scroll -= DpiScale(32);
            else if (action == SB_LINEDOWN)
                w->scroll += DpiScale(32);
            else if (action == SB_PAGEUP)
                w->scroll -= si.nPage;
            else if (action == SB_PAGEDOWN)
                w->scroll += si.nPage;
            StudyLayout(w);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor((HDC)wp, ThemeWindowTextColor());
            SetBkColor((HDC)wp,
                       msg == WM_CTLCOLORSTATIC ? ThemeMainWindowBackgroundColor() : ThemeControlBackgroundColor());
            return (LRESULT)(msg == WM_CTLCOLORSTATIC ? w->background : w->field);
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, w->background);
            return 1;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            StudyRefreshStyle(w);
            return 0;
        case WM_DPICHANGED: {
            RECT* r = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            StudyRefreshStyle(w);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static HWND MakeStudyControl(StudyWindow* w, int id, const WCHAR* klass, Str text, DWORD style) {
    HWND child = CreateWindowExW(0, klass, ToWStrTemp(text).s, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, w->hwnd,
                                 (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, (WPARAM)GetAppFontForDpi(DpiGet())->GetHFont(), FALSE);
    if (id <= scStatus) w->controls[id] = child;
    return child;
}
static void StudyAddChoices(StudyWindow* w, int id, SeqStrings choices) {
    for (Str s = SeqStrFirst(choices); s; s = SeqStrNext(s))
        SendMessageW(StudyControlAt(w, id), CB_ADDSTRING, 0, (LPARAM)ToWStrTemp(s).s);
    SendMessageW(StudyControlAt(w, id), CB_SETCURSEL, 0, 0);
}
void ShowStudyExport(MainWindow* owner) {
    WindowTab* tab = owner ? owner->CurrentTab() : nullptr;
    if (!tab || !EngineMupdfSupportsAnnotations(tab->GetEngine()) || !HasPermission(Perm::DiskAccess) ||
        !HasPermission(Perm::CopySelection))
        return;
    StudyDocument doc;
    CollectStudyNotes(tab->GetEngine(), tab->filePath, tab->selectedAnnotation, doc);
    StudyWindow w;
    w.doc = &doc;
    w.background = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    w.field = CreateSolidBrush(ThemeControlBackgroundColor());
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.lpszClassName = L"SumatraPDFEnhancedStudyExport";
    klass.lpfnWndProc = StudyWndProc;
    RegisterClassExW(&klass);
    DpiSetFromHwnd(owner->hwndFrame);
    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName, L"Export highlights and notes",
                                WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                DpiScale(850), DpiScale(790), owner->hwndFrame, nullptr, klass.hInstance, &w);
    if (!hwnd) return;
    Str labels[] = {Tr("Scope"), Tr("First page"), Tr("Last page"), Tr("Annotation type"),
                    Tr("Color"), Tr("Order"),      Tr("Format")};
    for (int id = scScope; id <= scFormat; id++) {
        MakeStudyControl(&w, id + 200, L"STATIC", labels[id - scScope], SS_NOPREFIX);
        bool page = id == scFrom || id == scTo;
        MakeStudyControl(&w, id, page ? L"EDIT" : L"COMBOBOX", page ? fmt("%d", id == scFrom ? 1 : doc.pages) : Str{},
                         WS_TABSTOP | (page ? ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER : CBS_DROPDOWNLIST | WS_VSCROLL));
    }
    StudyAddChoices(&w, scScope, "Document / page range\0Selected annotation\0");
    StudyAddChoices(&w, scType, "All annotation types\0");
    StudyAddChoices(&w, scColor, "All colors\0");
    StudyAddChoices(&w, scOrder, "Page order\0Creation time (unknown dates last)\0");
    StudyAddChoices(&w, scFormat, kStudyFormats);
    for (const auto& n : doc.notes) {
        if (!VecContains(w.types, n.type)) {
            VecAppend(w.types, n.type);
            SendMessageW(StudyControlAt(&w, scType), CB_ADDSTRING, 0,
                         (LPARAM)ToWStrTemp(SeqStrByIndex(AnnotationTypeNames(), (int)n.type)).s);
        }
        if (!VecContains(w.colors, n.color)) {
            VecAppend(w.colors, n.color);
            SendMessageW(StudyControlAt(&w, scColor), CB_ADDSTRING, 0, (LPARAM)ToWStrTemp(StudyColor(n.color)).s);
        }
    }
    MakeStudyControl(&w, scPreview, L"EDIT", {},
                     WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER);
    SendMessageW(StudyControlAt(&w, scPreview), EM_SETLIMITTEXT, INT_MAX, 0);
    MakeStudyControl(&w, scStatus, L"STATIC", {}, SS_NOPREFIX);
    MakeStudyControl(&w, scSave, L"BUTTON", Tr("Save export..."),
                     WS_TABSTOP | BS_DEFPUSHBUTTON | BS_MULTILINE | BS_NOTIFY);
    MakeStudyControl(&w, IDCANCEL, L"BUTTON", Tr("Close"), WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE | BS_NOTIFY);
    StudyRefreshStyle(&w);
    StudyPreview(&w);
    HWND ownerFrame = owner->hwndFrame;
    EnableWindow(ownerFrame, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(StudyControlAt(&w, scScope));
    MSG msg{};
    while (IsWindow(hwnd)) {
        int result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (msg.message == WM_KEYDOWN && IsWindow(hwnd)) StudyRevealFocus(&w, GetFocus());
    }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    if (IsWindow(ownerFrame)) {
        EnableWindow(ownerFrame, TRUE);
        SetForegroundWindow(ownerFrame);
    }
}

#if IS_DEBUG

static bool StudyPdfRoundtrip() {
    const char* stream = "BT /F1 12 Tf 20 740 Td (Source highlighted words) Tj ET";
    Str objects[] = {
        StrL("<< /Type /Catalog /Pages 2 0 R >>"),
        StrL("<< /Type /Pages /Count 1 /Kids [3 0 R] >>"),
        StrL("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 "
             "0 R /Annots [6 0 R 7 0 R 8 0 R] >>"),
        fmt("<< /Length %d >>\nstream\n%s\nendstream", (int)strlen(stream), Str(stream)),
        StrL("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"),
        StrL("<< /Type /Annot /Subtype /Highlight /Rect [18 736 200 755] /QuadPoints [18 755 200 755 18 736 200 736] "
             "/C [1 1 0] /Contents (Highlight comment) /CreationDate (D:20231114221320Z) >>"),
        StrL("<< /Type /Annot /Subtype /FreeText /Rect [10 10 500 80] /DA (/Helv 12 Tf 0 g) /Contents (Free text note) "
             ">>"),
        StrL("<< /Type /Annot /Subtype /Ink /Rect [10 90 100 120] /InkList [[10 90 100 120]] >>"),
    };
    str::Builder pdf;
    pdf.Append(StrL("%PDF-1.4\n"));
    Vec<int> offsets;
    for (int i = 0; i < dimof(objects); i++) {
        VecAppend(offsets, len(pdf));
        pdf.Append(fmt("%d 0 obj\n%s\nendobj\n", i + 1, objects[i]));
    }
    int xref = len(pdf);
    pdf.Append(fmt("xref\n0 %d\n0000000000 65535 f \n", dimof(objects) + 1));
    for (int offset : offsets) pdf.Append(fmt("%010d 00000 n \n", offset));
    pdf.Append(fmt("trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n", dimof(objects) + 1, xref));
    EngineBase* engine = CreateEngineMupdfFromData(ToStr(pdf), StrL("export.pdf"), nullptr);
    if (!engine) return false;
    bool ok = true;
    Vec<Annotation*> annotations;
    EngineMupdfGetAnnotations(engine, annotations);
    ok = len(annotations) == 3;
    if (ok) {
        SetContents(annotations[0], StrL("Unsaved edited comment"));
        StudyDocument doc;
        CollectStudyNotes(engine, StrL("C:\\Courses\\export.pdf"), annotations[0], doc);
        ok = len(doc.notes) == 3;
        if (ok)
            ok = str::Contains(doc.notes[0].quote, StrL("Source highlighted words")) &&
                 str::Eq(doc.notes[0].comment, StrL("Unsaved edited comment")) && doc.notes[0].selected &&
                 doc.notes[2].drawing && doc.notes[0].created > 0;
        StudyFilter f;
        f.selection = true;
        Vec<StudyNote*> selected;
        SelectStudyNotes(doc, f, selected);
        ok = ok && len(selected) == 1;
    }
    Str path = str::Dup(GetTempFilePathTemp(StrL("enhanced-export")));
    ok = ok && EngineMupdfSaveCopy(engine, path);
    SafeEngineRelease(&engine);
    Str saved = ok ? file::ReadFile(path) : Str{};
    if (ok) {
        engine = CreateEngineMupdfFromData(saved, StrL("reopened-export.pdf"), nullptr);
        ok = engine != nullptr;
        if (ok) {
            StudyDocument doc;
            CollectStudyNotes(engine, path, nullptr, doc);
            ok = len(doc.notes) == 3 && str::Eq(doc.notes[0].comment, StrL("Unsaved edited comment")) &&
                 str::Contains(doc.notes[0].quote, StrL("Source highlighted words"));
        }
    }
    SafeEngineRelease(&engine);
    str::Free(saved);
    file::Delete(path);
    str::Free(path);
    return ok;
}

void StudyExport_UnitTests() {
    utassert(StudyPdfRoundtrip());
    StudyDocument doc;
    doc.title = str::Dup(StrL("Study <notes> & café"));
    doc.source = str::Dup(StrL("C:\\Courses\\Résumé #1.pdf"));
    doc.pages = 3;
    StudyNote first;
    first.page = 2;
    first.pageLabel = str::Dup(StrL("ii"));
    first.type = AnnotationType::Highlight;
    first.color = 0xff00ff00;
    first.created = 1700000000;
    first.quote = str::Dup(StrL("α + β < 2\nLine two: [quote] # $ \\\""));
    first.comment = str::Dup(StrL("=SUM(1,2)\nNotes: 中文\t'&'"));
    first.author = str::Dup(StrL("Student"));
    first.selected = true;
    VecAppend(doc.notes, first);
    StudyNote ink;
    ink.page = 1;
    ink.ordinal = 1;
    ink.type = AnnotationType::Ink;
    ink.pageLabel = str::Dup(StrL("i"));
    ink.drawing = true;
    VecAppend(doc.notes, ink);
    StudyFilter f;
    Vec<StudyNote*> notes;
    SelectStudyNotes(doc, f, notes);
    utassert(len(notes) == 2 && notes[0]->page == 1);
    f.creationOrder = true;
    SelectStudyNotes(doc, f, notes);
    utassert(len(notes) == 2 && notes[0]->page == 2);
    f.selection = true;
    SelectStudyNotes(doc, f, notes);
    utassert(len(notes) == 1 && notes[0]->selected);
    f.selection = false;
    f.type = AnnotationType::Highlight;
    f.colorIndex = 1;
    f.color = first.color;
    SelectStudyNotes(doc, f, notes);
    utassert(len(notes) == 1);
    f.from = 3;
    SelectStudyNotes(doc, f, notes);
    utassert(len(notes) == 0);
    f = {};
    SelectStudyNotes(doc, f, notes);
    utassert(str::Contains(JsonText(StrL("a\n\"b")), StrL("\\n\\\"")));
    utassert(str::Contains(XmlText(StrL("<&>")), StrL("&lt;&amp;&gt;")));
    utassert(str::Contains(CsvText(StrL("=1")), StrL("'=1")));
    utassert(str::Contains(StudyLink(doc.source, 2), StrL("%23")));
    int page = 0;
    utassert(!StudyPage(StrL("999999999999"), 3, page));
    utassert(!StudyPage(StrL("0"), 3, page));
    utassert(StudyPage(StrL("2"), 3, page) && page == 2);
    WCHAR directory[32768]{};
    DWORD count = GetEnvironmentVariableW(L"ENHANCED_EXPORT_TEST_DIR", directory, dimof(directory));
    for (int format = 0; format < 7; format++) {
        str::Builder out;
        utassert(RenderStudy(out, doc, notes, (StudyFormat)format));
        utassert(len(out) > 0);
        if (count > 0 && count < dimof(directory))
            utassert(file::WriteFile(path::JoinTemp(ToUtf8Temp(WStr(directory)),
                                                    fmt("study-fixture.%s", SeqStrByIndex(kStudyExtensions, format))),
                                     ToStr(out)));
        Vec<StudyNote*> empty;
        str::Builder emptyOut;
        utassert(RenderStudy(emptyOut, doc, empty, (StudyFormat)format));
        utassert(len(emptyOut) > 0);
    }
}
#endif
