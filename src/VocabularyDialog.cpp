/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3. */

#include "base/Base.h"
#include "base/Win.h"
#include "base/File.h"
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
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif

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
    lcGuideStart,
    lcGuideText,
    lcGuidePrev,
    lcGuideNext,
    lcGuideSkip,
    lcGuideAction,
    lcFeedback,
    lcLast
};
struct LearningWindow {
    HWND hwnd = nullptr;
    MainWindow* owner = nullptr;
    bool dictionary = false, practice = false, revealed = false, checked = false;
    bool busy = false, updating = false, ready = false, lookupBusy = false;
    int scrollY = 0, contentHeight = 0;
    bool guideVisible = false;
    int guideStep = 0, feedbackKind = 0;
    ULONGLONG feedbackStart = 0;
    HICON smallIcon = nullptr, largeIcon = nullptr;
    HWND hoverButton = nullptr;
    HFONT titleFont = nullptr;
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
        DeleteObject(titleFont);
        DestroyIcon(smallIcon);
        DestroyIcon(largeIcon);
        DeleteObject(background);
        DeleteObject(fieldBackground);
    }
};
static Vec<LearningWindow*> gLearningWindows;
static int gLearningSerial = 0;

static HWND Control(LearningWindow* w, int id) {
    return w->controls[id];
}

static void LayoutLearning(LearningWindow* w);

constexpr int kGuideSteps = 9;
constexpr UINT_PTR kFeedbackTimer = 1;
constexpr int kFeedbackDuration = 480;
struct GuideStep {
    const char* title;
    const char* text;
    bool dictionary;
    int control;
    const char* action;
};
static const GuideStep kGuide[] = {
    {"Look up a word",
     "Select a word in your PDF and press Shift+D, or use Dictionary / meaning in the selection popup. You can also "
     "type a word here and choose Look up. Your PDF context is kept when saving a selected word.",
     true, lcQuery, "Go to lookup"},
    {"Choose a dictionary",
     "Choose a pack to inspect its language, source and license. WordNet is bundled. Download adds another pack only "
     "when you request it; Import accepts supported dictionary files. Lookups use installed packs offline.",
     true, lcPack, "Choose pack"},
    {"Read the meanings",
     "Look up your word, then read the definitions and their sources. If no meaning is found, check the spelling or "
     "install a pack for that language. Save word keeps the first displayed definition.",
     true, lcDetails, "Read results"},
    {"Save with context",
     "Choose a deck, then Save word to keep the definition, selected PDF context and source page. Mark learned saves "
     "it as already learned. Open learning hub takes you to your library.",
     true, lcSave, "Go to Save word"},
    {"Explore your library",
     "Search filters your saved words; the deck selector limits the library to one deck. Select a word to read its "
     "meaning and source. Mark learned toggles learned status; learned words are excluded from practice until marked "
     "unlearned.",
     false, lcQuery, "Open library"},
    {"Choose a study deck",
     "Select a built-in deck and choose Install deck to add its words. Create deck makes a personal deck. Import "
     "restores a vocabulary backup or imports a supported study pack. Read the deck source and license before "
     "installing.",
     false, lcDeck, "Choose deck"},
    {"Practice and grade",
     "Choose Flashcards, Meaning quiz, Word quiz, Spelling, Word scramble or Matching pairs, then Practice. Quizzes "
     "use Check answer and Next word. Flashcards use Show answer, then Again / Hard / Good / Easy. Matching removes "
     "correct pairs. Feedback explains the result without relying on color.",
     false, lcActivity, "Choose practice mode"},
    {"Review due words",
     "The library status shows how many words are due. Practice reviews due words first and also includes words due "
     "within the next day. Choose SM-2 or Leitner; your grades determine when words return. Again means the word needs "
     "another review.",
     false, lcPractice, "Go to Practice"},
    {"Keep a backup",
     "Export saves a JSON backup of words, decks, context and review progress. Import restores it or adds a supported "
     "vocabulary pack. Keep a copy somewhere safe before changing devices. Use Back to revisit a step, or Finish to "
     "close this guide; Start guide is always available.",
     false, lcExport, "Go to Export"}};
static int gGuideProgress[2]{};
static bool gGuideLoaded = false;
static TempStr GuidePath() {
    return path::JoinTemp(path::GetDirTemp(VocabularyStorePathTemp()), StrL("SumatraPDF-learning-guide.txt"));
}
static void LoadGuideProgress() {
    if (gGuideLoaded) return;
    gGuideLoaded = true;
    if (!CanAccessDisk() || gDontSaveSettings) return;
    Str bytes = file::ReadFile(GuidePath());
    defer {
        str::Free(bytes);
    };
    if (len(bytes) == 2 && bytes.s[0] >= '0' && bytes.s[0] <= '8' && bytes.s[1] >= '0' && bytes.s[1] <= '8') {
        gGuideProgress[0] = bytes.s[0] - '0';
        gGuideProgress[1] = bytes.s[1] - '0';
    }
}
static void SaveGuideProgress(LearningWindow* w) {
    gGuideProgress[w->dictionary ? 0 : 1] = w->guideStep;
    if (!HasPermission(Perm::SavePreferences) || !CanAccessDisk() || gDontSaveSettings) return;
    char bytes[2] = {(char)('0' + gGuideProgress[0]), (char)('0' + gGuideProgress[1])};
    TempStr temp = fmt("%s.tmp", GuidePath());
    if (file::WriteFile(temp, Str(bytes, 2))) {
        if (!MoveFileExW(CWStrTemp(temp), CWStrTemp(GuidePath()), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            DeleteFileW(CWStrTemp(temp));
    }
}
static void UpdateGuide(LearningWindow* w);
static void Feedback(LearningWindow* w, bool correct, Str message);
static void SetLearningIcons(LearningWindow* w);
static int LearningIconSize();
static bool HasLearningGlyph(int id);
static void FitPackDropdown(LearningWindow* w);

static void Text(LearningWindow* w, int id, Str s) {
    SetWindowTextW(Control(w, id), CWStrTemp(s));
    if (w->ready) {
        LayoutLearning(w);
    }
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

static void UpdateGuide(LearningWindow* w) {
    w->guideStep = std::clamp(w->guideStep, 0, kGuideSteps - 1);
    const GuideStep& step = kGuide[w->guideStep];
    Text(w, lcGuideText, fmt("Step %d of %d: %s\r\n%s", w->guideStep + 1, kGuideSteps, Tr(step.title), Tr(step.text)));
    Text(w, lcGuideNext, w->guideStep == kGuideSteps - 1 ? Tr("Finish") : Tr("Next"));
    Text(w, lcGuideAction, Tr(step.action));
    Text(w, lcGuideStart, w->guideVisible ? Tr("Hide guide") : Tr("Help / Start guide"));
    for (int id : {lcGuideText, lcGuidePrev, lcGuideNext, lcGuideSkip, lcGuideAction}) {
        ShowWindow(Control(w, id), w->guideVisible ? SW_SHOW : SW_HIDE);
    }
    EnableWindow(Control(w, lcGuidePrev), w->guideStep > 0);
    SaveGuideProgress(w);
    LayoutLearning(w);
}
static void Feedback(LearningWindow* w, bool correct, Str message) {
    KillTimer(w->hwnd, kFeedbackTimer);
    w->feedbackKind = correct ? 1 : -1;
    w->feedbackStart = GetTickCount64();
    Text(w, lcFeedback, fmt("%s  %s", correct ? StrL("✓") : StrL("✕"), message));
    ShowWindow(Control(w, lcFeedback), SW_SHOW);
    BOOL animate = FALSE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
    if (animate)
        SetTimer(w->hwnd, kFeedbackTimer, 40, nullptr);
    else
        w->feedbackStart = 0;
    LayoutLearning(w);
    InvalidateRect(Control(w, lcFeedback), nullptr, true);
}

static void Place(LearningWindow* w, int id, int x, int y, int dx, int dy) {
    if (Control(w, id)) {
        HWND child = Control(w, id);
        MoveWindow(child, x, y - w->scrollY, std::max(dx, 1), std::max(dy, 1), true);
        WCHAR klass[32]{};
        GetClassNameW(child, klass, dimof(klass));
        if (wcscmp(klass, L"EDIT") == 0) {
            HRGN region = CreateRoundRectRgn(0, 0, dx + 1, dy + 1, DpiScale(12), DpiScale(12));
            if (!SetWindowRgn(child, region, TRUE)) DeleteObject(region);
            if (GetWindowLongPtrW(child, GWL_STYLE) & ES_MULTILINE) {
                RECT text{DpiScale(10), DpiScale(8), dx - DpiScale(10), dy - DpiScale(8)};
                SendMessageW(child, EM_SETRECT, 0, (LPARAM)&text);
            }
        }
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
    FitPackDropdown(w);
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
static TempStr ChoiceLabel(int index) {
    char letters[16]{};
    int pos = sizeof(letters) - 1;
    do {
        letters[--pos] = (char)('A' + index % 26);
        index = index / 26 - 1;
    } while (index >= 0 && pos > 0);
    return fmt("(%s)", Str(letters + pos));
}
static int WrappedHeight(HWND control, WStr text, int width) {
    HDC dc = GetDC(control);
    HFONT font = (HFONT)SendMessageW(control, WM_GETFONT, 0, 0);
    HGDIOBJ old = SelectObject(dc, font ? font : GetAppFontForDpi(DpiGet())->GetHFont());
    RECT rc{0, 0, std::max(width, 1), 0};
    DrawTextW(dc, CWStrTemp(text), len(text), &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(dc, old);
    ReleaseDC(control, dc);
    return std::max((int)rc.bottom, GetAppFontSizeForDpi(DpiGet()));
}
static int LabelWidth(HWND control, int index) {
    HDC dc = GetDC(control);
    HGDIOBJ old = SelectObject(dc, GetAppFontForDpi(DpiGet())->GetHFont());
    WStr label = ToWStrTemp(ChoiceLabel(index));
    SIZE size{};
    GetTextExtentPoint32W(dc, CWStrTemp(label), len(label), &size);
    SelectObject(dc, old);
    ReleaseDC(control, dc);
    return size.cx + DpiScale(12);
}
static void WrapChoices(HWND control) {
    if (!control) {
        return;
    }
    int count = (int)SendMessageW(control, LB_GETCOUNT, 0, 0);
    int label = 0;
    if (GetDlgCtrlID(control) != lcLibrary) {
        for (int i = 0; i < count; i++) {
            label = std::max(label, LabelWidth(control, i));
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        RECT before, after;
        GetClientRect(control, &before);
        for (int i = 0; i < count; i++) {
            int height = WrappedHeight(control, LbGetTextTemp(control, i), before.right - label - DpiScale(24));
            SendMessageW(control, LB_SETITEMHEIGHT, i, height + DpiScale(20));
        }
        GetClientRect(control, &after);
        if (before.right == after.right) {
            break;
        }
    }
}
static void LayoutLearning(LearningWindow* w) {
    if (!w->ready) {
        return;
    }
    RECT client;
    GetClientRect(w->hwnd, &client);
    int pad = DpiScale(20), gap = DpiScale(10);
    int row = std::max(DpiScale(32), GetAppFontSizeForDpi(DpiGet()) + DpiScale(16));
    int width = std::max((int)client.right - pad * 2, row * 3), y = pad;
    // Stack overflowing groups instead of shrinking their text or hit targets.
    auto group = [&](std::initializer_list<int> ids) {
        int x = pad, height = row;
        for (int id : ids) {
            HWND child = Control(w, id);
            if (!child || !(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) {
                continue;
            }
            WCHAR klass[32]{};
            GetClassNameW(child, klass, dimof(klass));
            bool combo = wcscmp(klass, L"COMBOBOX") == 0;
            bool edit = wcscmp(klass, L"EDIT") == 0;
            int size = combo || edit ? std::max(width / 2, row * 4) : row * 3;
            if (!combo && !edit) {
                HDC dc = GetDC(child);
                HGDIOBJ old = SelectObject(dc, GetAppFontForDpi(DpiGet())->GetHFont());
                WStr text = ToWStrTemp(Read(w, id));
                SIZE extent{};
                GetTextExtentPoint32W(dc, CWStrTemp(text), len(text), &extent);
                SelectObject(dc, old);
                ReleaseDC(child, dc);
                size =
                    std::min(width, (int)extent.cx + pad * 2 + (HasLearningGlyph(id) ? LearningIconSize() + gap : 0));
            }
            size = std::min(width, size);
            if (x > pad && x + size > pad + width) {
                y += height + gap;
                x = pad;
                height = row;
            }
            int h =
                std::max(row, WrappedHeight(child, ToWStrTemp(Read(w, id)),
                                            size - gap * 2 - (HasLearningGlyph(id) ? LearningIconSize() + gap : 0)) +
                                  gap * 2);
            Place(w, id, x, y, size, combo ? row * 10 : h);
            height = std::max(height, combo ? row : h);
            x += size + gap;
        }
        y += height + gap;
    };
    int title = WrappedHeight(Control(w, lcTitle), ToWStrTemp(Read(w, lcTitle)), width - LearningIconSize() - pad);
    Place(w, lcTitle, pad, y, width, title);
    y += title + gap;
    group({lcGuideStart});
    if (w->guideVisible) {
        int guideHeight =
            WrappedHeight(Control(w, lcGuideText), ToWStrTemp(Read(w, lcGuideText)), width - pad * 2) + pad * 2;
        Place(w, lcGuideText, pad, y, width, guideHeight);
        y += guideHeight + gap;
        group({lcGuidePrev, lcGuideNext, lcGuideSkip, lcGuideAction});
    }
    group({lcQuery, lcLookup});
    if (w->dictionary) {
        group({lcPack, lcImportPack, lcDownload, lcRemovePack});
    } else {
        group({lcDeck, lcInstallDeck, lcDeleteDeck});
        group({lcNewDeck, lcCreateDeck, lcExport, lcImport});
        group({lcActivity, lcScheduler, lcPractice});
    }
    int detailHeight = std::max(row * 4, (int)client.bottom - y - row * 5);
    if (!w->dictionary && !w->practice) {
        if (width < row * 16) {
            Place(w, lcLibrary, pad, y, width, row * 4);
            y += row * 4 + gap;
            Place(w, lcDetails, pad, y, width, detailHeight);
        } else {
            int listWidth = width / 3;
            Place(w, lcLibrary, pad, y, listWidth, detailHeight);
            Place(w, lcDetails, pad + listWidth + gap, y, width - listWidth - gap, detailHeight);
        }
        WrapChoices(Control(w, lcLibrary));
        y += detailHeight + gap;
        group({lcLearned, lcDeleteWord});
    } else {
        Place(w, lcDetails, pad, y, width, detailHeight);
        y += detailHeight + gap;
        if (w->dictionary) {
            group({lcDeck, lcSave, lcLearned});
            group({lcOpenVocabulary});
        } else {
            bool matching = Selected(w, lcActivity) == (int)VocabActivity::MatchPairs;
            if ((GetWindowLongPtrW(Control(w, lcChoices), GWL_STYLE) & WS_VISIBLE)) {
                int listWidth = matching && width >= row * 16 ? (width - gap) / 2 : width;
                int listHeight = row * 5;
                Place(w, lcChoices, pad, y, listWidth, listHeight);
                WrapChoices(Control(w, lcChoices));
                if (matching) {
                    if (listWidth == width) {
                        y += listHeight + gap;
                        Place(w, lcPairs, pad, y, width, listHeight);
                    } else {
                        Place(w, lcPairs, pad + listWidth + gap, y, listWidth, listHeight);
                    }
                    WrapChoices(Control(w, lcPairs));
                }
                y += listHeight + gap;
            }
            group({lcAnswer, lcCheck});
            group({lcReveal, lcBack, lcAgain, lcHard, lcGood, lcEasy});
        }
    }
    if (w->feedbackKind) {
        int h = WrappedHeight(Control(w, lcFeedback), ToWStrTemp(Read(w, lcFeedback)), width - pad * 2) + pad;
        Place(w, lcFeedback, pad, y, width, h);
        y += h + gap;
    }
    int status = WrappedHeight(Control(w, lcStatus), ToWStrTemp(Read(w, lcStatus)), width);
    Place(w, lcStatus, pad, y, width, status);
    w->contentHeight = y + status + pad;
    int scrollY = std::clamp(w->scrollY, 0, std::max(0, w->contentHeight - (int)client.bottom));
    SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS};
    scroll.nMax = w->contentHeight - 1;
    scroll.nPage = client.bottom;
    scroll.nPos = scrollY;
    SetScrollInfo(w->hwnd, SB_VERT, &scroll, true);
    if (scrollY != w->scrollY) {
        w->scrollY = scrollY;
        LayoutLearning(w);
        return;
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
        Feedback(w, false, Tr("Not a match. Choose another definition and try again."));
        SendMessageW(Control(w, lcPairs), LB_SETCURSEL, (WPARAM)-1, 0);
        return;
    }
    Feedback(w, true, Tr("Correct match. Continue with the remaining pairs."));
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
    Feedback(w, correct,
             correct ? Tr("Correct. Review the answer, then choose Next word.")
                     : Tr("Not quite. Read the correct answer below, then choose Next word."));
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
    if (id == lcGuideStart || id == lcGuidePrev || id == lcGuideNext || id == lcGuideSkip || id == lcGuideAction) {
        if (id == lcGuideAction) {
            const GuideStep& step = kGuide[w->guideStep];
            LearningWindow* target = w;
            if (step.dictionary != w->dictionary) {
                if (step.dictionary)
                    ShowDictionaryDialog(w->owner);
                else
                    ShowVocabularyDialog(w->owner);
                for (LearningWindow* other : gLearningWindows) {
                    if (other->owner == w->owner && other->dictionary == step.dictionary) target = other;
                }
                target->guideStep = w->guideStep;
                target->guideVisible = true;
                UpdateGuide(target);
            }
            HWND child = Control(target, step.control);
            if (child && IsWindowVisible(child) && IsWindowEnabled(child)) {
                SetFocus(child);
                RevealFocusedControl(target, child);
            } else {
                Status(target, Tr("Complete the lookup or leave practice before using this step's action."));
            }
            return;
        }
        if (id == lcGuideStart) w->guideVisible = !w->guideVisible;
        if (id == lcGuidePrev) w->guideStep = std::max(0, w->guideStep - 1);
        if (id == lcGuideNext) {
            if (w->guideStep == kGuideSteps - 1) {
                w->guideVisible = false;
                w->guideStep = 0;
            } else
                w->guideStep++;
        }
        if (id == lcGuideSkip) w->guideVisible = false;
        UpdateGuide(w);
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
                Feedback(w, id == lcGood || id == lcEasy,
                         id == lcGood || id == lcEasy
                             ? Tr("Remembered. Your grade was saved; continue with the next card.")
                             : Tr("Needs more review. Your grade was saved; continue with the next card."));
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

constexpr WCHAR kChoiceListClass[] = L"SumatraEnhancedChoices";
struct ChoiceList {
    HWND hwnd = nullptr;
    StrVec strings;
    Vec<int> heights;
    int selected = -1, scroll = 0, hover = -1;
};
static int ChoiceTop(ChoiceList* list, int index) {
    int top = 0;
    for (int i = 0; i < index && i < len(list->heights); i++) {
        top += list->heights[i];
    }
    return top;
}
static void ScrollChoices(ChoiceList* list) {
    RECT rc;
    GetClientRect(list->hwnd, &rc);
    int total = ChoiceTop(list, len(list->heights));
    list->scroll = std::clamp(list->scroll, 0, std::max(0, total - (int)rc.bottom));
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMax = std::max(0, total - 1);
    si.nPage = rc.bottom;
    si.nPos = list->scroll;
    SetScrollInfo(list->hwnd, SB_VERT, &si, true);
    InvalidateRect(list->hwnd, nullptr, false);
}
static void ChooseRow(ChoiceList* list, int index, bool notify) {
    if (index < -1 || index >= len(list->strings)) {
        return;
    }
    list->selected = index;
    if (index >= 0) {
        RECT rc;
        GetClientRect(list->hwnd, &rc);
        int top = ChoiceTop(list, index), bottom = top + list->heights[index];
        if (top < list->scroll || bottom - top > rc.bottom) {
            list->scroll = top;
        } else if (bottom > list->scroll + rc.bottom) {
            list->scroll = bottom - rc.bottom;
        }
    }
    ScrollChoices(list);
    if (notify) {
        SendMessageW(GetParent(list->hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(list->hwnd), LBN_SELCHANGE),
                     (LPARAM)list->hwnd);
    }
}
static LRESULT CALLBACK ChoiceWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DpiScope dpi(hwnd);
    auto* list = (ChoiceList*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        list = new ChoiceList();
        list->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)list);
    }
    if (!list) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    switch (msg) {
        case LB_ADDSTRING:
            list->strings.Append(ToUtf8Temp(WStr((WCHAR*)lp)));
            VecAppend(list->heights, GetAppFontSizeForDpi(DpiGet()) + DpiScale(20));
            return len(list->strings) - 1;
        case LB_RESETCONTENT:
            list->strings.Reset();
            VecReset(list->heights);
            list->selected = -1;
            list->scroll = 0;
            ScrollChoices(list);
            return 0;
        case LB_DELETESTRING:
            if ((int)wp < 0 || (int)wp >= len(list->strings)) {
                return LB_ERR;
            }
            list->strings.RemoveAt((int)wp);
            VecRemoveAt(list->heights, (int)wp);
            list->selected = -1;
            ScrollChoices(list);
            return len(list->strings);
        case LB_ITEMFROMPOINT: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (x < 0 || x >= rc.right || y < 0 || y >= rc.bottom) {
                return MAKELONG(0, 1);
            }
            y += list->scroll;
            int top = 0;
            for (int i = 0; i < len(list->heights); i++) {
                top += list->heights[i];
                if (y < top) {
                    return MAKELONG(i, 0);
                }
            }
            return MAKELONG(0, 1);
        }
        case LB_GETCOUNT:
            return len(list->strings);
        case LB_GETCURSEL:
            return list->selected;
        case LB_SETCURSEL:
            ChooseRow(list, (int)wp, false);
            return list->selected;
        case LB_GETTEXTLEN:
        case LB_GETTEXT: {
            if ((int)wp < 0 || (int)wp >= len(list->strings)) {
                return LB_ERR;
            }
            WStr text = ToWStrTemp(list->strings[(int)wp]);
            if (msg == LB_GETTEXT) {
                memcpy((void*)lp, CWStrTemp(text), (len(text) + 1) * sizeof(WCHAR));
            }
            return len(text);
        }
        case LB_SETITEMHEIGHT:
            if ((int)wp < 0 || (int)wp >= len(list->heights)) {
                return LB_ERR;
            }
            list->heights[(int)wp] = std::max(1, (int)lp);
            ScrollChoices(list);
            return 0;
        case WM_SETFONT:
        case WM_SIZE:
            ScrollChoices(list);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(hwnd, nullptr, false);
            InvalidateRect(GetParent(hwnd), nullptr, false);
            if (msg == WM_SETFOCUS) {
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), LBN_SETFOCUS), (LPARAM)hwnd);
            }
            return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            int y = GET_Y_LPARAM(lp) + list->scroll, top = 0, hover = -1;
            for (int i = 0; i < len(list->heights); i++) {
                top += list->heights[i];
                if (y < top) {
                    hover = i;
                    break;
                }
            }
            if (hover != list->hover) {
                list->hover = hover;
                InvalidateRect(hwnd, nullptr, false);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            list->hover = -1;
            InvalidateRect(hwnd, nullptr, false);
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            int y = GET_Y_LPARAM(lp) + list->scroll;
            int top = 0;
            for (int i = 0; i < len(list->heights); i++) {
                top += list->heights[i];
                if (y < top) {
                    ChooseRow(list, i, true);
                    break;
                }
            }
            return 0;
        }
        case WM_KEYDOWN: {
            int index = list->selected;
            RECT rc;
            GetClientRect(hwnd, &rc);
            switch (wp) {
                case VK_UP:
                    index = std::max(0, index - 1);
                    break;
                case VK_DOWN:
                    index = std::min(len(list->strings) - 1, index + 1);
                    break;
                case VK_HOME:
                    index = 0;
                    break;
                case VK_END:
                    index = len(list->strings) - 1;
                    break;
                case VK_PRIOR:
                case VK_NEXT:
                    list->scroll += wp == VK_PRIOR ? -rc.bottom : rc.bottom;
                    ScrollChoices(list);
                    return 0;
                default:
                    return DefWindowProcW(hwnd, msg, wp, lp);
            }
            ChooseRow(list, index, true);
            return 0;
        }
        case WM_CHAR:
            if (GetDlgCtrlID(hwnd) == lcLibrary) {
                char prefix[2]{(char)wp, 0};
                for (int step = 1; step <= len(list->strings); step++) {
                    int index = (std::max(list->selected, 0) + step) % len(list->strings);
                    if (str::StartsWithI(list->strings[index], Str(prefix))) {
                        ChooseRow(list, index, true);
                        break;
                    }
                }
                return 0;
            }
            if (wp >= 'a' && wp <= 'z') {
                ChooseRow(list, (int)wp - 'a', true);
            } else if (wp >= 'A' && wp <= 'Z') {
                ChooseRow(list, (int)wp - 'A', true);
            }
            return 0;
        case WM_MOUSEWHEEL:
            list->scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * DpiScale(96);
            ScrollChoices(list);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof(si), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &si);
            switch (LOWORD(wp)) {
                case SB_LINEUP:
                    list->scroll -= DpiScale(32);
                    break;
                case SB_LINEDOWN:
                    list->scroll += DpiScale(32);
                    break;
                case SB_PAGEUP:
                    list->scroll -= si.nPage;
                    break;
                case SB_PAGEDOWN:
                    list->scroll += si.nPage;
                    break;
                case SB_THUMBTRACK:
                    list->scroll = si.nTrackPos;
                    break;
                case SB_TOP:
                    list->scroll = 0;
                    break;
                case SB_BOTTOM:
                    list->scroll = si.nMax;
                    break;
            }
            ScrollChoices(list);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(ThemeControlBackgroundColor());
            FillRect(dc, &rc, bg);
            DeleteObject(bg);
            int y = -list->scroll;
            for (int i = 0; i < len(list->strings); i++) {
                DRAWITEMSTRUCT item{};
                item.CtlType = ODT_LISTBOX;
                item.CtlID = GetDlgCtrlID(hwnd);
                item.itemID = i;
                item.hwndItem = hwnd;
                item.hDC = dc;
                item.rcItem = {0, y, rc.right, y + list->heights[i]};
                item.itemState = i == list->selected ? ODS_SELECTED : 0;
                if (i == list->hover) item.itemState |= ODS_HOTLIGHT;
                if (i == list->selected && GetFocus() == hwnd) {
                    item.itemState |= ODS_FOCUS;
                }
                if (item.rcItem.bottom > ps.rcPaint.top && item.rcItem.top < ps.rcPaint.bottom) {
                    SendMessageW(GetParent(hwnd), WM_DRAWITEM, item.CtlID, (LPARAM)&item);
                }
                y += list->heights[i];
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            KillTimer(hwnd, kFeedbackTimer);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete list;
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void DrawLearningChoice(DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    bool selected = (item->itemState & ODS_SELECTED) != 0;
    HBRUSH brush = CreateSolidBrush(ThemeControlBackgroundColor());
    FillRect(item->hDC, &rc, brush);
    DeleteObject(brush);
    if (selected || (item->itemState & ODS_HOTLIGHT)) {
        RECT panel = rc;
        InflateRect(&panel, -DpiScale(4), -DpiScale(3));
        brush = CreateSolidBrush(ThemeHotBackgroundColor());
        HGDIOBJ oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
        RoundRect(item->hDC, panel.left, panel.top, panel.right, panel.bottom, DpiScale(10), DpiScale(10));
        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);
        DeleteObject(brush);
        if (selected) {
            RECT stripe = panel;
            stripe.right = stripe.left + DpiScale(3);
            InflateRect(&stripe, 0, -DpiScale(5));
            brush = CreateSolidBrush(ThemeBrandColor());
            FillRect(item->hDC, &stripe, brush);
            DeleteObject(brush);
        }
    }
    if (item->itemID == (UINT)-1) {
        return;
    }
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, ThemeWindowTextColor());
    HGDIOBJ old = SelectObject(item->hDC, GetAppFontForDpi(DpiGet())->GetHFont());
    InflateRect(&rc, -DpiScale(10), -DpiScale(10));
    RECT label = rc;
    int count = (int)SendMessageW(item->hwndItem, LB_GETCOUNT, 0, 0);
    int labelWidth = 0;
    if (item->CtlID != lcLibrary) {
        for (int i = 0; i < count; i++) {
            labelWidth = std::max(labelWidth, LabelWidth(item->hwndItem, i));
        }
    }
    label.right = label.left + labelWidth;
    WStr letter = ToWStrTemp(ChoiceLabel(item->itemID));
    if (labelWidth) {
        DrawTextW(item->hDC, CWStrTemp(letter), len(letter), &label, DT_SINGLELINE | DT_NOPREFIX);
    }
    rc.left += labelWidth;
    WStr text = LbGetTextTemp(item->hwndItem, item->itemID);
    DrawTextW(item->hDC, CWStrTemp(text), len(text), &rc, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    if (item->itemState & ODS_FOCUS) {
        RECT focus = item->rcItem;
        InflateRect(&focus, -DpiScale(2), -DpiScale(2));
        DrawFocusRect(item->hDC, &focus);
    }
    SelectObject(item->hDC, old);
}
static void FitPackDropdown(LearningWindow* w) {
    HWND combo = Control(w, lcPack);
    if (!combo) return;
    HDC dc = GetDC(combo);
    HGDIOBJ font = SelectObject(dc, GetAppFontForDpi(DpiGet())->GetHFont());
    int width = 0;
    for (const auto& pack : w->packs) {
        WStr text = ToWStrTemp(pack.installed ? pack.title : fmt("%s · %s", pack.title, Tr("not installed")));
        SIZE size{};
        GetTextExtentPoint32W(dc, CWStrTemp(text), len(text), &size);
        width = std::max(width, (int)size.cx + LearningIconSize() + DpiScale(40));
    }
    SelectObject(dc, font);
    ReleaseDC(combo, dc);
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
        width = std::min(width, (int)(monitor.rcWork.right - monitor.rcWork.left) - DpiScale(32));
    SendMessageW(combo, CB_SETDROPPEDWIDTH, std::max(width, DpiScale(240)), 0);
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, GetAppFontSizeForDpi(DpiGet()) + DpiScale(16));
    SendMessageW(combo, CB_SETITEMHEIGHT, (WPARAM)-1, GetAppFontSizeForDpi(DpiGet()) + DpiScale(16));
}
static void DrawPackChoice(LearningWindow* w, DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    bool selected = (item->itemState & ODS_SELECTED) != 0;
    Color background = selected ? ThemeHotBackgroundColor() : ThemeControlBackgroundColor();
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(item->hDC, &rc, brush);
    DeleteObject(brush);
    if (item->itemID >= (UINT)len(w->packs)) return;
    const OfflineDictPack& pack = w->packs[item->itemID];
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, (item->itemState & ODS_DISABLED) ? ThemeWindowTextDisabledColor() : ThemeWindowTextColor());
    HGDIOBJ font = SelectObject(item->hDC, GetAppFontForDpi(DpiGet())->GetHFont());
    InflateRect(&rc, -DpiScale(8), 0);
    if (pack.installed) {
        int size = LearningIconSize();
        int top = rc.top + ((rc.bottom - rc.top) - size) / 2;
        bool dark = GetRValue(background) + GetGValue(background) + GetBValue(background) < 384;
        Color green = ThemeUsesHighContrastColors() ? ThemeWindowTextColor()
                      : dark                        ? RGB(93, 230, 145)
                                                    : RGB(17, 120, 58);
        HPEN pen = CreatePen(PS_SOLID, std::max(2, size / 10), green);
        HGDIOBJ oldPen = SelectObject(item->hDC, pen);
        MoveToEx(item->hDC, rc.left + size / 6, top + size / 2, nullptr);
        LineTo(item->hDC, rc.left + size * 5 / 12, top + size * 3 / 4);
        LineTo(item->hDC, rc.left + size * 5 / 6, top + size / 4);
        SelectObject(item->hDC, oldPen);
        DeleteObject(pen);
        rc.left += size + DpiScale(8);
    }
    WStr title = ToWStrTemp(pack.installed ? pack.title : fmt("%s · %s", pack.title, Tr("not installed")));
    DrawTextW(item->hDC, CWStrTemp(title), len(title), &rc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
    SelectObject(item->hDC, font);
}
static int LearningIconSize() {
    return std::clamp(GetAppFontSizeForDpi(DpiGet()), DpiScale(18), DpiScale(32));
}
static bool HasLearningGlyph(int id) {
    return id == lcLookup || id == lcSave || id == lcPractice || id == lcGuideStart || id == lcOpenVocabulary ||
           id == lcDownload || id == lcInstallDeck || id == lcExport || id == lcImport || id == lcImportPack;
}
static void DrawLearningGlyph(HDC dc, int id, RECT rc, Color ink) {
    int size = std::min((int)(rc.right - rc.left), (int)(rc.bottom - rc.top));
    int x = rc.left, y = rc.top;
    auto px = [&](int value) { return x + value * size / 24; };
    auto py = [&](int value) { return y + value * size / 24; };
    auto line = [&](int x1, int y1, int x2, int y2) {
        MoveToEx(dc, px(x1), py(y1), nullptr);
        LineTo(dc, px(x2), py(y2));
    };
    HPEN pen = CreatePen(PS_SOLID, std::max(1, size / 12), ink);
    HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (id == lcLookup) {
        Ellipse(dc, px(3), py(3), px(17), py(17));
        line(15, 15, 22, 22);
    } else if (id == lcPractice) {
        POINT triangle[]{{px(7), py(3)}, {px(21), py(12)}, {px(7), py(21)}};
        Polygon(dc, triangle, dimof(triangle));
    } else if (id == lcDownload || id == lcInstallDeck || id == lcImport || id == lcImportPack || id == lcExport) {
        bool up = id == lcExport;
        line(12, up ? 18 : 3, 12, up ? 3 : 18);
        line(7, up ? 8 : 13, 12, up ? 3 : 18);
        line(17, up ? 8 : 13, 12, up ? 3 : 18);
        line(3, 18, 3, 22);
        line(3, 22, 21, 22);
        line(21, 22, 21, 18);
    } else if (id == lcSave) {
        POINT bookmark[]{{px(5), py(3)}, {px(19), py(3)}, {px(19), py(22)}, {px(12), py(17)}, {px(5), py(22)}};
        Polygon(dc, bookmark, dimof(bookmark));
    } else {
        RoundRect(dc, px(2), py(4), px(22), py(21), size / 6, size / 6);
        line(12, 4, 12, 21);
        line(5, 8, 9, 8);
        line(15, 8, 19, 8);
        line(5, 12, 9, 12);
        line(15, 12, 19, 12);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}
static void DrawLearningButton(DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    auto* w = (LearningWindow*)GetWindowLongPtrW(GetParent(item->hwndItem), GWLP_USERDATA);
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool hot = w && w->hoverButton == item->hwndItem && !disabled;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool primary = item->CtlID == lcLookup || item->CtlID == lcSave || item->CtlID == lcPractice;
    Color bg = disabled         ? ThemeControlBackgroundColor()
               : primary        ? ThemeBrandColor()
               : hot || pressed ? ThemeHotBackgroundColor()
                                : ThemeControlBackgroundColor();
    Color ink = disabled ? ThemeWindowTextDisabledColor() : primary ? ThemeBrandTextColor() : ThemeWindowTextColor();
    HBRUSH brush = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    FillRect(item->hDC, &rc, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush(bg);
    Color edge = disabled                                            ? ThemeDisabledEdgeColor()
                 : (hot || pressed || (item->itemState & ODS_FOCUS)) ? ThemeHotEdgeColor()
                 : primary                                           ? bg
                                                                     : ThemeEdgeColor();
    HPEN pen = CreatePen(PS_SOLID, DpiScale(1), edge);
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rc.left, rc.top, rc.right, rc.bottom, DpiScale(14), DpiScale(14));
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, ink);
    HGDIOBJ oldFont = SelectObject(item->hDC, GetAppFontForDpi(DpiGet())->GetHFont());
    RECT textRect = rc;
    InflateRect(&textRect, -DpiScale(10), 0);
    if (HasLearningGlyph(item->CtlID)) {
        int size = LearningIconSize();
        RECT icon{textRect.left, (rc.bottom + rc.top - size) / 2, textRect.left + size,
                  (rc.bottom + rc.top + size) / 2};
        if (pressed) OffsetRect(&icon, 1, 1);
        DrawLearningGlyph(item->hDC, item->CtlID, icon, primary || disabled ? ink : ThemeBrandColor());
        textRect.left += size + DpiScale(8);
    }
    TempStr text = HwndGetTextTemp(item->hwndItem);
    RECT measured = textRect;
    DrawTextW(item->hDC, CWStrTemp(text), -1, &measured, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    textRect.top += std::max(0, (int)((textRect.bottom - textRect.top) - (measured.bottom - measured.top)) / 2);
    if (pressed) OffsetRect(&textRect, 1, 1);
    DrawTextW(item->hDC, CWStrTemp(text), -1, &textRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    if (item->itemState & ODS_FOCUS) {
        RECT focus = rc;
        InflateRect(&focus, -DpiScale(4), -DpiScale(4));
        DrawFocusRect(item->hDC, &focus);
    }
    SelectObject(item->hDC, oldFont);
}
static LRESULT CALLBACK LearningButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* w = (LearningWindow*)data;
    if (msg == WM_MOUSEMOVE && IsWindowEnabled(hwnd)) {
        if (w->hoverButton != hwnd) {
            w->hoverButton = hwnd;
            InvalidateRect(hwnd, nullptr, false);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
    } else if (msg == WM_MOUSELEAVE || msg == WM_ENABLE) {
        if (w->hoverButton == hwnd) w->hoverButton = nullptr;
        InvalidateRect(hwnd, nullptr, false);
    } else if (msg == WM_NCDESTROY) {
        if (w->hoverButton == hwnd) w->hoverButton = nullptr;
        RemoveWindowSubclass(hwnd, LearningButtonProc, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
static void DrawLearningPanel(LearningWindow* w, DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    bool title = item->CtlID == lcTitle;
    HBRUSH brush = CreateSolidBrush(title ? ThemeMainWindowBackgroundColor() : ThemeControlBackgroundColor());
    HPEN pen = CreatePen(PS_SOLID, DpiScale(1), title ? ThemeMainWindowBackgroundColor() : ThemeEdgeColor());
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rc.left, rc.top, rc.right, rc.bottom, DpiScale(16), DpiScale(16));
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    if (title) {
        int size = LearningIconSize();
        RECT icon{rc.left, rc.top, rc.left + size, rc.top + size};
        DrawLearningGlyph(item->hDC, w->dictionary ? lcGuideStart : lcPractice, icon, ThemeBrandColor());
        rc.left += size + DpiScale(20);
    } else
        InflateRect(&rc, -DpiScale(20), -DpiScale(20));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, ThemeWindowTextColor());
    HGDIOBJ font =
        SelectObject(item->hDC, title && w->titleFont ? w->titleFont : GetAppFontForDpi(DpiGet())->GetHFont());
    DrawTextW(item->hDC, CWStrTemp(Read(w, item->CtlID)), -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(item->hDC, font);
}
static void DrawLearningFrames(LearningWindow* w, HDC dc) {
    for (int id : {lcQuery, lcDetails, lcAnswer, lcNewDeck, lcLibrary, lcChoices, lcPairs}) {
        HWND child = Control(w, id);
        if (!child || !(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
        RECT rc;
        GetWindowRect(child, &rc);
        MapWindowPoints(nullptr, w->hwnd, (POINT*)&rc, 2);
        InflateRect(&rc, DpiScale(2), DpiScale(2));
        HBRUSH brush = CreateSolidBrush(ThemeControlBackgroundColor());
        HPEN pen = CreatePen(PS_SOLID, DpiScale(1), GetFocus() == child ? ThemeBrandColor() : ThemeEdgeColor());
        HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, DpiScale(14), DpiScale(14));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    }
}
static HICON MakeLearningIcon(int size, bool dictionary, Color accent, Color text) {
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
    u32* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void**)&pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        DeleteDC(dc);
        return nullptr;
    }
    memset(pixels, 0, size * size * sizeof(u32));
    HGDIOBJ old = SelectObject(dc, bitmap);
    HBRUSH green = CreateSolidBrush(accent);
    HPEN border = CreatePen(PS_SOLID, std::max(1, size / 16), text);
    HGDIOBJ oldBrush = SelectObject(dc, green), oldPen = SelectObject(dc, border);
    int pad = std::max(2, size / 8);
    RoundRect(dc, pad, pad, size - pad, size - pad, size / 5, size / 5);
    MoveToEx(dc, size / 2, pad + 2, nullptr);
    LineTo(dc, size / 2, size - pad - 2);
    if (dictionary) {
        for (int y : {size / 3, size / 2, size * 2 / 3}) {
            MoveToEx(dc, pad + size / 12, y, nullptr);
            LineTo(dc, size / 2 - size / 12, y);
            MoveToEx(dc, size / 2 + size / 12, y, nullptr);
            LineTo(dc, size - pad - size / 12, y);
        }
    } else {
        MoveToEx(dc, size / 2 + size / 10, size / 2, nullptr);
        LineTo(dc, size * 2 / 3, size * 5 / 8);
        LineTo(dc, size * 4 / 5, size / 3);
        MoveToEx(dc, pad + size / 12, size / 3, nullptr);
        LineTo(dc, size / 2 - size / 12, size / 3);
    }
    GdiFlush();
    HRGN opaque = CreateRoundRectRgn(pad, pad, size - pad, size - pad, size / 5, size / 5);
    for (int i = 0; i < size * size; i++)
        if ((pixels[i] & 0xffffff) || PtInRegion(opaque, i % size, i / size)) pixels[i] |= 0xff000000;
    DeleteObject(opaque);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    SelectObject(dc, old);
    DeleteObject(green);
    DeleteObject(border);
    DeleteDC(dc);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO iconInfo{TRUE, 0, 0, mask, bitmap};
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(mask);
    DeleteObject(bitmap);
    return icon;
}
static void SetLearningIcons(LearningWindow* w) {
    HICON smallIcon = MakeLearningIcon(DpiScale(16), w->dictionary, ThemeBrandColor(), ThemeBrandTextColor());
    HICON largeIcon = MakeLearningIcon(DpiScale(32), w->dictionary, ThemeBrandColor(), ThemeBrandTextColor());
    SendMessageW(w->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    SendMessageW(w->hwnd, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    DestroyIcon(w->smallIcon);
    DestroyIcon(w->largeIcon);
    w->smallIcon = smallIcon;
    w->largeIcon = largeIcon;
}
static void DrawFeedback(LearningWindow* w, DRAWITEMSTRUCT* item) {
    Color window = ThemeMainWindowBackgroundColor();
    bool dark = GetRValue(window) + GetGValue(window) + GetBValue(window) < 384;
    Color bg = w->feedbackKind > 0 ? (dark ? RGB(17, 62, 37) : RGB(224, 248, 232))
                                   : (dark ? RGB(77, 27, 32) : RGB(255, 231, 231));
    Color ink = w->feedbackKind > 0 ? (dark ? RGB(142, 237, 173) : RGB(20, 91, 43))
                                    : (dark ? RGB(255, 176, 181) : RGB(142, 27, 34));
    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(item->hDC, &item->rcItem, brush);
    DeleteObject(brush);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, ink);
    HGDIOBJ font = SelectObject(item->hDC, GetAppFontForDpi(DpiGet())->GetHFont());
    RECT rc = item->rcItem;
    InflateRect(&rc, -DpiScale(12), -DpiScale(6));
    DrawTextW(item->hDC, CWStrTemp(Read(w, lcFeedback)), -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(item->hDC, font);
    RECT bar = item->rcItem;
    bar.top = bar.bottom - DpiScale(3);
    ULONGLONG age = w->feedbackStart ? std::min(GetTickCount64() - w->feedbackStart, (ULONGLONG)kFeedbackDuration)
                                     : kFeedbackDuration;
    bar.right = bar.left + (int)((bar.right - bar.left) * age / kFeedbackDuration);
    brush = CreateSolidBrush(ink);
    FillRect(item->hDC, &bar, brush);
    DeleteObject(brush);
}

static void RefreshLearningStyle(LearningWindow* w) {
    SetLearningIcons(w);
    DeleteObject(w->titleFont);
    LOGFONTW title{};
    GetObjectW(GetAppFontForDpi(DpiGet())->GetHFont(), sizeof(title), &title);
    title.lfWeight = FW_BOLD;
    title.lfHeight = title.lfHeight * 6 / 5;
    w->titleFont = CreateFontIndirectW(&title);
    DeleteObject(w->background);
    DeleteObject(w->fieldBackground);
    w->background = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    w->fieldBackground = CreateSolidBrush(ThemeControlBackgroundColor());
    for (HWND child : w->controls) {
        if (child) {
            SendMessageW(child, WM_SETFONT,
                         (WPARAM)(child == Control(w, lcTitle) ? w->titleFont : GetAppFontForDpi(DpiGet())->GetHFont()),
                         true);
            InvalidateRect(child, nullptr, true);
        }
    }
    FitPackDropdown(w);
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
            info->ptMinTrackSize = {DpiScale(540), DpiScale(440)};
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
                ((wcscmp(klass, L"LISTBOX") == 0 || wcscmp(klass, kChoiceListClass) == 0) && notice == LBN_SETFOCUS)) {
                InvalidateRect(hwnd, nullptr, false);
                RevealFocusedControl(w, (HWND)lp);
                return 0;
            }
            LearningAction(w, LOWORD(wp), HIWORD(wp));
            return 0;
        }
        case WM_MEASUREITEM: {
            auto* item = (MEASUREITEMSTRUCT*)lp;
            if (item->CtlType == ODT_COMBOBOX) {
                item->itemHeight = GetAppFontSizeForDpi(DpiGet()) + DpiScale(16);
                return TRUE;
            }
            if (item->CtlType == ODT_LISTBOX) {
                item->itemHeight = GetAppFontSizeForDpi(DpiGet()) + DpiScale(20);
                return TRUE;
            }
            break;
        }
        case WM_TIMER:
            if (wp == kFeedbackTimer) {
                if (GetTickCount64() - w->feedbackStart >= kFeedbackDuration) KillTimer(hwnd, kFeedbackTimer);
                InvalidateRect(Control(w, lcFeedback), nullptr, false);
                return 0;
            }
            break;
        case WM_DRAWITEM:
            if (((DRAWITEMSTRUCT*)lp)->CtlType == ODT_COMBOBOX && ((DRAWITEMSTRUCT*)lp)->CtlID == lcPack) {
                DrawPackChoice(w, (DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            if (((DRAWITEMSTRUCT*)lp)->CtlID == lcTitle || ((DRAWITEMSTRUCT*)lp)->CtlID == lcGuideText) {
                DrawLearningPanel(w, (DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            if (((DRAWITEMSTRUCT*)lp)->CtlID == lcFeedback) {
                DrawFeedback(w, (DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            if (((DRAWITEMSTRUCT*)lp)->CtlType == ODT_LISTBOX) {
                DrawLearningChoice((DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            if (((DRAWITEMSTRUCT*)lp)->CtlType == ODT_BUTTON) {
                DrawLearningButton((DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            bool field = (HWND)lp == Control(w, lcDetails) || msg != WM_CTLCOLORSTATIC;
            SetTextColor((HDC)wp,
                         (HWND)lp == Control(w, lcStatus) ? ThemeWindowDarkerTextColor() : ThemeWindowTextColor());
            SetBkColor((HDC)wp, field ? ThemeControlBackgroundColor() : ThemeMainWindowBackgroundColor());
            return (LRESULT)(field ? w->fieldBackground : w->background);
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            FillRect(dc, &client, w->background);
            DrawLearningFrames(w, dc);
            EndPaint(hwnd, &ps);
            return 0;
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
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            if (msg == WM_SETTINGCHANGE) {
                BOOL animate = FALSE;
                SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
                if (!animate) {
                    KillTimer(hwnd, kFeedbackTimer);
                    w->feedbackStart = 0;
                }
            }
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
    if (id == lcPack) style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
    if (wcscmp(klass, L"EDIT") == 0 || wcscmp(klass, kChoiceListClass) == 0) style &= ~WS_BORDER;
    HWND child = CreateWindowExW(0, klass, CWStrTemp(text),
                                 WS_CHILD | WS_VISIBLE | (wcscmp(klass, L"STATIC") ? WS_TABSTOP : 0) | style, 0, 0, 1,
                                 1, w->hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    w->controls[id] = child;
    if (wcscmp(klass, L"EDIT") == 0)
        SendMessageW(child, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(DpiScale(10), DpiScale(10)));
    SendMessageW(child, WM_SETFONT, (WPARAM)GetAppFontForDpi(DpiGet())->GetHFont(), false);
    return child;
}

static void MakeButton(LearningWindow* w, int id, Str text) {
    HWND child = MakeControl(w, id, L"BUTTON", text, BS_OWNERDRAW | BS_NOTIFY);
    SetWindowSubclass(child, LearningButtonProc, 1, (DWORD_PTR)w);
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
    LoadGuideProgress();
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpfnWndProc = LearningWndProc;
    klass.lpszClassName = kLearningClass;
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&klass);
    klass.lpfnWndProc = ChoiceWndProc;
    klass.lpszClassName = kChoiceListClass;
    RegisterClassExW(&klass);
    auto* w = new LearningWindow();
    w->owner = owner;
    w->dictionary = dictionary;
    w->guideStep = gGuideProgress[dictionary ? 0 : 1];
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
    SetLearningIcons(w);
    MakeControl(
        w, lcTitle, L"STATIC",
        dictionary ? Tr("Offline dictionary · meanings worth keeping") : Tr("Learning hub · make new words familiar"),
        SS_OWNERDRAW | SS_NOPREFIX);
    MakeButton(w, lcGuideStart, Tr("Help / Start guide"));
    MakeControl(w, lcGuideText, L"STATIC", {}, SS_OWNERDRAW | SS_NOPREFIX);
    MakeButton(w, lcGuidePrev, Tr("Back"));
    MakeButton(w, lcGuideNext, Tr("Next"));
    MakeButton(w, lcGuideSkip, Tr("Skip"));
    MakeButton(w, lcGuideAction, Tr("Go to lookup"));
    MakeControl(w, lcFeedback, L"STATIC", {}, SS_OWNERDRAW | SS_NOPREFIX);
    ShowWindow(Control(w, lcFeedback), SW_HIDE);
    MakeControl(w, lcQuery, L"EDIT", {}, ES_AUTOHSCROLL | WS_BORDER);
    MakeButton(w, lcLookup, dictionary ? Tr("Look up") : Tr("Search"));
    MakeControl(w, lcDeck, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
    MakeControl(w, lcDetails, L"EDIT", {}, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER);
    SendMessageW(Control(w, lcDetails), EM_SETLIMITTEXT, 1024 * 1024, 0);
    MakeControl(w, lcStatus, L"STATIC",
                dictionary ? Tr("Lookup is entirely offline. Downloads only start when requested.") : Str{},
                SS_NOPREFIX);
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
        MakeControl(w, lcLibrary, kChoiceListClass, {}, WS_VSCROLL | WS_BORDER);
        MakeControl(w, lcAnswer, L"EDIT", {}, ES_AUTOHSCROLL | WS_BORDER);
        MakeControl(w, lcChoices, L"LISTBOX", {},
                    LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWVARIABLE | WS_VSCROLL | WS_BORDER);
        MakeControl(w, lcPairs, L"LISTBOX", {},
                    LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWVARIABLE | WS_VSCROLL | WS_BORDER);
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
    RefreshLearningStyle(w);
    UpdateGuide(w);
    for (int id : {lcLibrary}) {
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

#if IS_DEBUG
void VocabularyDialog_UnitTests() {
    Settings* savedSettings = gSettings;
    Settings* fixture = NewSettings({});
    utassert(fixture != nullptr);
    if (!fixture) {
        return;
    }
    gSettings = fixture;
    defer {
        gSettings = savedSettings;
        DeleteSettings(fixture);
    };
    utassert(dimof(kGuide) == kGuideSteps);
    utassert(kGuide[0].dictionary && kGuide[0].control == lcQuery);
    utassert(!kGuide[kGuideSteps - 1].dictionary && kGuide[kGuideSteps - 1].control == lcExport);
    HICON book = MakeLearningIcon(16, true, RGB(34, 197, 94), kColBlack);
    HICON vocabularyIcon = MakeLearningIcon(48, false, RGB(34, 197, 94), kColBlack);
    utassert(book != nullptr && vocabularyIcon != nullptr);
    DestroyIcon(book);
    DestroyIcon(vocabularyIcon);
    utassert(str::Eq(ChoiceLabel(0), StrL("(A)")));
    utassert(str::Eq(ChoiceLabel(4), StrL("(E)")));
    utassert(str::Eq(ChoiceLabel(25), StrL("(Z)")));
    utassert(str::Eq(ChoiceLabel(26), StrL("(AA)")));
    utassert(str::Eq(ChoiceLabel(701), StrL("(ZZ)")));
    utassert(str::Eq(ChoiceLabel(702), StrL("(AAA)")));
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpfnWndProc = ChoiceWndProc;
    klass.lpszClassName = kChoiceListClass;
    RegisterClassExW(&klass);
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 500, 500, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    utassert(parent != nullptr);
    if (!parent) {
        return;
    }
    defer {
        DestroyWindow(parent);
    };
    HWND control = CreateWindowExW(0, kChoiceListClass, L"", WS_CHILD | WS_VSCROLL, 0, 0, 150, 120, parent, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    utassert(control != nullptr);
    if (!control) {
        return;
    }
    str::Builder definition;
    for (int i = 0; i < 40; i++) {
        definition.Append(StrL("A long definition with several words that must remain readable. "));
    }
    Str raw = ToStrTemp(definition);
    SendMessageW(control, LB_ADDSTRING, 0, (LPARAM)CWStrTemp(raw));
    SendMessageW(control, LB_ADDSTRING, 0, (LPARAM)L"Second answer");
    WrapChoices(control);
    auto* list = (ChoiceList*)GetWindowLongPtrW(control, GWLP_USERDATA);
    int narrowHeight = list->heights[0];
    utassert(narrowHeight > 255);
    utassert(str::Eq(ToUtf8Temp(LbGetTextTemp(control, 0)), raw));
    MoveWindow(control, 0, 0, 450, 120, false);
    WrapChoices(control);
    utassert(list->heights[0] < narrowHeight);
    SendMessageW(control, LB_SETCURSEL, 0, 0);
    SendMessageW(control, WM_KEYDOWN, VK_DOWN, 0);
    utassert(SendMessageW(control, LB_GETCURSEL, 0, 0) == 1);
    SendMessageW(control, WM_KEYDOWN, VK_HOME, 0);
    utassert(SendMessageW(control, LB_GETCURSEL, 0, 0) == 0);
    SendMessageW(control, WM_CHAR, 'B', 0);
    utassert(SendMessageW(control, LB_GETCURSEL, 0, 0) == 1);
    VocabularyQuestion question;
    question.answer = str::Dup(StrL("Second answer"));
    utassert(VocabularyCheckAnswer(question, ToUtf8Temp(LbGetTextTemp(control, list->selected))));
    SendMessageW(control, LB_DELETESTRING, 0, 0);
    utassert(str::Eq(ToUtf8Temp(LbGetTextTemp(control, 0)), StrL("Second answer")));
    utassert(SendMessageW(control, LB_GETCURSEL, 0, 0) == -1);
    SendMessageW(control, LB_RESETCONTENT, 0, 0);
    utassert(SendMessageW(control, LB_GETCOUNT, 0, 0) == 0);
}
#endif
