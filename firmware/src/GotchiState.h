#pragma once
#include <Arduino.h>

enum class Mood : uint8_t {
    HAPPY     = 0,
    TIRED     = 1,
    HUNGRY    = 2,
    THIRSTY   = 3,
    DIZZY     = 4,
    EXCITED   = 5,
    SCARED    = 6,
    LAUGHING  = 7,
    ANGRY     = 8,
    SAD       = 9,
    SLEEPING  = 10,
};

struct GotchiStats {
    Mood     mood;
    uint8_t  hunger;   // 0-100, lower = más hambre
    uint8_t  thirst;   // 0-100, lower = más sed
    uint8_t  energy;   // 0-100, lower = más cansado
    uint16_t steps;
    bool     phoneBatteryWarn;
};

class GotchiState {
public:
    GotchiState();

    // Llamar cada ciclo del loop principal
    void update();

    // Eventos externos
    void feed();
    void drink();
    void pet();
    void onNoise(float rmsLevel);
    void onDizzy(float gyroMagnitude);
    void onStep();
    void onButtonClick();
    void onBTConnect();
    void onBTDisconnect();
    void onPhoneBattery(uint8_t level, bool charging);

    GotchiStats getStats() const;
    bool        isMoodChanged() const { return _moodChanged; }
    void        clearMoodChanged()    { _moodChanged = false; }

private:
    void _recalcMood();
    bool _isRaging() const;

    Mood    _mood;
    bool    _moodChanged;

    uint8_t  _hunger;
    uint8_t  _thirst;
    uint8_t  _energy;
    uint16_t _steps;

    bool _btConnected;
    bool _phoneBatteryWarn;

    // Estados temporales
    bool _isDizzy;
    bool _isExcited;
    bool _isLaughing;
    bool _isAngry;

    unsigned long _dizzyUntil;
    unsigned long _excitedUntil;
    unsigned long _laughingUntil;
    unsigned long _angryUntil;
    unsigned long _lastStatUpdate;

    // Detección de rabia por clicks rápidos
    static constexpr int           CLICK_HISTORY    = 6;
    static constexpr unsigned long RAGE_WINDOW_MS   = 3000;
    static constexpr unsigned long RAGE_DURATION_MS = 10000;
    static constexpr int           RAGE_THRESHOLD   = 5;

    unsigned long _clickTimes[CLICK_HISTORY];
    int           _clickHead;

    // Noise gate para no excitarse con cada ruido
    static constexpr float NOISE_THRESHOLD  = 400.0f;
    static constexpr float DIZZY_THRESHOLD  = 150.0f; // °/s
};
