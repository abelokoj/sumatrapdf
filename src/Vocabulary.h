/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3. */

#ifndef SUMATRA_VOCABULARY_H
#define SUMATRA_VOCABULARY_H

enum class VocabGrade {
    Again,
    Hard,
    Good,
    Easy
};
enum class VocabScheduler {
    Leitner,
    Sm2
};
enum class VocabActivity {
    Flashcards,
    MeaningChoice,
    WordChoice,
    Spelling,
    WordScramble,
    MatchPairs
};

struct VocabularyWord {
    Str id, word, definition, dictionaryId, context, sourcePath, deckId;
    StrVec deckIds;
    int page = 0, box = 0, reviews = 0, lapses = 0, intervalDays = 0, repetitions = 0;
    double ease = 2.5;
    i64 dueTime = 0, lastReviewed = 0, addedTime = 0;
    bool learned = false;
    ~VocabularyWord();
};

struct VocabularyDeck {
    Str id, name, description, source, license;
    StrVec words;
    bool builtin = false;
    ~VocabularyDeck();
};

struct VocabularyQuestion {
    Str wordId, prompt, answer;
    StrVec choices;
    int correctIndex = -1;
    VocabActivity activity = VocabActivity::Flashcards;
    ~VocabularyQuestion();
};

bool VocabularyLoad();
bool VocabularySave();
const Vec<VocabularyWord*>& VocabularyWords();
const Vec<VocabularyDeck*>& VocabularyDecks();
VocabularyWord* VocabularyFind(Str id);
VocabularyWord* VocabularyAdd(Str word, Str definition, Str dictionaryId = {}, Str context = {}, Str sourcePath = {},
                              int page = 0, Str deckId = {});
bool VocabularyRemove(Str id);
bool VocabularySetLearned(Str id, bool learned);
bool VocabularyReview(Str id, VocabGrade grade, VocabScheduler scheduler = VocabScheduler::Sm2, i64 now = 0);
void VocabularySearch(Str query, Str deckId, bool learnedOnly, Vec<VocabularyWord*>& out);
void VocabularyDue(Str deckId, Vec<VocabularyWord*>& out, i64 now = 0, bool studyAhead = false);
VocabularyDeck* VocabularyCreateDeck(Str name, Str description = {});
bool VocabularyRemoveDeck(Str id);
int VocabularyInstallDeck(Str id);
bool VocabularyExport(Str path);
bool VocabularyImport(Str path, bool merge = true);
Str VocabularyLastError();
Str VocabularyBuiltinMeaning(Str word);
bool VocabularyMakeQuestion(Str wordId, VocabActivity activity, VocabularyQuestion& out);
bool VocabularyCheckAnswer(const VocabularyQuestion& question, Str answer);
VocabularyWord* VocabularyWordOfDay(i64 now = 0);
Str VocabularyStorePathTemp();

#endif
