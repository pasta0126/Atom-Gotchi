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
    STARTLED  = 11,  // sobresaltado: caída, sacudida fuerte, despertar brusco
    ANNOYED   = 12,  // contrariado: ruido sostenido, demasiados mimos
};

struct GotchiStats {
    Mood     mood;
    uint8_t  hunger;          // 0-100, lower = más hambre
    uint8_t  thirst;          // 0-100, lower = más sed
    uint8_t  energy;          // 0-100, lower = más cansado
    uint16_t steps;
    bool     phoneBatteryWarn;
};

class GotchiState {
public:
    GotchiState();

    // Llamar cada ciclo del loop principal
    void update();

    // ── Acciones desde app ─────────────────────────────────────────────────
    void feed();
    void drink();
    void pet();

    // ── Eventos de sensores ────────────────────────────────────────────────
    void onNoise(float rmsLevel);
    void onSustainedNoise(bool active);    // ruido sostenido (>3s) activo/parado
    void onDizzy(float gyroMagnitude);     // giro sostenido (ya filtrado en Sensors)
    void onStep();
    void onFall();                         // pico brusco de aceleración
    void onShake(bool strong);             // sacudida: suave=despertar calmado, fuerte=sobresaltado
    void onFaceDown30s();                  // boca abajo 30s → ANGRY
    void onFaceUp5min();                   // boca arriba 5min → tender a dormir
    void onInactivity5min();               // sin movimiento 5min → tender a dormir
    void onButtonClick();                  // botón: petting 1-5 ok, >5 agobio
    void onPhoneBattery(uint8_t level, bool charging);
    void onContext(uint8_t hour, int8_t tempC);  // hora + temperatura desde teléfono

    GotchiStats getStats() const;
    bool        isMoodChanged() const { return _moodChanged; }
    void        clearMoodChanged()    { _moodChanged = false; }

private:
    void _recalcMood();

    Mood    _mood;
    bool    _moodChanged;

    uint8_t  _hunger;
    uint8_t  _thirst;
    uint8_t  _energy;
    uint16_t _steps;

    bool _phoneBatteryWarn;
    uint8_t  _hour;       // 0-23, enviado desde teléfono (default 12)
    int8_t   _tempC;      // temperatura ambiente en °C (default 20)

    // ── Estados temporales ─────────────────────────────────────────────────
    bool          _isDizzy;
    bool          _isExcited;
    bool          _isLaughing;
    bool          _isAngry;
    bool          _isStartled;
    bool          _isAnnoyed;
    bool          _inducedSleep;   // dormido por inactividad o posición
    bool          _noiseActive;    // ruido sostenido activo

    unsigned long _dizzyUntil;
    unsigned long _excitedUntil;
    unsigned long _laughingUntil;
    unsigned long _angryUntil;
    unsigned long _startledUntil;
    unsigned long _annoyedUntil;
    unsigned long _inducedSleepUntil;
    unsigned long _lastStatUpdate;

    // ── Petting (botón) ────────────────────────────────────────────────────
    int           _petCount;
    unsigned long _petWindowStart;
    static constexpr unsigned long PET_WINDOW_MS = 8000;  // ventana de 8s
    static constexpr int           PET_MAX       = 5;     // >5 → agobio

    // ── Rage por clicks rápidos (ya existente) ─────────────────────────────
    static constexpr int           CLICK_HISTORY    = 6;
    static constexpr unsigned long RAGE_WINDOW_MS   = 2000;
    static constexpr unsigned long RAGE_DURATION_MS = 8000;
    static constexpr int           RAGE_THRESHOLD   = 5;
    unsigned long _clickTimes[CLICK_HISTORY];
    int           _clickHead;

    // ── Noise gate para excitación sonora ─────────────────────────────────
    static constexpr float NOISE_THRESHOLD  = 400.0f;
    static constexpr float DIZZY_THRESHOLD  = 150.0f;  // °/s

    bool _isNight() const { return _hour >= 22 || _hour <= 6; }
};
