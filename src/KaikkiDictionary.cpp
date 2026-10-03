/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/JsonParser.h"
#include <winioctl.h>
#include "zlib.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "KaikkiDictionary.h"

static const KaikkiPack kPacks[] = {
    {StrL("kaikki-english"), StrL("Kaikki English"),
     StrL("https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl"),
     StrL("https://kaikki.org/dictionary/English/index.html"), StrL("CC BY-SA 4.0 and GFDL; Wiktionary contributors"),
     3335559018LL, 3335559018LL, false},
    {StrL("kaikki-simple-english"), StrL("Kaikki Simple English"),
     StrL("https://kaikki.org/dictionary/downloads/simple/simple-extract.jsonl.gz"),
     StrL("https://kaikki.org/simplewiktionary/"), StrL("CC BY-SA 4.0 and GFDL; Wiktionary contributors"), 4719269LL,
     37900246LL, true},
};

constexpr u64 kMaxDownload = 8ULL * 1024 * 1024 * 1024;
constexpr u64 kMaxExpanded = 16ULL * 1024 * 1024 * 1024;
constexpr u64 kMaxIndex = 2ULL * 1024 * 1024 * 1024;
constexpr int kMaxLine = 8 * 1024 * 1024;
constexpr int kMaxBucket = 64 * 1024 * 1024;
constexpr int kBuckets = 256;
constexpr int kBuffer = 64 * 1024;
constexpr u32 kStoreVersion = 1;
constexpr u32 kIndexMagic = 0x4b41494b;
constexpr int kMaxMatches = 256;
constexpr u64 kMaxResultBytes = 32ULL * 1024 * 1024;
static Mutex gInstallLock;
static Mutex gStoreLock;

struct StoreHeader {
    u32 magic = kIndexMagic;
    u32 version = kStoreVersion;
    u64 count = 0;
    u64 dataBytes = 0;
};

struct StoreEntry {
    u64 hash = 0;
    u64 offset = 0;
    u32 bytes = 0;
    u32 reserved = 0;
};
static_assert(sizeof(StoreEntry) == 24);
static_assert(sizeof(StoreHeader) == 24);

struct DiskFile {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~DiskFile() { Close(); }
    void Close() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
    bool Open(Str path, DWORD access = GENERIC_READ, DWORD creation = OPEN_EXISTING) {
        h = CreateFileW(CWStrTemp(path), access, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, creation,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        return h != INVALID_HANDLE_VALUE;
    }
    bool Read(void* data, DWORD bytes) {
        DWORD got = 0;
        return ReadFile(h, data, bytes, &got, nullptr) && got == bytes;
    }
    bool Write(const void* data, DWORD bytes) {
        DWORD wrote = 0;
        return WriteFile(h, data, bytes, &wrote, nullptr) && wrote == bytes;
    }
    bool Seek(u64 offset) {
        LARGE_INTEGER pos{};
        pos.QuadPart = (LONGLONG)offset;
        return offset <= INT64_MAX && SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    }
    u64 Size() {
        LARGE_INTEGER size{};
        return GetFileSizeEx(h, &size) && size.QuadPart >= 0 ? (u64)size.QuadPart : UINT64_MAX;
    }
};

static bool Fail(Str* error, Str message) {
    if (error) str::ReplaceWithCopy(error, message);
    return false;
}

static bool IsCancelled(const Func1<bool*>& callback) {
    bool cancel = false;
    callback.Call(&cancel);
    return cancel;
}

const KaikkiPack* GetKaikkiPacks(int& count) {
    count = dimof(kPacks);
    return kPacks;
}

const KaikkiPack* FindKaikkiPack(Str id) {
    for (const auto& pack : kPacks) {
        if (str::Eq(pack.id, id)) return &pack;
    }
    return nullptr;
}

static TempStr WordKey(Str text) {
    str::TrimWsBoth(text);
    while (len(text) && strchr("\"'.,;:!?()[]{}", text.s[0])) text = Str(text.s + 1, len(text) - 1);
    while (len(text) && strchr("\"'.,;:!?()[]{}", text.s[len(text) - 1])) text = Str(text.s, len(text) - 1);
    if (len(text) == 0 || len(text) > 512 ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.s, len(text), nullptr, 0))
        return {};
    Str spaces = str::ReplaceTemp(text, StrL("_"), StrL(" "));
    WStr wide = ToWStrTemp(spaces);
    int count = LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, wide.s, len(wide), nullptr, 0);
    if (count <= 0) return spaces;
    WCHAR* lower = AllocArray<WCHAR>(GetTempArena(), count + 1);
    LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, wide.s, len(wide), lower, count);
    return ToUtf8Temp(WStr(lower, count));
}

static u64 WordHash(Str word) {
    u64 hash = 14695981039346656037ULL;
    for (int i = 0; i < len(word); i++) hash = (hash ^ (u8)word.s[i]) * 1099511628211ULL;
    return hash;
}

struct RecordWords {
    StrVec words;
    bool headword = false, redirect = false;
    void Visit(json::Value* value) {
        if (value->type != json::Type::String) return;
        if (json::PathMatch(value->path, StrL("/redirect"))) redirect = true;
        bool head = json::PathMatch(value->path, StrL("/word"));
        bool form = json::PathMatch(value->path, StrL("/forms"), StrL("*"), StrL("/form"));
        if (!head && !form) return;
        headword |= head;
        Str key = WordKey(value->value);
        if (len(key) == 0) return;
        if (!words.Contains(key)) words.Append(key);
    }
    bool Parse(Str line) {
        str::TrimWsBoth(line);
        if (len(line) < 2 || line.s[0] != '{' || line.s[len(line) - 1] != '}') return false;
        return json::Parse(line, MkMethod1<RecordWords, json::Value*, &RecordWords::Visit>(this)) &&
               (headword || redirect);
    }
};

struct WebRequest {
    HINTERNET session = nullptr, connection = nullptr, request = nullptr;
    u64 length = 0;
    Str modified, etag;
    ~WebRequest() {
        if (request) InternetCloseHandle(request);
        if (connection) InternetCloseHandle(connection);
        if (session) InternetCloseHandle(session);
        str::Free(modified);
        str::Free(etag);
    }
    Str Header(DWORD query) {
        WCHAR buffer[1024]{};
        DWORD size = sizeof(buffer);
        if (!HttpQueryInfoW(request, query, buffer, &size, nullptr)) return {};
        return str::Dup(ToUtf8Temp(WStr(buffer, (int)(size / sizeof(WCHAR)))));
    }
    bool Open(const KaikkiPack& pack, const WCHAR* method, Str* error) {
        session = InternetOpenW(L"SumatraPDF offline dictionary", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!session) return Fail(error, StrL("Cannot start the dictionary download."));
        DWORD timeout = 30000;
        InternetSetOptionW(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(session, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        WStr url = ToWStrTemp(pack.url);
        URL_COMPONENTSW parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = (DWORD)-1;
        if (!InternetCrackUrlW(url.s, len(url), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
            return Fail(error, StrL("Invalid dictionary source URL."));
        connection = InternetConnectW(session, CWStrTemp(WStr(parts.lpszHostName, parts.dwHostNameLength)), parts.nPort,
                                      nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
        if (!connection) return Fail(error, StrL("Cannot connect to Kaikki."));
        DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_NO_AUTO_REDIRECT | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI;
        request = HttpOpenRequestW(connection, method, CWStrTemp(WStr(parts.lpszUrlPath, parts.dwUrlPathLength)),
                                   nullptr, nullptr, nullptr, flags, 0);
        if (!request || !HttpSendRequestW(request, L"Accept-Encoding: identity\r\n", (DWORD)-1, nullptr, 0))
            return Fail(error, StrL("The Kaikki request failed or timed out."));
        DWORD status = 0, size = sizeof(status);
        if (!HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr) ||
            status != HTTP_STATUS_OK)
            return Fail(error, fmt("Kaikki returned HTTP %d.", (int)status));
        Str contentLength = Header(HTTP_QUERY_CONTENT_LENGTH);
        defer {
            str::Free(contentLength);
        };
        if (len(contentLength) == 0 || len(contentLength) > 20)
            return Fail(error, StrL("Kaikki did not report the exact download size."));
        for (int i = 0; i < len(contentLength); i++) {
            char c = contentLength.s[i];
            if (c < '0' || c > '9' || length > kMaxDownload / 10)
                return Fail(error, StrL("The dictionary download exceeds its size limit."));
            length = length * 10 + (c - '0');
        }
        if (!length || length > kMaxDownload) return Fail(error, StrL("Invalid dictionary download size."));
        modified = Header(HTTP_QUERY_LAST_MODIFIED);
        etag = Header(HTTP_QUERY_ETAG);
        return true;
    }
};

bool KaikkiDownloadSize(Str id, i64& bytes, Str* error) {
    bytes = 0;
    const KaikkiPack* pack = FindKaikkiPack(id);
    if (!pack) return Fail(error, StrL("Unknown Kaikki dictionary."));
    WebRequest web;
    if (!web.Open(*pack, L"HEAD", error)) return false;
    bytes = (i64)web.length;
    return true;
}

static TempStr BucketPath(Str generation, int bucket) {
    return path::JoinTemp(generation, fmt("bucket-%02x.tmp", bucket));
}

struct StoreBuilder {
    Str folder;
    DiskFile data;
    DiskFile buckets[kBuckets];
    u64 bucketCounts[kBuckets]{};
    u64 dataBytes = 0, expandedBytes = 0, count = 0;
    str::Builder line;
    Str* error;
    explicit StoreBuilder(Str folder, Str* error) : folder(folder), error(error) {}
    bool Open() {
        return data.Open(path::JoinTemp(folder, StrL("records.jsonl")), GENERIC_WRITE, CREATE_NEW) ||
               Fail(error, StrL("Cannot create dictionary records."));
    }
    bool Record() {
        AutoArenaSavepoint scratch;
        Str record = ToStr(line);
        if (len(record) && record.s[len(record) - 1] == '\r') record = Str(record.s, len(record) - 1);
        if (len(record) == 0) return Fail(error, StrL("The dictionary contains an empty JSONL record."));
        RecordWords words;
        if (!words.Parse(record)) return Fail(error, StrL("The dictionary contains an invalid JSONL record."));
        if (!len(words.words)) {
            line.Reset();
            return true;
        }
        if (dataBytes + len(record) + 1 > kMaxExpanded) return Fail(error, StrL("Expanded dictionary exceeds 16 GiB."));
        if (!data.Write(record.s, len(record)) || !data.Write("\n", 1))
            return Fail(error, StrL("Cannot write dictionary records; check available disk space."));
        for (Str word : words.words) {
            StoreEntry entry{WordHash(word), dataBytes, (u32)len(record), 0};
            int bucket = (int)(entry.hash >> 56);
            if ((count + 1) * sizeof(StoreEntry) > kMaxIndex ||
                (bucketCounts[bucket] + 1) * sizeof(StoreEntry) > kMaxBucket)
                return Fail(error, StrL("Dictionary index exceeds its bounded storage limit."));
            auto& output = buckets[bucket];
            if (output.h == INVALID_HANDLE_VALUE && !output.Open(BucketPath(folder, bucket), GENERIC_WRITE, CREATE_NEW))
                return Fail(error, StrL("Cannot create dictionary index."));
            if (!output.Write(&entry, sizeof(entry))) return Fail(error, StrL("Cannot write dictionary index."));
            bucketCounts[bucket]++;
            count++;
        }
        dataBytes += len(record) + 1;
        line.Reset();
        return true;
    }
    bool Feed(const u8* bytes, int size) {
        if (size < 0 || expandedBytes + (u64)size > kMaxExpanded)
            return Fail(error, StrL("Expanded dictionary exceeds 16 GiB."));
        expandedBytes += size;
        while (size > 0) {
            auto end = (const u8*)memchr(bytes, '\n', size);
            int take = end ? (int)(end - bytes) : size;
            if ((u64)len(line) + take > kMaxLine) return Fail(error, StrL("A dictionary record exceeds 8 MiB."));
            if (!line.Append(Str((const char*)bytes, take)))
                return Fail(error, StrL("Not enough memory for a dictionary record."));
            if (end && !Record()) return false;
            size -= take + (end ? 1 : 0);
            bytes += take + (end ? 1 : 0);
        }
        return true;
    }
    bool Finish(const Func1<KaikkiProgress*>& progress, const Func1<bool*>& cancelled) {
        if (len(line) && !Record()) return false;
        if (!count) return Fail(error, StrL("The dictionary contains no words."));
        if (!FlushFileBuffers(data.h)) return Fail(error, StrL("Cannot flush dictionary records."));
        data.Close();
        DiskFile index;
        if (!index.Open(path::JoinTemp(folder, StrL("index.bin")), GENERIC_WRITE, CREATE_NEW))
            return Fail(error, StrL("Cannot create final dictionary index."));
        StoreHeader header{kIndexMagic, kStoreVersion, count, dataBytes};
        if (!index.Write(&header, sizeof(header))) return Fail(error, StrL("Cannot write dictionary index header."));
        for (int i = 0; i < kBuckets; i++) {
            if (IsCancelled(cancelled)) return Fail(error, StrL("Dictionary installation cancelled."));
            KaikkiProgress p{(i64)i, kBuckets, true};
            progress.Call(&p);
            buckets[i].Close();
            if (!bucketCounts[i]) continue;
            AutoArenaSavepoint scratch;
            Str bucketPath = BucketPath(folder, i);
            DiskFile bucket;
            Vec<StoreEntry> entries;
            if (!bucket.Open(bucketPath) || !VecResize(entries, (int)bucketCounts[i]) ||
                !bucket.Read(VecData(entries), (DWORD)(bucketCounts[i] * sizeof(StoreEntry))))
                return Fail(error, StrL("Cannot read dictionary index bucket."));
            VecSort(entries, [](const StoreEntry* a, const StoreEntry* b) {
                if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
                return a->offset < b->offset ? -1 : a->offset > b->offset ? 1 : 0;
            });
            if (!index.Write(VecData(entries), (DWORD)(bucketCounts[i] * sizeof(StoreEntry))))
                return Fail(error, StrL("Cannot write sorted dictionary index."));
            bucket.Close();
            file::Delete(bucketPath);
        }
        return FlushFileBuffers(index.h) != FALSE || Fail(error, StrL("Cannot flush dictionary index."));
    }
};

// Only the small current pointer changes when a complete generation is ready.
static bool IsGeneration(Str name) {
    if (len(name) != 29 || !str::StartsWith(name, StrL("gen-")) || name.s[20] != '-') return false;
    for (int i = 4; i < len(name); i++) {
        if (i == 20) continue;
        char c = name.s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static TempStr CurrentGeneration(Str packDir) {
    DiskFile current;
    char name[29];
    if (!current.Open(path::JoinTemp(packDir, StrL("current"))) || current.Size() != sizeof(name) ||
        !current.Read(name, sizeof(name)))
        return {};
    Str value(name, sizeof(name));
    return IsGeneration(value) ? str::DupTemp(value) : Str();
}

static bool OpenStore(Str generation, DiskFile& index, DiskFile& data, StoreHeader& header) {
    if (!index.Open(path::JoinTemp(generation, StrL("index.bin"))) || !index.Read(&header, sizeof(header)) ||
        header.magic != kIndexMagic || header.version != kStoreVersion || !header.count ||
        header.count > kMaxIndex / sizeof(StoreEntry) || !header.dataBytes || header.dataBytes > kMaxExpanded ||
        index.Size() != sizeof(header) + header.count * sizeof(StoreEntry))
        return false;
    return data.Open(path::JoinTemp(generation, StrL("records.jsonl"))) && data.Size() == header.dataBytes;
}

bool KaikkiPackInstalled(Str id, Str folder) {
    if (!FindKaikkiPack(id)) return false;
    AutoUnlockMutex lock(&gStoreLock);
    Str packDir = path::JoinTemp(folder, id);
    Str name = CurrentGeneration(packDir);
    if (len(name) == 0) return false;
    DiskFile index, data;
    StoreHeader header;
    return OpenStore(path::JoinTemp(packDir, name), index, data, header);
}

static bool ReadEntry(DiskFile& index, const StoreHeader& header, u64 pos, StoreEntry& entry) {
    return pos < header.count && index.Seek(sizeof(header) + pos * sizeof(entry)) &&
           index.Read(&entry, sizeof(entry)) && !entry.reserved && entry.bytes && entry.bytes <= kMaxLine &&
           entry.offset <= header.dataBytes && entry.bytes <= header.dataBytes - entry.offset;
}

bool LookupKaikkiRecords(Str id, Str folder, Str normalizedWord, StrVec& records, Str* error) {
    if (!FindKaikkiPack(id)) return Fail(error, StrL("Unknown Kaikki dictionary."));
    AutoUnlockMutex lock(&gStoreLock);
    Str packDir = path::JoinTemp(folder, id);
    Str name = CurrentGeneration(packDir);
    if (!len(name)) return true;
    DiskFile index, data;
    StoreHeader header;
    if (!OpenStore(path::JoinTemp(packDir, name), index, data, header))
        return Fail(error, StrL("Dictionary index is damaged; reinstall this pack."));
    Str word = WordKey(normalizedWord);
    if (!len(word)) return true;
    u64 hash = WordHash(word), low = 0, high = header.count;
    StoreEntry entry;
    while (low < high) {
        u64 mid = low + (high - low) / 2;
        if (!ReadEntry(index, header, mid, entry)) return Fail(error, StrL("Invalid dictionary index entry."));
        if (entry.hash < hash)
            low = mid + 1;
        else
            high = mid;
    }
    Vec<char> line;
    u64 lastOffset = UINT64_MAX;
    u64 resultBytes = 0;
    for (Str record : records) resultBytes += len(record);
    int candidates = 0;
    for (; low < header.count; low++) {
        if (!ReadEntry(index, header, low, entry)) return Fail(error, StrL("Invalid dictionary index entry."));
        if (entry.hash != hash) break;
        if (++candidates > 4096) return Fail(error, StrL("Too many dictionary index candidates."));
        if (entry.offset == lastOffset) continue;
        lastOffset = entry.offset;
        if (!VecResize(line, entry.bytes) || !data.Seek(entry.offset) || !data.Read(VecData(line), entry.bytes))
            return Fail(error, StrL("Cannot read dictionary record."));
        AutoArenaSavepoint scratch;
        RecordWords words;
        Str record(VecData(line), len(line));
        if (!words.Parse(record)) return Fail(error, StrL("Dictionary record is damaged."));
        // Hash collisions can never return the wrong word.
        if (!words.words.Contains(word)) continue;
        if (resultBytes + entry.bytes > kMaxResultBytes)
            return Fail(error, StrL("Dictionary results exceed the 32 MiB lookup limit."));
        records.Append(record);
        resultBytes += entry.bytes;
        if (len(records) >= kMaxMatches) break;
    }
    return true;
}

static bool PublishStore(const KaikkiPack& pack, Str packDir, Str name, const WebRequest& web,
                         const StoreBuilder& store, Str* error) {
    AutoUnlockMutex lock(&gStoreLock);
    Str generation = path::JoinTemp(packDir, name);
    SYSTEMTIME now{};
    GetSystemTime(&now);
    Str manifest =
        fmt("{\"format\":\"sumatra-kaikki-jsonl\",\"version\":%d,\"pack\":\"%s\","
            "\"source\":\"%s\",\"url\":\"%s\",\"license\":\"%s\","
            "\"download_bytes\":%llu,\"data_bytes\":%llu,\"index_entries\":%llu,"
            "\"installed_utc\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\","
            "\"source_last_modified\":\"%s\",\"source_etag\":\"%s\"}\n",
            (int)kStoreVersion, pack.id, pack.sourceUrl, pack.url, pack.license, web.length, store.dataBytes,
            store.count, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            json::EscapeStrTemp(web.modified), json::EscapeStrTemp(web.etag));
    if (!file::WriteFile(path::JoinTemp(generation, StrL("manifest.json")), manifest))
        return Fail(error, StrL("Cannot write dictionary source manifest."));
    Str old = CurrentGeneration(packDir);
    Str pending = path::JoinTemp(generation, StrL("current.tmp"));
    DiskFile pointer;
    if (!pointer.Open(pending, GENERIC_WRITE, CREATE_NEW) || !pointer.Write(name.s, len(name)) ||
        !FlushFileBuffers(pointer.h))
        return Fail(error, StrL("Cannot save dictionary generation."));
    pointer.Close();
    Str current = path::JoinTemp(packDir, StrL("current"));
    if (!MoveFileExW(CWStrTemp(pending), CWStrTemp(current), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return Fail(error, StrL("Cannot activate the installed dictionary."));
    if (len(old) && !str::Eq(old, name)) dir::RemoveAll(path::JoinTemp(packDir, old));
    return true;
}

struct GzipStream {
    z_stream stream{};
    bool initialized = false, ended = false;
    ~GzipStream() {
        if (initialized) inflateEnd(&stream);
    }
    bool Feed(StoreBuilder& store, const u8* bytes, int count) {
        if (ended) return Fail(store.error, StrL("Unexpected trailing gzip data."));
        if (!initialized) {
            if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK)
                return Fail(store.error, StrL("Cannot start gzip decompression."));
            initialized = true;
        }
        stream.next_in = (Bytef*)bytes;
        stream.avail_in = count;
        u8 buffer[kBuffer];
        do {
            stream.next_out = buffer;
            stream.avail_out = sizeof(buffer);
            int result = inflate(&stream, Z_NO_FLUSH);
            int wrote = sizeof(buffer) - stream.avail_out;
            if (wrote && !store.Feed(buffer, wrote)) return false;
            if (result == Z_STREAM_END) {
                ended = true;
                return !stream.avail_in || Fail(store.error, StrL("Unexpected trailing gzip data."));
            }
            if (result != Z_OK && result != Z_BUF_ERROR)
                return Fail(store.error, StrL("The gzip dictionary is corrupt."));
            if (!wrote && !stream.avail_in) return true;
        } while (stream.avail_in || !stream.avail_out);
        return true;
    }
};

i64 KaikkiRequiredFreeSpace(Str id, i64 downloadBytes) {
    const KaikkiPack* pack = FindKaikkiPack(id);
    if (!pack || downloadBytes <= 0 || (u64)downloadBytes > kMaxDownload) return 0;
    return pack->compressed ? 320LL * 1024 * 1024 : downloadBytes + kMaxIndex + 64 * 1024 * 1024;
}

bool InstallKaikkiPack(Str id, Str folder, const Func1<KaikkiProgress*>& progress, const Func1<bool*>& cancelled,
                       Str* error, i64 approvedBytes) {
    const KaikkiPack* pack = FindKaikkiPack(id);
    if (!pack) return Fail(error, StrL("Unknown Kaikki dictionary."));
    AutoUnlockMutex lock(&gInstallLock);
    if (IsCancelled(cancelled)) return Fail(error, StrL("Dictionary installation cancelled."));
    WebRequest web;
    if (!web.Open(*pack, L"GET", error)) return false;
    if (approvedBytes > 0 && (u64)approvedBytes != web.length)
        return Fail(error, StrL("The source size changed. Confirm the new download size and retry."));
    Str packDir = path::JoinTemp(folder, pack->id);
    if (!dir::CreateAll(packDir)) return Fail(error, StrL("Cannot create dictionary folder."));
    ULARGE_INTEGER available{};
    u64 needed = (u64)KaikkiRequiredFreeSpace(id, (i64)web.length);
    if (!GetDiskFreeSpaceExW(CWStrTemp(packDir), &available, nullptr, nullptr) || available.QuadPart < needed)
        return Fail(error, fmt("This installation needs at least %s of free disk space.", FormatFileSizeTemp(needed)));
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    u64 stamp = ((u64)time.dwHighDateTime << 32) | time.dwLowDateTime;
    Str name = fmt("gen-%016llx-%08x", stamp, GetCurrentThreadId());
    Str generation = path::JoinTemp(packDir, name);
    if (!dir::Create(generation)) return Fail(error, StrL("Cannot create dictionary installation workspace."));
    bool published = false;
    defer {
        if (!published) dir::RemoveAll(generation);
    };
    {
        StoreBuilder store(generation, error);
        if (!store.Open()) return false;
        GzipStream gzip;
        u8 input[kBuffer];
        u64 downloaded = 0;
        while (true) {
            if (IsCancelled(cancelled)) return Fail(error, StrL("Dictionary installation cancelled."));
            DWORD got = 0;
            if (!InternetReadFile(web.request, input, sizeof(input), &got))
                return Fail(error, StrL("Dictionary download failed or timed out."));
            if (!got) break;
            downloaded += got;
            if (downloaded > web.length || downloaded > kMaxDownload)
                return Fail(error, StrL("Dictionary download exceeded the reported size."));
            if (pack->compressed ? !gzip.Feed(store, input, got) : !store.Feed(input, got)) return false;
            KaikkiProgress p{(i64)downloaded, (i64)web.length, false};
            progress.Call(&p);
        }
        if (downloaded != web.length) return Fail(error, StrL("Dictionary download was truncated."));
        if (pack->compressed && !gzip.ended) return Fail(error, StrL("The gzip dictionary is truncated."));
        if (!store.Finish(progress, cancelled)) return false;
        if (IsCancelled(cancelled)) return Fail(error, StrL("Dictionary installation cancelled."));
        if (!PublishStore(*pack, packDir, name, web, store, error)) return false;
        published = true;
    }
    return true;
}

bool RemoveKaikkiPack(Str id, Str folder, Str* error) {
    if (!FindKaikkiPack(id)) return Fail(error, StrL("Unknown Kaikki dictionary."));
    AutoUnlockMutex installLock(&gInstallLock);
    AutoUnlockMutex lock(&gStoreLock);
    Str packDir = path::JoinTemp(folder, id);
    Str old = CurrentGeneration(packDir);
    Str current = path::JoinTemp(packDir, StrL("current"));
    if (file::Exists(current) && !file::Delete(current))
        return Fail(error, StrL("Cannot remove dictionary activation file."));
    if (len(old) && !dir::RemoveAll(path::JoinTemp(packDir, old)))
        return Fail(error, StrL("Cannot remove dictionary records."));
    return true;
}

#if IS_DEBUG
static void CancelTest(bool* cancel) {
    *cancel = true;
}

static bool IndexTestFile(Str source, Str generation, bool compressed, Str* error) {
    DiskFile input;
    StoreBuilder store(generation, error);
    GzipStream gzip;
    if (!input.Open(source) || !store.Open()) return false;
    u8 buffer[kBuffer];
    while (true) {
        DWORD got = 0;
        if (!ReadFile(input.h, buffer, sizeof(buffer), &got, nullptr)) return false;
        if (!got) break;
        if (compressed ? !gzip.Feed(store, buffer, got) : !store.Feed(buffer, got)) return false;
    }
    if (compressed && !gzip.ended) return false;
    return store.Finish({}, {});
}

void KaikkiDictionary_UnitTests() {
    AutoArenaSavepoint scratch;
    Str folder = GetTempFilePathTemp(StrL("kaikki-tests"));
    file::Delete(folder);
    utassert(dir::CreateAll(folder));
    defer {
        dir::RemoveAll(folder);
    };
    Str packDir = path::JoinTemp(folder, kPacks[0].id);
    Str name = StrL("gen-0000000000000001-00000001");
    Str generation = path::JoinTemp(packDir, name);
    utassert(dir::CreateAll(generation));
    Str error;
    defer {
        str::Free(error);
    };
    Str fixture = StrL(
        "{\"word\":\"Cat\",\"forms\":[{\"form\":\"cats\"}],\"senses\":[{\"glosses\":[\"animal\"]}]}\n"
        "{\"word\":\"cat\",\"senses\":[{\"glosses\":[\"person\"]}]}\n"
        "{\"word\":\"!\",\"pos\":\"symbol\",\"senses\":[{\"glosses\":[\"exclamation\"]}]}\n"
        "{\"word\":\";\",\"pos\":\"symbol\"}\n"
        "{\"word\":\")\",\"pos\":\"symbol\"}\n"
        "{\"word\":\"(\",\"pos\":\"symbol\"}\n"
        "{\"word\":\"\\\"\",\"pos\":\"symbol\"}\n");
    {
        StoreBuilder store(generation, &error);
        utassert(store.Open());
        for (int i = 0; i < len(fixture); i++) utassert(store.Feed((const u8*)fixture.s + i, 1));
        utassert(store.Finish({}, {}));
        WebRequest web;
        web.length = len(fixture);
        utassert(PublishStore(kPacks[0], packDir, name, web, store, &error));
    }
    utassert(KaikkiPackInstalled(kPacks[0].id, folder));
    StrVec records;
    utassert(LookupKaikkiRecords(kPacks[0].id, folder, StrL("CAT"), records, &error));
    utassert(len(records) == 2);
    records.Reset();
    utassert(LookupKaikkiRecords(kPacks[0].id, folder, StrL("cats"), records, &error));
    utassert(len(records) == 1);
    records.Reset();
    utassert(LookupKaikkiRecords(kPacks[0].id, folder, StrL("missing"), records, &error) && len(records) == 0);
    utassert(!RemoveKaikkiPack(StrL("../bad"), folder, &error));
    utassert(!IsGeneration(StrL("gen-../../../../../../../../../")));
    {
        RecordWords words;
        utassert(!words.Parse(StrL("{\"word\":\"cat\"")));
        utassert(words.Parse(StrL("{\"title\":\"Main page\",\"redirect\":\"Main Page\",\"pos\":\"hard-redirect\"}")));
    }
    Str badDir = path::JoinTemp(folder, StrL("bad"));
    utassert(dir::Create(badDir));
    {
        StoreBuilder store(badDir, &error);
        utassert(store.Open());
        utassert(!store.Feed((const u8*)"{broken}\n", 9));
    }
    Str cancelDir = path::JoinTemp(packDir, StrL("gen-0000000000000002-00000001"));
    utassert(dir::Create(cancelDir));
    {
        StoreBuilder store(cancelDir, &error);
        utassert(store.Open());
        utassert(store.Feed((const u8*)fixture.s, len(fixture)));
        utassert(!store.Finish({}, MkFunc1Void<bool*>(&CancelTest)));
    }
    utassert(str::Eq(CurrentGeneration(packDir), name));

    Vec<u8> compressed;
    utassert(VecResize(compressed, len(fixture) + 256));
    z_stream compressor{};
    utassert(deflateInit2(&compressor, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) ==
             Z_OK);
    compressor.next_in = (Bytef*)fixture.s;
    compressor.avail_in = len(fixture);
    compressor.next_out = VecData(compressed);
    compressor.avail_out = len(compressed);
    utassert(deflate(&compressor, Z_FINISH) == Z_STREAM_END);
    int gzipBytes = (int)compressor.total_out;
    deflateEnd(&compressor);
    Str gzipDir = path::JoinTemp(folder, StrL("gzip"));
    utassert(dir::Create(gzipDir));
    {
        StoreBuilder store(gzipDir, &error);
        GzipStream gzip;
        utassert(store.Open());
        for (int i = 0; i < gzipBytes; i++) utassert(gzip.Feed(store, VecData(compressed) + i, 1));
        utassert(gzip.ended);
        utassert(store.Finish({}, {}));
    }
    Str truncatedDir = path::JoinTemp(folder, StrL("truncated"));
    utassert(dir::Create(truncatedDir));
    {
        StoreBuilder store(truncatedDir, &error);
        GzipStream gzip;
        utassert(store.Open());
        utassert(gzip.Feed(store, VecData(compressed), gzipBytes - 4));
        utassert(!gzip.ended);
    }
    Str corruptDir = path::JoinTemp(folder, StrL("corrupt"));
    utassert(dir::Create(corruptDir));
    {
        StoreBuilder store(corruptDir, &error);
        GzipStream gzip;
        utassert(store.Open());
        compressed[gzipBytes - 8] ^= 1;
        utassert(!gzip.Feed(store, VecData(compressed), gzipBytes));
    }

    // A sparse record beyond 4 GiB exercises both u64 offsets and collision filtering.
    Str sparseDir = path::JoinTemp(folder, kPacks[1].id);
    Str sparseGen = path::JoinTemp(sparseDir, name);
    utassert(dir::CreateAll(sparseGen));
    Str dog = StrL("{\"word\":\"dog\",\"senses\":[{\"glosses\":[\"animal\"]}]}");
    u64 largeOffset = (1ULL << 32) + 123;
    {
        DiskFile data, index;
        utassert(data.Open(path::JoinTemp(sparseGen, StrL("records.jsonl")), GENERIC_WRITE, CREATE_NEW));
        DWORD returned = 0;
        DeviceIoControl(data.h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
        utassert(data.Seek(largeOffset) && data.Write(dog.s, len(dog)));
        utassert(index.Open(path::JoinTemp(sparseGen, StrL("index.bin")), GENERIC_WRITE, CREATE_NEW));
        StoreHeader header{kIndexMagic, kStoreVersion, 1, largeOffset + len(dog)};
        StoreEntry entry{WordHash(StrL("cat")), largeOffset, (u32)len(dog), 0};
        utassert(index.Write(&header, sizeof(header)) && index.Write(&entry, sizeof(entry)));
        utassert(file::WriteFile(path::JoinTemp(sparseDir, StrL("current")), name));
    }
    records.Reset();
    utassert(LookupKaikkiRecords(kPacks[1].id, folder, StrL("cat"), records, &error) && len(records) == 0);
    {
        DiskFile index;
        utassert(index.Open(path::JoinTemp(sparseGen, StrL("index.bin")), GENERIC_WRITE));
        StoreEntry entry{WordHash(StrL("dog")), largeOffset, (u32)len(dog), 0};
        utassert(index.Seek(sizeof(StoreHeader)) && index.Write(&entry, sizeof(entry)));
    }
    records.Reset();
    utassert(LookupKaikkiRecords(kPacks[1].id, folder, StrL("dog"), records, &error) && len(records) == 1);
    {
        DiskFile index;
        utassert(index.Open(path::JoinTemp(sparseGen, StrL("index.bin")), GENERIC_WRITE));
        StoreEntry entry{WordHash(StrL("dog")), UINT64_MAX, 100, 0};
        utassert(index.Seek(sizeof(StoreHeader)) && index.Write(&entry, sizeof(entry)));
    }
    records.Reset();
    utassert(!LookupKaikkiRecords(kPacks[1].id, folder, StrL("dog"), records, &error));

    const WCHAR* envNames[] = {L"SUMATRA_KAIKKI_TEST_JSONL", L"SUMATRA_KAIKKI_TEST_GZIP"};
    for (int i = 0; i < dimof(envNames); i++) {
        WCHAR source[32768]{};
        DWORD size = GetEnvironmentVariableW(envNames[i], source, dimof(source));
        if (!size || size >= dimof(source)) continue;
        Str fixtureRoot = path::JoinTemp(folder, fmt("full-fixture-%d", i));
        Str fixturePack = path::JoinTemp(fixtureRoot, kPacks[i].id);
        Str fixtureDir = path::JoinTemp(fixturePack, name);
        utassert(dir::CreateAll(fixtureDir));
        bool indexed = IndexTestFile(ToUtf8Temp(WStr(source, size)), fixtureDir, i == 1, &error);
        if (!indexed) logf("Kaikki fixture %d failed: %s\n", i, error);
        utassert(indexed);
        if (!indexed) continue;
        Str lookup;
        defer {
            str::Free(lookup);
        };
        StoreHeader header;
        {
            DiskFile index, data;
            bool opened = OpenStore(fixtureDir, index, data, header);
            utassert(opened);
            if (!opened) continue;
            utassert(header.count > 0);
            StoreEntry entry;
            bool read = ReadEntry(index, header, 0, entry);
            utassert(read);
            if (!read) continue;
            Vec<char> line;
            bool readRecord =
                VecResize(line, entry.bytes) && data.Seek(entry.offset) && data.Read(VecData(line), entry.bytes);
            utassert(readRecord);
            if (!readRecord) continue;
            RecordWords words;
            bool parsed = words.Parse(Str(VecData(line), len(line))) && len(words.words) > 0;
            utassert(parsed);
            if (!parsed) continue;
            lookup = str::Dup(words.words[0]);
        }
        StoreBuilder store(fixtureDir, &error);
        store.count = header.count;
        store.dataBytes = header.dataBytes;
        WebRequest web;
        web.length = file::GetSize(ToUtf8Temp(WStr(source, size)));
        utassert(PublishStore(kPacks[i], fixturePack, name, web, store, &error));
        records.Reset();
        utassert(LookupKaikkiRecords(kPacks[i].id, fixtureRoot, lookup, records, &error) && len(records) > 0);
    }
    WCHAR httpTest[4]{};
    if (GetEnvironmentVariableW(L"SUMATRA_KAIKKI_TEST_HTTP", httpTest, dimof(httpTest)) == 1 && httpTest[0] == '1') {
        for (const auto& pack : kPacks) {
            i64 bytes = 0;
            utassert(KaikkiDownloadSize(pack.id, bytes, &error));
            utassert(bytes > 0 && (u64)bytes <= kMaxDownload);
        }
    }
    utassert(KaikkiPackInstalled(kPacks[0].id, folder));
    utassert(RemoveKaikkiPack(kPacks[0].id, folder, &error));
    utassert(!KaikkiPackInstalled(kPacks[0].id, folder));
}
#endif
