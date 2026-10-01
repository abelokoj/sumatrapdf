/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3. */

#include "base/Base.h"
#include "base/Win.h"
#include "base/UITask.h"
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/PlatformFont.h"

// The two native modeless-window helpers do not require the full control hierarchy.
HWND GetCurrentModelessDialog();

void SetCurrentModelessDialog(HWND);
#include <commdlg.h>
#include "Settings.h"
#include "AppSettings.h"
#include "MainWindow.h"
#include "SumatraPDF.h"
#include "Theme.h"
#include "Translations.h"
#include "OfflineDictionary.h"
#include "Vocabulary.h"
#include "VocabularyDialog.h"

constexpr WCHAR kLearningClass[] = L"SumatraPDFEnhancedLearning";
enum LearningControl {
    lcTitle = 101,
    lcQuery,
    lcLookup,
    lcPack,
    lcImportPack,
    lcDownload,
    lcRemovePack,
    lcDeck,
    lcInstallDeck,
    lcNewDeck,
    lcCreateDeck,
    lcDeleteDeck,
    lcActivity,
    lcScheduler,
    lcPractice,
    lcLibrary,
    lcDetails,
    lcAnswer,
    lcCheck,
    lcReveal,
    lcAgain,
    lcHard,
    lcGood,
    lcEasy,
    lcSave,
    lcLearned,
    lcDeleteWord,
    lcExport,
    lcImport,
    lcStatus,
    lcChoices,
    lcPairs,
    lcBack,
    lcOpenVocabulary,
    lcLast
};
struct LearningWindow {
    HWND hwnd = nullptr;
    MainWindow* owner = nullptr;
    bool dictionary = false, practice = false, revealed = false, checked = false;
    bool busy = false, updating = false, ready = false, lookupBusy = false;
    int scrollY = 0, contentHeight = 0;
    int serial = 0, ticket = 0, page = 0, position = 0, correct = 0;
    HWND controls[lcLast]{};
    HBRUSH background = nullptr, fieldBackground = nullptr;
    Str context, source;
    Vec<OfflineDictPack> packs;
    Vec<OfflineMeaning> meanings;
    StrVec wordIds, deckIds, session, pairWords, pairDefinitions;
    VocabularyQuestion* question = nullptr;
    ~LearningWindow() {
        str::Free(context);
        str::Free(source);
        FreeOfflineMeanings(meanings);
        FreeDictionaryCatalog(packs);
        delete question;
        DeleteObject(background);
        DeleteObject(fieldBackground);
    }
};
static Vec<LearningWindow*> gLearningWindows;
static int gLearningSerial = 0;

static HWND Control(LearningWindow* w, int id) {
    return w->controls[id];
}

static void Text(LearningWindow* w, int id, Str s) {
    SetWindowTextW(Control(w, id), CWStrTemp(s));
}

static TempStr Read(LearningWindow* w, int id) {
    return HwndGetTextTemp(Control(w, id));
}

static int Selected(LearningWindow* w, int id) {
    return (int)SendMessageW(Control(w, id), CB_GETCURSEL, 0, 0);
}

static void AddChoice(LearningWindow* w, int id, Str text) {
    SendMessageW(Control(w, id), CB_ADDSTRING, 0, (LPARAM)CWStrTemp(text));
}

static void Status(LearningWindow* w, Str text) {
    Text(w, lcStatus, text);
}
static void Place(LearningWindow* w, int id, int x, int y, int dx, int dy) {
    if (Control(w, id)) {
        MoveWindow(Control(w, id), x, y - w->scrollY, std::max(dx, 1), std::max(dy, 1), true);
    }
}

static void Visible(LearningWindow* w, int id, bool value) {
    ShowWindow(Control(w, id), value ? SW_SHOW : SW_HIDE);
}
static Str CurrentDeck(LearningWindow* w) {
    int i = Selected(w, lcDeck);
    return i >= 0 && i < len(w->deckIds) ? w->deckIds[i] : Str{};
}
static VocabularyWord* SelectedWord(LearningWindow* w) {
    int i = (int)SendMessageW(Control(w, lcLibrary), LB_GETCURSEL, 0, 0);
    return i >= 0 && i < len(w->wordIds) ? VocabularyFind(w->wordIds[i]) : nullptr;
}
static void RefreshDecks(LearningWindow* w) {
    Str previous = str::Dup(CurrentDeck(w));
    w->updating = true;
    SendMessageW(Control(w, lcDeck), CB_RESETCONTENT, 0, 0);
    w->deckIds.Reset();
    AddChoice(w, lcDeck, Tr("All vocabulary"));
    w->deckIds.Append({});
    for (VocabularyDeck* deck : VocabularyDecks()) {
        AddChoice(w, lcDeck, deck->name);
        w->deckIds.Append(deck->id);
    }
    int i = w->deckIds.Find(previous);
    SendMessageW(Control(w, lcDeck), CB_SETCURSEL, std::max(i, 0), 0);
    str::Free(previous);
    w->updating = false;
}
static void RefreshPacks(LearningWindow* w) {
    FreeDictionaryCatalog(w->packs);
    GetDictionaryCatalog(w->packs);
    SendMessageW(Control(w, lcPack), CB_RESETCONTENT, 0, 0);
    for (const OfflineDictPack& pack : w->packs) {
        AddChoice(w, lcPack, fmt("%s · %s", pack.title, pack.installed ? Tr("installed") : Tr("not installed")));
    }
    SendMessageW(Control(w, lcPack), CB_SETCURSEL, 0, 0);
}
static void WordDetails(LearningWindow* w) {
    VocabularyWord* word = SelectedWord(w);
    EnableWindow(Control(w, lcLearned), word != nullptr);
    EnableWindow(Control(w, lcDeleteWord), word != nullptr);
    if (!word) {
        Text(w, lcDetails,
             Tr("No saved words here yet. Select a word in a document and press Shift+D to look it up and save it."));
        return;
    }
    str::Builder text;
    text.Append(fmt("%s\r\n\r\n%s", word->word, word->definition));
    if (len(word->context)) {
        text.Append(fmt("\r\n\r\nContext\r\n%s", word->context));
    }
    if (len(word->sourcePath)) {
        text.Append(fmt("\r\n\r\nSource: %s · page %d", word->sourcePath, word->page));
    }
    text.Append(
        fmt("\r\n\r\nReviews: %d · lapses: %d · interval: %d days", word->reviews, word->lapses, word->intervalDays));
    Text(w, lcDetails, ToStrTemp(text));
    Text(w, lcLearned, word->learned ? Tr("Mark unlearned") : Tr("Mark learned"));
}
static void RefreshLibrary(LearningWindow* w) {
    w->updating = true;
    Vec<VocabularyWord*> words;
    VocabularySearch(Read(w, lcQuery), CurrentDeck(w), false, words);
    w->wordIds.Reset();
    SendMessageW(Control(w, lcLibrary), LB_RESETCONTENT, 0, 0);
    for (VocabularyWord* word : words) {
        w->wordIds.Append(word->id);
        SendMessageW(Control(w, lcLibrary), LB_ADDSTRING, 0,
                     (LPARAM)CWStrTemp(fmt("%s%s", word->word, word->learned ? StrL("  ✓") : Str{})));
    }
    if (len(words)) {
        SendMessageW(Control(w, lcLibrary), LB_SETCURSEL, 0, 0);
    }
    Vec<VocabularyWord*> due;
    VocabularyDue(CurrentDeck(w), due);
    Status(w,
           len(words)
               ? fmt("%d saved words · %d due for review", len(words), len(due))
               : Tr("Start by selecting a built-in deck and Install deck, or save a word while reading with Shift+D."));
    w->updating = false;
    if (!w->practice) {
        WordDetails(w);
    }
}
static void LayoutLearning(LearningWindow* w) {
    if (!w->ready) {
        return;
    }
    RECT client;
    GetClientRect(w->hwnd, &client);
    int pad = DpiScale(16), gap = DpiScale(8),
        row = std::max(DpiScale(32), GetAppFontSizeForDpi(DpiGet()) + DpiScale(16));
    int viewport = client.bottom;
    client.bottom = std::max((int)client.bottom, row * 11 + gap * 12 + pad * 2);
    w->contentHeight = client.bottom;
    w->scrollY = std::clamp(w->scrollY, 0, std::max(0, (int)client.bottom - viewport));
    SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS};
    scroll.nMin = 0;
    scroll.nMax = client.bottom - 1;
    scroll.nPage = viewport;
    scroll.nPos = w->scrollY;
    SetScrollInfo(w->hwnd, SB_VERT, &scroll, true);
    int width = client.right - pad * 2, y = pad, action = DpiScale(110);
    Place(w, lcTitle, pad, y, width, row);
    y += row + gap;
    Place(w, lcQuery, pad, y, width - action - gap, row);
    Place(w, lcLookup, client.right - pad - action, y, action, row);
    y += row + gap;
    if (w->dictionary) {
        int b = DpiScale(94);
        Place(w, lcPack, pad, y, width - b * 3 - gap * 3, row * 10);
        Place(w, lcImportPack, client.right - pad - b * 3 - gap * 2, y, b, row);
        Place(w, lcDownload, client.right - pad - b * 2 - gap, y, b, row);
        Place(w, lcRemovePack, client.right - pad - b, y, b, row);
        y += row + gap;
        int footer = row * 3 + gap * 4;
        Place(w, lcDetails, pad, y, width, std::max(row * 2, (int)client.bottom - y - footer));
        y = std::max(y + row * 2 + gap, (int)client.bottom - footer + gap);
        Place(w, lcDeck, pad, y, width - DpiScale(240) - gap, row * 10);
        Place(w, lcSave, client.right - pad - DpiScale(240), y, DpiScale(114), row);
        Place(w, lcLearned, client.right - pad - DpiScale(118), y, DpiScale(118), row);
        y += row + gap;
        Place(w, lcOpenVocabulary, pad, y, DpiScale(180), row);
        Place(w, lcStatus, pad, y + row + gap, width, row);
    } else {
        Place(w, lcDeck, pad, y, width - DpiScale(240) - gap * 2, row * 10);
        Place(w, lcInstallDeck, client.right - pad - DpiScale(240) - gap, y, DpiScale(120), row);
        Place(w, lcDeleteDeck, client.right - pad - DpiScale(120), y, DpiScale(120), row);
        y += row + gap;
        Place(w, lcNewDeck, pad, y, width - DpiScale(320) - gap * 3, row);
        Place(w, lcCreateDeck, client.right - pad - DpiScale(320) - gap * 2, y, DpiScale(112), row);
        Place(w, lcExport, client.right - pad - DpiScale(200) - gap, y, DpiScale(96), row);
        Place(w, lcImport, client.right - pad - DpiScale(96), y, DpiScale(96), row);
        y += row + gap;
        Place(w, lcActivity, pad, y, width - DpiScale(256) - gap * 2, row * 10);
        Place(w, lcScheduler, client.right - pad - DpiScale(256) - gap, y, DpiScale(128), row * 4);
        Place(w, lcPractice, client.right - pad - DpiScale(120), y, DpiScale(120), row);
        y += row + gap;
        int bottom = client.bottom - pad - row * 2 - gap * 2;
        if (!w->practice) {
            int listWidth = std::max(width / 3, DpiScale(160));
            Place(w, lcLibrary, pad, y, listWidth, std::max(row * 2, bottom - y));
            Place(w, lcDetails, pad + listWidth + gap, y, width - listWidth - gap, std::max(row * 2, bottom - y));
            Place(w, lcLearned, pad, bottom + gap, DpiScale(148), row);
            Place(w, lcDeleteWord, pad + DpiScale(156), bottom + gap, DpiScale(120), row);
        } else {
            bool matching = Selected(w, lcActivity) == (int)VocabActivity::MatchPairs;
            int prompt = matching ? row * 2 : std::max(row * 2, (bottom - y) / 2);
            Place(w, lcDetails, pad, y, width, prompt);
            y += prompt + gap;
            Place(w, lcAnswer, pad, y, width - DpiScale(128), row);
            Place(w, lcChoices, pad, y, matching ? (width - gap) / 2 : width - DpiScale(128),
                  std::max(row * 2, bottom - y));
            Place(w, lcPairs, pad + (width + gap) / 2, y, (width - gap) / 2, std::max(row * 2, bottom - y));
            Place(w, lcCheck, client.right - pad - DpiScale(120), y, DpiScale(120), row);
            Place(w, lcReveal, pad, bottom + gap, DpiScale(120), row);
            Place(w, lcBack, pad, bottom + gap, DpiScale(120), row);
            int x = pad + DpiScale(128);
            int grades[] = {lcAgain, lcHard, lcGood, lcEasy};
            for (int id : grades) {
                Place(w, id, x, bottom + gap, DpiScale(92), row);
                x += DpiScale(100);
            }
        }
        Place(w, lcStatus, pad, client.bottom - pad - row, width, row);
    }
    InvalidateRect(w->hwnd, nullptr, false);
}
static void RevealFocusedControl(LearningWindow* w, HWND child) {
    if (!child || !w->ready) {
        return;
    }
    RECT rc, client;
    GetWindowRect(child, &rc);
    GetClientRect(w->hwnd, &client);
    MapWindowPoints(nullptr, w->hwnd, (POINT*)&rc, 2);
    int margin = DpiScale(12);
    if (rc.top < margin) {
        w->scrollY += rc.top - margin;
    } else if (rc.bottom > client.bottom - margin) {
        w->scrollY += rc.bottom - client.bottom + margin;
    } else {
        return;
    }
    LayoutLearning(w);
}

static void PracticeControls(LearningWindow* w) {
    bool active = w->practice;
    int library[] = {lcLibrary, lcLearned, lcDeleteWord};
    for (int id : library) {
        Visible(w, id, !active);
    }
    int mode = Selected(w, lcActivity);
    bool cards = mode == (int)VocabActivity::Flashcards;
    bool choices = mode == (int)VocabActivity::MeaningChoice || mode == (int)VocabActivity::WordChoice;
    bool pairs = mode == (int)VocabActivity::MatchPairs;
    Visible(w, lcAnswer, active && !cards && !choices && !pairs);
    Visible(w, lcChoices, active && (choices || pairs));
    Visible(w, lcPairs, active && pairs);
    Visible(w, lcCheck, active && !cards && !pairs);
    Visible(w, lcReveal, active && cards && !w->revealed);
    int grades[] = {lcAgain, lcHard, lcGood, lcEasy};
    for (int id : grades) {
        Visible(w, id, active && cards && w->revealed);
    }
    Visible(w, lcBack, active && (!cards || w->revealed));
    LayoutLearning(w);
}

static VocabScheduler Scheduler(LearningWindow* w) {
    return Selected(w, lcScheduler) == 0 ? VocabScheduler::Sm2 : VocabScheduler::Leitner;
}
static void NextQuestion(LearningWindow* w) {
    delete w->question;
    w->question = nullptr;
    if (w->position >= len(w->session)) {
        w->practice = false;
        PracticeControls(w);
        RefreshLibrary(w);
        Status(w, fmt("Session complete: %d of %d correct", w->correct, len(w->session)));
        return;
    }
    w->question = new VocabularyQuestion();
    if (!VocabularyMakeQuestion(w->session[w->position], (VocabActivity)Selected(w, lcActivity), *w->question)) {
        w->practice = false;
        PracticeControls(w);
        Status(w, Tr("Save more defined words before starting this activity."));
        return;
    }
    w->revealed = false;
    w->checked = false;
    Text(w, lcDetails, w->question->prompt);
    Text(w, lcAnswer, {});
    Text(w, lcCheck, Tr("Check answer"));
    SendMessageW(Control(w, lcChoices), LB_RESETCONTENT, 0, 0);
    for (Str choice : w->question->choices) {
        SendMessageW(Control(w, lcChoices), LB_ADDSTRING, 0, (LPARAM)CWStrTemp(choice));
    }
    Status(w, fmt("Review %d of %d · %d correct", w->position + 1, len(w->session), w->correct));
    PracticeControls(w);
    SetFocus(Control(w, len(w->question->choices) ? lcChoices : Selected(w, lcActivity) == 0 ? lcReveal : lcAnswer));
}
static void StartPractice(LearningWindow* w) {
    Vec<VocabularyWord*> due;
    VocabularyDue(CurrentDeck(w), due, 0, true);
    w->session.Reset();
    int newWords = 0;
    for (VocabularyWord* word : due) {
        if (word->learned || (!word->reviews && newWords >= 10)) {
            continue;
        }
        if (!word->reviews) {
            newWords++;
        }
        w->session.Append(word->id);
        if (len(w->session) >= 30) {
            break;
        }
    }
    if (!len(w->session)) {
        Status(w, Tr("No words to practice. Save a definition, install a deck, or mark a learned word unlearned."));
        return;
    }
    w->practice = true;
    w->position = 0;
    w->correct = 0;
    if (Selected(w, lcActivity) != (int)VocabActivity::MatchPairs) {
        NextQuestion(w);
        return;
    }
    delete w->question;
    w->question = nullptr;
    w->pairWords.Reset();
    w->pairDefinitions.Reset();
    SendMessageW(Control(w, lcChoices), LB_RESETCONTENT, 0, 0);
    SendMessageW(Control(w, lcPairs), LB_RESETCONTENT, 0, 0);
    int count = std::min(len(w->session), 6);
    for (int i = 0; i < count; i++) {
        VocabularyWord* word = VocabularyFind(w->session[i]);
        w->pairWords.Append(word->id);
        SendMessageW(Control(w, lcChoices), LB_ADDSTRING, 0, (LPARAM)CWStrTemp(word->word));
    }
    int rotation = count > 1 ? 1 + (int)(GetTickCount64() % (count - 1)) : 0;
    for (int i = 0; i < count; i++) {
        VocabularyWord* word = VocabularyFind(w->session[(i + rotation) % count]);
        w->pairDefinitions.Append(word->id);
        SendMessageW(Control(w, lcPairs), LB_ADDSTRING, 0, (LPARAM)CWStrTemp(word->definition));
    }
    Text(w, lcDetails,
         Tr("Match words to meanings. Select a word on the left and its definition on the right. Matching pairs "
            "disappear."));
    Status(w, fmt("%d pairs remaining", count));
    PracticeControls(w);
}
static void CheckPair(LearningWindow* w) {
    if (!HasPermission(Perm::SavePreferences)) {
        Status(w, Tr("Saving review progress is unavailable in restricted mode."));
        return;
    }
    int left = (int)SendMessageW(Control(w, lcChoices), LB_GETCURSEL, 0, 0);
    int right = (int)SendMessageW(Control(w, lcPairs), LB_GETCURSEL, 0, 0);
    if (left < 0 || right < 0 || left >= len(w->pairWords) || right >= len(w->pairDefinitions)) {
        return;
    }
    if (!str::Eq(w->pairWords[left], w->pairDefinitions[right])) {
        VocabularyReview(w->pairWords[left], VocabGrade::Again, Scheduler(w));
        Status(w, Tr("Not a match. Try another definition."));
        SendMessageW(Control(w, lcPairs), LB_SETCURSEL, (WPARAM)-1, 0);
        return;
    }
    VocabularyReview(w->pairWords[left], VocabGrade::Good, Scheduler(w));
    w->pairWords.RemoveAt(left);
    w->pairDefinitions.RemoveAt(right);
    SendMessageW(Control(w, lcChoices), LB_DELETESTRING, left, 0);
    SendMessageW(Control(w, lcPairs), LB_DELETESTRING, right, 0);
    SendMessageW(Control(w, lcChoices), LB_SETCURSEL, (WPARAM)-1, 0);
    SendMessageW(Control(w, lcPairs), LB_SETCURSEL, (WPARAM)-1, 0);
    if (!len(w->pairWords)) {
        w->practice = false;
        PracticeControls(w);
        RefreshLibrary(w);
        Status(w, Tr("All pairs matched. Review progress saved."));
    } else {
        Status(w, fmt("Correct · %d pairs remaining", len(w->pairWords)));
    }
}
static void CheckAnswer(LearningWindow* w) {
    if (!HasPermission(Perm::SavePreferences)) {
        Status(w, Tr("Saving review progress is unavailable in restricted mode."));
        return;
    }
    if (!w->question) {
        return;
    }
    if (w->checked) {
        w->position++;
        NextQuestion(w);
        return;
    }
    Str answer = Read(w, lcAnswer);
    if (len(w->question->choices)) {
        int i = (int)SendMessageW(Control(w, lcChoices), LB_GETCURSEL, 0, 0);
        if (i < 0 || i >= len(w->question->choices)) {
            Status(w, Tr("Choose an answer first."));
            return;
        }
        answer = w->question->choices[i];
    }
    bool correct = VocabularyCheckAnswer(*w->question, answer);
    VocabularyReview(w->question->wordId, correct ? VocabGrade::Good : VocabGrade::Again, Scheduler(w));
    w->correct += correct ? 1 : 0;
    w->checked = true;
    Text(w, lcDetails,
         fmt("%s\r\n\r\n%s\r\n\r\n%s", w->question->prompt, correct ? Tr("Correct") : Tr("Not quite"),
             w->question->answer));
    Text(w, lcCheck, Tr("Next word"));
    Status(w, Tr("Progress saved. Continue when ready."));
}
static TempStr ChooseLearningFile(HWND owner, bool save, bool dictionary) {
    WCHAR path[MAX_PATH]{};
    OPENFILENAMEW args{};
    args.lStructSize = sizeof(args);
    args.hwndOwner = owner;
    args.lpstrFile = path;
    args.nMaxFile = dimof(path);
    args.lpstrFilter = dictionary ? L"Offline dictionaries / WM "
                                    L"packs\0*.tsv;*.ifo;*.wmvocab.json;*.wmvocab.json.gz;*.json;*.gz\0All files\0*.*\0"
                                  : L"Vocabulary / WM packs\0*.json;*.gz\0All files\0*.*\0";
    args.lpstrDefExt = dictionary ? nullptr : L"json";
    args.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    bool picked = save ? GetSaveFileNameW(&args) : GetOpenFileNameW(&args);
    return picked ? ToUtf8Temp(path) : TempStr{};
}

enum class DictionaryJobKind {
    Lookup,
    Download,
    Import,
    Remove,
    WarmDeck
};
struct DictionaryJob {
    HWND hwnd;
    int serial, ticket;
    DictionaryJobKind kind;
    Str value, error;
    bool ok = false;
    Vec<OfflineMeaning> meanings;
    ~DictionaryJob() {
        str::Free(value);
        str::Free(error);
        FreeOfflineMeanings(meanings);
    }
};
static void CompleteDictionaryJob(DictionaryJob* job) {
    auto* w = IsWindow(job->hwnd) ? (LearningWindow*)GetWindowLongPtrW(job->hwnd, GWLP_USERDATA) : nullptr;
    if (!w || w->serial != job->serial || w->ticket != job->ticket) {
        delete job;
        return;
    }
    w->busy = false;
    w->lookupBusy = false;
    int actions[] = {lcLookup, lcImportPack, lcDownload, lcRemovePack, lcInstallDeck};
    for (int id : actions) {
        EnableWindow(Control(w, id), true);
    }
    if (job->kind == DictionaryJobKind::Lookup) {
        FreeOfflineMeanings(w->meanings);
        for (OfflineMeaning& meaning : job->meanings) {
            VecAppend(w->meanings, meaning);
        }
        VecReset(job->meanings);
        str::Builder text;
        for (OfflineMeaning& meaning : w->meanings) {
            text.Append(fmt("%s\r\n%s\r\n%s\r\n\r\n", meaning.headword, meaning.dictionary, meaning.definition));
        }
        if (!len(w->meanings)) {
            Text(w, lcDetails,
                 job->error ? job->error
                            : Tr("No offline definition found. Try another spelling or import a dictionary pack."));
        } else {
            if (len(w->context)) {
                text.Append(fmt("Context\r\n%s\r\n", w->context));
            }
            if (len(w->source)) {
                text.Append(fmt("Source: %s · page %d", w->source, w->page));
            }
            Text(w, lcDetails, ToStrTemp(text));
        }
        EnableWindow(Control(w, lcSave), len(w->meanings) > 0);
        EnableWindow(Control(w, lcLearned), len(w->meanings) > 0);
        Status(w, len(w->meanings) ? fmt("%d offline definition entries", len(w->meanings))
                                   : Tr("No result. Lookup never uses the network."));
    } else if (job->kind == DictionaryJobKind::WarmDeck && job->ok) {
        int added = VocabularyInstallDeck(job->value);
        RefreshDecks(w);
        RefreshLibrary(w);
        Status(w, added < 0 ? VocabularyLastError() : fmt("%d words added to this deck", added));
    } else {
        if (w->dictionary) {
            RefreshPacks(w);
            if (!job->ok && len(job->error)) {
                Text(w, lcDetails, job->error);
            }
        }
        Status(w, job->ok      ? Tr("Dictionary packs updated. Look up a word to try the installed pack.")
                  : job->error ? job->error
                               : Tr("Could not complete this action. You can retry."));
    }
    delete job;
}
static void RunDictionaryJob(DictionaryJob* job) {
    switch (job->kind) {
        case DictionaryJobKind::Lookup:
            job->ok = LookupOfflineWord(job->value, job->meanings, &job->error);
            break;
        case DictionaryJobKind::Download:
            job->ok = DownloadDictionaryPack(job->value, &job->error);
            break;
        case DictionaryJobKind::Import:
            job->ok = InstallDictionaryFile(job->value, &job->error);
            break;
        case DictionaryJobKind::Remove:
            job->ok = RemoveDictionaryPack(job->value, &job->error);
            break;
        case DictionaryJobKind::WarmDeck:
            job->ok = true;
            break;
    }
    uitask::Post(MkFunc0(CompleteDictionaryJob, job), "Complete offline dictionary action");
}
static void StartDictionaryJob(LearningWindow* w, DictionaryJobKind kind, Str value) {
    if (!CanAccessDisk()) {
        Status(w, Tr("Dictionary storage is unavailable in restricted mode."));
        return;
    }
    if (kind == DictionaryJobKind::Download && !HasPermission(Perm::InternetAccess)) {
        Status(w, Tr("Downloads are unavailable in restricted mode. Installed dictionaries still work offline."));
        return;
    }
    if (kind != DictionaryJobKind::Lookup && !HasPermission(Perm::SavePreferences)) {
        Status(w, Tr("Saving is unavailable in restricted mode."));
        return;
    }
    if (w->busy) {
        Status(w, Tr("Please wait for the current dictionary action to finish."));
        return;
    }
    auto* job = new DictionaryJob();
    job->hwnd = w->hwnd;
    job->serial = w->serial;
    job->ticket = ++w->ticket;
    job->kind = kind;
    job->value = str::Dup(value);
    w->busy = true;
    w->lookupBusy = kind == DictionaryJobKind::Lookup;
    int actions[] = {lcLookup, lcImportPack, lcDownload, lcRemovePack, lcInstallDeck};
    for (int id : actions) {
        EnableWindow(Control(w, id), false);
    }
    if (kind == DictionaryJobKind::Lookup) {
        EnableWindow(Control(w, lcSave), false);
        EnableWindow(Control(w, lcLearned), false);
    }
    Status(w, kind == DictionaryJobKind::Download
                  ? Tr("Downloading and verifying dictionary pack… You can keep reading.")
              : kind == DictionaryJobKind::Lookup ? Tr("Looking up this word in installed offline dictionaries…")
                                                  : Tr("Updating offline resources…"));
    RunAsync(MkFunc0(RunDictionaryJob, job), StrL("OfflineDictionaryAction"));
}
static void LookupWord(LearningWindow* w) {
    Str query = Read(w, lcQuery);
    if (!len(query)) {
        Status(w, Tr("Type a word or select one in the document first."));
        return;
    }
    StartDictionaryJob(w, DictionaryJobKind::Lookup, query);
}
static void SaveMeaning(LearningWindow* w, bool learned) {
    if (!len(w->meanings)) {
        return;
    }
    const OfflineMeaning& meaning = w->meanings[0];
    VocabularyWord* word = VocabularyAdd(meaning.headword, meaning.definition, meaning.dictionaryId, w->context,
                                         w->source, w->page, CurrentDeck(w));
    if (word && learned) {
        VocabularySetLearned(word->id, true);
    }
    Status(w, word ? learned ? Tr("Saved and marked learned.")
                             : Tr("Saved to vocabulary. Review it from the learning hub.")
                   : VocabularyLastError());
    InvalidateRect(w->owner->hwndCanvas, nullptr, false);
}
static void LearningAction(LearningWindow* w, int id, int notification) {
    if (!w->ready || w->updating) {
        return;
    }
    if (id == IDCANCEL) {
        DestroyWindow(w->hwnd);
        return;
    }
    if (id == IDOK) {
        if (w->dictionary) {
            LookupWord(w);
        } else if (w->practice) {
            if (Selected(w, lcActivity) == (int)VocabActivity::MatchPairs) {
                CheckPair(w);
            } else if (Selected(w, lcActivity) == (int)VocabActivity::Flashcards) {
                if (!w->revealed) {
                    LearningAction(w, lcReveal, BN_CLICKED);
                }
            } else {
                CheckAnswer(w);
            }
        } else {
            RefreshLibrary(w);
        }
        return;
    }
    if (id == lcPack && notification == CBN_SELCHANGE) {
        int i = Selected(w, lcPack);
        if (i >= 0 && i < len(w->packs)) {
            const auto& p = w->packs[i];
            Text(w, lcDetails,
                 fmt("%s\r\nLanguage: %s\r\nLicense: %s\r\nSource: %s\r\n\r\n%s", p.title, p.language, p.license,
                     p.sourceUrl,
                     p.installed ? Tr("Available offline. Lookup searches all installed dictionaries and shows the "
                                      "source for each result. Download refreshes the original pack.")
                                 : Tr("Download this pack, or import a dictionary file. Lookup remains offline.")));
            EnableWindow(Control(w, lcSave), false);
            EnableWindow(Control(w, lcLearned), false);
        }
        return;
    }
    if (id == lcQuery && notification == EN_CHANGE && !w->dictionary) {
        if (!w->practice) {
            RefreshLibrary(w);
        }
        return;
    }
    if (id == lcDeck && notification == CBN_SELCHANGE && !w->dictionary) {
        w->practice = false;
        PracticeControls(w);
        RefreshLibrary(w);
        return;
    }
    if (id == lcActivity && notification == CBN_SELCHANGE && w->practice) {
        StartPractice(w);
        return;
    }
    if (id == lcLibrary && notification == LBN_SELCHANGE) {
        WordDetails(w);
        return;
    }
    if ((id == lcChoices || id == lcPairs) && notification == LBN_SELCHANGE && w->practice &&
        Selected(w, lcActivity) == (int)VocabActivity::MatchPairs) {
        CheckPair(w);
        return;
    }
    if (notification != BN_CLICKED) {
        return;
    }
    if (id != lcLookup && id != lcBack && id != lcReveal && id != lcOpenVocabulary &&
        !HasPermission(Perm::SavePreferences)) {
        Status(w, Tr("Saving vocabulary and progress is unavailable in restricted mode."));
        return;
    }
    switch (id) {
        case lcLookup:
            if (w->dictionary) {
                LookupWord(w);
            } else {
                RefreshLibrary(w);
            }
            break;
        case lcSave:
            SaveMeaning(w, false);
            break;
        case lcLearned: {
            if (w->dictionary) {
                SaveMeaning(w, true);
                break;
            }
            VocabularyWord* word = SelectedWord(w);
            if (word) {
                VocabularySetLearned(word->id, !word->learned);
                RefreshLibrary(w);
            }
            break;
        }
        case lcDeleteWord: {
            VocabularyWord* word = SelectedWord(w);
            if (word) {
                VocabularyRemove(word->id);
                RefreshLibrary(w);
            }
            break;
        }
        case lcOpenVocabulary:
            ShowVocabularyDialog(w->owner);
            break;
        case lcPractice:
            StartPractice(w);
            break;
        case lcBack:
            w->practice = false;
            PracticeControls(w);
            RefreshLibrary(w);
            break;
        case lcReveal:
            if (w->question) {
                w->revealed = true;
                Text(w, lcDetails, fmt("%s\r\n\r\n%s", w->question->prompt, w->question->answer));
                PracticeControls(w);
            }
            break;
        case lcAgain:
        case lcHard:
        case lcGood:
        case lcEasy:
            if (w->question) {
                VocabularyReview(w->question->wordId, (VocabGrade)(id - lcAgain), Scheduler(w));
                w->correct += id == lcGood || id == lcEasy ? 1 : 0;
                w->position++;
                NextQuestion(w);
            }
            break;
        case lcCheck:
            CheckAnswer(w);
            break;
        case lcCreateDeck: {
            VocabularyDeck* deck = VocabularyCreateDeck(Read(w, lcNewDeck));
            if (deck) {
                RefreshDecks(w);
                SendMessageW(Control(w, lcDeck), CB_SETCURSEL, w->deckIds.Find(deck->id), 0);
                Text(w, lcNewDeck, {});
                RefreshLibrary(w);
            } else {
                Status(w, VocabularyLastError());
            }
            break;
        }
        case lcDeleteDeck:
            if (len(CurrentDeck(w)) && VocabularyRemoveDeck(CurrentDeck(w))) {
                RefreshDecks(w);
                RefreshLibrary(w);
            } else {
                Status(w, VocabularyLastError());
            }
            break;
        case lcInstallDeck:
            if (!len(CurrentDeck(w))) {
                Status(w, Tr("Select a built-in deck first."));
            } else {
                StartDictionaryJob(w, DictionaryJobKind::WarmDeck, CurrentDeck(w));
            }
            break;
        case lcImportPack: {
            if (!CanAccessDisk()) {
                Status(w, Tr("Import is unavailable in restricted mode."));
                break;
            }
            TempStr path = ChooseLearningFile(w->hwnd, false, true);
            if (len(path)) {
                StartDictionaryJob(w, DictionaryJobKind::Import, path);
            }
            break;
        }
        case lcDownload:
        case lcRemovePack: {
            int i = Selected(w, lcPack);
            if (i >= 0 && i < len(w->packs)) {
                StartDictionaryJob(w, id == lcDownload ? DictionaryJobKind::Download : DictionaryJobKind::Remove,
                                   w->packs[i].id);
            }
            break;
        }
        case lcExport:
        case lcImport: {
            if (!CanAccessDisk()) {
                Status(w, Tr("Import and export are unavailable in restricted mode."));
                break;
            }
            TempStr path = ChooseLearningFile(w->hwnd, id == lcExport, false);
            if (!len(path)) {
                break;
            }
            bool ok = id == lcExport ? VocabularyExport(path) : VocabularyImport(path);
            w->practice = false;
            RefreshDecks(w);
            PracticeControls(w);
            RefreshLibrary(w);
            Status(w, ok ? Tr("Vocabulary backup completed.") : VocabularyLastError());
            break;
        }
    }
    InvalidateRect(w->owner->hwndCanvas, nullptr, false);
}
static void LibraryContextMenu(LearningWindow* w, LPARAM point) {
    if (w->practice) {
        return;
    }
    POINT screen{GET_X_LPARAM(point), GET_Y_LPARAM(point)};
    if (screen.x == -1 && screen.y == -1) {
        RECT rc;
        GetWindowRect(Control(w, lcLibrary), &rc);
        screen = {rc.left + DpiScale(20), rc.top + DpiScale(20)};
    } else {
        POINT local = screen;
        ScreenToClient(Control(w, lcLibrary), &local);
        DWORD hit = (DWORD)SendMessageW(Control(w, lcLibrary), LB_ITEMFROMPOINT, 0, MAKELPARAM(local.x, local.y));
        if (!HIWORD(hit)) {
            SendMessageW(Control(w, lcLibrary), LB_SETCURSEL, LOWORD(hit), 0);
            WordDetails(w);
        }
    }
    VocabularyWord* word = SelectedWord(w);
    if (!word) {
        return;
    }
    constexpr int lookup = 10001;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, lookup, CWStrTemp(Tr("Look up offline")));
    AppendMenuW(menu, MF_STRING, lcLearned, CWStrTemp(word->learned ? Tr("Mark unlearned") : Tr("Mark learned")));
    AppendMenuW(menu, MF_STRING, lcDeleteWord, CWStrTemp(Tr("Remove word")));
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, w->hwnd, nullptr);
    DestroyMenu(menu);
    if (command == lookup) {
        ShowDictionaryDialog(w->owner, word->word, word->context, word->sourcePath, word->page);
    } else if (command) {
        LearningAction(w, command, BN_CLICKED);
    }
}

static void DrawLearningButton(DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool primary = item->CtlID == lcLookup || item->CtlID == lcSave || item->CtlID == lcPractice;
    Color bg = primary                            ? ThemeBrandColor()
               : (item->itemState & ODS_SELECTED) ? ThemeHotBackgroundColor()
                                                  : ThemeControlBackgroundColor();
    HBRUSH brush = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, DpiScale(1), primary ? bg : ThemeEdgeColor());
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rc.left, rc.top, rc.right, rc.bottom, DpiScale(12), DpiScale(12));
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, disabled  ? ThemeWindowTextDisabledColor()
                            : primary ? ThemeBrandTextColor()
                                      : ThemeWindowTextColor());
    HGDIOBJ oldFont = SelectObject(item->hDC, GetAppFontForDpi(DpiGet())->GetHFont());
    TempStr text = HwndGetTextTemp(item->hwndItem);
    InflateRect(&rc, -DpiScale(6), 0);
    DrawTextW(item->hDC, CWStrTemp(text), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (item->itemState & ODS_FOCUS) {
        InflateRect(&rc, -DpiScale(2), -DpiScale(4));
        DrawFocusRect(item->hDC, &rc);
    }
    SelectObject(item->hDC, oldFont);
}
static void RefreshLearningStyle(LearningWindow* w) {
    DeleteObject(w->background);
    DeleteObject(w->fieldBackground);
    w->background = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    w->fieldBackground = CreateSolidBrush(ThemeControlBackgroundColor());
    for (HWND child : w->controls) {
        if (child) {
            SendMessageW(child, WM_SETFONT, (WPARAM)GetAppFontForDpi(DpiGet())->GetHFont(), true);
            InvalidateRect(child, nullptr, true);
        }
    }
    LayoutLearning(w);
    InvalidateRect(w->hwnd, nullptr, true);
}
static LRESULT CALLBACK LearningWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DpiScope dpi(hwnd);
    auto* w = (LearningWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        w = (LearningWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        w->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
    }
    if (!w) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    switch (msg) {
        case WM_SIZE:
            LayoutLearning(w);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof(si), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &si);
            int offset = w->scrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP:
                    offset -= DpiScale(32);
                    break;
                case SB_LINEDOWN:
                    offset += DpiScale(32);
                    break;
                case SB_PAGEUP:
                    offset -= (int)si.nPage;
                    break;
                case SB_PAGEDOWN:
                    offset += (int)si.nPage;
                    break;
                case SB_THUMBTRACK:
                    offset = si.nTrackPos;
                    break;
                case SB_TOP:
                    offset = 0;
                    break;
                case SB_BOTTOM:
                    offset = si.nMax;
                    break;
            }
            w->scrollY = offset;
            LayoutLearning(w);
            return 0;
        }
        case WM_MOUSEWHEEL:
            w->scrollY -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * DpiScale(96);
            LayoutLearning(w);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = (MINMAXINFO*)lp;
            info->ptMinTrackSize = {DpiScale(660), DpiScale(540)};
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) {
                SetCurrentModelessDialog(hwnd);
                if (w->ready) {
                    RefreshDecks(w);
                    if (!w->dictionary && !w->practice) {
                        RefreshLibrary(w);
                    }
                }
            } else if (GetCurrentModelessDialog() == hwnd) {
                SetCurrentModelessDialog(nullptr);
            }
            break;
        case WM_CONTEXTMENU:
            if ((HWND)wp == Control(w, lcLibrary)) {
                LibraryContextMenu(w, lp);
                return 0;
            }
            break;
        case WM_COMMAND: {
            WCHAR klass[32]{};
            if (lp) {
                GetClassNameW((HWND)lp, klass, dimof(klass));
            }
            int notice = HIWORD(wp);
            if ((wcscmp(klass, L"EDIT") == 0 && notice == EN_SETFOCUS) ||
                (wcscmp(klass, L"BUTTON") == 0 && notice == BN_SETFOCUS) ||
                (wcscmp(klass, L"COMBOBOX") == 0 && notice == CBN_SETFOCUS) ||
                (wcscmp(klass, L"LISTBOX") == 0 && notice == LBN_SETFOCUS)) {
                RevealFocusedControl(w, (HWND)lp);
                return 0;
            }
            LearningAction(w, LOWORD(wp), HIWORD(wp));
            return 0;
        }
        case WM_DRAWITEM:
            if (((DRAWITEMSTRUCT*)lp)->CtlType == ODT_BUTTON) {
                DrawLearningButton((DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            bool field = (HWND)lp == Control(w, lcDetails) || msg != WM_CTLCOLORSTATIC;
            SetTextColor((HDC)wp, ThemeWindowTextColor());
            SetBkColor((HDC)wp, field ? ThemeControlBackgroundColor() : ThemeMainWindowBackgroundColor());
            return (LRESULT)(field ? w->fieldBackground : w->background);
        }
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
            RefreshLearningStyle(w);
            return 0;
        case WM_DPICHANGED: {
            RECT* rc = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshLearningStyle(w);
            return 0;
        }
        case WM_NCDESTROY:
            if (GetCurrentModelessDialog() == hwnd) {
                SetCurrentModelessDialog(nullptr);
            }
            VecRemove(gLearningWindows, w);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete w;
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static HWND MakeControl(LearningWindow* w, int id, const WCHAR* klass, Str text, DWORD style = 0) {
    HWND child = CreateWindowExW(0, klass, CWStrTemp(text),
                                 WS_CHILD | WS_VISIBLE | (wcscmp(klass, L"STATIC") ? WS_TABSTOP : 0) | style, 0, 0, 1,
                                 1, w->hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    w->controls[id] = child;
    SendMessageW(child, WM_SETFONT, (WPARAM)GetAppFontForDpi(DpiGet())->GetHFont(), false);
    return child;
}

static void MakeButton(LearningWindow* w, int id, Str text) {
    MakeControl(w, id, L"BUTTON", text, BS_OWNERDRAW | BS_NOTIFY);
}
static LearningWindow* OpenLearningWindow(MainWindow* owner, bool dictionary) {
    for (LearningWindow* w : gLearningWindows) {
        if (w->owner == owner && w->dictionary == dictionary) {
            ShowWindow(w->hwnd, SW_RESTORE);
            SetForegroundWindow(w->hwnd);
            return w;
        }
    }
    VocabularyLoad();
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpfnWndProc = LearningWndProc;
    klass.lpszClassName = kLearningClass;
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&klass);
    auto* w = new LearningWindow();
    w->owner = owner;
    w->dictionary = dictionary;
    w->serial = ++gLearningSerial;
    w->background = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    w->fieldBackground = CreateSolidBrush(ThemeControlBackgroundColor());
    DpiSetFromHwnd(owner->hwndFrame);
    RECT parent;
    GetWindowRect(owner->hwndFrame, &parent);
    VecAppend(gLearningWindows, w);
    HWND hwnd = CreateWindowExW(
        WS_EX_CONTROLPARENT, kLearningClass, dictionary ? L"Offline dictionary" : L"Vocabulary learning",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL, parent.left + DpiScale(36), parent.top + DpiScale(36),
        DpiScale(860), DpiScale(720), owner->hwndFrame, nullptr, GetModuleHandleW(nullptr), w);
    if (!hwnd) {
        if (VecRemove(gLearningWindows, w) >= 0) {
            delete w;
        }
        return nullptr;
    }
    MakeControl(
        w, lcTitle, L"STATIC",
        dictionary ? Tr("Offline dictionary · meanings worth keeping") : Tr("Learning hub · make new words familiar"));
    MakeControl(w, lcQuery, L"EDIT", {}, ES_AUTOHSCROLL | WS_BORDER);
    MakeButton(w, lcLookup, dictionary ? Tr("Look up") : Tr("Search"));
    MakeControl(w, lcDeck, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
    MakeControl(w, lcDetails, L"EDIT", {}, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER);
    SendMessageW(Control(w, lcDetails), EM_SETLIMITTEXT, 1024 * 1024, 0);
    MakeControl(w, lcStatus, L"STATIC",
                dictionary ? Tr("Lookup is entirely offline. Downloads only start when requested.") : Str{},
                SS_ENDELLIPSIS);
    MakeButton(w, lcLearned, Tr("Mark learned"));
    if (dictionary) {
        MakeControl(w, lcPack, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
        MakeButton(w, lcImportPack, Tr("Import…"));
        MakeButton(w, lcDownload, Tr("Download"));
        MakeButton(w, lcRemovePack, Tr("Remove"));
        MakeButton(w, lcSave, Tr("Save word"));
        MakeButton(w, lcOpenVocabulary, Tr("Open learning hub"));
        RefreshPacks(w);
        EnableWindow(Control(w, lcSave), false);
        EnableWindow(Control(w, lcLearned), false);
    } else {
        MakeButton(w, lcInstallDeck, Tr("Install deck"));
        MakeButton(w, lcDeleteDeck, Tr("Delete deck"));
        MakeControl(w, lcNewDeck, L"EDIT", {}, ES_AUTOHSCROLL | WS_BORDER);
        SendMessageW(Control(w, lcNewDeck), EM_SETCUEBANNER, true, (LPARAM)L"New deck name");
        MakeButton(w, lcCreateDeck, Tr("Create deck"));
        MakeButton(w, lcExport, Tr("Export…"));
        MakeButton(w, lcImport, Tr("Import…"));
        MakeControl(w, lcActivity, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
        MakeControl(w, lcScheduler, L"COMBOBOX", {}, CBS_DROPDOWNLIST);
        MakeButton(w, lcPractice, Tr("Practice"));
        MakeControl(w, lcLibrary, L"LISTBOX", {}, LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_BORDER);
        MakeControl(w, lcAnswer, L"EDIT", {}, ES_AUTOHSCROLL | WS_BORDER);
        MakeControl(w, lcChoices, L"LISTBOX", {}, LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_BORDER);
        MakeControl(w, lcPairs, L"LISTBOX", {}, LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_BORDER);
        MakeButton(w, lcCheck, Tr("Check answer"));
        MakeButton(w, lcReveal, Tr("Show answer"));
        MakeButton(w, lcBack, Tr("Back to library"));
        MakeButton(w, lcAgain, Tr("Again"));
        MakeButton(w, lcHard, Tr("Hard"));
        MakeButton(w, lcGood, Tr("Good"));
        MakeButton(w, lcEasy, Tr("Easy"));
        MakeButton(w, lcDeleteWord, Tr("Remove word"));
        Str modes[] = {Tr("Flashcards"), Tr("Meaning quiz"),  Tr("Word quiz"),
                       Tr("Spelling"),   Tr("Word scramble"), Tr("Matching pairs")};
        for (Str mode : modes) {
            AddChoice(w, lcActivity, mode);
        }
        AddChoice(w, lcScheduler, StrL("SM-2"));
        AddChoice(w, lcScheduler, Tr("Leitner"));
        SendMessageW(Control(w, lcActivity), CB_SETCURSEL, 0, 0);
        SendMessageW(Control(w, lcScheduler), CB_SETCURSEL, 0, 0);
    }
    RefreshDecks(w);
    w->ready = true;
    for (int id : {lcLibrary, lcChoices, lcPairs}) {
        if (Control(w, id)) {
            SendMessageW(Control(w, id), LB_SETHORIZONTALEXTENT, DpiScale(1600), 0);
        }
    }
    if (!dictionary) {
        PracticeControls(w);
        RefreshLibrary(w);
    }
    SendMessageW(Control(w, lcQuery), EM_SETCUEBANNER, true,
                 (LPARAM)(dictionary ? L"Type a word…" : L"Search your vocabulary…"));
    LayoutLearning(w);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(Control(w, lcQuery));
    SetCurrentModelessDialog(hwnd);
    return w;
}
void ShowDictionaryDialog(MainWindow* owner, Str word, Str context, Str source, int page) {
    LearningWindow* w = OpenLearningWindow(owner, true);
    if (!w) {
        return;
    }
    if (w->busy && len(word)) {
        if (!w->lookupBusy) {
            Status(w, Tr("Please wait for the pack update before looking up another word."));
            return;
        }
        w->ticket++;
        w->busy = false;
    }
    str::ReplaceWithCopy(&w->context, context);
    str::ReplaceWithCopy(&w->source, source);
    w->page = page;
    Text(w, lcQuery, word);
    if (len(word)) {
        LookupWord(w);
    }
}
void ShowVocabularyDialog(MainWindow* owner) {
    LearningWindow* w = OpenLearningWindow(owner, false);
    if (w && !w->practice) {
        RefreshDecks(w);
        RefreshLibrary(w);
    }
}
void RefreshVocabularyDialogs() {
    for (LearningWindow* w : gLearningWindows) {
        DpiScope dpi(w->hwnd);
        RefreshLearningStyle(w);
    }
}
void CloseVocabularyDialogs(MainWindow* owner) {
    for (int i = len(gLearningWindows) - 1; i >= 0; i--) {
        if (gLearningWindows[i]->owner == owner) {
            DestroyWindow(gLearningWindows[i]->hwnd);
        }
    }
}
