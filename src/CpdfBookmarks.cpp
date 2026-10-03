/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/AutoWin.h"
#include "base/Crypto.h"
#include "base/File.h"
#include "base/JsonParser.h"
#include "base/UITask.h"
#include "base/Win.h"
#include <wininet.h>

#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"
#include "Settings.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "DocController.h"
#include "DisplayModel.h"
#include "MainWindow.h"
#include "SumatraConfig.h"
#include "SumatraPDF.h"
#include "Theme.h"
#include "Translations.h"
#include "CpdfBookmarks.h"

static const Str kCpdfRevision = StrL("38b2556dc111670acb427fd9789a94ada971b2df");
static const Str kCpdfUrl = StrL(
    "https://raw.githubusercontent.com/coherentgraphics/cpdf-binaries/"
    "38b2556dc111670acb427fd9789a94ada971b2df/Windows-64bit/cpdf.exe");
static const Str kCpdfSha256 = StrL("91f51d30c063233e0b8a23b8bb459e4acbbd577559a0da52fcdbec37d3237351");
constexpr int kCpdfBytes = 11724417;
constexpr int kBookmarkTextMax = 1024 * 1024;
constexpr DWORD kCpdfTimeoutMs = 120000;

static bool ValidUtf8(Str text) {
    return len(text) == 0 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.s, len(text), nullptr, 0) > 0;
}

static bool ReadNonNegativeInt(Str text, int& value) {
    if (!len(text)) return false;
    int n = 0;
    for (int i = 0; i < len(text); i++) {
        int digit = text.s[i] - '0';
        if (digit < 0 || digit > 9 || n > (INT_MAX - digit) / 10) return false;
        n = n * 10 + digit;
    }
    value = n;
    return true;
}

static bool ReadQuoted(const char*& p, const char* end) {
    if (p == end || *p++ != '"') return false;
    while (p < end) {
        char c = *p++;
        if (c == '"') return true;
        if ((unsigned char)c < 32) return false;
        if (c == '\\') {
            if (p == end || (unsigned char)*p < 32) return false;
            p++;
        }
    }
    return false;
}

struct BookmarkRow {
    Str original;
    Str title;
    int level = -1;
    int page = -1;
    bool open = false;
    bool retarget = false;
    bool hasTitle = false;
    ~BookmarkRow() {
        str::Free(original);
        str::Free(title);
    }
    void OnValue(json::Value* value) {
        if (!value->path || value->path->next) return;
        Str key = json::PathSegKey(value->path);
        if (str::Eq(key, StrL("text")) && value->type == json::Type::String) {
            str::ReplaceWithCopy(&title, value->value);
            hasTitle = true;
        }
        if (str::Eq(key, StrL("level")) && value->type == json::Type::Number) ReadNonNegativeInt(value->value, level);
        if (str::Eq(key, StrL("page")) && value->type == json::Type::Number) ReadNonNegativeInt(value->value, page);
        if (str::Eq(key, StrL("open")) && value->type == json::Type::Bool) open = str::Eq(value->value, StrL("true"));
    }
};

static void SkipJsonSpace(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
}

static bool ReadJsonValue(const char*& p, const char* end) {
    if (p == end) return false;
    if (*p == '"') return ReadQuoted(p, end);
    if (*p != '{' && *p != '[') {
        while (p < end && *p != ',' && *p != '}' && *p != ']') p++;
        return true;
    }
    int depth = 0;
    do {
        if (*p == '"') {
            if (!ReadQuoted(p, end)) return false;
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        if (*p == '}' || *p == ']') depth--;
        if (depth > 128) return false;
        p++;
    } while (p < end && depth > 0);
    return depth == 0;
}

static void DeleteRows(Vec<BookmarkRow*>& rows) {
    for (auto row : rows) delete row;
    VecReset(rows);
}

static bool ValidRows(const Vec<BookmarkRow*>& rows, int pages) {
    int previous = -1;
    for (auto row : rows) {
        if (row->level < 0 || row->level > previous + 1 || row->level > 1024 || row->page < 0 || row->page > pages ||
            !ValidUtf8(row->title))
            return false;
        previous = row->level;
    }
    return true;
}

static bool ParseRows(Str text, int pages, Vec<BookmarkRow*>& rows) {
    if (len(text) < 2 || len(text) > kBookmarkTextMax || !ValidUtf8(text) || !json::Parse(text, {})) return false;
    const char* p = text.s;
    const char* end = p + len(text);
    SkipJsonSpace(p, end);
    if (p == end || *p++ != '[') return false;
    SkipJsonSpace(p, end);
    while (p < end && *p != ']') {
        if (*p != '{') return false;
        const char* start = p;
        if (!ReadJsonValue(p, end)) return false;
        auto row = new BookmarkRow();
        row->original = str::Dup(Str(start, (int)(p - start)));
        bool ok = json::Parse(row->original, MkMethod1<BookmarkRow, json::Value*, &BookmarkRow::OnValue>(row));
        if (!ok || !row->hasTitle || row->level < 0 || row->page < 0) {
            delete row;
            return false;
        }
        VecAppend(rows, row);
        SkipJsonSpace(p, end);
        if (p < end && *p == ',') {
            p++;
            SkipJsonSpace(p, end);
        } else
            break;
    }
    if (p == end || *p++ != ']') return false;
    SkipJsonSpace(p, end);
    return p == end && ValidRows(rows, pages);
}

// Replace only editor-owned fields; retain styles, actions and unfamiliar fields verbatim.
static TempStr SerializeRows(const Vec<BookmarkRow*>& rows) {
    str::Builder out;
    out.Append(StrL("[\n"));
    bool first = true;
    for (auto row : rows) {
        if (!first) out.Append(StrL(",\n"));
        first = false;
        out.Append(fmt("{\"level\":%d,\"text\":\"%s\",\"page\":%d,\"open\":%s", row->level,
                       json::EscapeStrTemp(row->title), row->page, row->open ? StrL("true") : StrL("false")));
        const char* p = row->original.s;
        const char* end = p ? p + len(row->original) : p;
        if (p < end) p++;
        while (p < end) {
            SkipJsonSpace(p, end);
            if (p == end || *p == '}') break;
            const char* field = p;
            if (!ReadQuoted(p, end)) break;
            Str key(field + 1, (int)(p - field - 2));
            SkipJsonSpace(p, end);
            if (p == end || *p++ != ':') break;
            SkipJsonSpace(p, end);
            if (!ReadJsonValue(p, end)) break;
            bool owned = str::Eq(key, StrL("level")) || str::Eq(key, StrL("text")) || str::Eq(key, StrL("page")) ||
                         str::Eq(key, StrL("open")) || (row->retarget && str::Eq(key, StrL("target")));
            if (!owned) {
                out.AppendChar(',');
                out.Append(Str(field, (int)(p - field)));
            }
            SkipJsonSpace(p, end);
            if (p < end && *p == ',') p++;
        }
        if (row->retarget || !len(row->original))
            out.Append(fmt(",\"target\":[{\"I\":%d},{\"N\":\"/Fit\"}]", row->page));
        out.AppendChar('}');
    }
    out.Append(StrL("\n]\n"));
    return ToStrTemp(out);
}

// Windows argv quoting; every path remains one argument without a shell.
static void AppendArg(str::Builder& cmd, Str arg) {
    if (len(cmd)) cmd.AppendChar(' ');
    cmd.AppendChar('"');
    int slashes = 0;
    for (int i = 0; i < len(arg); i++) {
        char c = arg.s[i];
        if (c == '\\') {
            slashes++;
            continue;
        }
        int n = c == '"' ? slashes * 2 + 1 : slashes;
        while (n-- > 0) cmd.AppendChar('\\');
        cmd.AppendChar(c);
        slashes = 0;
    }
    while (slashes-- > 0) cmd.Append(StrL("\\\\"));
    cmd.AppendChar('"');
}

static bool VerifyCpdf(Str path) {
    if (file::GetSize(path) != kCpdfBytes) return false;
    Str data = file::ReadFile(path);
    u8 digest[32]{};
    CalcSHA2Digest(data, digest);
    str::Free(data);
    return str::EqI(str::MemToHexTemp(Str((char*)digest, sizeof(digest))), kCpdfSha256);
}

static TempStr CpdfCachePath() {
    return path::JoinTemp(GetAppDataDirTemp(), StrL("tools"), StrL("cpdf-38b2556d.exe"));
}

struct BookmarkSession {
    LONG references = 1;
    Str original;
    Str snapshotDir;
    Str snapshot;
    Str executable;
    bool official = false;
    int pages = 0;
    ~BookmarkSession() {
        if (len(snapshotDir)) dir::RemoveAll(snapshotDir);
        str::Free(original);
        str::Free(snapshotDir);
        str::Free(snapshot);
        str::Free(executable);
    }
};

static void ReleaseSession(BookmarkSession* session) {
    if (InterlockedDecrement(&session->references) == 0) delete session;
}

enum class BookmarkAction {
    Load,
    Save
};
struct BookmarkWnd;
struct BookmarkJob {
    BookmarkSession* session = nullptr;
    BookmarkWnd* window = nullptr;
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BookmarkAction action = BookmarkAction::Load;
    bool download = false;
    bool success = false;
    Str text;
    Str target;
    Str error;
    ~BookmarkJob() {
        CloseHandle(cancel);
        str::Free(text);
        str::Free(target);
        str::Free(error);
        ReleaseSession(session);
    }
    bool Cancelled() const { return WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0; }
    void Fail(Str message) { str::ReplaceWithCopy(&error, message); }
};

static bool DownloadCpdf(BookmarkJob* job) {
    auto session = job->session;
    if (!dir::CreateForFile(session->executable)) {
        job->Fail(Tr("The tools folder is not writable. Choose a local cpdf executable instead."));
        return false;
    }
    Str partial = path::JoinTemp(session->snapshotDir, StrL("cpdf-download.part"));
    HINTERNET net =
        InternetOpenW(L"SumatraPDF Enhanced optional cpdf", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) {
        job->Fail(Tr("Unable to connect for the cpdf download. Choose a local executable or try again."));
        return false;
    }
    defer {
        InternetCloseHandle(net);
    };
    DWORD timeout = 10000;
    InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(net, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    HINTERNET request = InternetOpenUrlW(net, CWStrTemp(ToWStrTemp(kCpdfUrl)), nullptr, 0,
                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
    if (!request) {
        job->Fail(Tr("The cpdf download failed. Check your connection or choose a local executable."));
        return false;
    }
    defer {
        InternetCloseHandle(request);
    };
    DWORD status = 0, size = sizeof(status);
    if (!HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr) ||
        status != 200) {
        job->Fail(Tr("The official cpdf download returned an error. No executable was installed."));
        return false;
    }
    AutoCloseHandle out = CreateFileW(CWStrTemp(ToWStrTemp(partial)), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!out.IsValid()) {
        job->Fail(Tr("Unable to write the temporary cpdf download."));
        return false;
    }
    int total = 0;
    BYTE buffer[65536];
    while (!job->Cancelled()) {
        DWORD read = 0, written = 0;
        if (!InternetReadFile(request, buffer, sizeof(buffer), &read)) break;
        if (!read) return total == kCpdfBytes && FlushFileBuffers(out);
        if (read > (DWORD)(kCpdfBytes - total)) break;
        if (!WriteFile(out, buffer, read, &written, nullptr) || written != read) break;
        total += (int)read;
    }
    job->Fail(
        Tr("The cpdf download was interrupted or had an unexpected size. Try again or choose a local executable."));
    return false;
}

static bool InstallCpdf(BookmarkJob* job) {
    if (!DownloadCpdf(job)) return false;
    Str partial = path::JoinTemp(job->session->snapshotDir, StrL("cpdf-download.part"));
    if (job->Cancelled()) return false;
    if (!VerifyCpdf(partial)) {
        job->Fail(Tr(
            "The downloaded cpdf failed its SHA-256 check. It was not run. Choose a local executable or try again."));
        return false;
    }
    if (!MoveFileExW(CWStrTemp(ToWStrTemp(partial)), CWStrTemp(ToWStrTemp(job->session->executable)),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
        job->Fail(Tr("Unable to save the verified cpdf tool. Choose a writable data folder or a local executable."));
        return false;
    }
    file::WriteFile(path::JoinTemp(path::GetDirTemp(job->session->executable), StrL("cpdf-license.txt")),
                    StrL("Optional Coherent PDF executable. AGPL v3 or commercial license.\r\n"
                         "https://github.com/coherentgraphics/cpdf-binaries/blob/"
                         "38b2556dc111670acb427fd9789a94ada971b2df/LICENSE.md\r\n"
                         "https://www.coherentpdf.com/\r\n"));
    return true;
}

static bool RunCpdf(BookmarkJob* job, const StrVec& args, Str& result) {
    BookmarkSession* session = job->session;
    Str output = path::JoinTemp(session->snapshotDir, StrL("stdout.txt"));
    Str errors = path::JoinTemp(session->snapshotDir, StrL("stderr.txt"));
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    AutoCloseHandle out = CreateFileW(CWStrTemp(ToWStrTemp(output)), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    AutoCloseHandle err = CreateFileW(CWStrTemp(ToWStrTemp(errors)), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    AutoCloseHandle in =
        CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (!out.IsValid() || !err.IsValid() || !in.IsValid()) {
        job->Fail(Tr("Unable to create temporary files for cpdf."));
        return false;
    }
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    auto attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(bytes);
    if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) {
        free(attributes);
        job->Fail(Tr("Unable to prepare the cpdf process."));
        return false;
    }
    defer {
        DeleteProcThreadAttributeList(attributes);
        free(attributes);
    };
    HANDLE handles[] = {in, out, err};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr,
                                   nullptr)) {
        job->Fail(Tr("Unable to prepare cpdf output capture."));
        return false;
    }
    STARTUPINFOEXW start{};
    start.StartupInfo.cb = sizeof(start);
    start.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    start.StartupInfo.wShowWindow = SW_HIDE;
    start.StartupInfo.hStdInput = in;
    start.StartupInfo.hStdOutput = out;
    start.StartupInfo.hStdError = err;
    start.lpAttributeList = attributes;
    str::Builder cmd;
    AppendArg(cmd, session->executable);
    for (Str arg : args) AppendArg(cmd, arg);
    WStr command = ToWStrTemp(ToStr(cmd));
    PROCESS_INFORMATION process{};
    AutoCloseHandle processJob = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!processJob.IsValid() ||
        !SetInformationJobObject(processJob, JobObjectExtendedLimitInformation, &limit, sizeof(limit))) {
        job->Fail(Tr("Unable to prepare a cancellable cpdf process."));
        return false;
    }
    if (!CreateProcessW(CWStrTemp(ToWStrTemp(session->executable)), command.s, nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                        CWStrTemp(ToWStrTemp(session->snapshotDir)), &start.StartupInfo, &process)) {
        job->Fail(Tr("cpdf could not be started. Choose a compatible Windows executable and try again."));
        return false;
    }
    AutoCloseHandle proc = process.hProcess;
    AutoCloseHandle thread = process.hThread;
    if (!AssignProcessToJobObject(processJob, proc)) {
        TerminateProcess(proc, 1);
        WaitForSingleObject(proc, INFINITE);
        job->Fail(Tr("Unable to isolate the cpdf process."));
        return false;
    }
    ResumeThread(thread);
    DWORD elapsed = 0;
    while (WaitForSingleObject(proc, 100) == WAIT_TIMEOUT) {
        elapsed += 100;
        LARGE_INTEGER outSize{}, errSize{};
        GetFileSizeEx(out, &outSize);
        GetFileSizeEx(err, &errSize);
        if (job->Cancelled() || elapsed >= kCpdfTimeoutMs || outSize.QuadPart > kBookmarkTextMax ||
            errSize.QuadPart > kBookmarkTextMax) {
            TerminateJobObject(processJob, 1);
            WaitForSingleObject(proc, INFINITE);
            job->Fail(job->Cancelled() ? Tr("Cancelled.")
                                       : Tr("cpdf exceeded the time or output limit. Your original PDF is unchanged."));
            return false;
        }
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(proc, &exitCode);
    FlushFileBuffers(out);
    FlushFileBuffers(err);
    // Capture handles share read access; closing the process has flushed its writes.
    if (job->Cancelled()) return false;
    if (exitCode != 0) {
        Str detail = file::ReadFile(errors);
        Str displayDetail = ValidUtf8(detail) ? Str(detail.s, std::min(len(detail), 2000)) : Str{};
        job->Fail(fmt(Tr("cpdf could not process this PDF. It may need a password, have an invalid destination, or be "
                         "damaged.\n%s")
                          .s,
                      displayDetail));
        str::Free(detail);
        return false;
    }
    if (file::GetSize(output) > kBookmarkTextMax) {
        job->Fail(Tr("This bookmark list is too large for the editor."));
        return false;
    }
    result = file::ReadFile(output);
    return true;
}

struct BookmarkViewport : ScrollBox {
    explicit BookmarkViewport(ILayout* child) : ScrollBox(child) {}
    void ClipControls(ILayout* node) {
        if (auto* control = node->AsControl()) {
            Rect bounds = ChildPosWithinParent(control->hwnd);
            Rect clip = bounds.Intersect(lastBounds);
            clip.x -= bounds.x;
            clip.y -= bounds.y;
            HRGN region = CreateRectRgn(clip.x, clip.y, clip.x + clip.dx, clip.y + clip.dy);
            if (!SetWindowRgn(control->hwnd, region, FALSE)) DeleteObject(region);
        }
        for (int i = 0; i < node->LayoutChildCount(); i++) ClipControls(node->LayoutChildAt(i));
    }
    void SetBounds(Rect bounds) override {
        ScrollBox::SetBounds(bounds);
        ClipControls(child);
    }
};

struct BookmarkWnd : WindowBase {
    BookmarkSession* session = nullptr;
    BookmarkJob* job = nullptr;
    VirtListBox* list = nullptr;
    ListBoxModelStrings* model = nullptr;
    Vec<BookmarkRow*> rows;
    Vec<VirtButton*> editButtons;
    Vec<VirtText*> labels;
    VirtRichText* help = nullptr;
    ScrollBox* scroll = nullptr;
    Edit* title = nullptr;
    Edit* page = nullptr;
    Edit* level = nullptr;
    Checkbox* opened = nullptr;
    int selected = -1;
    VirtRichText* status = nullptr;
    VirtButton* save = nullptr;
    VirtButton* cancel = nullptr;
    bool ready = false;
    void SetStatus(Str text) {
        status->Reset();
        status->AddPlainText(text);
        DoLayout();
        HwndInvalidate(hwnd);
    }
    ~BookmarkWnd() override {
        if (job) SetEvent(job->cancel);
        DeleteRows(rows);
        ReleaseSession(session);
    }
    void SetBusy(bool busy) {
        list->SetIsEnabled(!busy && ready);
        title->SetIsEnabled(!busy && ready);
        page->SetIsEnabled(!busy && ready);
        level->SetIsEnabled(!busy && ready);
        opened->SetIsEnabled(!busy && ready);
        for (auto button : editButtons) button->SetIsEnabled(!busy && ready);
        save->SetIsEnabled(!busy && ready);
        cancel->SetText(busy ? Tr("Cancel operation") : Tr("Close"));
        DoLayout();
    }
    void Start(BookmarkAction action, bool download = false, Str target = {});
    void OnSave(VirtMouseEvent*);
    void SelectRow();
    void FillRow();
    void RefreshList(int index);
    bool ApplyRow();
    int SubtreeEnd(int index);
    void OnApply(VirtMouseEvent*) { ApplyRow(); }
    void OnAdd(VirtMouseEvent*);
    void OnRemove(VirtMouseEvent*);
    void OnUp(VirtMouseEvent*);
    void OnDown(VirtMouseEvent*);
    void OnCancel(VirtMouseEvent*) {
        if (job) {
            SetEvent(job->cancel);
            SetStatus(Tr("Cancelling…"));
            cancel->SetIsEnabled(false);
            HwndInvalidate(hwnd);
            return;
        }
        ScheduleDelete();
    }
    void OnDpi(WindowBase::DpiChangedEvent*);
    void OnMessage(WindowBase::WndProcEvent* ev) {
        if (!scroll) return;
        if (ev->msg == WM_PRINTCLIENT && ev->wparam && vroot) {
            PaintVirtTree(vroot, (HDC)ev->wparam, HwndClientRect(hwnd), ThemeWindowBackgroundColor());
        } else if (ev->msg == WM_VSCROLL && ev->lparam == 0) {
            scroll->OnVScroll(ev->wparam);
        } else if (ev->msg == WM_MOUSEWHEEL) {
            POINT point{GET_X_LPARAM(ev->lparam), GET_Y_LPARAM(ev->lparam)};
            ScreenToClient(hwnd, &point);
            if (list->lastBounds.Contains(Point{point.x, point.y})) return;
            VirtMouseEvent mouse;
            mouse.wheelDelta = GET_WHEEL_DELTA_WPARAM(ev->wparam);
            scroll->OnMouseWheel(&mouse);
        } else
            return;
        ((BookmarkViewport*)scroll)->ClipControls(scroll->child);
        ev->result = 0;
        ev->didHandle = true;
    }
    bool Create(MainWindow* win, bool visible = true);
};

static BookmarkWnd* gBookmarkWnd = nullptr;

static LRESULT CALLBACK BookmarkFocusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, BookmarkFocusProc, id);
    if (msg == WM_SETFOCUS) {
        auto* window = (BookmarkWnd*)data;
        Rect bounds = ChildPosWithinParent(hwnd);
        Rect view = window->scroll->lastBounds;
        int delta = bounds.y < view.y ? bounds.y - view.y : std::max(0, bounds.y + bounds.dy - view.y - view.dy);
        if (delta && window->scroll->ScrollBy(delta))
            ((BookmarkViewport*)window->scroll)->ClipControls(window->scroll->child);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static bool MakeSnapshot(BookmarkJob* job) {
    BookmarkSession* session = job->session;
    if (file::Exists(session->snapshot)) return true;
    // Deny writes while copying so cpdf sees one stable on-disk revision.
    AutoCloseHandle source = CreateFileW(CWStrTemp(ToWStrTemp(session->original)), GENERIC_READ, FILE_SHARE_READ,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!source.IsValid() || !file::Copy(session->snapshot, session->original, true)) {
        job->Fail(Tr("Unable to make a temporary PDF copy. Close other editors and check available disk space."));
        return false;
    }
    return !job->Cancelled();
}

static void CompleteJob(BookmarkJob* job) {
    BookmarkWnd* window = gBookmarkWnd;
    if (window == job->window && window->job == job) {
        window->job = nullptr;
        window->cancel->SetIsEnabled(true);
        if (job->success) {
            if (job->action == BookmarkAction::Load) {
                window->ready = ParseRows(job->text, window->session->pages, window->rows);
                if (!window->ready) DeleteRows(window->rows);
                window->RefreshList(len(window->rows) ? 0 : -1);
                window->SetStatus(
                    window->ready
                        ? Tr("Select a bookmark to edit. Save PDF copy keeps the original open and unchanged.")
                        : Tr("cpdf returned an unsupported outline. Your original PDF is unchanged."));
            } else {
                window->SetStatus(fmt(Tr("Saved a new PDF copy: %s").s, job->target));
            }
        } else {
            window->SetStatus(job->Cancelled() ? Tr("Cancelled. Your original PDF is unchanged.") : job->error);
        }
        window->SetBusy(false);
        EditSetFocus(window->title);
        HwndInvalidate(window->hwnd);
    }
    delete job;
}

static void ExecuteBookmarkJob(BookmarkJob* job) {
    auto session = job->session;
    bool ok = !job->Cancelled();
    if (ok && job->download) ok = InstallCpdf(job);
    if (ok && session->official && !VerifyCpdf(session->executable)) {
        job->Fail(Tr(
            "The cached cpdf executable failed its SHA-256 check. Close and reopen the editor to download it again."));
        ok = false;
    }
    if (ok) ok = MakeSnapshot(job);
    StrVec args;
    Str output;
    if (ok && job->action == BookmarkAction::Load) {
        args.Append(StrL("-list-bookmarks-json"));
        args.Append(session->snapshot);
        args.Append(StrL("-preserve-actions"));
        ok = RunCpdf(job, args, output);
        Vec<BookmarkRow*> parsed;
        if (ok && !ParseRows(output, session->pages, parsed)) {
            job->Fail(Tr("cpdf returned an unsupported bookmark format. The original PDF is unchanged."));
            ok = false;
        }
        DeleteRows(parsed);
        if (ok) str::ReplaceWithCopy(&job->text, output);
    } else if (ok) {
        Str data = path::JoinTemp(session->snapshotDir, StrL("bookmarks.json"));
        Str stage = path::JoinTemp(session->snapshotDir, StrL("bookmarks-copy.pdf"));
        if (!file::WriteFile(data, job->text)) {
            job->Fail(Tr("Unable to write the temporary bookmark list. Check available disk space."));
            ok = false;
        }
        if (ok) {
            args.Append(StrL("-add-bookmarks-json"));
            args.Append(data);
            args.Append(session->snapshot);
            args.Append(StrL("-o"));
            args.Append(stage);
            ok = RunCpdf(job, args, output);
        }
        if (ok && (!file::StartsWith(stage, StrL("%PDF-")) || job->Cancelled())) ok = false;
        if (ok) {
            // Copy into the destination directory, then publish without replacing any file.
            WCHAR staging[MAX_PATH]{};
            Str directory = path::GetDirTemp(job->target);
            if (!GetTempFileNameW(CWStrTemp(ToWStrTemp(directory)), L"cpb", 0, staging)) {
                job->Fail(Tr("Unable to create the new PDF copy in that folder. Choose a writable folder."));
                ok = false;
            } else {
                Str stagingPath = ToUtf8Temp(WStr(staging));
                ok = file::Copy(stagingPath, stage, false) && !job->Cancelled() &&
                     MoveFileExW(staging, CWStrTemp(ToWStrTemp(job->target)), MOVEFILE_WRITE_THROUGH);
                file::Delete(stagingPath);
                if (!ok)
                    job->Fail(
                        Tr("The copy could not be saved. Choose a new, unused filename in a writable folder. The "
                           "original PDF is unchanged."));
            }
        }
    }
    str::Free(output);
    job->success = ok && !job->Cancelled();
    if (!job->success && !len(job->error))
        job->Fail(Tr("cpdf did not create a valid PDF copy. The original PDF is unchanged."));
}

static void RunBookmarkJob(BookmarkJob* job) {
    ExecuteBookmarkJob(job);
    uitask::Post(MkFunc0(CompleteJob, job), "Complete cpdf bookmark operation");
}

void BookmarkWnd::Start(BookmarkAction action, bool download, Str target) {
    job = new BookmarkJob();
    job->session = session;
    InterlockedIncrement(&session->references);
    job->window = this;
    job->action = action;
    job->download = download;
    job->target = str::Dup(target);
    if (action == BookmarkAction::Save) job->text = str::Dup(SerializeRows(rows));
    SetStatus(download                         ? Tr("Downloading and verifying optional cpdf…")
              : action == BookmarkAction::Load ? Tr("Reading PDF bookmarks…")
                                               : Tr("Saving a new PDF copy…"));
    SetBusy(true);
    RunAsync(MkFunc0(RunBookmarkJob, job), StrL("CpdfBookmarks"));
}

void BookmarkWnd::OnSave(VirtMouseEvent*) {
    if (!ready || job) return;
    if (!ApplyRow() || !ValidRows(rows, session->pages)) return;
    if (!len(rows) && MessageBoxW(hwnd, L"Saving an empty list removes all bookmarks from the new copy. Continue?",
                                  L"Remove bookmarks from copy", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    WCHAR name[32768]{};
    TempStr suggested = fmt("%s-bookmarks.pdf", path::GetPathNoExtTemp(session->original));
    WStr suggestedW = ToWStrTemp(suggested);
    wcscpy_s(name, dimof(name), suggestedW.s);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = name;
    ofn.nMaxFile = dimof(name);
    ofn.lpstrFilter = L"PDF files\0*.pdf\0\0";
    ofn.lpstrDefExt = L"pdf";
    ofn.lpstrTitle = L"Save PDF copy with edited bookmarks";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    Str target = ToUtf8Temp(WStr(name));
    if (path::IsSame(target, session->original) || file::Exists(target)) {
        SetStatus(Tr("Choose a new filename. This editor never overwrites the original PDF or an existing file."));
        DoLayout();
        HwndInvalidate(hwnd);
        return;
    }
    Start(BookmarkAction::Save, false, target);
}

int BookmarkWnd::SubtreeEnd(int index) {
    int end = index + 1;
    while (end < len(rows) && rows[end]->level > rows[index]->level) end++;
    return end;
}

void BookmarkWnd::FillRow() {
    bool hasRow = selected >= 0 && selected < len(rows);
    title->SetText(hasRow ? rows[selected]->title : Str{});
    page->SetText(hasRow ? fmt("%d", rows[selected]->page) : Str{});
    level->SetText(hasRow ? fmt("%d", rows[selected]->level) : Str{});
    opened->SetIsChecked(hasRow && rows[selected]->open);
}

void BookmarkWnd::RefreshList(int index) {
    model->strings.Reset();
    for (auto row : rows) {
        str::Builder caption;
        for (int i = 0; i < std::min(row->level, 12); i++) caption.Append(StrL("  "));
        for (int i = 0; i < len(row->title); i++)
            caption.AppendChar((unsigned char)row->title.s[i] < 32 ? ' ' : row->title.s[i]);
        caption.Append(row->page ? fmt("  (%d)", row->page) : Tr("  (heading or link)"));
        model->strings.Append(ToStr(caption));
    }
    list->SetModel(model);
    selected = index;
    list->SetCurrentSelection(index);
    if (index >= 0) list->EnsureVisible(index);
    FillRow();
    DoLayout();
    HwndInvalidate(hwnd);
}

bool BookmarkWnd::ApplyRow() {
    if (selected < 0 || selected >= len(rows)) return true;
    auto row = rows[selected];
    int newPage = -1, newLevel = -1;
    if (!ReadNonNegativeInt(page->GetTextTemp(), newPage) || newPage > session->pages || (!newPage && row->page != 0) ||
        !ReadNonNegativeInt(level->GetTextTemp(), newLevel) || newLevel > 1024) {
        SetStatus(Tr("Enter a valid PDF page and level. Page 0 is kept only for existing headings or links."));
        return false;
    }
    int end = SubtreeEnd(selected);
    int delta = newLevel - row->level;
    for (int i = selected; i < end; i++) rows[i]->level += delta;
    if (!ValidRows(rows, session->pages)) {
        for (int i = selected; i < end; i++) rows[i]->level -= delta;
        SetStatus(
            Tr("Start with level 0 and increase nesting by at most one level at a time. Children move with their "
               "parent."));
        return false;
    }
    if (newPage != row->page) row->retarget = true;
    row->page = newPage;
    row->open = opened->IsChecked();
    str::ReplaceWithCopy(&row->title, title->GetTextTemp());
    RefreshList(selected);
    SetStatus(Tr("Changes are ready. Save PDF copy writes them to a new file."));
    return true;
}

void BookmarkWnd::SelectRow() {
    int next = list->GetCurrentSelection();
    if (next == selected) return;
    if (!ApplyRow()) {
        list->SetCurrentSelection(selected);
        return;
    }
    selected = next;
    list->SetCurrentSelection(next);
    FillRow();
}

void BookmarkWnd::OnAdd(VirtMouseEvent*) {
    if (!ready || job || !ApplyRow()) return;
    auto row = new BookmarkRow();
    row->title = str::Dup(Tr("New bookmark"));
    row->page = 1;
    row->level = selected >= 0 ? rows[selected]->level : 0;
    row->retarget = true;
    int index = selected >= 0 ? SubtreeEnd(selected) : len(rows);
    VecInsertAt(rows, index, row);
    RefreshList(index);
    EditSetFocus(title);
}

void BookmarkWnd::OnRemove(VirtMouseEvent*) {
    if (!ready || job || selected < 0) return;
    int end = SubtreeEnd(selected);
    if (end > selected + 1 && MessageBoxW(hwnd, L"Remove this bookmark and its children from the list?",
                                          L"Remove bookmarks", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    for (int i = selected; i < end; i++) delete rows[i];
    VecRemoveAtN(rows, selected, end - selected);
    RefreshList(std::min(selected, len(rows) - 1));
}

// Rotate two complete adjacent subtrees; no action or style fields are rewritten.
static void SwapSubtrees(Vec<BookmarkRow*>& rows, int start, int middle, int end) {
    Vec<BookmarkRow*> moved;
    for (int i = middle; i < end; i++) VecAppend(moved, rows[i]);
    for (int i = start; i < middle; i++) VecAppend(moved, rows[i]);
    for (int i = 0; i < len(moved); i++) rows[start + i] = moved[i];
}

void BookmarkWnd::OnUp(VirtMouseEvent*) {
    if (!ready || job || selected <= 0 || !ApplyRow()) return;
    int previous = selected - 1;
    while (previous >= 0 && rows[previous]->level > rows[selected]->level) previous--;
    if (previous < 0 || rows[previous]->level != rows[selected]->level) return;
    SwapSubtrees(rows, previous, selected, SubtreeEnd(selected));
    RefreshList(previous);
}

void BookmarkWnd::OnDown(VirtMouseEvent*) {
    if (!ready || job || selected < 0 || !ApplyRow()) return;
    int next = SubtreeEnd(selected);
    if (next == len(rows) || rows[next]->level != rows[selected]->level) return;
    int end = SubtreeEnd(next);
    int index = selected + end - next;
    SwapSubtrees(rows, selected, next, end);
    RefreshList(index);
}

void BookmarkWnd::OnDpi(WindowBase::DpiChangedEvent* ev) {
    DpiScope scope(hwnd);
    PlatformFont* font = GetAppFontForDpi((int)ev->dpiX);
    SetFont(font);
    list->font = font;
    title->SetFont(font);
    page->SetFont(font);
    level->SetFont(font);
    opened->SetFont(font);
    for (auto label : labels) label->font = font;
    help->font = font;
    help->layoutDx = -1;
    for (auto button : editButtons) button->font = font;
    status->font = font;
    status->layoutDx = -1;
    scroll->lineDy = PlatformFontLineHeight(font) + UiScalePx(8);
    save->font = font;
    cancel->font = font;
    DoLayout();
}

bool BookmarkWnd::Create(MainWindow* win, bool visible) {
    HWND owner = win ? win->hwndFrame : nullptr;
    PlatformFont* font = GetAppFontForDpi(DpiGetForHwnd(owner));
    CreateCustomArgs args;
    args.owner = owner;
    args.title = Tr("Edit PDF bookmarks");
    args.visible = false;
    args.style = WS_OVERLAPPEDWINDOW;
    args.font = font;
    args.icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
    onWndProc = MkMethod1<BookmarkWnd, WindowBase::WndProcEvent*, &BookmarkWnd::OnMessage>(this);
    CreateCustom(args);
    if (!hwnd) return false;
    DpiScope scope(hwnd);
    auto* column = new VBox();
    column->alignCross = CrossAxisAlign::Stretch;
    column->gap = UiScalePx(8);
    help = new VirtRichText();
    help->font = font;
    help->AddPlainText(Tr(
        "Edit titles, pages and nesting. Level 0 is top level. Existing bookmark styles and link actions are "
        "preserved. Changing a page replaces its destination with Fit page. Page 0 means a heading or a link action."));
    column->AddChild(help);
    auto addLabel = [&](VBox* parent, Str text) {
        auto* label = NewVirtText({.s = text, .font = font});
        VecAppend(labels, label);
        parent->AddChild(label);
        return label;
    };
    list = new VirtListBox();
    list->font = font;
    list->dpi = GetDpi();
    list->idealSizeLines = 8;
    model = new ListBoxModelStrings();
    list->SetModel(model);
    list->onSelectionChanged = MkMethod0<BookmarkWnd, &BookmarkWnd::SelectRow>(this);
    column->AddChild(list);
    title = new Edit();
    title->Create({.parent = hwnd, .isMultiLine = true, .withBorder = true, .idealSizeLines = 2, .font = font});
    SendMessageW(title->hwnd, EM_SETLIMITTEXT, 8192, 0);
    addLabel(column, Tr("Title:"));
    column->AddChild(title);
    auto* fields = new Wrap();
    fields->alignCross = CrossAxisAlign::CrossCenter;
    fields->colGap = fields->rowGap = UiScalePx(8);
    auto* pageGroup = new HBox();
    pageGroup->alignCross = CrossAxisAlign::CrossCenter;
    pageGroup->gap = UiScalePx(4);
    auto* pageLabel = NewVirtText({.s = Tr("Page:"), .font = font});
    VecAppend(labels, pageLabel);
    pageGroup->AddChild(pageLabel);
    page = new Edit();
    page->Create({.parent = hwnd, .withBorder = true, .numbersOnly = true, .idealWidthChars = 5, .font = font});
    pageGroup->AddChild(page);
    fields->AddChild(pageGroup);
    auto* levelGroup = new HBox();
    levelGroup->alignCross = CrossAxisAlign::CrossCenter;
    levelGroup->gap = UiScalePx(4);
    auto* levelLabel = NewVirtText({.s = Tr("Level:"), .font = font});
    VecAppend(labels, levelLabel);
    levelGroup->AddChild(levelLabel);
    level = new Edit();
    level->Create({.parent = hwnd, .withBorder = true, .numbersOnly = true, .idealWidthChars = 4, .font = font});
    levelGroup->AddChild(level);
    fields->AddChild(levelGroup);
    opened = new Checkbox();
    opened->Create({.parent = hwnd, .text = Tr("Children initially expanded"), .font = font});
    fields->AddChild(opened);
    column->AddChild(fields);
    auto* editActions = new Wrap();
    editActions->colGap = editActions->rowGap = UiScalePx(8);
    auto addAction = [&](Str text, const Func1<VirtMouseEvent*>& fn) {
        auto* button = NewThemedButton(hwnd, text, font, false);
        button->onClick = fn;
        VecAppend(editButtons, button);
        editActions->AddChild(button);
    };
    addAction(Tr("Apply"), MkMethod1<BookmarkWnd, VirtMouseEvent*, &BookmarkWnd::OnApply>(this));
    addAction(Tr("Add"), MkMethod1<BookmarkWnd, VirtMouseEvent*, &BookmarkWnd::OnAdd>(this));
    addAction(Tr("Remove"), MkMethod1<BookmarkWnd, VirtMouseEvent*, &BookmarkWnd::OnRemove>(this));
    addAction(Tr("Move up"), MkMethod1<BookmarkWnd, VirtMouseEvent*, &BookmarkWnd::OnUp>(this));
    addAction(Tr("Move down"), MkMethod1<BookmarkWnd, VirtMouseEvent*, &BookmarkWnd::OnDown>(this));
    column->AddChild(editActions);
    status = new VirtRichText();
    status->font = font;
    status->AddPlainText(Tr("Reading PDF bookmarks…"));
    column->AddChild(status);
    auto* root = new VBox();
    root->alignCross = CrossAxisAlign::Stretch;
    root->gap = UiScalePx(8);
    scroll = new BookmarkViewport(column);
    scroll->lineDy = PlatformFontLineHeight(font) + UiScalePx(8);
    root->AddChild(scroll, 1);
    auto* actions = new Wrap();
    actions->colGap = actions->rowGap = UiScalePx(8);
    cancel = NewThemedButton(hwnd, Tr("Close"), font, false);
    cancel->onClick = MkMethod1<BookmarkWnd, VirtMouseEvent*, &BookmarkWnd::OnCancel>(this);
    save = NewThemedButton(hwnd, Tr("Save PDF copy…"), font, true);
    save->onClick = MkMethod1<BookmarkWnd, VirtMouseEvent*, &BookmarkWnd::OnSave>(this);
    actions->AddChild(cancel);
    actions->AddChild(save);
    root->AddChild(actions);
    layout = new Padding(root, Insets{UiScalePx(12), UiScalePx(12), UiScalePx(12), UiScalePx(12)});
    for (HWND control : {title->hwnd, page->hwnd, level->hwnd, opened->hwnd})
        SetWindowSubclass(control, BookmarkFocusProc, 1, (DWORD_PTR)this);
    SetWindowLongPtrW(opened->hwnd, GWL_STYLE, GetWindowLongPtrW(opened->hwnd, GWL_STYLE) | BS_MULTILINE);
    Rect work = GetWorkAreaRect(HwndWindowRect(hwnd), hwnd);
    Size desired{std::min(UiScalePx(800), std::max(1, work.dx - UiScalePx(24))),
                 std::min(UiScalePx(650), std::max(1, work.dy - UiScalePx(24)))};
    HwndResizeClientSize(hwnd, desired.dx, desired.dy);
    DoLayout();
    if (owner) HwndCenterDialog(hwnd, owner);
    UpdateTheme();
    SetIsVisible(visible);
    return true;
}

static void CloseBookmarkWnd(WindowBase::CloseEvent* ev) {
    ev->e->didHandle = true;
    if (gBookmarkWnd) gBookmarkWnd->OnCancel(nullptr);
}

static void ClearBookmarkWnd() {
    gBookmarkWnd = nullptr;
}

bool CanEditPdfBookmarks(MainWindow* win) {
    if (!HasPermission(Perm::DiskAccess) || !IsMainWindowValidAndNotClosing(win) || !win->IsDocLoaded()) return false;
    DisplayModel* model = win->AsFixed();
    EngineBase* engine = model ? model->GetEngine() : nullptr;
    return engine && EngineMupdfIsPdf(engine) && file::Exists(win->ctrl->GetFilePath());
}

static bool ChooseCpdf(HWND owner, BookmarkSession* session, bool& download) {
    if (VerifyCpdf(CpdfCachePath())) {
        session->executable = str::Dup(CpdfCachePath());
        session->official = true;
        return true;
    }
    TASKDIALOG_BUTTON buttons[] = {{100, L"Download verified Windows64 cpdf"}, {101, L"Choose an existing cpdf.exe…"}};
    Str message =
        Tr("Bookmark editing uses the optional Coherent PDF tool.\n\n"
           "Download: 11.18 MiB from the official cpdf-binaries repository on GitHub. "
           "License: AGPL v3, with commercial licensing available from Coherent Graphics.\n\n"
           "The tool stays in your data folder. The official Windows download is x64; "
           "there is no native ARM64 version. You can choose a compatible executable you already trust.");
    WStr content = ToWStrTemp(message);
    WStr details =
        ToWStrTemp(fmt("Revision: %s\nSHA-256: %s\nExact size: 11,724,417 bytes\n\n"
                       "License: https://github.com/coherentgraphics/cpdf-binaries/blob/%s/LICENSE.md\n"
                       "The download is verified before it runs. No installer or system registration runs.",
                       kCpdfRevision, kCpdfSha256, kCpdfRevision));
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = owner;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = L"Optional PDF bookmark tool";
    config.pszMainInstruction = L"Choose how to use cpdf";
    config.pszContent = content.s;
    config.pszExpandedInformation = details.s;
    config.pszExpandedControlText = L"Download and license details";
    config.pszCollapsedControlText = L"Hide details";
    config.cButtons = dimof(buttons);
    config.pButtons = buttons;
    config.nDefaultButton = IDCANCEL;
    int selected = IDCANCEL;
    TaskDialogIndirect(&config, &selected, nullptr, nullptr);
    if (selected == 100) {
        if (!HasPermission(Perm::InternetAccess)) {
            MessageBoxW(owner, L"Internet access is disabled. Choose an existing cpdf executable instead.",
                        L"Download unavailable", MB_OK | MB_ICONINFORMATION);
            return false;
        }
        session->executable = str::Dup(CpdfCachePath());
        session->official = true;
        download = true;
        return true;
    }
    if (selected != 101) return false;
    WCHAR filename[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = dimof(filename);
    ofn.lpstrFilter = L"cpdf executable\0*.exe\0\0";
    ofn.lpstrTitle = L"Choose cpdf.exe you trust";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    session->executable = str::Dup(ToUtf8Temp(WStr(filename)));
    return true;
}

void ShowCpdfBookmarks(MainWindow* win) {
    if (!CanEditPdfBookmarks(win)) return;
    if (gBookmarkWnd) {
        HwndSetFocus(gBookmarkWnd->hwnd);
        return;
    }
    EngineBase* engine = win->AsFixed()->GetEngine();
    if (EngineHasUnsavedAnnotations(engine)) {
        MessageBoxW(win->hwndFrame,
                    L"Save your PDF annotation changes before editing bookmarks. The bookmark editor reads the saved "
                    L"file and never replaces your open document.",
                    L"Save PDF changes first", MB_OK | MB_ICONINFORMATION);
        return;
    }
    auto session = new BookmarkSession();
    session->original = str::Dup(win->ctrl->GetFilePath());
    session->pages = win->ctrl->PageCount();
    bool download = false;
    if (!ChooseCpdf(win->hwndFrame, session, download)) {
        ReleaseSession(session);
        return;
    }
    TempStr temp = GetTempFilePathTemp(StrL("cpb"));
    file::Delete(temp);
    if (!dir::Create(temp)) {
        ReleaseSession(session);
        MessageBoxW(win->hwndFrame, L"Unable to create a temporary folder. Check available disk space.",
                    L"Bookmark editor", MB_OK | MB_ICONERROR);
        return;
    }
    session->snapshotDir = str::Dup(temp);
    session->snapshot = path::Join(temp, StrL("original.pdf"));
    auto window = new BookmarkWnd();
    window->session = session;
    window->closeOnEsc = true;
    window->onClose = MkFunc1Void(CloseBookmarkWnd);
    window->onBeforeDelete = MkFunc0Void(ClearBookmarkWnd);
    window->onDpiChanged = MkMethod1<BookmarkWnd, WindowBase::DpiChangedEvent*, &BookmarkWnd::OnDpi>(window);
    if (!window->Create(win)) {
        delete window;
        return;
    }
    gBookmarkWnd = window;
    window->Start(BookmarkAction::Load, download);
    RunModalWindow(window->hwnd, win->hwndFrame);
}

#if defined(DEBUG)
static bool BookmarkSnapshots() {
    WCHAR folder[1024]{};
    DWORD length = GetEnvironmentVariableW(L"SUMATRA_CPDF_SNAPSHOTS", folder, dimof(folder));
    if (!length) return true;
    if (length >= dimof(folder) || !dir::CreateAll(ToUtf8Temp(folder))) return false;
    Settings* originalSettings = gSettings;
    gSettings = NewSettings({});
    defer {
        DeleteSettings(gSettings);
        gSettings = originalSettings;
        RefreshUiFonts();
    };
    bool ok = true;
    for (int variant = 0; variant < 2; variant++) {
        gSettings->uIFontSize = variant ? 28 : 0;
        gSettings->interfaceScale = variant ? 150 : 100;
        RefreshUiFonts();
        auto* window = new BookmarkWnd();
        window->session = new BookmarkSession();
        window->session->pages = 10;
        if (!window->Create(nullptr, false)) {
            delete window;
            return false;
        }
        ok &= !IsWindowVisible(window->hwnd);
        if (variant) HwndResizeClientSize(window->hwnd, DpiScale(640), DpiScale(760));
        auto* row = new BookmarkRow();
        row->title = str::Dup(StrL("Introduction and reading notes"));
        row->page = 1;
        row->level = 0;
        VecAppend(window->rows, row);
        window->ready = true;
        window->RefreshList(0);
        window->SetBusy(false);
        ok &= ValidRows(window->rows, window->session->pages);
        window->title->SetText(StrL("Edited chapter"));
        ok &= window->ApplyRow() && str::Eq(window->rows[0]->title, StrL("Edited chapter"));
        window->OnAdd(nullptr);
        ok &= len(window->rows) == 2 && window->selected == 1;
        window->OnUp(nullptr);
        ok &= window->selected == 0 && str::Eq(window->rows[1]->title, StrL("Edited chapter"));
        window->OnDown(nullptr);
        ok &= window->selected == 1 && str::Eq(window->rows[0]->title, StrL("Edited chapter"));
        window->OnRemove(nullptr);
        ok &= len(window->rows) == 1 && ValidRows(window->rows, window->session->pages);
        window->title->SetText(StrL("Introduction and reading notes"));
        ok &= window->ApplyRow();
        window->SetStatus(StrL("Changes are ready. Save PDF copy writes them to a new file."));
        Size size = HwndClientRect(window->hwnd).Size();
        ok &= window->save->lastBounds.Bottom() <= size.dy && window->save->lastBounds.Right() <= size.dx;
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), size.dx, -size.dy, 1, 32, BI_RGB};
        void* pixels = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels) {
            delete window;
            DeleteDC(dc);
            return false;
        }
        HGDIOBJ previous = SelectObject(dc, bitmap);
        SendMessageW(window->hwnd, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
        for (HWND control : {window->title->hwnd, window->page->hwnd, window->level->hwnd, window->opened->hwnd}) {
            Rect bounds = ChildPosWithinParent(control);
            Rect visible = bounds.Intersect(window->scroll->lastBounds);
            int state = SaveDC(dc);
            IntersectClipRect(dc, visible.x, visible.y, visible.Right(), visible.Bottom());
            SetViewportOrgEx(dc, bounds.x, bounds.y, nullptr);
            SendMessageW(control, WM_PRINT, (WPARAM)dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
            RestoreDC(dc, state);
        }
        GdiFlush();
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
        header.bfSize = header.bfOffBits + size.dx * size.dy * 4;
        str::Builder data;
        data.Append(Str((const char*)&header, sizeof(header)));
        data.Append(Str((const char*)&info.bmiHeader, sizeof(info.bmiHeader)));
        data.Append(Str((const char*)pixels, size.dx * size.dy * 4));
        ok &= file::WriteFile(path::JoinTemp(ToUtf8Temp(folder), fmt("bookmarks-%d.bmp", variant)), ToStr(data));
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        DestroyWindow(window->hwnd);
        delete window;
    }
    return ok;
}

static BookmarkSession* MakeTestSession(Str executable) {
    auto session = new BookmarkSession();
    Str temp = GetTempFilePathTemp(StrL("cbt"));
    file::Delete(temp);
    session->snapshotDir = str::Dup(temp);
    if (!dir::Create(temp)) {
        ReleaseSession(session);
        return nullptr;
    }
    session->snapshot = path::Join(temp, StrL("snapshot.pdf"));
    session->original = path::Join(temp, StrL("fixture.pdf"));
    session->executable = str::Dup(executable);
    session->pages = 1;
    Str objects[] = {
        StrL("<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>"),
        StrL("<< /Type /Pages /Count 1 /Kids [3 0 R] >>"),
        StrL("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>"),
        StrL("<< /Type /Outlines /First 5 0 R /Last 6 0 R /Count 2 >>"),
        StrL("<< /Title (Styled destination) /Parent 4 0 R /Next 6 0 R /Dest [3 0 R /XYZ 20 600 null] /C [1 0 0] /F 3 "
             ">>"),
        StrL("<< /Title (Web link) /Parent 4 0 R /Prev 5 0 R /A << /S /URI /URI (https://example.org/bookmark) >> >>")};
    str::Builder pdf;
    pdf.Append(StrL("%PDF-1.4\n"));
    Vec<int> offsets;
    for (int i = 0; i < dimof(objects); i++) {
        VecAppend(offsets, len(pdf));
        pdf.Append(fmt("%d 0 obj\n%s\nendobj\n", i + 1, objects[i]));
    }
    int xref = len(pdf);
    pdf.Append(StrL("xref\n0 7\n0000000000 65535 f \n"));
    for (int offset : offsets) pdf.Append(fmt("%010d 00000 n \n", offset));
    pdf.Append(fmt("trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n", xref));
    if (!file::WriteFile(session->original, ToStr(pdf))) {
        ReleaseSession(session);
        return nullptr;
    }
    return session;
}

static bool CpdfRoundtripTests() {
    WCHAR executable[32768]{};
    DWORD size = GetEnvironmentVariableW(L"SUMATRA_CPDF_TEST_EXE", executable, dimof(executable));
    if (!size) return true; // Opt-in local executable only; tests never download.
    if (size >= dimof(executable)) return false;
    auto session = MakeTestSession(ToUtf8Temp(WStr(executable)));
    if (!session) return false;
    defer {
        ReleaseSession(session);
    };
    Str original = file::ReadFile(session->original);
    defer {
        str::Free(original);
    };
    BookmarkJob load;
    load.session = session;
    InterlockedIncrement(&session->references);
    ExecuteBookmarkJob(&load);
    Vec<BookmarkRow*> rows;
    defer {
        DeleteRows(rows);
    };
    bool ok = load.success && ParseRows(load.text, 1, rows) && len(rows) == 2;
    if (!ok) return false;
    ok &= rows[1]->page == 0;
    str::ReplaceWithCopy(&rows[0]->title, StrL("Edited title"));
    BookmarkJob save;
    save.session = session;
    InterlockedIncrement(&session->references);
    save.action = BookmarkAction::Save;
    save.text = str::Dup(SerializeRows(rows));
    save.target = path::Join(session->snapshotDir, StrL("edited.pdf"));
    ExecuteBookmarkJob(&save);
    ok &= save.success;
    Str after = file::ReadFile(session->original);
    ok &= str::Eq(original, after);
    str::Free(after);
    if (!save.success) return false;
    StrVec args;
    args.Append(StrL("-list-bookmarks-json"));
    args.Append(save.target);
    args.Append(StrL("-preserve-actions"));
    Str listed;
    ok &= RunCpdf(&load, args, listed);
    Vec<BookmarkRow*> output;
    ok &= ParseRows(listed, 1, output) && len(output) == 2;
    if (len(output) == 2) {
        ok &= str::Eq(output[0]->title, StrL("Edited title")) && output[1]->page == 0;
        ok &= str::Contains(listed, StrL("/XYZ")) && str::Contains(listed, StrL("600"));
        ok &= str::Contains(listed, StrL("example.org/bookmark"));
        ok &= str::Contains(listed, StrL("\"bold\": true")) && str::Contains(listed, StrL("\"italic\": true"));
    }
    DeleteRows(output);
    str::Free(listed);
    // Saving to an existing path must fail without changing either file.
    ExecuteBookmarkJob(&save);
    ok &= !save.success;
    BookmarkJob empty;
    empty.session = session;
    InterlockedIncrement(&session->references);
    empty.action = BookmarkAction::Save;
    empty.text = str::Dup(StrL("[]"));
    empty.target = path::Join(session->snapshotDir, StrL("empty.pdf"));
    ExecuteBookmarkJob(&empty);
    ok &= empty.success;
    SetEvent(empty.cancel);
    file::Delete(empty.target);
    ExecuteBookmarkJob(&empty);
    ok &= !empty.success && !file::Exists(empty.target);
    return ok;
}

bool CpdfBookmarks_UnitTests() {
    Vec<BookmarkRow*> rows;
    Str source = StrL(
        "[{\"level\":0,\"text\":\"序章\",\"page\":1,\"open\":false,\"target\":[{\"I\":1},{\"N\":\"/"
        "XYZ\"},{\"F\":20},{\"F\":600},null],\"colour\":[1,0,0],\"bold\":true,\"italic\":true},{\"level\":1,\"text\":"
        "\"Link\",\"page\":0,\"target\":{\"custom\":\"URI\"}}]");
    bool ok = ParseRows(source, 3, rows) && len(rows) == 2;
    if (len(rows) == 2) {
        str::ReplaceWithCopy(&rows[0]->title, StrL("New \"title\""));
        Str serialized = SerializeRows(rows);
        ok &= json::Parse(serialized, {}) && str::Contains(serialized, StrL("\"colour\":[1,0,0]")) &&
              str::Contains(serialized, StrL("\"custom\":\"URI\""));
        rows[0]->retarget = true;
        ok &= !str::Contains(SerializeRows(rows), StrL("/XYZ")) && str::Contains(SerializeRows(rows), StrL("/Fit"));
    }
    DeleteRows(rows);
    ok &= ParseRows(StrL("[]"), 3, rows);
    for (Str bad :
         {StrL("{}"), StrL("[{\"level\":1,\"text\":\"bad\",\"page\":1}]"), StrL("[{\"level\":0,\"page\":4}]"),
          StrL("[{\"level\":0,\"page\":1.5}]"), StrL("[{\"level\":0,\"page\":-1}]"), StrL("[null]"), StrL("[{]")}) {
        ok &= !ParseRows(bad, 3, rows);
        DeleteRows(rows);
    }
    str::Builder quoted;
    AppendArg(quoted, StrL("C:\\a b\\"));
    AppendArg(quoted, StrL("a\"b"));
    ok &= str::Eq(ToStr(quoted), StrL("\"C:\\a b\\\\\" \"a\\\"b\""));
    return ok && CpdfRoundtripTests() && BookmarkSnapshots();
}
#endif
