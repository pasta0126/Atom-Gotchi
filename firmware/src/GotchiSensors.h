#pragma once
#include <M5Unified.h>
#include "GotchiState.h"

class GotchiSensors {
public:
    explicit GotchiSensors(GotchiState& state);
    void begin();
    void update(); // llamar cada loop

private:
    GotchiState& _state;

    // IMU
    void _updateIMU();
    bool  _stepPending;
    float _lastMagnitude;
    unsigned long _lastIMURead;

    static constexpr float STEP_HIGH  = 1.25f; // g
    static constexpr float STEP_LOW   = 0.85f; // g
    static constexpr float DIZZY_GYRO = 150.0f; // °/s (giro rápido)

    // Mic
    void _updateMic();
    unsigned long _lastMicRead;
    static constexpr int MIC_BUF_SIZE = 256;
    int16_t _micBuf[MIC_BUF_SIZE];
    static constexpr unsigned long MIC_INTERVAL_MS = 300;

    // Botón
    void _updateButton();
    bool _lastBtnState;
};
