/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Http.h"
#include "base/Zip.h"
#include "base/JsonParser.h"
#include "Settings.h"
#include "AppSettings.h"
#include "VocabularyDecks.h"
#include "Vocabulary.h"
#include "OfflineDictionary.h"

constexpr int kDictionaryMaxFile = 256 * 1024 * 1024;
constexpr int kDictionaryMaxEntries = 2000000;
constexpr int kDictionaryMaxDefinition = 256 * 1024;
constexpr int kDictionaryMaxWord = 512;
constexpr int kDictionaryMaxResults = 64;

static RecursiveMutex gDictionaryLock;
static Mutex gDictionaryInstallLock;
static Str kWordNetId = StrL("wordnet-en");
static Str kWordNetTitle = StrL("Princeton WordNet 3.0 (English)");
static const char* kWordNetFiles[] = {"data.noun", "data.verb", "data.adj", "data.adv"};
static const char* kWordNetExceptions[] = {"noun.exc", "verb.exc", "adj.exc", "adv.exc"};
static const char* kWordNetPackFiles[] = {"data.noun", "data.verb", "data.adj", "data.adv",
                                          "noun.exc",  "verb.exc",  "adj.exc",  "adv.exc"};

static bool FailDictionary(Str* error, Str message) {
    if (error) {
        str::ReplaceWithCopy(error, message);
    }
    return false;
}

TempStr GetDictionaryDirTemp() {
    return path::JoinTemp(path::GetDirTemp(GetSettingsPathTemp()), StrL("dictionaries"));
}

static bool IsValidUtf8(Str text) {
    return len(text) > 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.s, len(text), nullptr, 0) > 0;
}

static Str Trim(Str text) {
    str::TrimWsBoth(text);
    return text;
}

static Str NextToken(Str& text) {
    text = Trim(text);
    int n = 0;
    while (n < len(text) && !str::IsWs(text.s[n])) {
        n++;
    }
    Str token(text.s, n);
    text = Str(text.s + n, len(text) - n);
    return token;
}

static TempStr WordKey(Str text) {
    text = Trim(text);
    while (len(text) > 0 && strchr("\"'.,;:!?()[]{}", text.s[0])) {
        text = Str(text.s + 1, len(text) - 1);
    }
    while (len(text) > 0 && strchr("\"'.,;:!?()[]{}", text.s[len(text) - 1])) {
        text = Str(text.s, len(text) - 1);
    }
    if (len(text) == 0 || len(text) > kDictionaryMaxWord || !IsValidUtf8(text)) {
        return {};
    }
    TempStr spaces = str::ReplaceTemp(text, StrL("_"), StrL(" "));
    TempWStr wide = ToWStrTemp(spaces);
    int count = LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, wide.s, len(wide), nullptr, 0);
    if (count <= 0) {
        return str::DupTemp(spaces);
    }
    WCHAR* lower = AllocArray<WCHAR>(GetTempArena(), count + 1);
    LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, wide.s, len(wide), lower, count);
    return ToUtf8Temp(WStr(lower, count));
}

static TempStr PlainDefinition(Str text) {
    str::Builder plain;
    bool tag = false;
    for (int i = 0; i < len(text); i++) {
        char c = text.s[i];
        if (c == '<') {
            tag = true;
            plain.AppendChar(' ');
        } else if (c == '>') {
            tag = false;
        } else if (!tag && c != 0 && ((u8)c >= 32 || c == '\n' || c == '\t')) {
            plain.AppendChar(c);
        }
    }
    TempStr value = str::ReplaceTemp(ToStr(plain), StrL("&amp;"), StrL("&"));
    value = str::ReplaceTemp(value, StrL("&lt;"), StrL("<"));
    value = str::ReplaceTemp(value, StrL("&gt;"), StrL(">"));
    value = str::ReplaceTemp(value, StrL("&quot;"), StrL("\""));
    value = str::ReplaceTemp(value, StrL("&nbsp;"), StrL(" "));
    return str::DupTemp(Trim(value));
}

struct DictEntry {
    Str key, headword, definition, id, title;
};

struct DictInflection {
    Str word, lemma;
};

struct DictionaryIndex {
    Arena* arena = ArenaNew();
    Vec<DictEntry> entries;
    Vec<DictInflection> inflections;
    ~DictionaryIndex() { ArenaDelete(arena); }

    bool Add(Str word, Str definition, Str id, Str title) {
        TempStr key = WordKey(word);
        if (len(key) == 0 || len(definition) == 0 || len(definition) > kDictionaryMaxDefinition ||
            !IsValidUtf8(definition) || len(entries) >= kDictionaryMaxEntries ||
            arena->Pos() > (u64)kDictionaryMaxFile) {
            return false;
        }
        TempStr plain = PlainDefinition(definition);
        if (len(plain) == 0) {
            return false;
        }
        VecAppend(entries, {str::Dup(arena, key), str::Dup(arena, word), str::Dup(arena, plain), str::Dup(arena, id),
                            str::Dup(arena, title)});
        return true;
    }

    void Exceptions(Str data) {
        while (len(data) > 0 && len(inflections) < 100000) {
            char* end = (char*)memchr(data.s, '\n', len(data));
            int n = end ? (int)(end - data.s) : len(data);
            Str line(data.s, n);
            data = end ? Str(end + 1, len(data) - n - 1) : Str();
            TempStr word = WordKey(NextToken(line));
            if (len(word) == 0) {
                continue;
            }
            for (int i = 0; i < 32 && len(Trim(line)) > 0; i++) {
                TempStr lemma = WordKey(NextToken(line));
                if (len(lemma) > 0) {
                    VecAppend(inflections, {str::Dup(arena, word), str::Dup(arena, lemma)});
                }
            }
        }
    }

    bool Tsv(Str data, Str id, Str title) {
        if (len(data) >= 3 && memcmp(data.s, "\xef\xbb\xbf", 3) == 0) {
            data = Str(data.s + 3, len(data) - 3);
        }
        int start = len(entries);
        while (len(data) > 0) {
            char* end = (char*)memchr(data.s, '\n', len(data));
            int n = end ? (int)(end - data.s) : len(data);
            Str line = Trim(Str(data.s, n));
            data = end ? Str(end + 1, len(data) - n - 1) : Str();
            if (len(line) == 0 || line.s[0] == '#') {
                continue;
            }
            char* tab = (char*)memchr(line.s, '\t', len(line));
            if (!tab ||
                !Add(Str(line.s, (int)(tab - line.s)), Str(tab + 1, len(line) - (int)(tab + 1 - line.s)), id, title)) {
                return false;
            }
        }
        return len(entries) > start;
    }

    bool WordNet(Str data) {
        int start = len(entries);
        while (len(data) > 0) {
            char* end = (char*)memchr(data.s, '\n', len(data));
            int n = end ? (int)(end - data.s) : len(data);
            Str line(data.s, n);
            data = end ? Str(end + 1, len(data) - n - 1) : Str();
            if (len(line) < 10 || line.s[0] < '0' || line.s[0] > '9') {
                continue;
            }
            char* bar = (char*)memchr(line.s, '|', len(line));
            if (!bar) {
                return false;
            }
            Str tokens(line.s, (int)(bar - line.s));
            NextToken(tokens);
            NextToken(tokens);
            NextToken(tokens);
            Str wordCount = NextToken(tokens);
            char* tail = nullptr;
            unsigned long count = strtoul(CStrTemp(wordCount), &tail, 16);
            if (!count || count > 255 || !tail || *tail != 0) {
                return false;
            }
            Str gloss = Trim(Str(bar + 1, len(line) - (int)(bar + 1 - line.s)));
            for (unsigned long i = 0; i < count; i++) {
                Str word = NextToken(tokens);
                NextToken(tokens);
                if (str::EndsWith(word, StrL("(a)")) || str::EndsWith(word, StrL("(p)"))) {
                    word = Str(word.s, len(word) - 3);
                } else if (str::EndsWith(word, StrL("(ip)"))) {
                    word = Str(word.s, len(word) - 4);
                }
                TempStr display = str::ReplaceTemp(word, StrL("_"), StrL(" "));
                if (!Add(display, gloss, kWordNetId, kWordNetTitle)) {
                    return false;
                }
            }
        }
        return len(entries) > start;
    }

    bool StarDict(Str info, Str index, Str dictionary, Str id, Str title) {
        if (!str::StartsWith(info, StrL("StarDict's dict ifo file\n")) &&
            !str::StartsWith(info, StrL("StarDict's dict ifo file\r\n"))) {
            return false;
        }
        if (str::Contains(info, StrL("idxoffsetbits=64"))) {
            return false;
        }
        Str types;
        char* typeStart = strstr(CStrTemp(info), "sametypesequence=");
        if (typeStart) {
            typeStart += strlen("sametypesequence=");
            int n = 0;
            while (typeStart[n] && typeStart[n] != '\n' && typeStart[n] != '\r') {
                n++;
            }
            types = Str(typeStart, n);
        }
        int start = len(entries);
        while (len(index) > 0) {
            char* zero = (char*)memchr(index.s, 0, len(index));
            if (!zero || zero - index.s > kDictionaryMaxWord || len(index) - (int)(zero - index.s) < 9) {
                return false;
            }
            Str word(index.s, (int)(zero - index.s));
            const u8* numbers = (const u8*)zero + 1;
            u32 offset = ((u32)numbers[0] << 24) | ((u32)numbers[1] << 16) | ((u32)numbers[2] << 8) | numbers[3];
            u32 size = ((u32)numbers[4] << 24) | ((u32)numbers[5] << 16) | ((u32)numbers[6] << 8) | numbers[7];
            int used = (int)(zero - index.s) + 9;
            index = Str(index.s + used, len(index) - used);
            if (size == 0 || size > kDictionaryMaxDefinition || (u64)offset + size > (u64)len(dictionary)) {
                return false;
            }
            Str record(dictionary.s + offset, (int)size);
            str::Builder definition;
            int field = 0;
            while (len(record) > 0) {
                char type = len(types) > 0 ? (field < len(types) ? types.s[field] : 0) : record.s[0];
                if (len(types) == 0) {
                    record = Str(record.s + 1, len(record) - 1);
                }
                if (!type) {
                    return false;
                }
                bool last = len(types) > 0 && field == len(types) - 1;
                if (type >= 'A' && type <= 'Z') {
                    if (last) {
                        break;
                    }
                    if (len(record) < 4) {
                        return false;
                    }
                    const u8* p = (const u8*)record.s;
                    u32 n = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
                    if ((u64)n + 4 > (u64)len(record)) {
                        return false;
                    }
                    record = Str(record.s + n + 4, len(record) - (int)n - 4);
                } else {
                    char* z = last ? nullptr : (char*)memchr(record.s, 0, len(record));
                    if (!last && !z) {
                        return false;
                    }
                    int n = last ? len(record) : (int)(z - record.s);
                    if (strchr("mlhngx", type)) {
                        if (len(definition) > 0) {
                            definition.AppendChar('\n');
                        }
                        definition.Append(Str(record.s, n));
                    }
                    record = last ? Str() : Str(record.s + n + 1, len(record) - n - 1);
                }
                field++;
            }
            if (!Add(word, ToStr(definition), id, title)) {
                return false;
            }
        }
        return len(entries) > start;
    }
};

static DictionaryIndex* gDictionaryIndex = nullptr;

void ResetOfflineDictionaryCache() {
    AutoUnlockRecursiveMutex lock(&gDictionaryLock);
    delete gDictionaryIndex;
    gDictionaryIndex = nullptr;
}

static bool ReadDictionary(Str path, Str& out) {
    i64 size = file::GetSize(path);
    if (size <= 0 || size > kDictionaryMaxFile) {
        return false;
    }
    out = file::ReadFile(path);
    if (len(out) != size) {
        str::Free(out);
        out = {};
        return false;
    }
    return true;
}

static bool IsWmPack(Str path) {
    return str::EndsWithI(path, StrL(".wmvocab.json")) || str::EndsWithI(path, StrL(".wmvocab.json.gz"));
}
static TempStr WmPackId(Str path) {
    Str name = path::GetBaseNameTemp(path);
    int suffix = str::EndsWithI(name, StrL(".gz")) ? 16 : 13;
    return str::DupTemp(Str(name.s, len(name) - suffix));
}
struct WmSense {
    Str definition, pos;
};
struct WmWord {
    Str word;
    Vec<WmSense> senses;
};
struct WmParse {
    Arena* arena = ArenaNew();
    Vec<WmWord*> words;
    Str format, title;
    bool valid = true;
    bool version = false;
    ~WmParse() {
        for (WmWord* word : words) delete word;
        ArenaDelete(arena);
    }
    void Visit(json::Value* v) {
        if (!valid) {
            v->stop = true;
            return;
        }
        if (json::PathMatch(v->path, StrL("/version")))
            version = v->type == json::Type::Number && str::Eq(v->value, StrL("1"));
        if (json::PathMatch(v->path, StrL("/format"))) format = str::Dup(arena, v->value);
        if (json::PathMatch(v->path, StrL("/pack"), StrL("/name"))) title = str::Dup(arena, v->value);
        if (!str::Eq(json::PathSegKey(json::PathNth(v->path, 0)), StrL("words"))) return;
        int wordIndex = json::PathSegIndex(json::PathNth(v->path, 1));
        if (wordIndex < 0 || wordIndex >= 20000 || arena->Pos() > (u64)kDictionaryMaxFile) {
            valid = false;
            v->stop = true;
            return;
        }
        while (len(words) <= wordIndex) VecAppend(words, new WmWord());
        WmWord* word = words[wordIndex];
        if (json::PathMatch(v->path, StrL("/words"), StrL("*"), StrL("/word"))) {
            word->word = str::Dup(arena, v->value);
            return;
        }
        if (!str::Eq(json::PathSegKey(json::PathNth(v->path, 2)), StrL("senses"))) return;
        int sense = json::PathSegIndex(json::PathNth(v->path, 3));
        if (sense < 0 || sense > 255) {
            valid = false;
            v->stop = true;
            return;
        }
        while (len(word->senses) <= sense) VecAppend(word->senses, WmSense{});
        Str key = json::PathSegKey(json::PathNth(v->path, 4));
        if (v->type != json::Type::String) return;
        if (str::Eq(key, StrL("definition"))) word->senses[sense].definition = str::Dup(arena, v->value);
        if (str::Eq(key, StrL("pos"))) word->senses[sense].pos = str::Dup(arena, v->value);
    }
};
static bool LoadWmJson(DictionaryIndex& index, Str text, Str title) {
    WmParse pack;
    if (!IsValidUtf8(text) || !json::Parse(text, MkMethod1<WmParse, json::Value*, &WmParse::Visit>(&pack)) ||
        !pack.valid || !pack.version || !str::Eq(pack.format, StrL("wmkeyboard-vocab")))
        return false;
    if (len(pack.title)) title = pack.title;
    int start = len(index.entries);
    for (WmWord* word : pack.words) {
        if (len(word->word) == 0) return false;
        for (WmSense& sense : word->senses) {
            if (len(sense.definition) == 0) continue;
            Str definition = len(sense.pos) ? fmt("%s: %s", sense.pos, sense.definition) : sense.definition;
            if (!index.Add(word->word, definition, StrL("wmkeyboard-vocab-en"), title)) return false;
        }
    }
    return len(index.entries) > start;
}
static bool LoadDictionaryFile(DictionaryIndex& target, Str filePath, Str* error) {
    Str id = path::GetBaseNameTemp(path::GetPathNoExtTemp(filePath));
    Str data;
    if (!ReadDictionary(filePath, data)) {
        return FailDictionary(error, StrL("Dictionary file is missing or exceeds 256 MB."));
    }
    bool ok = false;
    if (IsWmPack(filePath)) {
        if (str::EndsWithI(filePath, StrL(".gz"))) {
            Str raw = Ungzip(data, kDictionaryMaxFile);
            str::Free(data);
            data = raw;
        }
        ok = LoadWmJson(target, data, WmPackId(filePath));
    } else if (str::EqI(path::GetExtTemp(filePath), StrL(".tsv"))) {
        ok = target.Tsv(data, id, id);
    } else {
        Str base = path::GetPathNoExtTemp(filePath);
        Str index, dictionary, compressed;
        bool hasIndex = ReadDictionary(fmt("%s.idx", base), index);
        if (!hasIndex && ReadDictionary(fmt("%s.idx.gz", base), compressed)) {
            index = Ungzip(compressed, kDictionaryMaxFile);
            str::Free(compressed);
        }
        bool hasDict = ReadDictionary(fmt("%s.dict", base), dictionary);
        if (!hasDict && ReadDictionary(fmt("%s.dict.dz", base), compressed)) {
            dictionary = Ungzip(compressed, kDictionaryMaxFile);
            str::Free(compressed);
        }
        int entryStart = len(target.entries);
        if (len(index) > 0 && len(dictionary) > 0) {
            ok = target.StarDict(data, index, dictionary, id, id);
        }
        int originalCount = len(target.entries) - entryStart;
        Str aliases;
        if (ok && file::Exists(fmt("%s.syn", base))) {
            ok = ReadDictionary(fmt("%s.syn", base), aliases);
            Str remaining = aliases;
            while (ok && len(remaining) > 0) {
                char* zero = (char*)memchr(remaining.s, 0, len(remaining));
                if (!zero || zero - remaining.s > kDictionaryMaxWord ||
                    len(remaining) - (int)(zero - remaining.s) < 5) {
                    ok = false;
                    break;
                }
                Str alias(remaining.s, (int)(zero - remaining.s));
                const u8* bytes = (const u8*)zero + 1;
                u32 reference = ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) | ((u32)bytes[2] << 8) | bytes[3];
                if (reference >= (u32)originalCount) {
                    ok = false;
                    break;
                }
                DictEntry original = target.entries[entryStart + (int)reference];
                ok = target.Add(alias, original.definition, id, id);
                int used = (int)(zero - remaining.s) + 5;
                remaining = Str(remaining.s + used, len(remaining) - used);
            }
            str::Free(aliases);
        }
        str::Free(index);
        str::Free(dictionary);
    }
    str::Free(data);
    return ok ? true
              : FailDictionary(error, StrL("Malformed dictionary or unsupported StarDict fields/64-bit offsets."));
}

static bool LoadWordNet(DictionaryIndex& index, Str folder) {
    int start = len(index.entries);
    for (const char* name : kWordNetFiles) {
        Str data;
        if (!ReadDictionary(path::JoinTemp(folder, Str(name)), data)) {
            VecRemoveAtN(index.entries, start, len(index.entries) - start);
            return false;
        }
        bool ok = index.WordNet(data);
        str::Free(data);
        if (!ok) {
            VecRemoveAtN(index.entries, start, len(index.entries) - start);
            return false;
        }
    }
    for (const char* name : kWordNetExceptions) {
        Str data;
        if (ReadDictionary(path::JoinTemp(folder, Str(name)), data)) {
            index.Exceptions(data);
            str::Free(data);
        }
    }
    return true;
}

static void ImportPaths(StrVec& files) {
    WIN32_FIND_DATAW info{};
    HANDLE find = FindFirstFileW(CWStrTemp(path::JoinTemp(GetDictionaryDirTemp(), StrL("*"))), &info);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        TempStr name = ToUtf8Temp(WStr(info.cFileName));
        Str ext = path::GetExtTemp(name);
        if (str::EqI(ext, StrL(".tsv")) || str::EqI(ext, StrL(".ifo")) || IsWmPack(name)) {
            files.Append(path::JoinTemp(GetDictionaryDirTemp(), name));
        }
    } while (FindNextFileW(find, &info));
    FindClose(find);
}

void FreeOfflineMeanings(Vec<OfflineMeaning>& meanings) {
    for (OfflineMeaning& meaning : meanings) {
        str::Free(meaning.headword);
        str::Free(meaning.definition);
        str::Free(meaning.dictionary);
        str::Free(meaning.dictionaryId);
    }
    VecClear(meanings);
}

static void MatchWord(DictionaryIndex& index, Str key, Vec<OfflineMeaning>& out, bool englishOnly,
                      bool wmOnly = false) {
    Vec<DictEntry>& entries = index.entries;
    int low = 0, high = len(entries);
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (str::Cmp(entries[middle].key, key) < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    for (int i = low; i < len(entries) && str::Eq(entries[i].key, key) && len(out) < kDictionaryMaxResults; i++) {
        DictEntry& e = entries[i];
        if ((englishOnly && !str::Eq(e.id, kWordNetId)) || (wmOnly && !str::Eq(e.id, StrL("wmkeyboard-vocab-en")))) {
            continue;
        }
        bool duplicate = false;
        for (OfflineMeaning& m : out) {
            if (str::Eq(m.dictionaryId, e.id) && str::Eq(m.definition, e.definition)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            VecAppend(out, {str::Dup(e.headword), str::Dup(e.definition), str::Dup(e.title), str::Dup(e.id)});
        }
    }
}

bool LookupOfflineWord(Str word, Vec<OfflineMeaning>& out, Str* error) {
    FreeOfflineMeanings(out);
    TempStr key = WordKey(word);
    if (len(key) == 0) {
        return FailDictionary(error, StrL("Select a word or short phrase (up to 512 UTF-8 bytes)."));
    }
    AutoUnlockRecursiveMutex lock(&gDictionaryLock);
    if (!gDictionaryIndex) {
        gDictionaryIndex = new DictionaryIndex();
        bool wordNet = LoadWordNet(*gDictionaryIndex, path::JoinTemp(GetDictionaryDirTemp(), kWordNetId));
        if (!wordNet) {
            LoadWordNet(*gDictionaryIndex, path::JoinTemp(GetSelfExeDirTemp(), StrL("dictionaries\\wordnet-en")));
        }
        StrVec files;
        ImportPaths(files);
        for (Str file : files) {
            int start = len(gDictionaryIndex->entries);
            if (!LoadDictionaryFile(*gDictionaryIndex, file, nullptr)) {
                VecRemoveAtN(gDictionaryIndex->entries, start, len(gDictionaryIndex->entries) - start);
            }
        }
        VecSort(gDictionaryIndex->entries,
                [](const DictEntry* a, const DictEntry* b) { return str::Cmp(a->key, b->key); });
    }
    if (len(gDictionaryIndex->entries) == 0 && len(VocabularyBuiltinMeaning(key)) == 0) {
        return FailDictionary(error,
                              StrL("No dictionary installed. Download WordNet or import a TSV/StarDict dictionary."));
    }
    MatchWord(*gDictionaryIndex, key, out, false, true);
    if (len(out)) return true;
    Str builtin = VocabularyBuiltinMeaning(key);
    if (len(builtin)) {
        VecAppend(out,
                  {str::Dup(key), str::Dup(builtin), str::Dup(StrL("WMKeyboard vocabulary packs (Wiktionary/WordNet)")),
                   str::Dup(StrL("wmkeyboard-vocab-en"))});
        return true;
    }
    MatchWord(*gDictionaryIndex, key, out, false);
    if (len(out) == 0) {
        StrVec candidates;
        for (DictInflection& inflection : gDictionaryIndex->inflections) {
            if (str::Eq(inflection.word, key)) {
                candidates.Append(inflection.lemma);
            }
        }
        struct Suffix {
            const char* end;
            const char* replace;
        };
        const Suffix suffixes[] = {{"ies", "y"}, {"s", ""},    {"es", ""},  {"ing", ""}, {"ing", "e"},
                                   {"ied", "y"}, {"ed", ""},   {"ed", "e"}, {"er", ""},  {"er", "e"},
                                   {"est", ""},  {"est", "e"}, {"'s", ""}};
        for (const Suffix& suffix : suffixes) {
            int n = len(Str(suffix.end));
            if (len(key) > n + 1 && str::EndsWith(key, Str(suffix.end))) {
                Str base(key.s, len(key) - n);
                candidates.Append(fmt("%s%s", base, Str(suffix.replace)));
                if ((str::Eq(Str(suffix.end), StrL("ing")) || str::Eq(Str(suffix.end), StrL("ed"))) && len(base) > 2 &&
                    base.s[len(base) - 1] == base.s[len(base) - 2]) {
                    candidates.Append(Str(base.s, len(base) - 1));
                }
            }
        }
        for (Str candidate : candidates) {
            MatchWord(*gDictionaryIndex, candidate, out, true);
        }
    }
    return true;
}

void FreeDictionaryCatalog(Vec<OfflineDictPack>& packs) {
    for (OfflineDictPack& p : packs) {
        str::Free(p.id);
        str::Free(p.title);
        str::Free(p.language);
        str::Free(p.license);
        str::Free(p.sourceUrl);
    }
    VecClear(packs);
}

void GetDictionaryCatalog(Vec<OfflineDictPack>& packs) {
    FreeDictionaryCatalog(packs);
    for (const auto& source : builtinVocabDecks) {
        Str packId = fmt("wm-%s", Str(source.id));
        VecAppend(
            packs,
            {str::Dup(packId), str::Dup(Str(source.name)), str::Dup(StrL("English")),
             str::Dup(StrL("Wiktionary CC BY-SA 3.0; WordNet license; lists MIT")),
             str::Dup(fmt("https://github.com/wasi-master/wmkeyboard-data/blob/master/vocab/en/%s.wmvocab.json.gz",
                          Str(source.id))),
             true, true});
    }
    bool bundled = file::Exists(path::JoinTemp(GetSelfExeDirTemp(), StrL("dictionaries\\wordnet-en\\data.noun")));
    bool installed = file::Exists(path::JoinTemp(GetDictionaryDirTemp(), StrL("wordnet-en\\data.noun")));
    VecAppend(packs, {str::Dup(kWordNetId), str::Dup(kWordNetTitle), str::Dup(StrL("English")),
                      str::Dup(StrL("Princeton WordNet license")), str::Dup(StrL("https://wordnet.princeton.edu/")),
                      installed || bundled, bundled});
    StrVec files;
    ImportPaths(files);
    for (Str file : files) {
        if (IsWmPack(file)) {
            Str sourceId = WmPackId(file);
            bool known = false;
            for (const auto& source : builtinVocabDecks) known |= str::Eq(sourceId, Str(source.id));
            if (!known)
                VecAppend(packs,
                          {str::Dup(fmt("wm-%s", sourceId)), str::Dup(sourceId), str::Dup(StrL("Imported")),
                           str::Dup(StrL("Original pack attribution stored in imported JSON")), Str(), true, false});
            continue;
        }
        Str id = path::GetBaseNameTemp(path::GetPathNoExtTemp(file));
        VecAppend(packs, {str::Dup(id), str::Dup(id), str::Dup(StrL("Imported")),
                          str::Dup(StrL("Source dictionary license")), Str(), true, false});
    }
}

bool DownloadDictionaryPack(Str id, Str* error) {
    AutoUnlockMutex installLock(&gDictionaryInstallLock);
    if (str::StartsWith(id, StrL("wm-"))) {
        Str sourceId(id.s + 3, len(id) - 3);
        bool known = false;
        for (const auto& deck : builtinVocabDecks) known |= str::Eq(sourceId, Str(deck.id));
        if (!known) return FailDictionary(error, StrL("Unknown vocabulary dictionary pack."));
        if (!dir::CreateAll(GetDictionaryDirTemp()))
            return FailDictionary(error, StrL("Cannot create dictionary folder."));
        Str pending = path::JoinTemp(GetDictionaryDirTemp(), fmt("%s.wmvocab.json.gz.pending", sourceId));
        Str url =
            fmt("https://raw.githubusercontent.com/wasi-master/wmkeyboard-data/master/vocab/en/%s.wmvocab.json.gz",
                sourceId);
        bool ok = HttpGetToFile(url, pending, {}, kDictionaryMaxFile);
        Str compressed, jsonText;
        if (ok) ok = ReadDictionary(pending, compressed);
        if (ok) jsonText = Ungzip(compressed, kDictionaryMaxFile);
        str::Free(compressed);
        DictionaryIndex check;
        ok = ok && LoadWmJson(check, jsonText, sourceId);
        str::Free(jsonText);
        if (ok) {
            AutoUnlockRecursiveMutex lock(&gDictionaryLock);
            ok =
                file::Copy(path::JoinTemp(GetDictionaryDirTemp(), fmt("%s.wmvocab.json.gz", sourceId)), pending, false);
            if (ok) ResetOfflineDictionaryCache();
        }
        file::Delete(pending);
        return ok ? true : FailDictionary(error, StrL("Vocabulary dictionary download or validation failed."));
    }
    if (!str::Eq(id, kWordNetId)) {
        return FailDictionary(error, StrL("Unknown dictionary pack."));
    }
    Str folder = str::Dup(path::JoinTemp(GetDictionaryDirTemp(), StrL("wordnet-en.pending")));
    if (!dir::CreateAll(folder)) {
        str::Free(folder);
        return FailDictionary(error, StrL("Cannot create dictionary download folder."));
    }
    bool ok = true;
    for (const char* name : kWordNetPackFiles) {
        Str url =
            fmt("https://raw.githubusercontent.com/nltk/wordnet/ce91915ae38a341ae845be4d825ef6003cddf395/wn/data/"
                "wordnet-3.0/%s",
                Str(name));
        if (!HttpGetToFile(url, path::JoinTemp(folder, Str(name)), {}, kDictionaryMaxFile)) {
            ok = false;
            break;
        }
    }
    if (ok) {
        ok = HttpGetToFile(StrL("https://wordnetcode.princeton.edu/3.0/LICENSE"),
                           path::JoinTemp(folder, StrL("LICENSE")), {}, 65536);
    }
    DictionaryIndex validation;
    ok = ok && LoadWordNet(validation, folder);
    if (ok) {
        AutoUnlockRecursiveMutex lock(&gDictionaryLock);
        Str destination = path::JoinTemp(GetDictionaryDirTemp(), kWordNetId);
        ok = dir::CreateAll(destination);
        for (const char* name : kWordNetPackFiles) {
            if (ok) {
                ok = file::Copy(path::JoinTemp(destination, Str(name)), path::JoinTemp(folder, Str(name)), false);
            }
        }
        if (ok) {
            ok = file::Copy(path::JoinTemp(destination, StrL("LICENSE")), path::JoinTemp(folder, StrL("LICENSE")),
                            false);
        }
        if (ok) {
            ResetOfflineDictionaryCache();
        }
    }
    for (const char* name : kWordNetPackFiles) {
        file::Delete(path::JoinTemp(folder, Str(name)));
    }
    file::Delete(path::JoinTemp(folder, StrL("LICENSE")));
    RemoveDirectoryW(CWStrTemp(folder));
    str::Free(folder);
    return ok ? true
              : FailDictionary(
                    error, StrL("Download or validation failed. Check connection and dictionary folder permissions."));
}

bool InstallDictionaryFile(Str source, Str* error) {
    AutoUnlockMutex installLock(&gDictionaryInstallLock);
    Str extension = path::GetExtTemp(source);
    bool tsv = str::EqI(extension, StrL(".tsv"));
    bool wm = IsWmPack(source);
    if (!tsv && !wm && !str::EqI(extension, StrL(".ifo"))) {
        return FailDictionary(error, StrL("Choose a .wmvocab.json(.gz), UTF-8 .tsv or StarDict .ifo file."));
    }
    if (str::EqI(path::GetBaseNameTemp(path::GetPathNoExtTemp(source)), kWordNetId)) {
        return FailDictionary(error,
                              StrL("Rename this import; wordnet-en is reserved for the bundled English dictionary."));
    }
    DictionaryIndex validation;
    if (!LoadDictionaryFile(validation, source, error)) {
        return false;
    }
    AutoUnlockRecursiveMutex lock(&gDictionaryLock);
    if (!dir::CreateAll(GetDictionaryDirTemp())) {
        return FailDictionary(error, StrL("Cannot create dictionary folder."));
    }
    Str destination = path::JoinTemp(GetDictionaryDirTemp(), path::GetBaseNameTemp(source));
    if (path::IsSame(source, destination)) {
        ResetOfflineDictionaryCache();
        return true;
    }
    bool ok = file::Copy(destination, source, false);
    if (!tsv && !wm) {
        Str base = path::GetPathNoExtTemp(source);
        Str target = path::GetPathNoExtTemp(destination);
        const char* extensions[] = {".idx", ".idx.gz", ".dict", ".dict.dz", ".syn"};
        for (const char* ext : extensions) {
            Str from = fmt("%s%s", base, Str(ext));
            if (file::Exists(from)) {
                ok = file::Copy(fmt("%s%s", target, Str(ext)), from, false) && ok;
            }
        }
    }
    if (ok) {
        ResetOfflineDictionaryCache();
    }
    return ok ? true : FailDictionary(error, StrL("Cannot copy dictionary files into the portable dictionary folder."));
}

bool RemoveDictionaryPack(Str id, Str* error) {
    AutoUnlockMutex installLock(&gDictionaryInstallLock);
    if (len(id) == 0 || str::Contains(id, StrL("..")) || str::Contains(id, StrL("/")) ||
        str::Contains(id, StrL("\\"))) {
        return FailDictionary(error, StrL("Invalid dictionary identifier."));
    }
    AutoUnlockRecursiveMutex lock(&gDictionaryLock);
    bool removed = false;
    if (str::StartsWith(id, StrL("wm-"))) {
        Str sourceId(id.s + 3, len(id) - 3);
        removed = file::Delete(path::JoinTemp(GetDictionaryDirTemp(), fmt("%s.wmvocab.json.gz", sourceId)));
        removed = file::Delete(path::JoinTemp(GetDictionaryDirTemp(), fmt("%s.wmvocab.json", sourceId))) || removed;
    } else if (str::Eq(id, kWordNetId)) {
        Str folder = path::JoinTemp(GetDictionaryDirTemp(), id);
        for (const char* name : kWordNetPackFiles) {
            removed = file::Delete(path::JoinTemp(folder, Str(name))) || removed;
        }
        file::Delete(path::JoinTemp(folder, StrL("LICENSE")));
        RemoveDirectoryW(CWStrTemp(folder));
    } else {
        const char* extensions[] = {".tsv", ".ifo", ".idx", ".idx.gz", ".dict", ".dict.dz", ".syn"};
        for (const char* ext : extensions) {
            removed = file::Delete(path::JoinTemp(GetDictionaryDirTemp(), fmt("%s%s", id, Str(ext)))) || removed;
        }
    }
    ResetOfflineDictionaryCache();
    return removed ? true
                   : FailDictionary(error, StrL("This dictionary is bundled with the app or is already absent."));
}

#if IS_DEBUG
bool OfflineDictionary_UnitTests() {
    DictionaryIndex tsv;
    bool ok =
        tsv.Tsv(StrL("cat\ta small mammal\nCAT\ta feline\n"), StrL("test"), StrL("Test")) && len(tsv.entries) == 2;
    ok = ok && str::Eq(tsv.entries[0].key, tsv.entries[1].key);
    ok = ok && !tsv.Tsv(StrL("missing tab\n"), StrL("bad"), StrL("Bad"));
    const char idx[] = {'c', 'a', 't', 0, 0, 0, 0, 0, 0, 0, 0, 6};
    DictionaryIndex star;
    Str info = StrL("StarDict's dict ifo file\nversion=2.4.2\nsametypesequence=m\n");
    ok = ok && star.StarDict(info, Str(idx, sizeof(idx)), StrL("feline"), StrL("star"), StrL("Star"));
    ok = ok && !star.StarDict(info, Str(idx, sizeof(idx) - 1), StrL("feline"), StrL("star"), StrL("Star"));
    ok = ok && !star.StarDict(info, Str(idx, sizeof(idx)), StrL("tiny"), StrL("star"), StrL("Star"));
    ok = ok && !star.StarDict(StrL("not an ifo"), Str(idx, sizeof(idx)), StrL("feline"), StrL("star"), StrL("Star"));
    const char badUtf8[] = {'w', '\t', char(-1), '\n'};
    ok = ok && !tsv.Tsv(Str(badUtf8, sizeof(badUtf8)), StrL("bad"), StrL("Bad"));
    ok = ok && str::Eq(WordKey(StrL("  (CAF\xC3\x89). ")), WordKey(StrL("caf\xC3\xA9")));
    ok = ok && str::Eq(WordKey(StrL("ice_cream")), StrL("ice cream"));
    ok = ok && !star.StarDict(StrL("StarDict's dict ifo file\nidxoffsetbits=64\n"), Str(idx, sizeof(idx)),
                              StrL("feline"), StrL("star"), StrL("Star"));
    DictionaryIndex net;
    ok = ok && net.WordNet(StrL("00001740 03 n 01 entity 0 000 | that which exists\n")) && len(net.entries) == 1;
    ok = ok && !net.WordNet(StrL("00001740 03 n FF entity 0 | invalid word count\n"));
    DictionaryIndex wm;
    ok = ok &&
         LoadWmJson(
             wm,
             StrL("{\"format\":\"wmkeyboard-vocab\",\"version\":1,\"pack\":{\"name\":\"Test\"},\"words\":[{\"word\":"
                  "\"cat\",\"senses\":[{\"pos\":\"noun\",\"definition\":\"A feline\"}]}]}"),
             StrL("Test"));
    ok = ok && len(wm.entries) == 1 && str::Eq(wm.entries[0].definition, StrL("noun: A feline"));
    ok = ok && !LoadWmJson(wm, StrL("{\"format\":\"wrong\",\"words\":[]}"), StrL("Bad"));
    // Exercise the real bundled dictionaries and lookup priority without network access.
    ResetOfflineDictionaryCache();
    Vec<OfflineMeaning> meanings;
    Str error;
    bool found = LookupOfflineWord(StrL("abhor"), meanings, &error);
    ok = ok && found && len(meanings) > 0;
    if (found && len(meanings) > 0) {
        ok = ok && str::Eq(meanings[0].dictionaryId, StrL("wmkeyboard-vocab-en")) &&
             str::Eq(meanings[0].definition, VocabularyBuiltinMeaning(StrL("abhor")));
    }
    FreeOfflineMeanings(meanings);
    found = LookupOfflineWord(StrL("cat"), meanings, &error);
    bool wordNetFound = false;
    for (OfflineMeaning& meaning : meanings) {
        wordNetFound |= str::Eq(meaning.dictionaryId, kWordNetId) && len(meaning.definition) > 0;
    }
    ok = ok && len(VocabularyBuiltinMeaning(StrL("cat"))) == 0 && found && wordNetFound;
    FreeOfflineMeanings(meanings);
    str::Free(error);
    ResetOfflineDictionaryCache();
    return ok;
}
#endif
