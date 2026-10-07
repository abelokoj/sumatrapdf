/* Copyright 2026 the SumatraPDF Enhanced contributors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"
#include "base/Http.h"
#include "base/Crypto.h"
#include "base/JsonParser.h"
#include "base/UITask.h"

#include "gui/Layout.h"
#include "gui/UIModels.h"
#include "Settings.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "Flags.h"
#include "Version.h"
#include "SumatraConfig.h"
#include "Translations.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "Notifications.h"
#include "UpdateCheck.h"
#include "EnhancedUpdate.h"

#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif

static const Str kEnhancedReleaseAPI =
    StrL("https://api.github.com/repos/abelokoj/sumatrapdf-enhanced/releases/latest");
static const Str kEnhancedReleases = StrL("https://github.com/abelokoj/sumatrapdf-enhanced/releases/latest");
static const Str kEnhancedDownloadRoot = StrL("https://github.com/abelokoj/sumatrapdf-enhanced/releases/download/");
static Kind kEnhancedUpdateNotice = StrL("enhanced-update-check").s;
static Kind kEnhancedAvailableNotice = StrL("enhanced-update-available").s;
constexpr i64 kEnhancedMaxDownload = 256LL * 1024 * 1024;
constexpr int kEnhancedMaxMetadata = 1024 * 1024;
constexpr int kEnhancedMaxAssets = 64;

struct EnhancedAsset {
    Str name, url, digest, state;
    i64 size = 0;
    unsigned seen = 0;
};

struct EnhancedRelease {
    Str tag, version;
    Vec<EnhancedAsset> assets;
    EnhancedAsset* selected = nullptr;
    bool draft = true;
    bool prerelease = true;
    bool invalid = false;
    unsigned seen = 0;
    ~EnhancedRelease() {
        str::Free(tag);
        for (auto& a : assets) {
            str::Free(a.name);
            str::Free(a.url);
            str::Free(a.digest);
            str::Free(a.state);
        }
    }
};

static bool gEnhancedUpdateBusy = false;
static EnhancedRelease* gEnhancedPending = nullptr;

static bool EnhancedVersionParts(Str s, int out[3]) {
    int at = 0;
    for (int part = 0; part < 3; part++) {
        if (at >= len(s) || s.s[at] < '0' || s.s[at] > '9') return false;
        int start = at, value = 0;
        while (at < len(s) && s.s[at] >= '0' && s.s[at] <= '9') {
            value = value * 10 + s.s[at++] - '0';
            if (value > 65535) return false;
        }
        if (at - start > 1 && s.s[start] == '0') return false;
        out[part] = value;
        if (part < 2 && (at >= len(s) || s.s[at++] != '.')) return false;
    }
    return at == len(s);
}

static bool EnhancedDigestValid(Str digest) {
    if (len(digest) != 71 || !str::StartsWith(digest, StrL("sha256:"))) return false;
    for (int i = 7; i < len(digest); i++) {
        char c = digest.s[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) return false;
    }
    return true;
}

static bool EnhancedSeen(EnhancedRelease* r, unsigned& seen, unsigned bit, json::Value* v, json::Type type) {
    if ((seen & bit) || v->type != type) {
        r->invalid = true;
        return false;
    }
    seen |= bit;
    return true;
}

static void EnhancedVisit(EnhancedRelease* r, json::Value* v) {
    if (json::PathMatch(v->path, StrL("/tag_name"))) {
        if (EnhancedSeen(r, r->seen, 1, v, json::Type::String)) r->tag = str::Dup(v->value);
    } else if (json::PathMatch(v->path, StrL("/draft"))) {
        if (EnhancedSeen(r, r->seen, 2, v, json::Type::Bool)) r->draft = str::Eq(v->value, StrL("true"));
    } else if (json::PathMatch(v->path, StrL("/prerelease"))) {
        if (EnhancedSeen(r, r->seen, 4, v, json::Type::Bool)) r->prerelease = str::Eq(v->value, StrL("true"));
    } else if (json::PathMatch(v->path, StrL("/assets"), StrL("*"), StrL("*"))) {
        int index = json::PathSegIndex(json::PathNth(v->path, 1));
        if (index < 0 || index >= kEnhancedMaxAssets) {
            r->invalid = true;
            return;
        }
        while (len(r->assets) <= index) VecAppend(r->assets, EnhancedAsset{});
        auto& a = r->assets[index];
        Str key = json::PathSegKey(json::PathNth(v->path, 2));
        if (str::Eq(key, StrL("name"))) {
            if (EnhancedSeen(r, a.seen, 1, v, json::Type::String)) a.name = str::Dup(v->value);
        } else if (str::Eq(key, StrL("browser_download_url"))) {
            if (EnhancedSeen(r, a.seen, 2, v, json::Type::String)) a.url = str::Dup(v->value);
        } else if (str::Eq(key, StrL("digest"))) {
            if (v->type == json::Type::Null) {
                if (a.seen & 4) r->invalid = true;
                a.seen |= 4;
            } else if (EnhancedSeen(r, a.seen, 4, v, json::Type::String))
                a.digest = str::Dup(v->value);
        } else if (str::Eq(key, StrL("state"))) {
            if (EnhancedSeen(r, a.seen, 8, v, json::Type::String)) a.state = str::Dup(v->value);
        } else if (str::Eq(key, StrL("size"))) {
            if (!EnhancedSeen(r, a.seen, 16, v, json::Type::Number)) return;
            for (int i = 0; i < len(v->value); i++) {
                char c = v->value.s[i];
                if (c < '0' || c > '9' || a.size > kEnhancedMaxDownload) {
                    r->invalid = true;
                    return;
                }
                a.size = a.size * 10 + c - '0';
            }
        }
    }
}

static EnhancedRelease* ParseEnhancedRelease(Str jsonText, Str arch, bool installed) {
    if (len(jsonText) == 0 || len(jsonText) > kEnhancedMaxMetadata) return nullptr;
    auto* r = new EnhancedRelease();
    bool ok = json::Parse(jsonText, MkFunc1<EnhancedRelease, json::Value*>(EnhancedVisit, r));
    if (!ok || r->invalid || r->seen != 7 || r->draft || r->prerelease ||
        !str::StartsWith(r->tag, StrL("enhanced-v"))) {
        delete r;
        return nullptr;
    }
    r->version = Str(r->tag.s + 10, len(r->tag) - 10);
    int parts[3];
    if (!EnhancedVersionParts(r->version, parts)) {
        delete r;
        return nullptr;
    }
    if (!str::Eq(arch, StrL("x64")) && !str::Eq(arch, StrL("arm64"))) return r;
    TempStr name =
        fmt("SumatraPDF-Enhanced-v%s-%s-%s.exe", r->version, arch, installed ? StrL("install") : StrL("portable"));
    TempStr url = fmt("%s%s/%s", kEnhancedDownloadRoot, r->tag, name);
    for (auto& a : r->assets) {
        if (!str::Eq(a.name, name)) continue;
        if (r->selected || a.seen != 31 || !str::Eq(a.state, StrL("uploaded")) || !str::Eq(a.url, url) ||
            !EnhancedDigestValid(a.digest) || a.size < 4096 || a.size > kEnhancedMaxDownload) {
            delete r;
            return nullptr;
        }
        r->selected = &a;
    }
    return r;
}

static TempStr EnhancedNativeArch() {
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) return StrL("arm64");
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) return StrL("x64");
    return {};
}

static bool EnhancedHashMatches(Str bytes, const EnhancedAsset& a) {
    if (len(bytes) != a.size || !EnhancedDigestValid(a.digest)) return false;
    u8 hash[32]{};
    CalcSHA2Digest(bytes, hash);
    return str::EqI(str::MemToHexTemp(Str((char*)hash, dimof(hash))), Str(a.digest.s + 7, 64));
}

static bool EnhancedMachineMatches(Str bytes, Str arch) {
    if (len(bytes) < 64 || bytes.s[0] != 'M' || bytes.s[1] != 'Z') return false;
    u32 offset;
    memcpy(&offset, bytes.s + 60, sizeof(offset));
    if (offset > (u32)len(bytes) - 24) return false;
    u32 signature;
    u16 machine;
    memcpy(&signature, bytes.s + offset, sizeof(signature));
    memcpy(&machine, bytes.s + offset + 4, sizeof(machine));
    return signature == 0x4550 && machine == (str::Eq(arch, StrL("arm64")) ? 0xAA64 : 0x8664);
}

static bool EnhancedFileIdentity(Str path, Str version) {
    DWORD unused = 0;
    DWORD size = GetFileVersionInfoSizeW(CWStrTemp(path), &unused);
    if (size == 0 || size > kEnhancedMaxMetadata) return false;
    auto* bytes = AllocArray<u8>(size);
    bool ok = GetFileVersionInfoW(CWStrTemp(path), 0, size, bytes) != FALSE;
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT infoSize = 0;
    int parts[3];
    ok = ok && EnhancedVersionParts(version, parts) && VerQueryValueW(bytes, L"\\", (void**)&fixed, &infoSize) &&
         infoSize >= sizeof(*fixed);
    if (ok)
        ok = fixed->dwSignature == 0xFEEF04BD && HIWORD(fixed->dwProductVersionMS) == parts[0] &&
             LOWORD(fixed->dwProductVersionMS) == parts[1] && HIWORD(fixed->dwProductVersionLS) == parts[2] &&
             LOWORD(fixed->dwProductVersionLS) == 0;
    WCHAR* product = nullptr;
    if (ok)
        ok = VerQueryValueW(bytes, L"\\StringFileInfo\\040904E4\\ProductName", (void**)&product, &infoSize) &&
             infoSize > 0 && wcscmp(product, L"SumatraPDF Enhanced") == 0;
    free(bytes);
    return ok;
}

static bool VerifyEnhancedFile(Str path, EnhancedRelease* release, Str arch) {
    Str bytes = file::ReadFile(path);
    bool ok =
        release->selected && EnhancedHashMatches(bytes, *release->selected) && EnhancedMachineMatches(bytes, arch);
    str::Free(bytes);
    ok = ok && EnhancedFileIdentity(path, release->version);
    TempStr signer = GetExecutableSignerTemp(GetSelfExePathTemp());
    if (ok && len(signer) > 0) {
        TempStr downloadedSigner = GetExecutableSignerTemp(path);
        ok = str::Eq(signer, downloadedSigner) && IsPEFileSigned(path);
    }
    return ok;
}

struct EnhancedJob {
    HWND owner = nullptr;
    UpdateCheck check = UpdateCheck::UserInitiated;
    EnhancedRelease* release = nullptr;
    Str arch, path, error;
    bool installed = false;
    ~EnhancedJob() {
        delete release;
        str::Free(arch);
        str::Free(path);
        str::Free(error);
    }
};

static void EnhancedNotice(HWND owner, Str message, int timeout = 5000) {
    MainWindow* win = FindMainWindowByHwnd(owner);
    if (!win) return;
    RemoveNotificationsForGroup(win->hwndCanvas, kEnhancedUpdateNotice);
    NotificationCreateArgs args;
    args.hwndParent = win->hwndCanvas;
    args.msg = message;
    args.plainText = true;
    args.groupId = kEnhancedUpdateNotice;
    args.timeoutMs = timeout;
    ShowNotification(args);
}

static int EnhancedDialog(HWND owner, Str title, Str content, Str action, Str other = {}) {
    TASKDIALOG_BUTTON buttons[2]{};
    buttons[0] = {100, CWStrTemp(action)};
    if (len(other) > 0) buttons[1] = {101, CWStrTemp(other)};
    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = owner;
    cfg.pszWindowTitle = L"SumatraPDF Enhanced update";
    cfg.pszMainInstruction = CWStrTemp(title);
    cfg.pszContent = CWStrTemp(content);
    cfg.pszMainIcon = TD_INFORMATION_ICON;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT;
    cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    cfg.cButtons = len(other) > 0 ? 2 : 1;
    cfg.pButtons = buttons;
    cfg.nDefaultButton = IDCANCEL;
    int button = 0;
    return SUCCEEDED(TaskDialogIndirect(&cfg, &button, nullptr, nullptr)) ? button : IDCANCEL;
}

static void EnhancedDownloadDone(EnhancedJob* job) {
    AutoDelete cleanup(job);
    struct ResetBusy {
        ~ResetBusy() { gEnhancedUpdateBusy = false; }
    } resetBusy;
    MainWindow* win = FindMainWindowByHwnd(job->owner);
    if (!win) {
        file::Delete(job->path);
        return;
    }
    RemoveNotificationsForGroup(win->hwndCanvas, kEnhancedUpdateNotice);
    if (len(job->error) > 0) {
        EnhancedNotice(job->owner, job->error, 10000);
        file::Delete(job->path);
        return;
    }
    WCHAR destination[32768]{};
    lstrcpynW(destination, CWStrTemp(job->release->selected->name), dimof(destination));
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = job->owner;
    ofn.lpstrFile = destination;
    ofn.nMaxFile = dimof(destination);
    ofn.lpstrFilter = L"Enhanced executable\0*.exe\0";
    ofn.lpstrDefExt = L"exe";
    ofn.lpstrTitle = L"Save verified Enhanced update";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) {
        file::Delete(job->path);
        return;
    }
    Str target = ToUtf8Temp(WStr(destination));
    if (path::IsSame(target, GetSelfExePathTemp())) {
        EnhancedNotice(job->owner, Tr("Save the update under a new filename, then close Enhanced before replacing it."),
                       10000);
        file::Delete(job->path);
        return;
    }
    Str staged = str::Dup(MakeUniqueFilePathTemp(fmt("%s.enhanced-update", target)));
    bool saved = file::Copy(staged, job->path, true) && VerifyEnhancedFile(staged, job->release, job->arch) &&
                 MoveFileExW(CWStrTemp(staged), destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    file::Delete(staged);
    str::Free(staged);
    file::Delete(job->path);
    if (!saved) {
        EnhancedNotice(job->owner,
                       Tr("The verified update could not be saved. Choose a writable folder and try again."), 10000);
        return;
    }
    file::SetZoneIdentifier(target);
    if (!job->installed) {
        EnhancedNotice(
            job->owner,
            fmt(Tr("Portable update saved: %s. Close Enhanced before replacing your portable app.").s, target), 15000);
        return;
    }
    int choice = EnhancedDialog(
        job->owner, Tr("Verified installer saved"),
        Tr("You can run the Enhanced installer now. Save your document changes before completing installation."),
        Tr("Run installer"));
    if (choice == 100 && VerifyEnhancedFile(target, job->release, job->arch) &&
        !CreateProcessHelper(target, StrL("-install")))
        EnhancedNotice(job->owner,
                       Tr("The installer could not be started. Run the saved installer to update Enhanced."), 10000);
}

static void EnhancedDownloadWorker(EnhancedJob* job) {
    job->path = str::Dup(GetTempFilePathTemp(StrL("enhanced-update")));
    bool ok = HttpGetToFile(job->release->selected->url, job->path, {}, job->release->selected->size);
    ok = ok && VerifyEnhancedFile(job->path, job->release, job->arch);
    if (!ok)
        job->error = str::Dup(Tr("The update could not be downloaded or verified. No application files were changed."));
    uitask::Post(MkFunc0<EnhancedJob>(EnhancedDownloadDone, job), "EnhancedDownloadDone");
}

static bool EnhancedOffer(EnhancedJob* job) {
    if (!IsWindow(job->owner)) return false;
    auto* release = job->release;
    TempStr content = fmt(Tr("Enhanced %s is available. Your version is %s. Device: %s.\nThe download will be verified "
                             "before you save or run it.")
                              .s,
                          release->version, StrL(ENHANCED_VERSION_STRA), job->arch);
    if (!release->selected) {
        int choice = EnhancedDialog(job->owner, Tr("New Enhanced release"),
                                    Tr("This release has no verified executable for your device. Open its release page "
                                       "to see available downloads."),
                                    Tr("Open releases"));
        if (choice == 100) SumatraLaunchBrowser(kEnhancedReleases);
        return false;
    }
    int choice =
        EnhancedDialog(job->owner, Tr("New Enhanced release"), content,
                       job->installed ? Tr("Download installer") : Tr("Download portable app"), Tr("Open releases"));
    if (choice == 101) SumatraLaunchBrowser(kEnhancedReleases);
    if (choice != 100 || !IsWindow(job->owner) || !HasPermission(Perm::InternetAccess | Perm::DiskAccess)) return false;
    EnhancedNotice(job->owner, Tr("Downloading and verifying Enhanced update..."), 0);
    RunAsync(MkFunc0<EnhancedJob>(EnhancedDownloadWorker, job), StrL("EnhancedUpdateDownload"));
    return true;
}

static void EnhancedCheckDone(EnhancedJob* job) {
    MainWindow* win = FindMainWindowByHwnd(job->owner);
    if (!win) {
        delete job;
        gEnhancedUpdateBusy = false;
        return;
    }
    RemoveNotificationsForGroup(win->hwndCanvas, kEnhancedUpdateNotice);
    if (job->release) {
        GetSystemTimeAsFileTime(&gSettings->timeOfLastUpdateCheck);
        ScheduleSaveSettings();
    }
    if (len(job->error) > 0 || !job->release) {
        if (job->check == UpdateCheck::UserInitiated) EnhancedNotice(job->owner, job->error, 10000);
        delete job;
        gEnhancedUpdateBusy = false;
        return;
    }
    if (CompareProgramVersion(job->release->version, StrL(ENHANCED_VERSION_STRA)) <= 0) {
        delete gEnhancedPending;
        gEnhancedPending = nullptr;
        RemoveNotificationsForGroup(win->hwndCanvas, kEnhancedAvailableNotice);
        if (job->check == UpdateCheck::UserInitiated)
            EnhancedNotice(job->owner, Tr("You have the latest SumatraPDF Enhanced version."));
        delete job;
        gEnhancedUpdateBusy = false;
        return;
    }
    if (job->check == UpdateCheck::Automatic) {
        delete gEnhancedPending;
        gEnhancedPending = job->release;
        job->release = nullptr;
        NotificationCreateArgs args;
        args.hwndParent = win->hwndCanvas;
        args.msg = fmt(Tr("Enhanced %s is available. %s").s, gEnhancedPending->version,
                       StrL("[Download update](CmdInstallPrereleaseUpdate)"));
        args.groupId = kEnhancedAvailableNotice;
        args.corner = NotifCorner::BottomLeft;
        ShowNotification(args);
        delete job;
        gEnhancedUpdateBusy = false;
        return;
    }
    if (EnhancedOffer(job)) return;
    delete job;
    gEnhancedUpdateBusy = false;
}

static void EnhancedCheckWorker(EnhancedJob* job) {
    Str metadata = str::Dup(GetTempFilePathTemp(StrL("enhanced-release")));
    bool ok = HttpGetToFile(kEnhancedReleaseAPI, metadata, {}, kEnhancedMaxMetadata);
    Str jsonText = ok ? file::ReadFile(metadata) : Str{};
    if (ok) job->release = ParseEnhancedRelease(jsonText, job->arch, job->installed);
    str::Free(jsonText);
    file::Delete(metadata);
    str::Free(metadata);
    if (!job->release)
        job->error = str::Dup(
            Tr("Enhanced releases could not be checked. Try again later or open the repository's Releases page."));
    uitask::Post(MkFunc0<EnhancedJob>(EnhancedCheckDone, job), "EnhancedCheckDone");
}

void StartEnhancedUpdateCheck(MainWindow* win, UpdateCheck check) {
    if (!win || gEnhancedUpdateBusy || gIsStoreBuild || !HasPermission(Perm::InternetAccess)) return;
    if (check == UpdateCheck::Automatic) {
        if (!gSettings->checkForUpdates || !HasPermission(Perm::SavePreferences)) return;
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        constexpr int kCheckInterval = 24 * 60 * 60;
        if (FileTimeDiffInSecs(now, gSettings->timeOfLastUpdateCheck) < kCheckInterval) return;
    }
    auto* job = new EnhancedJob();
    job->owner = win->hwndFrame;
    job->check = check;
    job->arch = str::Dup(EnhancedNativeArch());
    job->installed = !IsRunningInPortableMode();
    gEnhancedUpdateBusy = true;
    if (check == UpdateCheck::UserInitiated) EnhancedNotice(job->owner, Tr("Checking Enhanced releases..."), 0);
    RunAsync(MkFunc0<EnhancedJob>(EnhancedCheckWorker, job), StrL("EnhancedReleaseCheck"));
}

bool HasEnhancedUpdate() {
    return gEnhancedPending != nullptr;
}

void DownloadEnhancedUpdate(MainWindow* win) {
    if (!win || !gEnhancedPending || gEnhancedUpdateBusy || !HasPermission(Perm::InternetAccess | Perm::DiskAccess))
        return;
    auto* job = new EnhancedJob();
    job->owner = win->hwndFrame;
    job->release = gEnhancedPending;
    gEnhancedPending = nullptr;
    job->arch = str::Dup(EnhancedNativeArch());
    job->installed = !IsRunningInPortableMode();
    RemoveNotificationsForGroup(win->hwndCanvas, kEnhancedAvailableNotice);
    gEnhancedUpdateBusy = true;
    if (EnhancedOffer(job)) return;
    delete job;
    gEnhancedUpdateBusy = false;
}

#if IS_DEBUG

static TempStr EnhancedTestJson(Str arch, bool installed, Str digest, Str url = {}) {
    TempStr name = fmt("SumatraPDF-Enhanced-v0.2.3-%s-%s.exe", arch, installed ? StrL("install") : StrL("portable"));
    TempStr expected = fmt("%senhanced-v0.2.3/%s", kEnhancedDownloadRoot, name);
    return fmt(
        R"({"tag_name":"enhanced-v0.2.3","draft":false,"prerelease":false,"assets":[{"name":"%s","state":"uploaded","size":8192,"digest":"%s","browser_download_url":"%s"}]})",
        name, digest, len(url) > 0 ? url : expected);
}

void EnhancedUpdate_UnitTests() {
    TempStr fixturePath = GetEnvVariableTemp(StrL("ENHANCED_UPDATE_METADATA"));
    if (len(fixturePath) > 0) {
        Str fixture = file::ReadFile(fixturePath);
        Str fixtureArches[] = {StrL("x64"), StrL("arm64")};
        for (Str arch : fixtureArches) {
            for (int choice = 0; choice < 2; choice++) {
                auto* release = ParseEnhancedRelease(fixture, arch, choice != 0);
                utassert(release && release->selected && EnhancedDigestValid(release->selected->digest));
                delete release;
            }
        }
        str::Free(fixture);
    }
    Str digest = StrL("sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    Str arches[] = {StrL("x64"), StrL("arm64")};
    for (Str arch : arches) {
        for (int installChoice = 0; installChoice < 2; installChoice++) {
            bool installed = installChoice != 0;
            Str jsonText = EnhancedTestJson(arch, installed, digest);
            auto* release = ParseEnhancedRelease(jsonText, arch, installed);
            utassert(release && release->selected && str::Eq(release->version, StrL("0.2.3")));
            delete release;
            release = ParseEnhancedRelease(jsonText, arch, !installed);
            utassert(release && !release->selected);
            delete release;
            Str evilURLs[] = {
                StrL("https://github.com/sumatrapdfreader/sumatrapdf/releases/download/enhanced-v0.2.3/file.exe"),
                StrL("http://github.com/abelokoj/sumatrapdf-enhanced/releases/download/enhanced-v0.2.3/file.exe"),
                StrL("https://github.com.evil.example/abelokoj/sumatrapdf-enhanced/file.exe"),
                StrL("https://github.com/abelokoj/sumatrapdf-enhanced/releases/download/enhanced-v0.2.3/../file.exe")};
            for (Str url : evilURLs) {
                release = ParseEnhancedRelease(EnhancedTestJson(arch, installed, digest, url), arch, installed);
                utassert(release == nullptr);
                delete release;
            }
            release = ParseEnhancedRelease(EnhancedTestJson(arch, installed, StrL("sha256:bad")), arch, installed);
            utassert(release == nullptr);
            delete release;
        }
    }
    int parts[3];
    utassert(EnhancedVersionParts(StrL("0.10.12"), parts) && parts[1] == 10);
    Str badVersions[] = {StrL("1.2"),    StrL("1.2.3.4"),   StrL("01.2.3"),    StrL("1.2.3-beta"),
                         StrL("1.2.-3"), StrL("1.2.65536"), StrL("1.2.3/../a")};
    for (Str version : badVersions) utassert(!EnhancedVersionParts(version, parts));
    Str badJSON[] = {
        StrL("{}"),
        StrL("[]"),
        StrL("{bad}"),
        StrL(R"({"tag_name":"enhanced-v0.2.3","draft":true,"prerelease":false})"),
        StrL(R"({"tag_name":"enhanced-v0.2.3","draft":false,"prerelease":true})"),
        StrL(R"({"tag_name":"enhanced-v0.2.3","draft":"false","prerelease":false})"),
        StrL(R"({"tag_name":"v3.7","draft":false,"prerelease":false})"),
        StrL(R"({"tag_name":"enhanced-v0.2.3","tag_name":"enhanced-v0.2.4","draft":false,"prerelease":false})")};
    for (Str value : badJSON) {
        auto* release = ParseEnhancedRelease(value, StrL("x64"), true);
        utassert(release == nullptr);
        delete release;
    }
    EnhancedAsset asset;
    Str bytes = StrL("verified update payload");
    u8 hash[32]{};
    CalcSHA2Digest(bytes, hash);
    asset.size = len(bytes);
    asset.digest = fmt("sha256:%s", str::MemToHexTemp(Str((char*)hash, dimof(hash))));
    utassert(EnhancedHashMatches(bytes, asset));
    utassert(!EnhancedHashMatches(StrL("changed! update payload"), asset));
    asset.size++;
    utassert(!EnhancedHashMatches(bytes, asset));
    char header[128]{};
    header[0] = 'M';
    header[1] = 'Z';
    u32 offset = 64, signature = 0x4550;
    u16 machine = 0x8664;
    memcpy(header + 60, &offset, 4);
    memcpy(header + 64, &signature, 4);
    memcpy(header + 68, &machine, 2);
    utassert(EnhancedMachineMatches(Str(header, dimof(header)), StrL("x64")));
    utassert(!EnhancedMachineMatches(Str(header, dimof(header)), StrL("arm64")));
    machine = 0xAA64;
    memcpy(header + 68, &machine, 2);
    utassert(EnhancedMachineMatches(Str(header, dimof(header)), StrL("arm64")));
    offset = UINT32_MAX;
    memcpy(header + 60, &offset, 4);
    utassert(!EnhancedMachineMatches(Str(header, dimof(header)), StrL("x64")));
    utassert(EnhancedFileIdentity(GetSelfExePathTemp(), StrL(ENHANCED_VERSION_STRA)));
    utassert(!EnhancedFileIdentity(GetSelfExePathTemp(), StrL("99.99.99")));
}
#endif
