/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3. */
#include "base/Base.h"
#include "base/File.h"
#include "base/JsonParser.h"
#include "base/Zip.h"
#include "Settings.h"
#include "AppSettings.h"
#include "OfflineDictionary.h"
#include "VocabularyDecks.h"
#include "VocabularyAttributions.h"
#include "VocabularyMeanings.h"
#include "Vocabulary.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif

constexpr int kMaxVocabWords = 20000;
constexpr int kMaxVocabDecks = 200;
constexpr int kMaxVocabBytes = 64 * 1024 * 1024;
constexpr i64 kVocabDay = 86400;
constexpr int kMaxReviewDays = 3650;

VocabularyWord::~VocabularyWord() {
    str::Free(id);
    str::Free(word);
    str::Free(definition);
    str::Free(dictionaryId);
    str::Free(context);
    str::Free(sourcePath);
    str::Free(deckId);
}
VocabularyDeck::~VocabularyDeck() {
    str::Free(id);
    str::Free(name);
    str::Free(description);
    str::Free(source);
    str::Free(license);
}
VocabularyQuestion::~VocabularyQuestion() {
    str::Free(wordId);
    str::Free(prompt);
    str::Free(answer);
}
struct VocabularyData {
    Vec<VocabularyWord*> words;
    Vec<VocabularyDeck*> decks;
    Str path, error;
    bool loaded = false, readOnly = false, batch = false, test = false;
    ~VocabularyData() {
        for (auto* w : words) delete w;
        for (auto* d : decks) delete d;
        str::Free(path);
        str::Free(error);
    }
};
static VocabularyData vocabulary;
static VocabularyData* data = &vocabulary;
static i64 Now(i64 value = 0) {
    return value > 0 ? value : (i64)time(nullptr);
}
static bool Fail(Str message) {
    str::ReplaceWithCopy(&data->error, message);
    return false;
}
Str VocabularyLastError() {
    return data->error;
}
Str VocabularyStorePathTemp() {
    if (len(data->path)) return data->path;
    return path::JoinTemp(path::GetDirTemp(GetSettingsPathTemp()), StrL("SumatraPDF-vocabulary.json"));
}
static TempStr NewId() {
    GUID g;
    if (FAILED(CoCreateGuid(&g))) return fmt("word-%lld-%u", Now(), GetTickCount());
    return fmt("%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1],
               g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}
static TempStr CleanWord(Str input) {
    if (len(input) > 256) return {};
    Str word = str::DupTemp(input);
    str::TrimWSInPlace(word, str::TrimOpt::Both);
    if (len(word) == 0 || str::ContainsChar(word, '\0') || str::ContainsChar(word, '\n') ||
        str::ContainsChar(word, '\r'))
        return {};
    return word;
}
static bool SameWord(Str a, Str b) {
    if (str::EqI(a, b)) return true;
    bool unicode = false;
    for (int i = 0; i < len(a); i++) unicode |= (u8)a.s[i] >= 128;
    for (int i = 0; i < len(b); i++) unicode |= (u8)b.s[i] >= 128;
    if (!unicode) return false;
    WStr wa = ToWStrTemp(a), wb = ToWStrTemp(b);
    return CompareStringOrdinal(wa.s, len(wa), wb.s, len(wb), TRUE) == CSTR_EQUAL;
}
static bool HasDeck(const VocabularyWord* word, Str deck) {
    return len(deck) == 0 || str::Eq(word->deckId, deck) || word->deckIds.Find(deck) >= 0;
}
static void AttachDeck(VocabularyWord* word, Str deck) {
    if (!len(deck)) return;
    if (!len(word->deckId)) str::ReplaceWithCopy(&word->deckId, deck);
    if (word->deckIds.Find(deck) < 0) word->deckIds.Append(deck);
}
static void AddBuiltins(VocabularyData& target) {
    for (const auto& source : builtinVocabDecks) {
        VocabularyDeck* deck = nullptr;
        for (auto* d : target.decks)
            if (str::Eq(d->id, Str(source.id))) {
                deck = d;
                break;
            }
        if (deck) {
            deck->builtin = true;
            if (len(deck->words) == 0) Split(&deck->words, Str(source.words), StrL("\n"), true);
            continue;
        }
        deck = new VocabularyDeck;
        deck->id = str::Dup(Str(source.id));
        deck->name = str::Dup(Str(source.name));
        deck->description = str::Dup(Str(source.description));
        deck->builtin = true;
        deck->source = str::Dup(fmt(
            "https://github.com/wasi-master/wmkeyboard-data/blob/master/vocab/en/%s.wmvocab.json.gz", Str(source.id)));
        deck->license =
            str::Dup(StrL("Word lists: MIT, Copyright (c) 2026 Wasi Master. Meanings: Wiktionary via kaikki.org, CC "
                          "BY-SA 3.0; WordNet, WordNet License. Extracted definition senses only. Full original "
                          "attribution: data/vocabulary/pack-attributions.json; docs/vocabulary-attribution.md."));
        for (const auto& item : builtinVocabAttributions) {
            if (str::Eq(deck->id, Str(item.id))) str::ReplaceWithCopy(&deck->license, Str(item.notice));
        }
        Split(&deck->words, Str(source.words), StrL("\n"), true);
        VecAppend(target.decks, deck);
    }
}
static VocabularyWord* FindIn(const Vec<VocabularyWord*>& words, Str id) {
    for (auto* w : words)
        if (str::Eq(w->id, id)) return w;
    return nullptr;
}
static VocabularyWord* FindWord(const Vec<VocabularyWord*>& words, Str word, Str dictionary) {
    for (auto* w : words)
        if (SameWord(w->word, word) && str::Eq(w->dictionaryId, dictionary)) return w;
    return nullptr;
}
static VocabularyDeck* FindDeck(const Vec<VocabularyDeck*>& decks, Str id) {
    for (auto* d : decks)
        if (str::Eq(d->id, id)) return d;
    return nullptr;
}
static void StringField(str::Builder& out, const char* key, Str value, bool comma = true) {
    out.Append(fmt("\"%s\":\"%s\"%s", Str(key), Str(json::EscapeStrTemp(value)), comma ? StrL(",") : StrL("")));
}
static Str Serialize(const VocabularyData& state) {
    str::Builder out;
    out.Append(StrL("{\"format\":\"sumatrapdf-vocabulary\",\"version\":1,\"decks\":["));
    bool first = true;
    for (auto* d : state.decks) {
        if (!first) out.AppendChar(',');
        first = false;
        out.AppendChar('{');
        StringField(out, "id", d->id);
        StringField(out, "name", d->name);
        StringField(out, "description", d->description);
        StringField(out, "source", d->source);
        StringField(out, "license", d->license, false);
        out.AppendChar('}');
    }
    out.Append(StrL("],\"words\":["));
    first = true;
    for (auto* w : state.words) {
        if (!first) out.AppendChar(',');
        first = false;
        out.AppendChar('{');
        StringField(out, "id", w->id);
        StringField(out, "word", w->word);
        StringField(out, "definition", w->definition);
        StringField(out, "dictionaryId", w->dictionaryId);
        StringField(out, "context", w->context);
        StringField(out, "sourcePath", w->sourcePath);
        StringField(out, "deckId", w->deckId);
        out.Append(fmt("\"repetitions\":%d,", w->repetitions));
        out.Append(StrL("\"deckIds\":["));
        for (int i = 0; i < len(w->deckIds); i++) {
            if (i) out.AppendChar(',');
            out.Append(fmt("\"%s\"", Str(json::EscapeStrTemp(w->deckIds[i]))));
        }
        out.Append(
            fmt("],\"page\":%d,\"box\":%d,\"reviews\":%d,\"lapses\":%d,\"intervalDays\":%d,\"ease\":%.4f,\"dueTime\":%"
                "lld,\"lastReviewed\":%lld,\"addedTime\":%lld,\"learned\":%s}",
                w->page, w->box, w->reviews, w->lapses, w->intervalDays, w->ease, w->dueTime, w->lastReviewed,
                w->addedTime, w->learned ? StrL("true") : StrL("false")));
    }
    out.Append(StrL("]}\n"));
    return out.TakeStr();
}
static bool WriteAtomic(Str fileName, Str bytes) {
    if (len(bytes) > kMaxVocabBytes) return Fail(StrL("Vocabulary exceeds the 64 MB storage limit."));
    if (!dir::CreateForFile(fileName)) return Fail(StrL("Cannot create the vocabulary folder."));
    Str temp = str::Dup(fmt("%s.%lu.%lu.tmp", fileName, GetCurrentProcessId(), GetTickCount()));
    defer {
        file::Delete(temp);
        str::Free(temp);
    };
    if (!file::WriteFile(temp, bytes))
        return Fail(StrL("Cannot write vocabulary. Check disk space and folder access."));
    if (!MoveFileExW(CWStrTemp(ToWStrTemp(temp)), CWStrTemp(ToWStrTemp(fileName)),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return Fail(StrL("Cannot replace the vocabulary file. The previous copy was preserved."));
    return true;
}
struct VocabReader {
    VocabularyData parsed;
    int version = 0;
    Str format, packId, packName;
    str::Builder attribution;
    bool invalid = false;
    ~VocabReader() {
        str::Free(format);
        str::Free(packId);
        str::Free(packName);
    }
};
static bool ReadString(Str* field, json::Value* value, int maximum, VocabReader* reader) {
    if (value->type != json::Type::String || len(value->value) > maximum || str::ContainsChar(value->value, '\0')) {
        reader->invalid = true;
        value->stop = true;
        return false;
    }
    str::ReplaceWithCopy(field, value->value);
    return true;
}
static i64 Integer(json::Value* value, i64 maximum) {
    if (value->type != json::Type::Number) return 0;
    double number = strtod(CStrTemp(value->value), nullptr);
    if (!isfinite(number) || number < 0) return 0;
    return (i64)std::min(number, (double)maximum);
}
static void ReadVocab(VocabReader* r, json::Value* v) {
    if (json::PathMatch(v->path, StrL("/version"))) {
        r->version = (int)Integer(v, 100);
        return;
    }
    if (json::PathMatch(v->path, StrL("/format"))) {
        ReadString(&r->format, v, 64, r);
        return;
    }
    if (json::PathMatch(v->path, StrL("/pack"), StrL("/id"))) {
        ReadString(&r->packId, v, 128, r);
        return;
    }
    if (json::PathMatch(v->path, StrL("/pack"), StrL("/name"))) {
        ReadString(&r->packName, v, 256, r);
        return;
    }
    Str first = json::PathSegKey(json::PathNth(v->path, 0));
    if (str::Eq(first, StrL("pack"))) {
        Str sub = json::PathSegKey(json::PathNth(v->path, 1));
        if ((str::Eq(sub, StrL("attribution")) || str::Eq(sub, StrL("sources"))) && len(r->attribution) < 32000) {
            r->attribution.Append(json::PathFormatTemp(v->path));
            r->attribution.Append(StrL(": "));
            r->attribution.Append(v->value);
            r->attribution.AppendChar('\n');
        }
        return;
    }
    bool word = str::Eq(first, StrL("words")), deck = str::Eq(first, StrL("decks"));
    if (!word && !deck) return;
    int index = json::PathSegIndex(json::PathNth(v->path, 1));
    if (index < 0 || index >= (word ? kMaxVocabWords : kMaxVocabDecks)) {
        r->invalid = true;
        v->stop = true;
        return;
    }
    Str key = json::PathSegKey(json::PathNth(v->path, 2));
    if (word) {
        while (len(r->parsed.words) <= index) VecAppend(r->parsed.words, new VocabularyWord);
        VocabularyWord* w = r->parsed.words[index];
        if (str::Eq(key, StrL("id")))
            ReadString(&w->id, v, 128, r);
        else if (str::Eq(key, StrL("word")))
            ReadString(&w->word, v, 256, r);
        else if (str::Eq(key, StrL("definition")))
            ReadString(&w->definition, v, 16000, r);
        else if (str::Eq(key, StrL("dictionaryId")))
            ReadString(&w->dictionaryId, v, 256, r);
        else if (str::Eq(key, StrL("context")))
            ReadString(&w->context, v, 8192, r);
        else if (str::Eq(key, StrL("sourcePath")))
            ReadString(&w->sourcePath, v, 2048, r);
        else if (str::Eq(key, StrL("deckId")))
            ReadString(&w->deckId, v, 128, r);
        else if (str::Eq(key, StrL("deckIds"))) {
            if (v->type != json::Type::String || len(v->value) > 128 || len(w->deckIds) > 200) {
                r->invalid = true;
                v->stop = true;
                return;
            }
            if (w->deckIds.Find(v->value) < 0) w->deckIds.Append(v->value);
        } else if (str::Eq(key, StrL("page")))
            w->page = (int)Integer(v, 10000000);
        else if (str::Eq(key, StrL("box")))
            w->box = (int)Integer(v, 4);
        else if (str::Eq(key, StrL("repetitions")))
            w->repetitions = (int)Integer(v, 1000000);
        else if (str::Eq(key, StrL("reviews")))
            w->reviews = (int)Integer(v, 1000000);
        else if (str::Eq(key, StrL("lapses")))
            w->lapses = (int)Integer(v, 1000000);
        else if (str::Eq(key, StrL("intervalDays")))
            w->intervalDays = (int)Integer(v, kMaxReviewDays);
        else if (str::Eq(key, StrL("ease")))
            w->ease = limitValue(strtod(CStrTemp(v->value), nullptr), 1.3, 10.0);
        else if (str::Eq(key, StrL("dueTime")))
            w->dueTime = Integer(v, 4102444800LL);
        else if (str::Eq(key, StrL("lastReviewed")))
            w->lastReviewed = Integer(v, 4102444800LL);
        else if (str::Eq(key, StrL("addedTime")))
            w->addedTime = Integer(v, 4102444800LL);
        else if (str::Eq(key, StrL("learned")))
            w->learned = v->type == json::Type::Bool && str::Eq(v->value, StrL("true"));
        else if (str::Eq(key, StrL("senses")) && json::PathSegIndex(json::PathNth(v->path, 3)) >= 0 &&
                 str::Eq(json::PathSegKey(json::PathNth(v->path, 4)), StrL("definition")) &&
                 v->type == json::Type::String) {
            if (len(w->definition) + len(v->value) > 16000) {
                r->invalid = true;
                v->stop = true;
                return;
            }
            Str combined = str::Dup(fmt("%s%s%s", w->definition, len(w->definition) ? StrL("\n") : StrL(""), v->value));
            str::Free(w->definition);
            w->definition = combined;
        }
        return;
    }
    while (len(r->parsed.decks) <= index) VecAppend(r->parsed.decks, new VocabularyDeck);
    VocabularyDeck* d = r->parsed.decks[index];
    if (str::Eq(key, StrL("id")))
        ReadString(&d->id, v, 128, r);
    else if (str::Eq(key, StrL("name")))
        ReadString(&d->name, v, 256, r);
    else if (str::Eq(key, StrL("description")))
        ReadString(&d->description, v, 8192, r);
    else if (str::Eq(key, StrL("source")))
        ReadString(&d->source, v, 2048, r);
    else if (str::Eq(key, StrL("license")))
        ReadString(&d->license, v, 32000, r);
}
static bool ParseVocab(Str bytes, VocabReader& r) {
    if (len(bytes) > kMaxVocabBytes || !json::Parse(bytes, MkFunc1(ReadVocab, &r)) || r.invalid || r.version != 1)
        return Fail(StrL("Invalid or unsupported vocabulary file. No words were replaced."));
    bool wm = str::Eq(r.format, StrL("wmkeyboard-vocab"));
    if (!wm && !str::Eq(r.format, StrL("sumatrapdf-vocabulary")))
        return Fail(StrL("This file is not a supported vocabulary export."));
    if (wm) {
        auto* deck = new VocabularyDeck;
        deck->id = str::Dup(fmt("import-%s", len(r.packId) ? r.packId : Str(NewId())));
        deck->name = str::Dup(len(r.packName) ? r.packName : StrL("Imported vocabulary"));
        deck->source = str::Dup(StrL("Imported WM Keyboard vocabulary pack"));
        deck->license = str::Dup(ToStrTemp(r.attribution));
        VecAppend(r.parsed.decks, deck);
        for (auto* w : r.parsed.words) {
            str::ReplaceWithCopy(&w->id, NewId());
            AttachDeck(w, deck->id);
            if (!len(w->dictionaryId)) w->dictionaryId = str::Dup(StrL("wmkeyboard-import"));
        }
    }
    for (int i = 0; i < len(r.parsed.words); i++) {
        VocabularyWord* w = r.parsed.words[i];
        if (!len(CleanWord(w->word)) || !len(w->id) || (FindIn(r.parsed.words, w->id) != w))
            return Fail(StrL("Vocabulary contains invalid words or duplicate IDs."));
        AttachDeck(w, w->deckId);
        if (!isfinite(w->ease)) w->ease = 2.5;
    }
    for (auto* d : r.parsed.decks)
        if (!len(d->id) || !len(d->name) || FindDeck(r.parsed.decks, d->id) != d)
            return Fail(StrL("Vocabulary contains invalid or duplicate decks."));
    return true;
}
static bool ReadVocabFile(Str path, VocabReader& reader) {
    i64 size = file::GetSize(path);
    if (size < 0 || size > kMaxVocabBytes) return Fail(StrL("Vocabulary file is unreadable or larger than 64 MB."));
    Str bytes = file::ReadFile(path);
    defer {
        str::Free(bytes);
    };
    if (!len(bytes)) return Fail(StrL("Vocabulary file is empty or unreadable."));
    if (len(bytes) >= 2 && (u8)bytes.s[0] == 0x1f && (u8)bytes.s[1] == 0x8b) {
        Str unpacked = Ungzip(bytes, kMaxVocabBytes);
        if (!len(unpacked)) return Fail(StrL("Compressed vocabulary is invalid or larger than 64 MB."));
        bool success = ParseVocab(unpacked, reader);
        str::Free(unpacked);
        return success;
    }
    return ParseVocab(bytes, reader);
}
bool VocabularyLoad() {
    if (data->loaded) return !data->readOnly;
    data->loaded = true;
    AddBuiltins(*data);
    Str path = VocabularyStorePathTemp();
    if (!file::Exists(path)) return true;
    VocabReader reader;
    if (!ReadVocabFile(path, reader)) {
        data->readOnly = true;
        return false;
    }
    for (auto* w : reader.parsed.words) VecAppend(data->words, w);
    VecReset(reader.parsed.words);
    for (auto* d : reader.parsed.decks) {
        if (FindDeck(data->decks, d->id))
            delete d;
        else
            VecAppend(data->decks, d);
    }
    VecReset(reader.parsed.decks);
    return true;
}
bool VocabularySave() {
    if (!VocabularyLoad() || data->readOnly)
        return Fail(StrL(
            "The vocabulary store is damaged. Export or restore it before saving; its original file was preserved."));
    if (data->batch || (gDontSaveSettings && !data->test)) return true;
    Str serialized = Serialize(*data);
    defer {
        str::Free(serialized);
    };
    if (!WriteAtomic(VocabularyStorePathTemp(), serialized)) return false;
    str::ReplaceWithCopy(&data->error, {});
    return true;
}
const Vec<VocabularyWord*>& VocabularyWords() {
    VocabularyLoad();
    return data->words;
}
const Vec<VocabularyDeck*>& VocabularyDecks() {
    VocabularyLoad();
    return data->decks;
}
VocabularyWord* VocabularyFind(Str id) {
    VocabularyLoad();
    return FindIn(data->words, id);
}
VocabularyWord* VocabularyAdd(Str input, Str definition, Str dictionary, Str context, Str source, int page, Str deck) {
    if (!VocabularyLoad()) return nullptr;
    Str word = CleanWord(input);
    if (!len(word) || len(definition) > 16000 || len(context) > 8192 || len(source) > 2048 || len(dictionary) > 256 ||
        len(deck) > 128) {
        Fail(StrL("The word or definition is too long or invalid."));
        return nullptr;
    }
    auto* w = FindWord(data->words, word, dictionary);
    if (!w) {
        if (len(data->words) >= kMaxVocabWords) {
            Fail(StrL("The vocabulary limit is 20,000 saved words."));
            return nullptr;
        }
        w = new VocabularyWord;
        w->id = str::Dup(NewId());
        w->word = str::Dup(word);
        w->addedTime = Now();
        w->dictionaryId = str::Dup(dictionary);
        VecAppend(data->words, w);
    }
    if (len(definition)) str::ReplaceWithCopy(&w->definition, definition);
    if (len(context)) str::ReplaceWithCopy(&w->context, context);
    if (len(source)) {
        str::ReplaceWithCopy(&w->sourcePath, source);
        w->page = std::max(0, page);
    }
    AttachDeck(w, deck);
    return VocabularySave() ? w : nullptr;
}
bool VocabularyRemove(Str id) {
    VocabularyWord* w = VocabularyFind(id);
    if (!w || data->readOnly) return false;
    VecRemove(data->words, w);
    delete w;
    return VocabularySave();
}
bool VocabularySetLearned(Str id, bool learned) {
    VocabularyWord* w = VocabularyFind(id);
    if (!w || data->readOnly) return false;
    w->learned = learned;
    if (learned) {
        w->dueTime = Now() + 30 * kVocabDay;
        w->intervalDays = 30;
        w->box = 4;
    } else {
        w->dueTime = Now();
        w->intervalDays = 0;
        w->box = 0;
        w->repetitions = 0;
    }
    return VocabularySave();
}
bool VocabularyReview(Str id, VocabGrade grade, VocabScheduler scheduler, i64 now) {
    VocabularyWord* w = VocabularyFind(id);
    if (!w || data->readOnly) return false;
    now = Now(now);
    int interval = 1;
    if (scheduler == VocabScheduler::Leitner) {
        constexpr int gaps[] = {1, 3, 7, 14, 30};
        int previousBox = w->box;
        if (grade == VocabGrade::Again)
            w->box = 0;
        else if (grade != VocabGrade::Hard)
            w->box = std::min(4, w->box + (grade == VocabGrade::Easy ? 2 : 1));
        interval = gaps[limitValue(w->box, 0, 4)];
        w->learned = grade != VocabGrade::Again && grade != VocabGrade::Hard && previousBox >= 4;
    } else {
        int quality = grade == VocabGrade::Again  ? 1
                      : grade == VocabGrade::Hard ? 3
                      : grade == VocabGrade::Good ? 4
                                                  : 5;
        w->ease = limitValue(w->ease + 0.1 - (5 - quality) * (0.08 + (5 - quality) * 0.02), 1.3, 10.0);
        if (grade == VocabGrade::Again) {
            w->repetitions = 0;
            interval = 1;
        } else {
            interval = w->repetitions == 0   ? 1
                       : w->repetitions == 1 ? 6
                                             : std::max(w->intervalDays + 1, (int)round(w->intervalDays * w->ease));
            if (grade == VocabGrade::Easy && w->repetitions >= 2) interval = (int)round(interval * 1.3);
            w->repetitions = std::min(w->repetitions + 1, 1000000);
        }
        w->learned = interval >= 60;
    }
    if (grade == VocabGrade::Again) {
        w->lapses = std::min(w->lapses + 1, 1000000);
        w->learned = false;
    }
    w->intervalDays = limitValue(interval, 1, kMaxReviewDays);
    w->reviews = std::min(w->reviews + 1, 1000000);
    w->lastReviewed = now;
    w->dueTime = now + (i64)w->intervalDays * kVocabDay;
    return VocabularySave();
}
void VocabularySearch(Str query, Str deck, bool learnedOnly, Vec<VocabularyWord*>& out) {
    VecReset(out);
    for (auto* w : VocabularyWords()) {
        if (!HasDeck(w, deck) || (learnedOnly && !w->learned)) continue;
        if (len(query) && !str::ContainsI(w->word, query) && !str::ContainsI(w->definition, query) &&
            !str::ContainsI(w->context, query))
            continue;
        VecAppend(out, w);
    }
}
void VocabularyDue(Str deck, Vec<VocabularyWord*>& out, i64 now, bool ahead) {
    VecReset(out);
    now = Now(now) + (ahead ? kVocabDay : 0);
    for (auto* w : VocabularyWords())
        if (HasDeck(w, deck) && w->dueTime <= now && len(w->definition)) VecAppend(out, w);
    VecSort(out, [](VocabularyWord* const* a, VocabularyWord* const* b) {
        if ((*a)->dueTime != (*b)->dueTime) return (*a)->dueTime < (*b)->dueTime ? -1 : 1;
        return str::CmpI((*a)->word, (*b)->word);
    });
}
VocabularyDeck* VocabularyCreateDeck(Str name, Str description) {
    if (!VocabularyLoad() || !len(name) || len(name) > 256 || len(description) > 8192 ||
        len(data->decks) >= kMaxVocabDecks)
        return nullptr;
    auto* d = new VocabularyDeck;
    d->id = str::Dup(NewId());
    d->name = str::Dup(name);
    d->description = str::Dup(description);
    VecAppend(data->decks, d);
    return VocabularySave() ? d : nullptr;
}
bool VocabularyRemoveDeck(Str id) {
    VocabularyLoad();
    VocabularyDeck* d = FindDeck(data->decks, id);
    if (!d || d->builtin || data->readOnly) return false;
    for (auto* w : data->words) {
        int index = w->deckIds.Find(id);
        if (index >= 0) w->deckIds.RemoveAt(index);
        if (str::Eq(w->deckId, id)) str::ReplaceWithCopy(&w->deckId, len(w->deckIds) ? w->deckIds[0] : Str{});
    }
    VecRemove(data->decks, d);
    delete d;
    return VocabularySave();
}
Str VocabularyBuiltinMeaning(Str word) {
    int first = 0, end = dimofi(builtinVocabMeanings);
    while (first < end) {
        int middle = first + (end - first) / 2;
        int comparison = str::CmpI(word, Str(builtinVocabMeanings[middle].word));
        if (comparison == 0) return Str(builtinVocabMeanings[middle].definition);
        if (comparison < 0)
            end = middle;
        else
            first = middle + 1;
    }
    return {};
}

int VocabularyInstallDeck(Str id) {
    if (!VocabularyLoad()) return -1;
    auto* deck = FindDeck(data->decks, id);
    if (!deck) return -1;
    data->batch = true;
    defer {
        data->batch = false;
    };
    int added = 0;
    for (int i = 0; i < len(deck->words); i++) {
        Str word = deck->words[i];
        Str definition = VocabularyBuiltinMeaning(word);
        Str dictionary = StrL("wmkeyboard-vocab-en");
        Vec<OfflineMeaning> meanings;
        Str error{};
        if (!len(definition) && LookupOfflineWord(word, meanings, &error) && len(meanings)) {
            definition = meanings[0].definition;
            dictionary = meanings[0].dictionaryId;
        }
        bool existed = FindWord(data->words, word, dictionary) != nullptr;
        if (len(definition) && VocabularyAdd(word, definition, dictionary, {}, {}, 0, id) && !existed) added++;
        FreeOfflineMeanings(meanings);
        str::Free(error);
    }
    data->batch = false;
    if (!VocabularySave()) return -1;
    return added;
}
bool VocabularyExport(Str path) {
    VocabularyLoad();
    Str bytes = Serialize(*data);
    defer {
        str::Free(bytes);
    };
    return WriteAtomic(path, bytes);
}
template <typename T>
static void SwapLists(Vec<T>& a, Vec<T>& b) {
    T* elements = a.els;
    int count = a.len, capacity = a.cap;
    a.els = b.els;
    a.len = b.len;
    a.cap = b.cap;
    b.els = elements;
    b.len = count;
    b.cap = capacity;
}

bool VocabularyImport(Str path, bool merge) {
    VocabularyLoad();
    VocabReader imported;
    if (!ReadVocabFile(path, imported)) return false;
    VocabReader combined;
    if (merge) {
        Str existing = Serialize(*data);
        bool valid = ParseVocab(existing, combined);
        str::Free(existing);
        if (!valid) return false;
    }
    auto& target = combined.parsed;
    if (len(target.words) + len(imported.parsed.words) > kMaxVocabWords ||
        len(target.decks) + len(imported.parsed.decks) > kMaxVocabDecks) {
        return Fail(StrL("Import exceeds the vocabulary size limit."));
    }
    for (auto* word : imported.parsed.words) {
        auto* existing = FindWord(target.words, word->word, word->dictionaryId);
        if (!existing) {
            if (FindIn(target.words, word->id)) str::ReplaceWithCopy(&word->id, NewId());
            VecAppend(target.words, word);
            continue;
        }
        for (int i = 0; i < len(word->deckIds); i++) AttachDeck(existing, word->deckIds[i]);
        if (word->lastReviewed > existing->lastReviewed) {
            existing->box = word->box;
            existing->reviews = word->reviews;
            existing->lapses = word->lapses;
            existing->intervalDays = word->intervalDays;
            existing->repetitions = word->repetitions;
            existing->ease = word->ease;
            existing->dueTime = word->dueTime;
            existing->lastReviewed = word->lastReviewed;
            existing->learned = word->learned;
        }
        if (!len(existing->definition)) str::ReplaceWithCopy(&existing->definition, word->definition);
        delete word;
    }
    VecReset(imported.parsed.words);
    for (auto* deck : imported.parsed.decks) {
        if (FindDeck(target.decks, deck->id))
            delete deck;
        else
            VecAppend(target.decks, deck);
    }
    VecReset(imported.parsed.decks);
    AddBuiltins(target);
    Str bytes = Serialize(target);
    bool success = (gDontSaveSettings && !data->test) || WriteAtomic(VocabularyStorePathTemp(), bytes);
    str::Free(bytes);
    if (!success) return false;
    SwapLists(data->words, target.words);
    SwapLists(data->decks, target.decks);
    data->readOnly = false;
    str::ReplaceWithCopy(&data->error, {});
    return true;
}

static u32 randomState = 0;
static u32 Random(u32 maximum) {
    if (!randomState) randomState = GetTickCount() ^ 0x6b8421u;
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return maximum ? randomState % maximum : 0;
}
bool VocabularyMakeQuestion(Str id, VocabActivity activity, VocabularyQuestion& out) {
    auto* word = VocabularyFind(id);
    if (!word || !len(word->definition)) return false;
    str::ReplaceWithCopy(&out.wordId, id);
    out.activity = activity;
    out.correctIndex = -1;
    out.choices.Reset();
    bool meanings = activity == VocabActivity::MeaningChoice;
    str::ReplaceWithCopy(&out.prompt,
                         activity == VocabActivity::Flashcards || meanings ? word->word : word->definition);
    str::ReplaceWithCopy(&out.answer,
                         meanings || activity == VocabActivity::Flashcards ? word->definition : word->word);
    if (activity == VocabActivity::WordScramble) {
        for (int i = 0; i < len(word->word); i++)
            if ((u8)word->word.s[i] >= 128) return false;
        Str scrambled = str::Dup(word->word);
        for (int i = len(scrambled) - 1; i > 0; i--) {
            int j = (int)Random(i + 1);
            char t = scrambled.s[i];
            scrambled.s[i] = scrambled.s[j];
            scrambled.s[j] = t;
        }
        if (str::Eq(scrambled, word->word) && len(scrambled) > 1) {
            char first = scrambled.s[0];
            memmove(scrambled.s, scrambled.s + 1, len(scrambled) - 1);
            scrambled.s[len(scrambled) - 1] = first;
        }
        str::ReplaceWithCopy(&out.prompt, fmt("%s\n\n%s", scrambled, word->definition));
        str::Free(scrambled);
        return true;
    }
    if (activity == VocabActivity::MeaningChoice || activity == VocabActivity::WordChoice ||
        activity == VocabActivity::MatchPairs) {
        out.choices.Append(out.answer);
        int total = len(data->words), start = (int)Random(total);
        for (int i = 0; i < total && len(out.choices) < 4; i++) {
            auto* alternative = data->words[(start + i) % total];
            Str option = meanings ? alternative->definition : alternative->word;
            if (len(option) && out.choices.Find(option) < 0) out.choices.Append(option);
        }
        if (len(out.choices) < 2) {
            Fail(StrL("Save at least two different words to start a choice activity."));
            return false;
        }
        int destination = (int)Random(len(out.choices));
        Str correct = str::Dup(out.choices[0]);
        Str other = str::Dup(out.choices[destination]);
        out.choices.SetAt(0, other);
        out.choices.SetAt(destination, correct);
        str::Free(correct);
        str::Free(other);
        out.correctIndex = destination;
    }
    return true;
}
bool VocabularyCheckAnswer(const VocabularyQuestion& question, Str input) {
    if (len(input) > 16000 || len(question.answer) > 16000) return false;
    Str answer = str::DupTemp(input), expected = str::DupTemp(question.answer);
    str::TrimWSInPlace(answer, str::TrimOpt::Both);
    str::TrimWSInPlace(expected, str::TrimOpt::Both);
    return len(answer) > 0 && len(expected) > 0 && SameWord(answer, expected);
}

static u64 DailyWordHash(Str word) {
    u64 hash = 1469598103934665603ULL;
    for (int i = 0; i < len(word); i++) {
        u8 character = (u8)word.s[i];
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        hash = (hash ^ character) * 1099511628211ULL;
    }
    return hash;
}
VocabularyWord* VocabularyWordOfDay(i64 now) {
    Vec<VocabularyWord*> ordered;
    for (auto* word : VocabularyWords()) VecAppend(ordered, word);
    int count = len(ordered);
    if (!count) return nullptr;
    VecSort(ordered, [](VocabularyWord* const* a, VocabularyWord* const* b) {
        u64 first = DailyWordHash((*a)->word), second = DailyWordHash((*b)->word);
        if (first != second) return first < second ? -1 : 1;
        return str::CmpI((*a)->word, (*b)->word);
    });
    int index = (int)((Now(now) / kVocabDay) % count);
    for (int i = 0; i < count; i++) {
        auto* word = ordered[(index + i) % count];
        if (!word->learned) return word;
    }
    return ordered[index];
}

#if IS_DEBUG
void Vocabulary_UnitTests() {
    VocabularyData isolated;
    isolated.loaded = true;
    isolated.test = true;
    isolated.path = str::Dup(GetTempFilePathTemp(StrL("vocabulary-tests")));
    VocabularyData* saved = data;
    data = &isolated;
    defer {
        data = saved;
        file::Delete(isolated.path);
    };
    auto* word = VocabularyAdd(StrL("  abhor  "), StrL("To strongly dislike."), StrL("test"),
                               StrL("A quote with \"quotation marks\" and\nnew line"), StrL("C:\\reading\\source.pdf"),
                               12, StrL("deck-one"));
    utassert(word != nullptr);
    if (!word) return;
    Str id = str::Dup(word->id);
    defer {
        str::Free(id);
    };
    auto* same = VocabularyAdd(StrL("ABHOR"), StrL("To strongly dislike."), StrL("test"), {}, {}, 0, StrL("deck-two"));
    utassert(same == word && len(isolated.words) == 1 && len(word->deckIds) == 2);
    utassert(VocabularyReview(id, VocabGrade::Good, VocabScheduler::Leitner, 1700000000));
    utassert(word->intervalDays == 3);
    utassert(VocabularyReview(id, VocabGrade::Good, VocabScheduler::Leitner, 1700100000));
    utassert(word->intervalDays == 7);
    utassert(VocabularyReview(id, VocabGrade::Again, VocabScheduler::Leitner, 1700200000));
    utassert(word->box == 0 && !word->learned && word->lapses == 1);
    utassert(VocabularyReview(id, VocabGrade::Good, VocabScheduler::Sm2, 1700300000));
    utassert(word->intervalDays == 1 && word->repetitions == 1);
    utassert(VocabularyReview(id, VocabGrade::Good, VocabScheduler::Sm2, 1700400000));
    utassert(word->intervalDays == 6 && word->repetitions == 2);
    utassert(VocabularyReview(id, VocabGrade::Good, VocabScheduler::Sm2, 1700500000));
    utassert(word->intervalDays == 15);
    utassert(VocabularyReview(id, VocabGrade::Again, VocabScheduler::Sm2, 1700600000));
    utassert(word->intervalDays == 1 && word->repetitions == 0);

    Str bytes = Serialize(isolated);
    defer {
        str::Free(bytes);
    };
    VocabReader reader;
    utassert(ParseVocab(bytes, reader));
    utassert(len(reader.parsed.words) == 1);
    utassert(str::Eq(reader.parsed.words[0]->context, word->context));
    utassert(reader.parsed.words[0]->page == 12);
    VocabReader corrupt;
    utassert(!ParseVocab(StrL("{broken"), corrupt));
    utassert(len(isolated.words) == 1);
    VocabReader tooLarge;
    utassert(!ParseVocab(StrL("{\"format\":\"sumatrapdf-vocabulary\",\"version\":99,\"words\":[]}"), tooLarge));
    VocabReader wm;
    utassert(ParseVocab(StrL("{\"format\":\"wmkeyboard-vocab\",\"version\":1,\"pack\":{\"id\":\"custom\",\"name\":"
                             "\"Custom\",\"attribution\":[{\"name\":\"Author\",\"license\":\"MIT\"}]},\"words\":[{"
                             "\"word\":\"abate\",\"senses\":[{\"definition\":\"Become less intense\"}]}]}"),
                        wm));
    utassert(len(wm.parsed.words) == 1 && len(wm.parsed.decks) == 1 &&
             str::Contains(wm.parsed.decks[0]->license, StrL("MIT")));
    utassert(VocabularySetLearned(id, true));
    utassert(word->learned);
    utassert(VocabularySetLearned(id, false));
    utassert(!word->learned);
    utassert(VocabularyAdd(StrL("abate"), StrL("Become less intense."), StrL("test")) != nullptr);
    VocabularyQuestion q;
    utassert(VocabularyMakeQuestion(id, VocabActivity::WordChoice, q));
    utassert(q.correctIndex >= 0 && VocabularyCheckAnswer(q, q.choices[q.correctIndex]));
    Vec<VocabularyWord*> due;
    VocabularyDue({}, due, 1700200000);
    utassert(len(due) == 1);
    VocabReader disk;
    utassert(ReadVocabFile(isolated.path, disk));
    utassert(len(disk.parsed.words) == 2 && disk.parsed.words[0]->page == 12);
    Str exported = str::Dup(GetTempFilePathTemp(StrL("vocabulary-export-test")));
    defer {
        file::Delete(exported);
        str::Free(exported);
    };
    utassert(VocabularyExport(exported));
    utassert(VocabularyRemove(id));
    utassert(VocabularyFind(id) == nullptr);
    utassert(VocabularyImport(exported, true));
    utassert(VocabularyFind(id) != nullptr);
    int before = len(isolated.words);
    Str bad = str::Dup(GetTempFilePathTemp(StrL("vocabulary-corrupt-test")));
    defer {
        file::Delete(bad);
        str::Free(bad);
    };
    utassert(file::WriteFile(bad, StrL("{broken")));
    utassert(!VocabularyImport(bad, false));
    utassert(len(isolated.words) == before && VocabularyFind(id) != nullptr);
    VocabularyQuestion flash;
    utassert(VocabularyMakeQuestion(id, VocabActivity::Flashcards, flash));
    utassert(str::Eq(flash.answer, VocabularyFind(id)->definition));
    utassert(!VocabularyCheckAnswer(flash, {}));
}
#endif
