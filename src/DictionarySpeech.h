/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3 */

enum class DictionarySpeechStatus {
    Idle,
    Loading,
    Speaking,
    Error
};

struct DictionarySpeechState;

struct DictionarySpeech {
    explicit DictionarySpeech(HWND owner, UINT notifyMessage);
    ~DictionarySpeech();

    DictionarySpeech(const DictionarySpeech&) = delete;
    DictionarySpeech& operator=(const DictionarySpeech&) = delete;

    void GetVoiceNames(StrVec& out);
    void Speak(Str word, int voiceIndex = 0);
    void PlayRecording(Str httpsUrl, Str fallbackWord, int voiceIndex = 0);
    void Stop();
    DictionarySpeechStatus GetStatus(Str* message = nullptr);

  private:
    DictionarySpeechState* state = nullptr;
};

void DictionarySpeech_UnitTests();
