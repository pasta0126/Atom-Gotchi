#include "GotchiSensors.h"
#include <math.h>

GotchiSensors::GotchiSensors(GotchiState& state)
    : _state(state),
      _stepPending(false), _lastMagnitude(1.0f), _lastIMURead(0),
      _lastMicRead(0), _lastBtnState(false)
{
    memset(_micBuf, 0, sizeof(_micBuf));
}

void GotchiSensors::begin() {
    // El IMU se inicializa con M5.begin() — nada extra necesario

    // Micrófono PDM (Atom S3R: DATA=GPIO6, CLK=GPIO5)
    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate = 8000;
    mic_cfg.dma_buf_count = 2;
    mic_cfg.dma_buf_len   = MIC_BUF_SIZE;
    M5.Mic.config(mic_cfg);
    M5.Mic.begin();
}

void GotchiSensors::update() {
    _updateIMU();
    _updateMic();
    _updateButton();
}

// ─── IMU ──────────────────────────────────────────────────────────────────────

void GotchiSensors::_updateIMU() {
    unsigned long now = millis();
    if (now - _lastIMURead < 50) return; // 20 Hz
    _lastIMURead = now;

    float ax, ay, az, gx, gy, gz;
    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro(&gx, &gy, &gz);

    // Magnitud de aceleración (en g, con gravedad ≈1)
    float mag = sqrtf(ax * ax + ay * ay + az * az);

    // Detección de pasos: peak-valley sobre magnitud
    if (!_stepPending && mag > STEP_HIGH) {
        _stepPending = true;
    } else if (_stepPending && mag < STEP_LOW) {
        _stepPending = false;
        _state.onStep();
    }
    _lastMagnitude = mag;

    // Detección de giro rápido (mareo)
    float gyroMag = sqrtf(gx * gx + gy * gy + gz * gz);
    _state.onDizzy(gyroMag);
}

// ─── Micrófono ────────────────────────────────────────────────────────────────

void GotchiSensors::_updateMic() {
    unsigned long now = millis();
    if (now - _lastMicRead < MIC_INTERVAL_MS) return;
    _lastMicRead = now;

    if (!M5.Mic.record(_micBuf, MIC_BUF_SIZE, 8000)) return;

    // RMS del buffer
    float sum = 0.0f;
    for (int i = 0; i < MIC_BUF_SIZE; i++) {
        float s = (float)_micBuf[i];
        sum += s * s;
    }
    float rms = sqrtf(sum / MIC_BUF_SIZE);

    _state.onNoise(rms);
}

// ─── Botón ────────────────────────────────────────────────────────────────────

void GotchiSensors::_updateButton() {
    bool pressed = M5.BtnA.wasPressed();
    if (pressed) {
        _state.onButtonClick();
    }
}
