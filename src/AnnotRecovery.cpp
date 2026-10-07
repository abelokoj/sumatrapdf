/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/DirScan.h"
#include "base/GuessFileType.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif

#include <wincrypt.h>

extern "C" {
#include <mupdf/pdf.h>
}

#include "gui/UIModels.h"
#include "Settings.h"
#include "Annotation.h"
#include "DisplayMode.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "EngineMupdf.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "WindowTab.h"
#include "AnnotRecovery.h"
#include "SumatraLog.h"

constexpr DWORD kRecoveryQuietMs = 2000;
constexpr DWORD kRecoveryMaxDelayMs = 15000;
constexpr u32 kRecoveryMagic = 0x52504553;
constexpr u32 kRecoveryVersion = 1;
constexpr u32 kRecoveryHasDigest = 1;

struct RecoveryHeader {
    u32 magic = kRecoveryMagic;
    u32 version = kRecoveryVersion;
    i64 sourceSize = -1;
    FILETIME sourceTime{};
    u64 revision = 0;
    u8 digest[32]{};
    u32 pathBytes = 0;
    u32 flags = 0;
};
static_assert(sizeof(RecoveryHeader) == 72);

struct RecoverySession {
    EngineBase* engine = nullptr;
    Str source;
    Str base;
    RecoveryHeader header;
    HANDLE wake = nullptr;
    HANDLE thread = nullptr;
    HANDLE lock = INVALID_HANDLE_VALUE;
    u64 revision = 1;
    u64 savedRevision = 0;
    DWORD changedAt = 0;
    bool stop = false;
    bool discard = false;
    bool finished = false;
    bool failed = false;
};

static Mutex gRecoveryMutex;
static Vec<RecoverySession*> gRecoverySessions;
static u32 gRecoverySerial = 0;

static TempStr RecoveryDir() {
    return path::JoinTemp(GetAppDataDirTemp(), StrL("annotation-recovery"));
}

static TempStr ManifestPath(Str base) {
    return str::JoinTemp(base, StrL(".manifest"));
}

static TempStr SnapshotPath(Str base, u64 revision) {
    return fmt("%s-%llu.pdf", base, revision);
}

static bool HashSource(Str path, u8 digest[32]) {
    HANDLE sourceHandle = file::OpenReadOnly(path);
    if (sourceHandle == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE;
    if (ok) ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    u8 buffer[65536];
    while (ok) {
        DWORD read = 0;
        ok = ReadFile(sourceHandle, buffer, sizeof(buffer), &read, nullptr) != FALSE;
        if (!ok || read == 0) break;
        ok = CryptHashData(hash, buffer, read, 0) != FALSE;
    }
    DWORD bytes = 32;
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &bytes, 0) != FALSE && bytes == 32;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(sourceHandle);
    return ok;
}

static bool PromoteAtomic(Str temporary, Str destination) {
    HANDLE file = CreateFileW(CWStrTemp(temporary), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return ok && MoveFileExW(CWStrTemp(temporary), CWStrTemp(destination),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

static bool WriteManifest(Str base, const RecoveryHeader& header, Str source) {
    str::Builder data;
    data.Append(Str((const char*)&header, sizeof(header)));
    data.Append(source);
    TempStr temporary = str::JoinTemp(base, StrL(".manifest.partial"));
    bool ok = file::WriteFile(temporary, ToStr(data)) && PromoteAtomic(temporary, ManifestPath(base));
    if (!ok) file::Delete(temporary);
    return ok;
}

static bool ReadManifest(Str manifest, RecoveryHeader& header, Str& source) {
    Str data = file::ReadFile(manifest);
    bool ok = len(data) >= sizeof(header);
    if (ok) {
        memcpy(&header, data.s, sizeof(header));
        ok = header.magic == kRecoveryMagic && header.version == kRecoveryVersion && header.sourceSize >= 0 &&
             header.pathBytes > 0 && header.pathBytes <= 131072 && header.pathBytes == len(data) - sizeof(header) &&
             header.revision > 0 && (header.flags & ~kRecoveryHasDigest) == 0;
    }
    if (ok) {
        Str path{data.s + sizeof(header), (int)header.pathBytes};
        ok = memchr(path.s, 0, len(path)) == nullptr;
        if (ok) source = str::Dup(path);
    }
    str::Free(data);
    return ok;
}

static bool SessionIsActive(Str base) {
    TempStr lockPath = str::JoinTemp(base, StrL(".lock"));
    HANDLE lock =
        CreateFileW(CWStrTemp(lockPath), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return file::Exists(lockPath);
    CloseHandle(lock);
    return false;
}

static void RemoveSessionFiles(RecoverySession* session) {
    file::Delete(ManifestPath(session->base));
    file::Delete(SnapshotPath(session->base, session->header.revision));
    file::Delete(str::JoinTemp(session->base, StrL(".partial.pdf")));
    file::Delete(str::JoinTemp(session->base, StrL(".manifest.partial")));
}

static bool WriteSnapshot(RecoverySession* session, u64 revision) {
    auto* engine = AsEngineMupdf(session->engine);
    if (!engine) return false;
    RecoveryHeader header = session->header;
    header.revision = revision;
    header.pathBytes = (u32)len(session->source);
    if ((header.flags & kRecoveryHasDigest) == 0) {
        FILETIME before = file::GetModificationTime(session->source);
        if (file::GetSize(session->source) == header.sourceSize && FileTimeEq(before, header.sourceTime) &&
            HashSource(session->source, header.digest) &&
            FileTimeEq(before, file::GetModificationTime(session->source)) &&
            file::GetSize(session->source) == header.sourceSize)
            header.flags |= kRecoveryHasDigest;
    }
    TempStr temporary = str::JoinTemp(session->base, StrL(".partial.pdf"));
    bool ok = false;
    {
        AutoUnlockRecursiveMutex lock(&engine->docLock);
        if (engine->journalNesting > 0) return false;
        ok = EngineMupdfSaveRecoverySnapshot(engine, temporary);
    }
    AutoUnlockMutex lock(&gRecoveryMutex);
    if ((session->stop && session->discard) || !ok) {
        file::Delete(temporary);
        return false;
    }
    TempStr snapshot = SnapshotPath(session->base, revision);
    ok = PromoteAtomic(temporary, snapshot) && WriteManifest(session->base, header, session->source);
    if (ok) {
        u64 previous = session->header.revision;
        session->header = header;
        session->savedRevision = revision;
        if (previous != revision) file::Delete(SnapshotPath(session->base, previous));
    } else {
        file::Delete(temporary);
        file::Delete(snapshot);
    }
    return ok;
}

static DWORD WINAPI RecoveryWorker(void* parameter) {
    auto* session = (RecoverySession*)parameter;
    DWORD pendingSince = GetTickCount();
    for (;;) {
        WaitForSingleObject(session->wake, kRecoveryQuietMs);
        u64 revision;
        bool finalSnapshot = false;
        {
            AutoUnlockMutex lock(&gRecoveryMutex);
            if (session->stop) {
                if (session->discard || session->revision == session->savedRevision) break;
                finalSnapshot = true;
            }
            if (session->revision == session->savedRevision) {
                pendingSince = GetTickCount();
                continue;
            }
            DWORD now = GetTickCount();
            if (!finalSnapshot && now - session->changedAt < kRecoveryQuietMs &&
                now - pendingSince < kRecoveryMaxDelayMs)
                continue;
            revision = session->revision;
        }
        bool ok = WriteSnapshot(session, revision);
        {
            AutoUnlockMutex lock(&gRecoveryMutex);
            session->failed = !ok && !session->stop;
        }
        if (!ok) logf("Annotation recovery snapshot failed: %s\n", session->source);
        if (finalSnapshot) break;
        pendingSince = GetTickCount();
    }
    bool discard;
    EngineBase* engine;
    {
        AutoUnlockMutex lock(&gRecoveryMutex);
        discard = session->discard;
        engine = session->engine;
        session->engine = nullptr;
    }
    if (discard) RemoveSessionFiles(session);
    if (session->lock != INVALID_HANDLE_VALUE) CloseHandle(session->lock);
    file::Delete(str::JoinTemp(session->base, StrL(".lock")));
    SafeEngineRelease(&engine);
    {
        AutoUnlockMutex lock(&gRecoveryMutex);
        session->finished = true;
    }
    return 0;
}

static void FreeFinishedSessions() {
    for (int i = len(gRecoverySessions) - 1; i >= 0; i--) {
        auto* session = gRecoverySessions[i];
        if (!session->finished) continue;
        CloseHandle(session->thread);
        CloseHandle(session->wake);
        str::Free(session->source);
        str::Free(session->base);
        delete session;
        VecRemoveAt(gRecoverySessions, i);
    }
}

void AnnotRecoveryChanged(WindowTab* tab) {
    if (gDontSaveSettings || !tab) return;
    EngineBase* engine = tab->GetEngine();
    auto* pdf = AsEngineMupdf(engine);
    if (!pdf || !pdf->pdfdoc || !EngineMupdfHasUnsavedAnnotations(engine) || len(tab->filePath) == 0) return;
    AutoUnlockMutex lock(&gRecoveryMutex);
    FreeFinishedSessions();
    for (auto* session : gRecoverySessions) {
        if (session->engine != engine || session->stop) continue;
        session->revision++;
        session->changedAt = GetTickCount();
        SetEvent(session->wake);
        return;
    }
    TempStr directory = RecoveryDir();
    if (!dir::CreateAll(directory)) return;
    auto* session = new RecoverySession;
    session->source = str::Dup(path::NormalizeTemp(tab->filePath));
    session->base = str::Dup(
        path::JoinTemp(directory, fmt("%u-%llu-%u", GetCurrentProcessId(), GetTickCount64(), ++gRecoverySerial)));
    session->header.sourceSize = pdf->fileSizeAtLoad;
    session->header.sourceTime = pdf->fileTimeAtLoad;
    session->changedAt = GetTickCount();
    session->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    session->lock = CreateFileW(CWStrTemp(str::JoinTemp(session->base, StrL(".lock"))), GENERIC_READ | GENERIC_WRITE, 0,
                                nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    session->engine = engine;
    engine->AddRef();
    if (session->wake && session->lock != INVALID_HANDLE_VALUE)
        session->thread = CreateThread(nullptr, 0, RecoveryWorker, session, 0, nullptr);
    if (!session->thread) {
        if (session->wake) CloseHandle(session->wake);
        if (session->lock != INVALID_HANDLE_VALUE) CloseHandle(session->lock);
        SafeEngineRelease(&session->engine);
        file::Delete(str::JoinTemp(session->base, StrL(".lock")));
        str::Free(session->source);
        str::Free(session->base);
        delete session;
        return;
    }
    VecAppend(gRecoverySessions, session);
}

void AnnotRecoveryForget(WindowTab* tab) {
    if (!tab) return;
    AutoUnlockMutex lock(&gRecoveryMutex);
    for (auto* session : gRecoverySessions) {
        if (session->engine != tab->GetEngine()) continue;
        session->stop = session->discard = true;
        SetEvent(session->wake);
    }
    FreeFinishedSessions();
}

void AnnotRecoveryClose(WindowTab* tab) {
    if (!tab) return;
    AutoUnlockMutex lock(&gRecoveryMutex);
    for (auto* session : gRecoverySessions) {
        if (session->engine != tab->GetEngine()) continue;
        session->stop = true;
        SetEvent(session->wake);
    }
    FreeFinishedSessions();
}

void AnnotRecoveryShutdown() {
    Vec<HANDLE> threads;
    {
        AutoUnlockMutex lock(&gRecoveryMutex);
        for (auto* session : gRecoverySessions) {
            session->stop = true;
            SetEvent(session->wake);
            VecAppend(threads, session->thread);
        }
    }
    for (HANDLE thread : threads) WaitForSingleObject(thread, INFINITE);
    AutoUnlockMutex lock(&gRecoveryMutex);
    FreeFinishedSessions();
    VecReset(gRecoverySessions);
}

static TempStr FindRecoveryIn(Str directory, Str source, bool& changedSource) {
    changedSource = false;
    Str best;
    bool bestMatches = false;
    FILETIME bestTime{};
    u8 digest[32]{};
    bool hashed = false, validHash = false;
    i64 size = file::GetSize(source);
    DirIter files(directory);
    for (auto* entry : files) {
        if (!str::EndsWithI(entry->name, StrL(".manifest"))) continue;
        RecoveryHeader header;
        Str original;
        if (!ReadManifest(entry->filePath, header, original)) continue;
        Str base{entry->filePath.s, len(entry->filePath) - (int)sizeof(".manifest") + 1};
        bool samePath = path::IsSame(source, original);
        str::Free(original);
        if (SessionIsActive(base)) continue;
        if (!samePath && (size != header.sourceSize || (header.flags & kRecoveryHasDigest) == 0)) continue;
        if (!hashed) {
            validHash = HashSource(source, digest);
            hashed = true;
        }
        bool matches = validHash && (header.flags & kRecoveryHasDigest) != 0 && size == header.sourceSize &&
                       memcmp(digest, header.digest, sizeof(digest)) == 0;
        if (!samePath && !matches) continue;
        TempStr snapshot = SnapshotPath(base, header.revision);
        if (!file::Exists(snapshot) || file::GetSize(snapshot) <= 0) continue;
        if (best && (bestMatches ? !matches : false)) continue;
        if (best && bestMatches == matches && CompareFileTime(&bestTime, &entry->modificationTime) >= 0) continue;
        str::ReplaceWithCopy(&best, snapshot);
        bestTime = entry->modificationTime;
        bestMatches = matches;
    }
    TempStr result = str::DupTemp(best);
    changedSource = best && !bestMatches;
    str::Free(best);
    return result;
}

TempStr AnnotRecoveryFind(Str source, bool& changedSource) {
    changedSource = false;
    if (gDontSaveSettings || len(source) == 0) return {};
    return FindRecoveryIn(RecoveryDir(), source, changedSource);
}

bool AnnotRecoveryDiscard(Str snapshot) {
    TempStr directory = RecoveryDir();
    if (!path::IsSame(path::GetDirTemp(snapshot), directory)) return false;
    DirIter files(directory);
    for (auto* entry : files) {
        if (!str::EndsWithI(entry->name, StrL(".manifest"))) continue;
        RecoveryHeader header;
        Str original;
        if (!ReadManifest(entry->filePath, header, original)) continue;
        str::Free(original);
        Str base{entry->filePath.s, len(entry->filePath) - (int)sizeof(".manifest") + 1};
        if (!path::IsSame(snapshot, SnapshotPath(base, header.revision)) || SessionIsActive(base)) continue;
        bool removed = file::Delete(snapshot);
        if (removed) file::Delete(entry->filePath);
        return removed;
    }
    return false;
}

TempStr AnnotRecoveryStatus(WindowTab* tab) {
    if (!tab) return {};
    AutoUnlockMutex lock(&gRecoveryMutex);
    for (auto* session : gRecoverySessions) {
        if (session->engine != tab->GetEngine() || session->stop) continue;
        if (session->failed) return StrL("Recovery backup failed");
        return session->revision == session->savedRevision ? StrL("Recovery backup saved")
                                                           : StrL("Saving recovery backup");
    }
    return {};
}

#if IS_DEBUG
static bool TestRecoverySnapshot(Str directory) {
    const char* objects[] = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
    };
    str::Builder pdf;
    pdf.Append(StrL("%PDF-1.4\n"));
    Vec<int> offsets;
    for (int i = 0; i < dimof(objects); i++) {
        VecAppend(offsets, len(pdf));
        pdf.Append(fmt("%d 0 obj\n%s\nendobj\n", i + 1, Str(objects[i])));
    }
    int xref = len(pdf);
    pdf.Append(fmt("xref\n0 %d\n0000000000 65535 f \n", dimof(objects) + 1));
    for (int offset : offsets) pdf.Append(fmt("%010d 00000 n \n", offset));
    pdf.Append(fmt("trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n", dimof(objects) + 1, xref));
    TempStr source = path::JoinTemp(directory, StrL("real-source.pdf"));
    bool ok = file::WriteFile(source, ToStr(pdf));
    EngineBase* engine = CreateEngineMupdfFromData(ToStr(pdf), source, nullptr);
    if (!engine) return false;
    RecoverySession session;
    session.engine = engine;
    session.source = source;
    session.base = path::JoinTemp(directory, StrL("real-session"));
    session.header.sourceSize = file::GetSize(source);
    session.header.sourceTime = file::GetModificationTime(source);
    Vec<int> counts;
    Vec<PointF> points;
    VecAppend(counts, 2);
    VecAppend(points, {10, 10});
    VecAppend(points, {50, 50});
    AnnotCreateArgs args{AnnotationType::Ink};
    args.inkStrokeCounts = &counts;
    args.inkPoints = &points;
    args.inkPenStyle = 3;
    args.borderWidth = 2.f;
    Annotation* annot = EngineMupdfCreateAnnotation(engine, 1, points[0], &args);
    Vec<Annotation*> beforeRemoved;
    Vec<Annotation*> beforeAnnotations;
    utassert(EngineMupdfUndo(engine, beforeRemoved));
    EngineMupdfGetAnnotations(engine, beforeAnnotations);
    utassert(len(beforeAnnotations) == 0);
    DeleteVecMembers(beforeRemoved);
    utassert(EngineMupdfRedo(engine, beforeRemoved));
    int undoBeforeSnapshot = EngineMupdfUndoPos(AsEngineMupdf(engine), nullptr);
    ok = ok && annot && EngineMupdfHasUnsavedAnnotations(engine) && WriteSnapshot(&session, 1) &&
         EngineMupdfHasUnsavedAnnotations(engine);
    utassert(ok);
    utassert(EngineMupdfUndoPos(AsEngineMupdf(engine), nullptr) == undoBeforeSnapshot);
    Str unchanged = file::ReadFile(source);
    ok = ok && str::Eq(unchanged, ToStr(pdf));
    str::Free(unchanged);
    Str recoveredData = file::ReadFile(SnapshotPath(session.base, 1));
    EngineBase* recovered = CreateEngineMupdfFromData(recoveredData, StrL("recovered.pdf"), nullptr);
    Vec<Annotation*> annotations;
    if (recovered) {
        EngineMupdfGetAnnotations(recovered, annotations);
        ok = ok && len(annotations) == 1 && InkPenStyleTag(annotations[0]) == 3;
        utassert(ok);
    } else
        ok = false;
    SafeEngineRelease(&recovered);
    str::Free(recoveredData);
    Vec<Annotation*> removed;
    bool undone = EngineMupdfUndo(engine, removed);
    utassert(undone);
    EngineMupdfGetAnnotations(engine, annotations);
    utassert(len(annotations) == 0);
    DeleteVecMembers(removed);
    bool redone = EngineMupdfRedo(engine, removed);
    utassert(redone);
    ok = ok && undone && len(annotations) == 0 && redone;
    utassert(ok);
    EngineMupdfGetAnnotations(engine, annotations);
    ok = ok && len(annotations) == 1 && WriteSnapshot(&session, 2) && !file::Exists(SnapshotPath(session.base, 1)) &&
         file::Exists(SnapshotPath(session.base, 2));
    utassert(ok);
    EngineMupdfBeginOperation(engine, "Open gesture");
    ok = ok && !WriteSnapshot(&session, 3);
    EngineMupdfEndOperation(engine);
    session.stop = session.discard = true;
    ok = ok && !WriteSnapshot(&session, 3) && file::Exists(SnapshotPath(session.base, 2));
    session.discard = false;
    ok = ok && WriteSnapshot(&session, 3) && file::Exists(SnapshotPath(session.base, 3));
    utassert(ok);
    SafeEngineRelease(&engine);
    return ok;
}

bool AnnotRecovery_UnitTests() {
    Settings* savedSettings = gSettings;
    if (!gSettings) gSettings = NewSettings({});
    Str directory = str::Dup(GetTempFilePathTemp(StrL("annotation-recovery")));
    file::Delete(directory);
    bool ok = dir::CreateAll(directory);
    TempStr source = path::JoinTemp(directory, StrL("original.pdf"));
    TempStr moved = path::JoinTemp(directory, StrL("moved.pdf"));
    TempStr base = path::JoinTemp(directory, StrL("session"));
    TempStr snapshot = SnapshotPath(base, 1);
    ok = ok && file::WriteFile(source, StrL("original document")) && file::WriteFile(snapshot, StrL("recovered copy"));
    RecoveryHeader header;
    header.sourceSize = file::GetSize(source);
    header.sourceTime = file::GetModificationTime(source);
    header.revision = 1;
    header.pathBytes = (u32)len(source);
    header.flags = kRecoveryHasDigest;
    ok = ok && HashSource(source, header.digest) && WriteManifest(base, header, source);
    bool changed = true;
    ok = ok && path::IsSame(FindRecoveryIn(directory, source, changed), snapshot) && !changed;
    utassert(ok);
    ok = ok && file::Copy(moved, source, false) && path::IsSame(FindRecoveryIn(directory, moved, changed), snapshot) &&
         !changed;
    utassert(ok);
    ok = ok && file::WriteFile(source, StrL("external document")) &&
         path::IsSame(FindRecoveryIn(directory, source, changed), snapshot) && changed;
    utassert(ok);
    Str manifestBefore = file::ReadFile(ManifestPath(base));
    ok = ok && !PromoteAtomic(path::JoinTemp(directory, StrL("missing.partial")), ManifestPath(base));
    Str manifestAfter = file::ReadFile(ManifestPath(base));
    ok = ok && str::Eq(manifestBefore, manifestAfter);
    str::Free(manifestBefore);
    str::Free(manifestAfter);
    HANDLE active = CreateFileW(CWStrTemp(str::JoinTemp(base, StrL(".lock"))), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    ok = ok && active != INVALID_HANDLE_VALUE && len(FindRecoveryIn(directory, moved, changed)) == 0;
    utassert(ok);
    if (active != INVALID_HANDLE_VALUE) CloseHandle(active);
    ok = ok && file::WriteFile(ManifestPath(base), StrL("corrupted")) &&
         len(FindRecoveryIn(directory, moved, changed)) == 0;
    utassert(ok);
    ok = ok && TestRecoverySnapshot(directory);
    dir::RemoveAll(directory);
    str::Free(directory);
    if (!savedSettings) DeleteSettings(gSettings);
    gSettings = savedSettings;
    return ok;
}
#endif
