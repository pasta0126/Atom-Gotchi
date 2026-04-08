#pragma once
#include <M5Unified.h>
#include "GotchiState.h"

class GotchiSensors {
public:
    explicit GotchiSensors(GotchiState& state);
    void begin();
    void update(); // llamar cada loop

    float lastMicRms() const { return _lastRms; }

private:
    GotchiState& _state;

    // ── IMU ────────────────────────────────────────────────────────────────
    void _updateIMU();

    // Paso (peak-valley sobre magnitud de aceleración)
    bool  _stepPending;
    float _lastMagnitude;

    // Orientación (boca arriba/abajo con timers)
    unsigned long _faceDownSince;   // 0 = no está boca abajo
    unsigned long _faceUpSince;     // 0 = no está boca arriba
    bool          _faceDownFired;   // ya disparó onFaceDown30s
    bool          _faceUpFired;     // ya disparó onFaceUp5min

    // Inactividad (sin movimiento significativo)
    unsigned long _lastMovementMs;
    bool          _inactivityFired;
    float         _accelVariance[10]; // ventana para detectar variación
    int           _varHead;

    // Caída brusca
    bool  _freefallPending;         // magnitud cayó a ~0 (caída libre)
    unsigned long _freefallStart;

    // Sacudida (gyro sostenido)
    unsigned long _shakeStart;      // cuándo empezó la sacudida actual
    bool          _shakePending;    // hay sacudida en curso
    bool          _shakeStrong;     // fue sacudida fuerte

    // Giro sostenido → mareo
    unsigned long _dizzyStart;
    bool          _dizzyPending;

    unsigned long _lastIMURead;

    static constexpr float STEP_HIGH           = 1.25f;  // g
    static constexpr float STEP_LOW            = 0.85f;  // g
    static constexpr float FACE_THRESHOLD      = 0.82f;  // g en eje Z
    static constexpr float FALL_FREEFALL       = 0.35f;  // g total < esto = caída libre
    static constexpr float FALL_IMPACT         = 2.5f;   // g total > esto = impacto
    static constexpr float MOVEMENT_THRESHOLD  = 0.13f;  // g variación = hay movimiento
    static constexpr float SHAKE_LIGHT_GYRO    = 60.0f;  // °/s sacudida suave
    static constexpr float SHAKE_STRONG_GYRO   = 260.0f; // °/s sacudida fuerte
    static constexpr float DIZZY_GYRO          = 150.0f; // °/s giro → mareo
    static constexpr unsigned long IMU_INTERVAL_MS       = 50;   // 20 Hz
    static constexpr unsigned long FACE_DOWN_MS          = 30000;  // 30s boca abajo → angry
    static constexpr unsigned long FACE_UP_MS            = 300000; // 5min boca arriba → sleep
    static constexpr unsigned long INACTIVITY_MS         = 300000; // 5min sin movimiento
    static constexpr unsigned long SHAKE_MIN_MS          = 500;   // sacudida mínima 500ms
    static constexpr unsigned long DIZZY_MIN_MS          = 1200;  // giro mínimo 1.2s → mareo
    static constexpr unsigned long FREEFALL_MAX_MS       = 500;   // ventana de caída libre

    // ── Micrófono ──────────────────────────────────────────────────────────
    void _updateMic();
    unsigned long _lastMicRead;
    static constexpr int MIC_BUF_SIZE = 256;
    int16_t _micBuf[MIC_BUF_SIZE];
    static constexpr unsigned long MIC_INTERVAL_MS = 300;

    float         _lastRms;             // último RMS calculado (para display)

    // Ruido sostenido
    unsigned long _noiseSustainedStart;   // cuándo empezó el ruido fuerte
    bool          _noiseSustainedActive;
    static constexpr float         NOISE_SUSTAINED_THRESHOLD = 550.0f;
    static constexpr unsigned long NOISE_SUSTAINED_MS        = 3000; // 3s

    // ── Botón ──────────────────────────────────────────────────────────────
    void _updateButton();
};
