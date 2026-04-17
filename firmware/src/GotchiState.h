#pragma once
#include <Arduino.h>

enum class Mood : uint8_t {
    HAPPY     = 0,
    SLEEPING  = 1,
    BORED     = 2,
    DIZZY     = 3,
    EXCITED   = 4,
    LAUGHING  = 5,
    ANGRY     = 6,
    STARTLED  = 7,
    ANNOYED   = 8,
    HUNGRY    = 9,
    SAD       = 10,
    SICK      = 11,
    DEAD      = 12,
};

struct GotchiStats {
    Mood     mood;
    uint8_t  hunger;
    uint8_t  happiness;
    uint8_t  energy;
    bool     needsAttention;
    bool     needsClean;
    bool     isSick;
};

class GotchiState {
public:
    GotchiState();

    void update();

    // ── Vitals desde el servidor ───────────────────────────────────────────
    void setVitals(int hunger, int happiness, int energy,
                   bool sick, bool dead, bool sleeping, bool needsClean);

    // ── Acciones desde app/web ─────────────────────────────────────────────
    void pet();

    // ── Eventos de sensores ────────────────────────────────────────────────
    void onNoise(float rmsLevel);
    void onSustainedNoise(bool active);
    void onDizzy(float gyroMagnitude);
    void onFall();
    void onShake(bool strong);
    void onFaceDown30s();
    void onFaceUp5min();
    void onInactivity5min();
    void onButtonClick();

    GotchiStats getStats() const;
    bool        isMoodChanged() const { return _moodChanged; }
    void        clearMoodChanged()    { _moodChanged = false; }

private:
    void _recalcMood();
    void _touchInteraction();

    Mood         _mood;
    bool         _moodChanged;

    // ── Vitals (recibidos del servidor) ────────────────────────────────────
    uint8_t      _hunger;
    uint8_t      _happiness;
    uint8_t      _energy;
    bool         _isSick;
    bool         _isDead;
    bool         _serverSleeping;
    bool         _needsAttentionFlag;
    bool         _needsClean;

    // ── Flags de sensores con timestamps ──────────────────────────────────
    bool          _isDizzy;
    bool          _isExcited;
    bool          _isLaughing;
    bool          _isAngry;
    bool          _isStartled;
    bool          _isAnnoyed;
    bool          _inducedSleep;
    bool          _noiseActive;

    unsigned long _dizzyUntil;
    unsigned long _excitedUntil;
    unsigned long _laughingUntil;
    unsigned long _angryUntil;
    unsigned long _startledUntil;
    unsigned long _annoyedUntil;
    unsigned long _inducedSleepUntil;
    unsigned long _lastInteractionTime;

    int           _petCount;
    unsigned long _petWindowStart;
    static constexpr unsigned long PET_WINDOW_MS     = 8000;
    static constexpr int           PET_MAX           = 5;

    static constexpr int           CLICK_HISTORY     = 6;
    static constexpr unsigned long RAGE_WINDOW_MS    = 2000;
    static constexpr unsigned long RAGE_DURATION_MS  = 8000;
    static constexpr int           RAGE_THRESHOLD    = 5;
    unsigned long _clickTimes[CLICK_HISTORY];
    int           _clickHead;

    static constexpr float         NOISE_THRESHOLD   = 400.0f;
    static constexpr float         DIZZY_THRESHOLD   = 150.0f;
    static constexpr unsigned long BORED_TIMEOUT_MS  = 180000UL; // 3 min sin interacción
};
