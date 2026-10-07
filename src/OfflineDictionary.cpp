/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Http.h"
#include "base/Zip.h"
#include "base/JsonParser.h"
#include "base/HtmlTags.h"
#include "base/LzmaSimpleArchive.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "EmbeddedResources.h"
#include "Settings.h"
#include "AppSettings.h"
#include "VocabularyDecks.h"
#include "Vocabulary.h"
#include "GumboHtmlParser.h"
#include "KaikkiDictionary.h"
#include "OfflineDictionary.h"

constexpr int kDictionaryMaxFile = 256 * 1024 * 1024;
constexpr int kDictionaryMaxEntries = 2000000;
constexpr int kDictionaryMaxDefinition = 256 * 1024;
constexpr int kDictionaryMaxWord = 512;
constexpr int kDictionaryMaxResults = 64;

static RecursiveMutex gDictionaryLock;
static Mutex gDictionaryInstallLock;
static Str kWordNetId = StrL("wordnet-en");
static Str kDictionaryDownloadRoot = StrL("https://raw.githubusercontent.com/abelokoj/sumatrapdf-enhanced/master/");
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
    for (int i = 0; i < len(text); i++) {
        char c = text.s[i];
        if (c == '<') {
            int end = i + 1;
            while (end < len(text) && text.s[end] != '>') end++;
            if (end == len(text)) {
                plain.AppendChar(c);
                continue;
            }
            Str tag(text.s + i + 1, end - i - 1);
            if (len(tag) > 0 && tag.s[0] == '/') tag = Str(tag.s + 1, len(tag) - 1);
            tag = NextToken(tag);
            if ((str::EqI(tag, StrL("p")) || str::EqI(tag, StrL("div")) || str::EqI(tag, StrL("br")) ||
                 str::EqI(tag, StrL("br/")) || str::EqI(tag, StrL("li")) || str::EqI(tag, StrL("ol")) ||
                 str::EqI(tag, StrL("ul"))) &&
                len(plain) > 0 && plain.els[len(plain) - 1] != '\n') {
                plain.AppendChar('\n');
            }
            i = end;
        } else if (c != 0 && ((u8)c >= 32 || c == '\n' || c == '\t')) {
            plain.AppendChar(c);
        }
    }
    TempStr value = ResolveHtmlEntitiesTemp(ToStr(plain));
    str::Builder clean;
    bool space = false;
    for (int i = 0; i < len(value); i++) {
        char c = value.s[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (len(clean) && clean.els[len(clean) - 1] != '\n') clean.AppendChar('\n');
            space = false;
            continue;
        }
        if (c == ' ' || c == '\t') {
            space = len(clean) && clean.els[len(clean) - 1] != '\n';
            continue;
        }
        if (space) clean.AppendChar(' ');
        space = false;
        clean.AppendChar(c);
    }
    return str::DupTemp(Trim(ToStr(clean)));
}

TempStr DictionaryPlainText(Str definition) {
    Str plain = PlainDefinition(definition);
    str::Builder out;
    while (len(plain)) {
        char* end = (char*)memchr(plain.s, '\n', len(plain));
        int n = end ? (int)(end - plain.s) : len(plain);
        Str line = Trim(Str(plain.s, n));
        plain = end ? Str(end + 1, len(plain) - n - 1) : Str{};
        // Old extracted Wiktionary glosses repeated the parent before its more specific child.
        int child = str::IndexOf(line, StrL(".:"));
        if (child >= 0 && child + 2 < len(line)) line = Trim(Str(line.s + child + 2, len(line) - child - 2));
        if (!len(line)) continue;
        if (len(out)) out.AppendChar('\n');
        out.Append(line);
    }
    return str::DupTemp(ToStr(out));
}

static Str ArenaList(Arena* arena, Str existing, Str item) {
    item = Trim(item);
    if (!len(item) || len(item) > 2048 || len(existing) > 16384) return existing;
    Str remaining = existing;
    while (len(remaining)) {
        char* end = (char*)memchr(remaining.s, '\n', len(remaining));
        int n = end ? (int)(end - remaining.s) : len(remaining);
        if (str::EqI(Str(remaining.s, n), item)) return existing;
        remaining = end ? Str(end + 1, len(remaining) - n - 1) : Str{};
    }
    return str::Dup(arena, fmt("%s%s%s", existing, len(existing) ? StrL("\n") : Str{}, item));
}

static TempStr FullPos(Str pos) {
    const char* shortNames[] = {"n", "v", "a", "s", "r", "adj", "adv", "intj", "prep", "conj", "det", "pron", "num"};
    const char* fullNames[] = {"noun",       "verb",    "adjective",    "adjective",   "adverb",
                               "adjective",  "adverb",  "interjection", "preposition", "conjunction",
                               "determiner", "pronoun", "numeral"};
    for (int i = 0; i < dimofi(shortNames); i++)
        if (str::EqI(pos, Str(shortNames[i]))) return str::DupTemp(Str(fullNames[i]));
    return WordKey(pos);
}

static OfflineMeaning CopyMeaning(Arena* arena, const OfflineMeaning& source) {
    OfflineMeaning out;
    out.headword = str::Dup(arena, source.headword);
    out.definition = str::Dup(arena, source.definition);
    out.dictionary = str::Dup(arena, source.dictionary);
    out.dictionaryId = str::Dup(arena, source.dictionaryId);
    out.partOfSpeech = str::Dup(arena, source.partOfSpeech);
    out.example = str::Dup(arena, source.example);
    out.synonyms = str::Dup(arena, source.synonyms);
    out.antonyms = str::Dup(arena, source.antonyms);
    out.phonetic = str::Dup(arena, source.phonetic);
    out.phoneticUk = str::Dup(arena, source.phoneticUk);
    out.audioUrl = str::Dup(arena, source.audioUrl);
    out.audioUrlUk = str::Dup(arena, source.audioUrlUk);
    out.sourceUrl = str::Dup(arena, source.sourceUrl);
    out.license = str::Dup(arena, source.license);
    return out;
}

TempStr DictionaryMeaningText(const Vec<OfflineMeaning>& meanings) {
    str::Builder out;
    Str lastWord, lastPos;
    int number = 0;
    for (const OfflineMeaning& meaning : meanings) {
        if (len(meaning.partOfSpeech) &&
            (!str::Eq(lastPos, meaning.partOfSpeech) || !str::Eq(lastWord, meaning.headword))) {
            if (len(out)) out.AppendChar('\n');
            out.Append(meaning.partOfSpeech);
            out.AppendChar('\n');
            number = 0;
        }
        lastWord = meaning.headword;
        lastPos = meaning.partOfSpeech;
        if (len(out) && out.els[len(out) - 1] != '\n') out.AppendChar('\n');
        out.Append(fmt("%d. %s", ++number, DictionaryPlainText(meaning.definition)));
        if (len(meaning.example)) out.Append(fmt("\nExample: %s", meaning.example));
    }
    return str::DupTemp(ToStr(out));
}

struct DictEntry {
    Str key, headword, definition, id, title;
    OfflineMeaning meaning;
    int order = 0;
};

struct DictInflection {
    Str word, lemma;
};

struct DictionaryIndex {
    Arena* arena = ArenaNew();
    Vec<DictEntry> entries;
    Vec<DictInflection> inflections;
    ~DictionaryIndex() { ArenaDelete(arena); }

    bool Add(Str word, Str definition, Str id, Str title, const OfflineMeaning* detail = nullptr) {
        TempStr key = WordKey(word);
        if (len(key) == 0 || len(definition) == 0 || len(definition) > kDictionaryMaxDefinition ||
            !IsValidUtf8(definition) || len(entries) >= kDictionaryMaxEntries ||
            arena->Pos() > (u64)kDictionaryMaxFile) {
            return false;
        }
        TempStr plain = DictionaryPlainText(definition);
        if (len(plain) == 0) {
            return false;
        }
        OfflineMeaning value = detail ? *detail : OfflineMeaning{};
        value.headword = word;
        value.definition = plain;
        value.dictionaryId = id;
        value.dictionary = title;
        OfflineMeaning stored = CopyMeaning(arena, value);
        VecAppend(entries, {str::Dup(arena, key), stored.headword, stored.definition, stored.dictionaryId,
                            stored.dictionary, stored, len(entries)});
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
            Str partOfSpeech = FullPos(NextToken(tokens));
            Str wordCount = NextToken(tokens);
            char* tail = nullptr;
            unsigned long count = strtoul(CStrTemp(wordCount), &tail, 16);
            if (!count || count > 255 || !tail || *tail != 0) {
                return false;
            }
            Str gloss = Trim(Str(bar + 1, len(line) - (int)(bar + 1 - line.s)));
            Str definition = gloss;
            str::Builder examples;
            char* quote = (char*)memchr(gloss.s, '"', len(gloss));
            if (quote) {
                definition = Trim(Str(gloss.s, (int)(quote - gloss.s)));
                while (len(definition) && definition.s[len(definition) - 1] == ';')
                    definition = Trim(Str(definition.s, len(definition) - 1));
                Str remainder(quote, len(gloss) - (int)(quote - gloss.s));
                while (len(remainder)) {
                    char* startQuote = (char*)memchr(remainder.s, '"', len(remainder));
                    if (!startQuote) break;
                    Str after(startQuote + 1, len(remainder) - (int)(startQuote + 1 - remainder.s));
                    char* endQuote = (char*)memchr(after.s, '"', len(after));
                    if (!endQuote) break;
                    if (len(examples)) examples.AppendChar('\n');
                    examples.Append(Str(after.s, (int)(endQuote - after.s)));
                    remainder = Str(endQuote + 1, len(after) - (int)(endQuote + 1 - after.s));
                }
            }
            StrVec words;
            for (unsigned long i = 0; i < count; i++) {
                Str word = NextToken(tokens);
                NextToken(tokens);
                if (str::EndsWith(word, StrL("(a)")) || str::EndsWith(word, StrL("(p)"))) {
                    word = Str(word.s, len(word) - 3);
                } else if (str::EndsWith(word, StrL("(ip)"))) {
                    word = Str(word.s, len(word) - 4);
                }
                TempStr display = str::ReplaceTemp(word, StrL("_"), StrL(" "));
                words.Append(display);
            }
            for (Str word : words) {
                OfflineMeaning detail;
                detail.partOfSpeech = partOfSpeech;
                detail.example = ToStr(examples);
                detail.sourceUrl = StrL("https://wordnet.princeton.edu/");
                detail.license = StrL("Princeton WordNet 3.0 license");
                for (Str synonym : words)
                    if (!str::EqI(synonym, word)) detail.synonyms = ArenaList(arena, detail.synonyms, synonym);
                if (!Add(word, definition, kWordNetId, kWordNetTitle, &detail)) {
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
    Str definition, pos, example, synonyms, antonyms;
};
struct WmWord {
    Str word;
    Str phonetic, phoneticUk, audio, audioUk, synonyms, antonyms, forms;
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
            if (v->type != json::Type::String || len(v->value) > kDictionaryMaxWord) {
                valid = false;
                v->stop = true;
                return;
            }
            word->word = str::Dup(arena, v->value);
            return;
        }
        Str field = json::PathSegKey(json::PathNth(v->path, 2));
        Str accent = json::PathSegKey(json::PathNth(v->path, 3));
        if (v->type == json::Type::String && len(v->value) <= kDictionaryMaxDefinition) {
            if (str::Eq(field, StrL("ipa"))) {
                if (str::Eq(accent, StrL("us"))) word->phonetic = str::Dup(arena, v->value);
                if (str::Eq(accent, StrL("uk"))) word->phoneticUk = str::Dup(arena, v->value);
            }
            if (str::Eq(field, StrL("audio"))) {
                if (str::Eq(accent, StrL("us"))) word->audio = str::Dup(arena, v->value);
                if (str::Eq(accent, StrL("uk"))) word->audioUk = str::Dup(arena, v->value);
            }
            if (str::Eq(field, StrL("forms"))) word->forms = ArenaList(arena, word->forms, v->value);
            if (str::Eq(field, StrL("synonyms"))) word->synonyms = ArenaList(arena, word->synonyms, v->value);
            if (str::Eq(field, StrL("antonyms"))) word->antonyms = ArenaList(arena, word->antonyms, v->value);
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
        if (len(v->value) > kDictionaryMaxDefinition) {
            valid = false;
            v->stop = true;
            return;
        }
        if (str::Eq(key, StrL("definition"))) word->senses[sense].definition = str::Dup(arena, v->value);
        if (str::Eq(key, StrL("pos"))) word->senses[sense].pos = str::Dup(arena, v->value);
        if (str::Eq(key, StrL("example"))) word->senses[sense].example = str::Dup(arena, v->value);
        if (str::Eq(key, StrL("synonyms")))
            word->senses[sense].synonyms = ArenaList(arena, word->senses[sense].synonyms, v->value);
        if (str::Eq(key, StrL("antonyms")))
            word->senses[sense].antonyms = ArenaList(arena, word->senses[sense].antonyms, v->value);
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
        StrVec forms;
        Split(&forms, word->forms, StrL("\n"), true);
        for (Str form : forms) {
            Str key = WordKey(form), lemma = WordKey(word->word);
            if (len(key) && len(lemma) && !str::Eq(key, lemma))
                VecAppend(index.inflections, {str::Dup(index.arena, key), str::Dup(index.arena, lemma)});
        }
        for (WmSense& sense : word->senses) {
            if (len(sense.definition) == 0) continue;
            OfflineMeaning detail;
            detail.partOfSpeech = FullPos(sense.pos);
            detail.example = sense.example;
            detail.synonyms = word->synonyms;
            detail.antonyms = word->antonyms;
            StrVec related;
            Split(&related, sense.synonyms, StrL("\n"), true);
            for (Str item : related) detail.synonyms = ArenaList(pack.arena, detail.synonyms, item);
            related.Reset();
            Split(&related, sense.antonyms, StrL("\n"), true);
            for (Str item : related) detail.antonyms = ArenaList(pack.arena, detail.antonyms, item);
            detail.phonetic = word->phonetic;
            detail.phoneticUk = word->phoneticUk;
            detail.audioUrl = str::StartsWith(word->audio, StrL("https://")) ? word->audio : Str{};
            detail.audioUrlUk = str::StartsWith(word->audioUk, StrL("https://")) ? word->audioUk : Str{};
            detail.sourceUrl = fmt("https://en.wiktionary.org/wiki/%s", URLEncodeMayTruncateTemp(word->word, 2048));
            detail.license = StrL(
                "WMKeyboard word lists: MIT; Wiktionary meanings: CC BY-SA 3.0; WordNet meanings: WordNet license. "
                "Full pack attribution is retained in the downloaded pack.");
            if (!index.Add(word->word, sense.definition, StrL("wmkeyboard-vocab-en"), title, &detail)) return false;
        }
    }
    return len(index.entries) > start;
}

static void LoadBundledWm(DictionaryIndex& target) {
    for (const auto& source : builtinVocabDecks) {
        Str name = fmt("%s.wmvocab.json.gz", Str(source.id));
        Str compressed;
        bool read = ReadDictionary(path::JoinTemp(GetSelfExeDirTemp(), fmt("dictionaries\\%s", name)), compressed);
        if (!read) {
            int size = 0;
            u8* data = GetEmbeddedFileData(fmt("dictionaries\\%s", name), &size);
            compressed = Str((char*)data, size);
        }
        if (!len(compressed)) continue;
        Str jsonText = Ungzip(compressed, kDictionaryMaxFile);
        str::Free(compressed);
        int start = len(target.entries);
        if (!LoadWmJson(target, jsonText, Str(source.name)))
            VecRemoveAtN(target.entries, start, len(target.entries) - start);
        str::Free(jsonText);
    }
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
                ok = target.Add(alias, original.definition, id, id, &original.meaning);
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

static bool ReadWordNetFile(Str folder, Str name, Str& data) {
    if (len(folder) > 0) {
        return ReadDictionary(path::JoinTemp(folder, name), data);
    }
    int size = 0;
    u8* bytes = GetEmbeddedFileData(fmt("dictionaries\\wordnet-en\\%s", name), &size);
    data = Str((char*)bytes, size);
    return bytes != nullptr;
}

static bool HasEmbeddedWordNet() {
    auto* archive = GetEmbeddedArchive();
    if (!archive) {
        return false;
    }
    for (const char* name : kWordNetPackFiles) {
        if (lzma::GetIdxFromName(archive, fmt("dictionaries\\wordnet-en\\%s", Str(name))) < 0) {
            return false;
        }
    }
    return true;
}

static bool LoadWordNet(DictionaryIndex& index, Str folder) {
    int start = len(index.entries);
    for (const char* name : kWordNetFiles) {
        Str data;
        if (!ReadWordNetFile(folder, Str(name), data)) {
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
        if (ReadWordNetFile(folder, Str(name), data)) {
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
        str::Free(meaning.partOfSpeech);
        str::Free(meaning.example);
        str::Free(meaning.synonyms);
        str::Free(meaning.antonyms);
        str::Free(meaning.phonetic);
        str::Free(meaning.phoneticUk);
        str::Free(meaning.audioUrl);
        str::Free(meaning.audioUrlUk);
        str::Free(meaning.sourceUrl);
        str::Free(meaning.license);
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
            if (str::Eq(m.dictionaryId, e.id) && str::Eq(m.definition, e.definition) &&
                str::Eq(m.partOfSpeech, e.meaning.partOfSpeech)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            VecAppend(out, CopyMeaning(nullptr, e.meaning));
        }
    }
}

static bool AppendKaikkiMatches(Str word, Vec<OfflineMeaning>& out, Str* error);

bool LookupOfflineWord(Str word, Vec<OfflineMeaning>& out, Str* error) {
    if (error) {
        str::Free(*error);
        *error = {};
    }
    FreeOfflineMeanings(out);
    TempStr key = WordKey(word);
    if (len(key) == 0) {
        return FailDictionary(error, StrL("Select a word or short phrase (up to 512 UTF-8 bytes)."));
    }
    AutoUnlockRecursiveMutex lock(&gDictionaryLock);
    if (!gDictionaryIndex) {
        gDictionaryIndex = new DictionaryIndex();
        LoadBundledWm(*gDictionaryIndex);
        bool wordNet = LoadWordNet(*gDictionaryIndex, path::JoinTemp(GetDictionaryDirTemp(), kWordNetId));
        if (!wordNet) {
            wordNet =
                LoadWordNet(*gDictionaryIndex, path::JoinTemp(GetSelfExeDirTemp(), StrL("dictionaries\\wordnet-en")));
        }
        if (!wordNet) {
            LoadWordNet(*gDictionaryIndex, Str());
        }
        StrVec files;
        ImportPaths(files);
        for (Str file : files) {
            int start = len(gDictionaryIndex->entries);
            if (!LoadDictionaryFile(*gDictionaryIndex, file, nullptr)) {
                VecRemoveAtN(gDictionaryIndex->entries, start, len(gDictionaryIndex->entries) - start);
            }
        }
        VecSort(gDictionaryIndex->entries, [](const DictEntry* a, const DictEntry* b) {
            int comparison = str::Cmp(a->key, b->key);
            return comparison ? comparison : a->order - b->order;
        });
    }
    if (len(gDictionaryIndex->entries) == 0 && len(VocabularyBuiltinMeaning(key)) == 0) {
        if (!AppendKaikkiMatches(key, out, error)) return false;
        if (len(out)) return true;
        int packCount = 0;
        const KaikkiPack* packs = GetKaikkiPacks(packCount);
        for (int i = 0; i < packCount; i++) {
            if (KaikkiPackInstalled(packs[i].id, GetDictionaryDirTemp())) return true;
        }
        return FailDictionary(error,
                              StrL("No dictionary installed. Download WordNet or import a TSV/StarDict dictionary."));
    }
    MatchWord(*gDictionaryIndex, key, out, false, true);
    if (!len(out)) {
        for (const DictInflection& inflection : gDictionaryIndex->inflections) {
            if (str::Eq(inflection.word, key)) MatchWord(*gDictionaryIndex, inflection.lemma, out, false, true);
        }
    }
    if (len(out)) return AppendKaikkiMatches(key, out, error);
    Str builtin = VocabularyBuiltinMeaning(key);
    if (len(builtin)) {
        Str remaining = DictionaryPlainText(builtin);
        while (len(remaining) && len(out) < kDictionaryMaxResults) {
            char* end = (char*)memchr(remaining.s, '\n', len(remaining));
            int n = end ? (int)(end - remaining.s) : len(remaining);
            OfflineMeaning value;
            value.headword = key;
            value.definition = Str(remaining.s, n);
            value.dictionary = StrL("WMKeyboard vocabulary packs (Wiktionary/WordNet)");
            value.dictionaryId = StrL("wmkeyboard-vocab-en");
            value.sourceUrl = fmt("https://en.wiktionary.org/wiki/%s", URLEncodeMayTruncateTemp(key, 2048));
            value.license = StrL("Wiktionary CC BY-SA 3.0; Princeton WordNet license; WMKeyboard word lists MIT");
            if (n) VecAppend(out, CopyMeaning(nullptr, value));
            remaining = end ? Str(end + 1, len(remaining) - n - 1) : Str{};
        }
        return AppendKaikkiMatches(key, out, error);
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
    return AppendKaikkiMatches(key, out, error);
}

constexpr int kDictionaryOfflineMaxBytes = 16 * 1024 * 1024;
constexpr int kDictionaryOnlineMaxBytes = 4 * 1024 * 1024;
constexpr int kDictionaryOnlineMaxItems = 128;

struct OnlineSound {
    Str text, audio, tags;
};

struct OnlineBlock {
    Str pos, synonyms, antonyms;
    Vec<WmSense> senses;
};

struct OnlineWord {
    Str word, phonetic, language, license, licenseUrl, sourceUrl;
    Vec<OnlineSound> sounds;
    Vec<OnlineBlock*> blocks;
    ~OnlineWord() {
        for (OnlineBlock* block : blocks) delete block;
    }
};

struct OnlineParse {
    Arena* arena = ArenaNew();
    DictionarySource source;
    Str query;
    bool valid = true;
    int maxItems = kDictionaryOnlineMaxItems;
    u64 maxAllocation = 16 * 1024 * 1024;
    Vec<OnlineWord*> words;
    OnlineParse(DictionarySource provider, Str word) : source(provider), query(word) {}
    ~OnlineParse() {
        for (OnlineWord* word : words) delete word;
        ArenaDelete(arena);
    }

    bool Check(int index, json::Value* value) {
        if (index >= 0 && index < maxItems && arena->Pos() < maxAllocation) return true;
        valid = false;
        value->stop = true;
        return false;
    }

    void Visit(json::Value* value) {
        if (!valid) {
            value->stop = true;
            return;
        }
        if (value->type != json::Type::String) return;
        if (len(value->value) > kDictionaryMaxDefinition || !Check(0, value)) {
            valid = false;
            value->stop = true;
            return;
        }
        bool freeApi = source == DictionarySource::FreeDictionary;
        bool rest = source == DictionarySource::WiktionaryRest;
        int base = freeApi ? 1 : 0;
        int index = freeApi ? json::PathSegIndex(json::PathNth(value->path, 0)) : 0;
        if (freeApi && index < 0) return;
        if (rest && !str::Eq(json::PathSegKey(json::PathNth(value->path, 0)), StrL("en"))) return;
        if (!Check(index, value)) return;
        while (len(words) <= index) VecAppend(words, new OnlineWord());
        OnlineWord* word = words[index];
        if (rest) word->word = query;
        Str field = json::PathSegKey(json::PathNth(value->path, base));
        int depth = 0;
        for (StrNode* part = value->path; part; part = part->next) depth++;
        if (depth == base + 1) {
            if (str::Eq(field, StrL("word"))) word->word = str::Dup(arena, value->value);
            if (str::Eq(field, StrL("phonetic"))) word->phonetic = str::Dup(arena, value->value);
            if (str::Eq(field, StrL("lang_code"))) word->language = str::Dup(arena, value->value);
        }
        if (str::Eq(field, StrL("license"))) {
            Str key = json::PathSegKey(json::PathNth(value->path, base + 1));
            if (str::Eq(key, StrL("name"))) word->license = str::Dup(arena, value->value);
            if (str::Eq(key, StrL("url"))) word->licenseUrl = str::Dup(arena, value->value);
            return;
        }
        if (str::Eq(field, StrL("sourceUrls"))) {
            if (!len(word->sourceUrl) && str::StartsWith(value->value, StrL("https://")))
                word->sourceUrl = str::Dup(arena, value->value);
            return;
        }
        if (str::Eq(field, StrL("phonetics")) || str::Eq(field, StrL("sounds"))) {
            int soundIndex = json::PathSegIndex(json::PathNth(value->path, base + 1));
            if (!Check(soundIndex, value)) return;
            while (len(word->sounds) <= soundIndex) VecAppend(word->sounds, OnlineSound{});
            OnlineSound& sound = word->sounds[soundIndex];
            Str key = json::PathSegKey(json::PathNth(value->path, base + 2));
            if (str::Eq(key, StrL("text")) || str::Eq(key, StrL("ipa"))) sound.text = str::Dup(arena, value->value);
            if (str::Eq(key, StrL("audio")) || str::Eq(key, StrL("mp3_url")))
                sound.audio = str::Dup(arena, value->value);
            if (str::Eq(key, StrL("tags"))) sound.tags = ArenaList(arena, sound.tags, value->value);
            return;
        }
        int blockIndex = 0, offset = base;
        if (freeApi) {
            if (!str::Eq(field, StrL("meanings"))) return;
            blockIndex = json::PathSegIndex(json::PathNth(value->path, base + 1));
            offset += 2;
        } else if (rest) {
            blockIndex = json::PathSegIndex(json::PathNth(value->path, 1));
            offset = 2;
        }
        if (!Check(blockIndex, value)) return;
        while (len(word->blocks) <= blockIndex) VecAppend(word->blocks, new OnlineBlock());
        OnlineBlock* block = word->blocks[blockIndex];
        Str key = json::PathSegKey(json::PathNth(value->path, offset));
        if (str::Eq(key, StrL("partOfSpeech")) || str::Eq(key, StrL("pos"))) {
            block->pos = str::Dup(arena, value->value);
            return;
        }
        if (str::Eq(key, StrL("synonyms")) || str::Eq(key, StrL("antonyms"))) {
            Str item = value->value;
            if (!freeApi && !str::Eq(json::PathSegKey(json::PathNth(value->path, offset + 2)), StrL("word"))) return;
            if (str::Eq(key, StrL("synonyms")))
                block->synonyms = ArenaList(arena, block->synonyms, item);
            else
                block->antonyms = ArenaList(arena, block->antonyms, item);
            return;
        }
        if (!str::Eq(key, StrL("definitions")) && !str::Eq(key, StrL("senses"))) return;
        int senseIndex = json::PathSegIndex(json::PathNth(value->path, offset + 1));
        if (!Check(senseIndex, value)) return;
        while (len(block->senses) <= senseIndex) VecAppend(block->senses, WmSense{});
        WmSense& sense = block->senses[senseIndex];
        key = json::PathSegKey(json::PathNth(value->path, offset + 2));
        if (str::Eq(key, StrL("definition")) || str::Eq(key, StrL("glosses")))
            sense.definition = str::Dup(arena, value->value);
        if (str::Eq(key, StrL("example"))) sense.example = str::Dup(arena, value->value);
        if (str::Eq(key, StrL("examples"))) {
            Str exampleKey = json::PathSegKey(json::PathNth(value->path, offset + 4));
            if (!len(sense.example) && (rest || str::Eq(exampleKey, StrL("text"))))
                sense.example = str::Dup(arena, value->value);
        }
        if (str::Eq(key, StrL("synonyms")) || str::Eq(key, StrL("antonyms"))) {
            if (!freeApi && !str::Eq(json::PathSegKey(json::PathNth(value->path, offset + 4)), StrL("word"))) return;
            if (str::Eq(key, StrL("synonyms")))
                sense.synonyms = ArenaList(arena, sense.synonyms, value->value);
            else
                sense.antonyms = ArenaList(arena, sense.antonyms, value->value);
        }
    }

    void Append(Vec<OfflineMeaning>& out) {
        for (OnlineWord* word : words) {
            if (!len(word->word) || (len(word->language) && !str::Eq(word->language, StrL("en")))) continue;
            OfflineMeaning detail;
            detail.headword = word->word;
            detail.phonetic = word->phonetic;
            detail.dictionaryId = source == DictionarySource::FreeDictionary   ? StrL("dictionary-api-en")
                                  : source == DictionarySource::WiktionaryRest ? StrL("wiktionary-rest-en")
                                                                               : StrL("wiktionary-en");
            detail.dictionary = source == DictionarySource::FreeDictionary   ? StrL("Free Dictionary API")
                                : source == DictionarySource::WiktionaryRest ? StrL("Wiktionary")
                                                                             : StrL("Wiktionary via Kaikki");
            detail.sourceUrl = len(word->sourceUrl) ? word->sourceUrl
                                                    : fmt("https://en.wiktionary.org/wiki/%s",
                                                          URLEncodeMayTruncateTemp(word->word, 2048));
            detail.license =
                len(word->license)
                    ? fmt("%s%s%s", word->license, len(word->licenseUrl) ? StrL(" · ") : Str{}, word->licenseUrl)
                    : StrL("Wiktionary content: CC BY-SA; source attribution and licensing apply.");
            for (const OnlineSound& sound : word->sounds) {
                bool british = str::Contains(sound.tags, StrL("UK")) || str::Contains(sound.tags, StrL("British")) ||
                               str::Contains(sound.tags, StrL("Received-Pronunciation"));
                bool american =
                    str::Contains(sound.tags, StrL("US")) || str::Contains(sound.tags, StrL("General-American"));
                Str* ipa = british ? &detail.phoneticUk : &detail.phonetic;
                Str* audio = british ? &detail.audioUrlUk : &detail.audioUrl;
                if (len(sound.text) && (!len(*ipa) || american)) *ipa = sound.text;
                if (str::StartsWith(sound.audio, StrL("https://")) && (!len(*audio) || american)) {
                    *audio = sound.audio;
                    if (len(sound.text)) *ipa = sound.text;
                }
            }
            for (OnlineBlock* block : word->blocks) {
                detail.partOfSpeech = FullPos(block->pos);
                for (const WmSense& sense : block->senses) {
                    if (len(out) >= kDictionaryMaxResults) return;
                    Str definition = DictionaryPlainText(sense.definition);
                    if (!len(definition)) continue;
                    detail.definition = definition;
                    detail.example = PlainDefinition(sense.example);
                    detail.synonyms = block->synonyms;
                    detail.antonyms = block->antonyms;
                    StrVec related;
                    Split(&related, sense.synonyms, StrL("\n"), true);
                    for (Str item : related) detail.synonyms = ArenaList(arena, detail.synonyms, item);
                    related.Reset();
                    Split(&related, sense.antonyms, StrL("\n"), true);
                    for (Str item : related) detail.antonyms = ArenaList(arena, detail.antonyms, item);
                    VecAppend(out, CopyMeaning(nullptr, detail));
                }
            }
        }
    }
};

static bool ParseOnlineDictionary(Str body, Str query, DictionarySource source, Vec<OfflineMeaning>& out,
                                  int maxBytes = kDictionaryOnlineMaxBytes) {
    if (!IsValidUtf8(body) || len(body) > maxBytes) return false;
    if (source == DictionarySource::Wiktionary) {
        bool parsed = false;
        while (len(body) && len(out) < kDictionaryMaxResults) {
            char* end = (char*)memchr(body.s, '\n', len(body));
            int n = end ? (int)(end - body.s) : len(body);
            Str line = Trim(Str(body.s, n));
            body = end ? Str(end + 1, len(body) - n - 1) : Str{};
            if (!len(line)) continue;
            OnlineParse parser(source, query);
            if (maxBytes > kDictionaryOnlineMaxBytes) {
                parser.maxItems = 2048;
                parser.maxAllocation = 64 * 1024 * 1024;
            }
            if (!json::Parse(line, MkMethod1<OnlineParse, json::Value*, &OnlineParse::Visit>(&parser)) || !parser.valid)
                return false;
            parser.Append(out);
            parsed = true;
        }
        return parsed;
    }
    OnlineParse parser(source, query);
    if (!json::Parse(body, MkMethod1<OnlineParse, json::Value*, &OnlineParse::Visit>(&parser)) || !parser.valid)
        return false;
    parser.Append(out);
    return true;
}

static bool ParseKaikkiRecord(Str record, Str word, const KaikkiPack& pack, Vec<OfflineMeaning>& out) {
    Vec<OfflineMeaning> meanings;
    if (!ParseOnlineDictionary(record, word, DictionarySource::Wiktionary, meanings, kDictionaryOfflineMaxBytes)) {
        FreeOfflineMeanings(meanings);
        return false;
    }
    Str baseUrl = str::Eq(pack.id, StrL("kaikki-simple-english")) ? StrL("https://simple.wiktionary.org/wiki/")
                                                                  : StrL("https://en.wiktionary.org/wiki/");
    for (OfflineMeaning& meaning : meanings) {
        if (len(out) >= kDictionaryMaxResults) break;
        bool duplicate = false;
        for (const OfflineMeaning& existing : out) {
            if (str::Eq(existing.dictionaryId, pack.id) && str::Eq(existing.headword, meaning.headword) &&
                str::Eq(existing.partOfSpeech, meaning.partOfSpeech) &&
                str::Eq(existing.definition, meaning.definition)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        str::ReplaceWithCopy(&meaning.dictionary, pack.title);
        str::ReplaceWithCopy(&meaning.dictionaryId, pack.id);
        str::ReplaceWithCopy(&meaning.license, pack.license);
        str::ReplaceWithCopy(&meaning.sourceUrl,
                             fmt("%s%s", baseUrl, URLEncodeMayTruncateTemp(meaning.headword, 2048)));
        VecAppend(out, CopyMeaning(nullptr, meaning));
    }
    FreeOfflineMeanings(meanings);
    return true;
}

static bool AppendKaikkiMatches(Str word, Vec<OfflineMeaning>& out, Str* error) {
    int count = 0;
    const KaikkiPack* packs = GetKaikkiPacks(count);
    Str failure;
    for (int i = 0; i < count && len(out) < kDictionaryMaxResults; i++) {
        const KaikkiPack& pack = packs[i];
        if (!KaikkiPackInstalled(pack.id, GetDictionaryDirTemp())) continue;
        StrVec records;
        Str packError;
        if (!LookupKaikkiRecords(pack.id, GetDictionaryDirTemp(), word, records, &packError)) {
            if (len(packError)) str::ReplaceWithCopy(&failure, packError);
            str::Free(packError);
            continue;
        }
        for (Str record : records) {
            if (len(out) >= kDictionaryMaxResults) break;
            if (!ParseKaikkiRecord(record, word, pack, out))
                str::ReplaceWithCopy(
                    &failure, StrL("An installed Kaikki entry could not be read. Reinstall the dictionary pack."));
        }
        str::Free(packError);
    }
    bool ok = len(out) || !len(failure);
    if (!ok) FailDictionary(error, failure);
    str::Free(failure);
    return ok;
}

static TempStr DictionaryUrl(Str word, DictionarySource source) {
    Str encoded = URLEncodeMayTruncateTemp(word, 2048);
    if (source == DictionarySource::FreeDictionary)
        return fmt("https://api.dictionaryapi.dev/api/v2/entries/en/%s", encoded);
    if (source == DictionarySource::WiktionaryRest)
        return fmt("https://en.wiktionary.org/api/rest_v1/page/definition/%s?redirect=true", encoded);
    int first = 1;
    while (first < len(word) && ((u8)word.s[first] & 0xc0) == 0x80) first++;
    int second = first;
    if (second < len(word)) second++;
    while (second < len(word) && ((u8)word.s[second] & 0xc0) == 0x80) second++;
    return fmt("https://kaikki.org/dictionary/English/meaning/%s/%s/%s.jsonl",
               URLEncodeMayTruncateTemp(Str(word.s, first), 2048), URLEncodeMayTruncateTemp(Str(word.s, second), 2048),
               encoded);
}

static bool DictionaryHttpGet(Str url, str::Builder& body, DWORD& status) {
    HINTERNET session =
        InternetOpenW(L"SumatraPDF-Enhanced dictionary (+https://github.com/abelokoj/sumatrapdf-enhanced)",
                      INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!session) return false;
    DWORD connectTimeout = 8000, readTimeout = 12000;
    InternetSetOptionW(session, INTERNET_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
    InternetSetOptionW(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &readTimeout, sizeof(readTimeout));
    HINTERNET request = InternetOpenUrlW(session, CWStrTemp(url), L"Accept: application/json\r\n", (DWORD)-1,
                                         INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD |
                                             INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_AUTH,
                                         0);
    bool ok = false;
    if (request) {
        DWORD size = sizeof(status);
        ok = HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr) != FALSE;
        while (ok && status == 200) {
            char buffer[16384];
            DWORD read = 0;
            if (!InternetReadFile(request, buffer, sizeof(buffer), &read)) {
                ok = false;
                break;
            }
            if (!read) break;
            if ((u64)len(body) + read > kDictionaryOnlineMaxBytes || !body.Append(Str(buffer, (int)read))) {
                ok = false;
                break;
            }
        }
        InternetCloseHandle(request);
    }
    InternetCloseHandle(session);
    return ok;
}

bool LookupDictionaryWord(Str word, DictionarySource source, Vec<OfflineMeaning>& out, Str* error) {
    if (error) {
        str::Free(*error);
        *error = {};
    }
    if (source == DictionarySource::Offline) return LookupOfflineWord(word, out, error);
    FreeOfflineMeanings(out);
    Str key = WordKey(word);
    if (!len(key)) return FailDictionary(error, StrL("Enter a word or short phrase (up to 512 UTF-8 bytes)."));
    const DictionarySource fallback[] = {DictionarySource::Wiktionary, DictionarySource::WiktionaryRest,
                                         DictionarySource::FreeDictionary};
    int count = source == DictionarySource::OnlineFallback ? dimofi(fallback) : 1;
    bool answered = false;
    for (int i = 0; i < count; i++) {
        DictionarySource provider = source == DictionarySource::OnlineFallback ? fallback[i] : source;
        str::Builder body;
        DWORD status = 0;
        if (!DictionaryHttpGet(DictionaryUrl(key, provider), body, status)) continue;
        if (status == 404) {
            answered = true;
            continue;
        }
        if (status != 200) continue;
        if (!ParseOnlineDictionary(ToStr(body), key, provider, out)) {
            FreeOfflineMeanings(out);
            continue;
        }
        answered = true;
        if (len(out)) return true;
    }
    return answered ? true
                    : FailDictionary(error, StrL("Could not reach the selected dictionary sources. Check your "
                                                 "connection or choose Offline dictionaries."));
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
        VecAppend(packs,
                  {str::Dup(packId), str::Dup(Str(source.name)), str::Dup(StrL("English")),
                   str::Dup(StrL("Wiktionary CC BY-SA 3.0; WordNet license; lists MIT")),
                   str::Dup(fmt(
                       "https://github.com/abelokoj/sumatrapdf-enhanced/blob/master/data/vocabulary/%s.wmvocab.json.gz",
                       Str(source.id))),
                   true, true});
    }
    bool bundled = HasEmbeddedWordNet() ||
                   file::Exists(path::JoinTemp(GetSelfExeDirTemp(), StrL("dictionaries\\wordnet-en\\data.noun")));
    bool installed = file::Exists(path::JoinTemp(GetDictionaryDirTemp(), StrL("wordnet-en\\data.noun")));
    VecAppend(packs, {str::Dup(kWordNetId), str::Dup(kWordNetTitle), str::Dup(StrL("English")),
                      str::Dup(StrL("Princeton WordNet license")), str::Dup(StrL("https://wordnet.princeton.edu/")),
                      installed || bundled, bundled});

    int kaikkiCount = 0;
    const KaikkiPack* kaikki = GetKaikkiPacks(kaikkiCount);
    for (int i = 0; i < kaikkiCount; i++) {
        const KaikkiPack& pack = kaikki[i];
        VecAppend(packs, {str::Dup(pack.id), str::Dup(pack.title), str::Dup(StrL("English")), str::Dup(pack.license),
                          str::Dup(pack.sourceUrl), KaikkiPackInstalled(pack.id, GetDictionaryDirTemp()), false,
                          pack.downloadBytes, pack.expandedBytes, pack.compressed});
    }
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

bool GetDictionaryDownloadSize(Str id, i64& bytes, Str* error) {
    bytes = 0;
    if (FindKaikkiPack(id)) return KaikkiDownloadSize(id, bytes, error);
    Vec<OfflineDictPack> catalog;
    GetDictionaryCatalog(catalog);
    bool known = false;
    for (const OfflineDictPack& pack : catalog) {
        if (str::Eq(pack.id, id)) {
            known = true;
            bytes = pack.downloadBytes;
            break;
        }
    }
    FreeDictionaryCatalog(catalog);
    return known ? true : FailDictionary(error, StrL("Unknown dictionary pack."));
}

bool DownloadDictionaryPack(Str id, Str* error, const Func1<KaikkiProgress*>& progress, const Func1<bool*>& cancelled,
                            i64 approvedBytes) {
    AutoUnlockMutex installLock(&gDictionaryInstallLock);
    if (FindKaikkiPack(id))
        return InstallKaikkiPack(id, GetDictionaryDirTemp(), progress, cancelled, error, approvedBytes);
    if (str::StartsWith(id, StrL("wm-"))) {
        Str sourceId(id.s + 3, len(id) - 3);
        bool known = false;
        for (const auto& deck : builtinVocabDecks) known |= str::Eq(sourceId, Str(deck.id));
        if (!known) return FailDictionary(error, StrL("Unknown vocabulary dictionary pack."));
        if (!dir::CreateAll(GetDictionaryDirTemp()))
            return FailDictionary(error, StrL("Cannot create dictionary folder."));
        Str pending = path::JoinTemp(GetDictionaryDirTemp(), fmt("%s.wmvocab.json.gz.pending", sourceId));
        Str url = fmt("%sdata/vocabulary/%s.wmvocab.json.gz", kDictionaryDownloadRoot, sourceId);
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
        Str url = fmt("%ssrc/dictionaries/wordnet-en/%s", kDictionaryDownloadRoot, Str(name));
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
    if (FindKaikkiPack(id)) return RemoveKaikkiPack(id, GetDictionaryDirTemp(), error);
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
    {
        int packCount = 0;
        const KaikkiPack* packs = GetKaikkiPacks(packCount);
        utassert(packs != nullptr && packCount == 2);
        const KaikkiPack* simple = FindKaikkiPack(StrL("kaikki-simple-english"));
        const KaikkiPack* english = FindKaikkiPack(StrL("kaikki-english"));
        utassert(simple != nullptr && english != nullptr);
        if (simple && english) {
            Vec<OfflineMeaning> entries;
            VecAppend(entries, CopyMeaning(nullptr, {StrL("sick"), StrL("An existing WM meaning"),
                                                     StrL("WM vocabulary"), StrL("wmkeyboard-vocab-en")}));
            Str fixture = StrL(
                "{\"word\":\"sick\",\"forms\":[{\"form\":\"sick\",\"tags\":[\"positive\"]},{\"form\":\"sicker\","
                "\"tags\":[\"comparative\"]},{\"form\":\"sickest\",\"tags\":[\"superlative\"]}],\"lang_code\":\"en\","
                "\"pos\":\"adj\",\"senses\":[{\"glosses\":[\"If a person, animal, or plant is sick, it has a disease "
                "and is not healthy.\"],\"examples\":[{\"text\":\"I'm sorry, I'm too sick to go to work "
                "today.\"},{\"text\":\"If you're sick, you should go and see a doctor.\"}]},{\"glosses\":[\"A way of "
                "saying something is bad. (Could mean one or both of the following.)\",\"If something is sick it makes "
                "you feel like vomiting. Usually this is not literal, but just means very "
                "bad.\"],\"raw_tags\":[\"informal\",\"informal\"],\"categories\":[\"Colloquial\"],\"examples\":[{"
                "\"text\":\"Oh sick! There is dog shit all over everything.\"}]},{\"glosses\":[\"A way of saying "
                "something is bad. (Could mean one or both of the following.)\",\"Sick can mean similar to person who "
                "is mentally ill (crazy), as an insult. This can also mean something bad that a crazy person might "
                "do.\"],\"raw_tags\":[\"informal\",\"informal\"],\"categories\":[\"Colloquial\"],\"examples\":[{"
                "\"text\":\"Killing children is just plain sick.\"},{\"text\":\"That guy is really sick in the "
                "head.\"}]},{\"glosses\":[\"You say this about something that is cool. That is, something you like or "
                "see as very "
                "good.\"],\"raw_tags\":[\"slang\"],\"categories\":[\"Slang\"],\"examples\":[{\"text\":\"That song is "
                "sick!\"}]}],\"sounds\":[{\"enpr\":\"sĭk\"},{\"ipa\":\"/sɪk/\"},{\"sampa\":\"/sIk/"
                "\"},{\"audio\":\"en-us-sick.ogg\",\"tags\":[\"US\"]},{\"audio\":\"en-uk-sick.ogg\",\"tags\":[\"UK\"]},"
                "{\"homophones\":[\"sic\"]}]}");
            utassert(ParseKaikkiRecord(fixture, StrL("sicker"), *simple, entries));
            utassert(len(entries) == 5);
            if (len(entries) == 5) {
                utassert(str::Eq(entries[0].dictionaryId, StrL("wmkeyboard-vocab-en")));
                utassert(str::Eq(entries[1].headword, StrL("sick")));
                utassert(str::Eq(entries[1].dictionaryId, simple->id));
                utassert(str::Eq(entries[1].dictionary, simple->title));
                utassert(str::Eq(entries[1].license, simple->license));
                utassert(str::Eq(entries[1].sourceUrl, StrL("https://simple.wiktionary.org/wiki/sick")));
                utassert(str::Eq(entries[1].partOfSpeech, StrL("adjective")));
                utassert(str::Eq(entries[1].phonetic, StrL("/sɪk/")));
                utassert(str::Eq(entries[1].example, StrL("I'm sorry, I'm too sick to go to work today.")));
                utassert(str::StartsWith(entries[2].definition,
                                         StrL("If something is sick it makes you feel like vomiting.")));
                utassert(!str::Contains(entries[2].definition, StrL("A way of saying something is bad.")));
            }
            utassert(ParseKaikkiRecord(fixture, StrL("sicker"), *simple, entries) && len(entries) == 5);
            utassert(!ParseKaikkiRecord(StrL("{broken"), StrL("sick"), *simple, entries) && len(entries) == 5);
            FreeOfflineMeanings(entries);
            utassert(ParseKaikkiRecord(
                StrL("{\"word\":\"IP address\",\"lang_code\":\"en\",\"pos\":\"noun\",\"senses\":[{\"glosses\":[\"An IP "
                     "address is a number that is used to identify a computer. It is short for Internet Protocol "
                     "address.\"],\"examples\":[{\"text\":\"The IP address is 123.45.678.9.\"}]}]}"),
                StrL("ip addresses"), *english, entries));
            utassert(len(entries) == 1);
            if (len(entries))
                utassert(str::Eq(entries[0].sourceUrl, StrL("https://en.wiktionary.org/wiki/IP%20address")));
            FreeOfflineMeanings(entries);
        }
        Vec<OfflineDictPack> catalog;
        GetDictionaryCatalog(catalog);
        int found = 0;
        for (const OfflineDictPack& pack : catalog) {
            if (!str::StartsWith(pack.id, StrL("kaikki-"))) continue;
            found++;
            utassert(!pack.bundled && pack.downloadBytes >= 0 && pack.expandedBytes > 0);
            utassert(len(pack.license) > 0 && len(pack.sourceUrl) > 0);
            utassert(pack.installed == KaikkiPackInstalled(pack.id, GetDictionaryDirTemp()));
        }
        utassert(found == 2);
        FreeDictionaryCatalog(catalog);
    }

    utassert(str::Eq(PlainDefinition(StrL("<p>caf&eacute; &#x27;quoted&#39; &#8212; sense</p>")),
                     StrL("café 'quoted' — sense")));
    {
        DictionaryIndex pack;
        Str wmRecord = StrL(
            "{\"format\":\"wmkeyboard-vocab\",\"version\":1,\"pack\":{\"name\":\"Test "
            "dictionary\"},\"words\":[{\"word\":\"abhor\",\"ipa\":{\"us\":\"/æbˈhɔɹ/\",\"uk\":\"/əbˈhɔː/"
            "\"},\"audio\":{\"us\":\"https://example.org/"
            "abhor.mp3\"},\"forms\":[\"abhorred\"],\"synonyms\":[\"hate\"],\"senses\":[{\"pos\":\"verb\","
            "\"definition\":\"To detest.\",\"example\":\"I abhor "
            "waiting.\",\"synonyms\":[\"loathe\"]},{\"pos\":\"verb\",\"definition\":\"To reject.\"}]}]}");
        utassert(LoadWmJson(pack, wmRecord, StrL("Test")));
        utassert(len(pack.entries) == 2 && len(pack.inflections) == 1);
        if (len(pack.entries) == 2) {
            utassert(str::Eq(pack.entries[0].meaning.partOfSpeech, StrL("verb")));
            utassert(str::Eq(pack.entries[0].meaning.example, StrL("I abhor waiting.")));
            utassert(str::Eq(pack.entries[0].meaning.synonyms, StrL("hate\nloathe")));
            utassert(str::Eq(pack.entries[0].meaning.phoneticUk, StrL("/əbˈhɔː/")));
            utassert(str::Eq(pack.entries[0].meaning.audioUrl, StrL("https://example.org/abhor.mp3")));
            Vec<OfflineMeaning> grouped;
            MatchWord(pack, StrL("abhor"), grouped, false, true);
            utassert(str::Eq(DictionaryMeaningText(grouped),
                             StrL("verb\n1. To detest.\nExample: I abhor waiting.\n2. To reject.")));
            FreeOfflineMeanings(grouped);
        }
    }

    utassert(str::Eq(PlainDefinition(StrL("inter<b>national</b> &amp; &#39;quoted&#39;")),
                     StrL("international & 'quoted'")));
    utassert(str::Eq(DictionaryPlainText(StrL("Parent meaning.: A specific sense.\nAnother sense.")),
                     StrL("A specific sense.\nAnother sense.")));
    {
        DictionaryIndex glossary;
        utassert(glossary.WordNet(
            StrL("00001740 03 n 02 cat 0 feline 0 000 | a small domesticated mammal; \"a pet cat\"\n")));
        utassert(len(glossary.entries) == 2);
        utassert(str::Eq(glossary.entries[0].definition, StrL("a small domesticated mammal")));
        utassert(str::Eq(glossary.entries[0].meaning.partOfSpeech, StrL("noun")));
        utassert(str::Eq(glossary.entries[0].meaning.example, StrL("a pet cat")));
        utassert(str::Eq(glossary.entries[0].meaning.synonyms, StrL("feline")));
    }
    {
        Vec<OfflineMeaning> entries;
        Str api = StrL(
            "[{\"word\":\"clear\",\"phonetic\":\"/klɪə/\",\"phonetics\":[{\"text\":\"/klɪr/\",\"audio\":\"https://"
            "example.org/clear.mp3\"}],\"license\":{\"name\":\"CC BY-SA "
            "3.0\",\"url\":\"https://creativecommons.org/licenses/by-sa/3.0/\"},\"sourceUrls\":[\"https://"
            "en.wiktionary.org/wiki/"
            "clear\"],\"meanings\":[{\"partOfSpeech\":\"adjective\",\"synonyms\":[\"plain\"],\"antonyms\":[\"obscure\"]"
            ",\"definitions\":[{\"definition\":\"Easy to understand.\",\"example\":\"A clear "
            "explanation.\",\"synonyms\":[\"lucid\"]},{\"definition\":\"Transparent.\"}]},{\"partOfSpeech\":\"verb\","
            "\"definitions\":[{\"definition\":\"To remove.\"}]}]}]");
        utassert(ParseOnlineDictionary(api, StrL("clear"), DictionarySource::FreeDictionary, entries));
        utassert(len(entries) == 3);
        if (len(entries) == 3) {
            utassert(str::Eq(entries[0].partOfSpeech, StrL("adjective")));
            utassert(str::Eq(entries[2].partOfSpeech, StrL("verb")));
            utassert(str::Eq(entries[0].definition, StrL("Easy to understand.")));
            utassert(str::Eq(entries[1].definition, StrL("Transparent.")));
            utassert(str::Eq(entries[0].example, StrL("A clear explanation.")));
            utassert(str::Eq(entries[0].synonyms, StrL("plain\nlucid")));
            utassert(str::Eq(entries[0].antonyms, StrL("obscure")));
            utassert(str::Eq(entries[0].phonetic, StrL("/klɪr/")));
            utassert(str::Eq(entries[0].audioUrl, StrL("https://example.org/clear.mp3")));
            utassert(str::Contains(entries[0].license, StrL("CC BY-SA 3.0")));
        }
        FreeOfflineMeanings(entries);
        Str kaikki = StrL(
            "{\"word\":\"ablution\",\"lang_code\":\"en\",\"pos\":\"noun\",\"senses\":[{\"glosses\":[\"The act of "
            "washing something.\",\"The act of washing the body as a religious rite.\"],\"examples\":[{\"text\":\"He "
            "performed his "
            "ablutions.\"}],\"synonyms\":[{\"word\":\"washing\"}]}],\"sounds\":[{\"ipa\":\"/əˈbluːʃən/"
            "\",\"mp3_url\":\"https://example.org/ablution.mp3\",\"tags\":[\"UK\"]}]}\n");
        utassert(ParseOnlineDictionary(kaikki, StrL("ablution"), DictionarySource::Wiktionary, entries));
        utassert(len(entries) == 1);
        if (len(entries) == 1) {
            utassert(str::Eq(entries[0].definition, StrL("The act of washing the body as a religious rite.")));
            utassert(str::Eq(entries[0].example, StrL("He performed his ablutions.")));
            utassert(str::Eq(entries[0].synonyms, StrL("washing")));
            utassert(str::Eq(entries[0].phoneticUk, StrL("/əˈbluːʃən/")));
            utassert(str::Eq(entries[0].audioUrlUk, StrL("https://example.org/ablution.mp3")));
        }
        FreeOfflineMeanings(entries);
        utassert(ParseOnlineDictionary(
            StrL("{\"en\":[{\"partOfSpeech\":\"Noun\",\"definitions\":[{\"definition\":\"A <b>small</b> "
                 "mammal.\",\"examples\":[\"A cat sleeps.\"]},{\"definition\":\"A person.\"}]}]}"),
            StrL("cat"), DictionarySource::WiktionaryRest, entries));
        utassert(len(entries) == 2);
        if (len(entries) == 2) {
            utassert(str::Eq(entries[0].definition, StrL("A small mammal.")));
            utassert(str::Eq(entries[0].example, StrL("A cat sleeps.")));
            utassert(str::Eq(entries[0].headword, StrL("cat")));
        }
        FreeOfflineMeanings(entries);
        utassert(!ParseOnlineDictionary(StrL("{broken"), StrL("cat"), DictionarySource::WiktionaryRest, entries));
        utassert(ParseOnlineDictionary(StrL("{\"title\":\"No Definitions Found\"}"), StrL("cat"),
                                       DictionarySource::FreeDictionary, entries) &&
                 len(entries) == 0);
        utassert(str::Eq(DictionaryUrl(StrL("ice cream"), DictionarySource::FreeDictionary),
                         StrL("https://api.dictionaryapi.dev/api/v2/entries/en/ice%20cream")));
        utassert(str::Eq(DictionaryUrl(StrL("a"), DictionarySource::Wiktionary),
                         StrL("https://kaikki.org/dictionary/English/meaning/a/a/a.jsonl")));
    }

    utassert(str::Eq(PlainDefinition(StrL("<p>First meaning.</p><p>Second meaning.</p>")),
                     StrL("First meaning.\nSecond meaning.")));
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
    {
        DictionaryIndex embedded;
        ok = ok && HasEmbeddedWordNet() && LoadWordNet(embedded, Str());
        ok = ok && len(embedded.entries) > 100000 && len(embedded.inflections) > 0;
        bool entityFound = false;
        for (const DictEntry& entry : embedded.entries) {
            entityFound |= str::Eq(entry.key, StrL("entity")) && len(entry.definition) > 0;
        }
        ok = ok && entityFound;
    }
    Vec<OfflineDictPack> catalog;
    GetDictionaryCatalog(catalog);
    int wmCount = 0;
    for (const OfflineDictPack& pack : catalog) {
        if (pack.bundled && str::StartsWith(pack.id, StrL("wm-"))) {
            wmCount++;
            ok = ok &&
                 str::StartsWith(pack.sourceUrl,
                                 StrL("https://github.com/abelokoj/sumatrapdf-enhanced/blob/master/data/vocabulary/"));
            ok = ok && len(pack.license) > 0;
        }
    }
    ok = ok && wmCount == 11;
    FreeDictionaryCatalog(catalog);
    DictionaryIndex wm;
    ok = ok &&
         LoadWmJson(
             wm,
             StrL("{\"format\":\"wmkeyboard-vocab\",\"version\":1,\"pack\":{\"name\":\"Test\"},\"words\":[{\"word\":"
                  "\"cat\",\"senses\":[{\"pos\":\"noun\",\"definition\":\"A feline\"}]}]}"),
             StrL("Test"));
    ok = ok && len(wm.entries) == 1 && str::Eq(wm.entries[0].definition, StrL("A feline")) &&
         str::Eq(wm.entries[0].meaning.partOfSpeech, StrL("noun"));
    ok = ok && !LoadWmJson(wm, StrL("{\"format\":\"wrong\",\"words\":[]}"), StrL("Bad"));
    // Exercise the real bundled dictionaries and lookup priority without network access.
    ResetOfflineDictionaryCache();
    Vec<OfflineMeaning> meanings;
    Str error;
    bool found = LookupOfflineWord(StrL("abhor"), meanings, &error);
    ok = ok && found && len(meanings) > 0;
    if (found && len(meanings) > 0) {
        ok = ok && str::Eq(meanings[0].dictionaryId, StrL("wmkeyboard-vocab-en")) &&
             str::Contains(VocabularyBuiltinMeaning(StrL("abhor")), meanings[0].definition);
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
