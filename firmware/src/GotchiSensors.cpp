#include "GotchiSensors.h"
#include <math.h>

GotchiSensors::GotchiSensors(GotchiState& state)
    : _state(state), _lastRms(0),
      _lastMagnitude(1.0f),
      _faceDownSince(0), _faceUpSince(0),
      _faceDownFired(false), _faceUpFired(false),
      _lastMovementMs(0), _inactivityFired(false),
      _varHead(0),
      _freefallPending(false), _freefallStart(0),
      _shakeStart(0), _shakePending(false), _shakeStrong(false),
      _dizzyStart(0), _dizzyPending(false),
      _lastIMURead(0),
      _lastMicRead(0),
      _noiseSustainedStart(0), _noiseSustainedActive(false)
{
    memset(_micBuf, 0, sizeof(_micBuf));
    memset(_accelVariance, 0, sizeof(_accelVariance));
}

void GotchiSensors::begin() {
    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate    = 8000;
    mic_cfg.dma_buf_count  = 2;
    mic_cfg.dma_buf_len    = MIC_BUF_SIZE;
    M5.Mic.config(mic_cfg);
    M5.Mic.begin();

    _lastMovementMs = millis();
}

void GotchiSensors::update() {
    _updateIMU();
    _updateMic();
    _updateButton();
}

// ─── IMU ──────────────────────────────────────────────────────────────────────

void GotchiSensors::_updateIMU() {
    unsigned long now = millis();
    if (now - _lastIMURead < IMU_INTERVAL_MS) return;
    _lastIMURead = now;

    float ax, ay, az, gx, gy, gz;
    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro(&gx, &gy, &gz);

    float mag     = sqrtf(ax*ax + ay*ay + az*az);
    float gyroMag = sqrtf(gx*gx + gy*gy + gz*gz);

    // ── 1. Detección de movimiento (para inactividad) ────────────────────
    // Guardamos variación de magnitud en ventana circular
    float variation = fabsf(mag - _lastMagnitude);
    _accelVariance[_varHead] = variation;
    _varHead = (_varHead + 1) % 10;

    float totalVar = 0;
    for (int i = 0; i < 10; i++) totalVar += _accelVariance[i];
    float avgVar = totalVar / 10.0f;

    bool hasMovement = (avgVar > MOVEMENT_THRESHOLD);

    if (hasMovement) {
        _lastMovementMs  = now;
        _inactivityFired = false;
        _faceUpFired     = false;  // resetear si hay movimiento
    } else if (!_inactivityFired && (now - _lastMovementMs) >= INACTIVITY_MS) {
        _inactivityFired = true;
        _state.onInactivity5min();
    }

    // ── 3. Caída brusca (freefall → impacto) ─────────────────────────────
    if (!_freefallPending && mag < FALL_FREEFALL) {
        _freefallPending = true;
        _freefallStart   = now;
    } else if (_freefallPending) {
        if (mag > FALL_IMPACT) {
            // Impacto tras caída libre → caída real
            _freefallPending = false;
            _state.onFall();
        } else if ((now - _freefallStart) > FREEFALL_MAX_MS) {
            // Expiró la ventana de caída libre sin impacto
            _freefallPending = false;
        }
    }

    // ── 4. Orientación (boca arriba / boca abajo) ─────────────────────────
    // En Atom S3R con display al frente:
    //   Display mirando hacia arriba → az ≈ -1g
    //   Display mirando hacia abajo  → az ≈ +1g
    bool isFaceUp   = (az < -FACE_THRESHOLD);
    bool isFaceDown = (az >  FACE_THRESHOLD);

    // Boca abajo
    if (isFaceDown) {
        if (_faceDownSince == 0) {
            _faceDownSince  = now;
            _faceDownFired  = false;
        } else if (!_faceDownFired && (now - _faceDownSince) >= FACE_DOWN_MS) {
            _faceDownFired = true;
            _state.onFaceDown30s();
        }
    } else {
        _faceDownSince = 0;
        _faceDownFired = false;
    }

    // Boca arriba (solo si no hay movimiento significativo)
    if (isFaceUp && !hasMovement) {
        if (_faceUpSince == 0) {
            _faceUpSince  = now;
            _faceUpFired  = false;
        } else if (!_faceUpFired && (now - _faceUpSince) >= FACE_UP_MS) {
            _faceUpFired = true;
            _state.onFaceUp5min();
        }
    } else {
        if (!isFaceUp) _faceUpSince = 0;
        // Si hay movimiento pero sigue boca arriba no reiniciamos el timer,
        // solo pausamos hasta que haya quietud de nuevo
    }

    // ── 5. Sacudida (gyro sostenido) ─────────────────────────────────────
    bool isShaking = (gyroMag >= SHAKE_LIGHT_GYRO);

    if (isShaking) {
        if (!_shakePending) {
            _shakePending = true;
            _shakeStart   = now;
            _shakeStrong  = (gyroMag >= SHAKE_STRONG_GYRO);
        } else {
            // Actualizar si se vuelve fuerte
            if (gyroMag >= SHAKE_STRONG_GYRO) _shakeStrong = true;

            if ((now - _shakeStart) >= SHAKE_MIN_MS) {
                // Sacudida confirmada → notificar
                _state.onShake(_shakeStrong);
                _shakePending = false;  // reset para no disparar múltiples veces
            }
        }
    } else {
        _shakePending = false;
        _shakeStrong  = false;
    }

    // ── 6. Giro sostenido → mareo (distinto de sacudida puntual) ─────────
    // Solo si gyro está en rango "mareo" (150-260 °/s) sostenido
    bool isDizzyRange = (gyroMag >= DIZZY_GYRO && gyroMag < SHAKE_STRONG_GYRO);

    if (isDizzyRange) {
        if (!_dizzyPending) {
            _dizzyPending = true;
            _dizzyStart   = now;
        } else if ((now - _dizzyStart) >= DIZZY_MIN_MS) {
            _state.onDizzy(gyroMag);
            _dizzyPending = false;
        }
    } else {
        _dizzyPending = false;
    }

    _lastMagnitude = mag;
}

// ─── Micrófono ────────────────────────────────────────────────────────────────

void GotchiSensors::_updateMic() {
    unsigned long now = millis();
    if (now - _lastMicRead < MIC_INTERVAL_MS) return;
    _lastMicRead = now;

    if (!M5.Mic.record(_micBuf, MIC_BUF_SIZE, 8000)) return;

    float sum = 0.0f;
    for (int i = 0; i < MIC_BUF_SIZE; i++) {
        float s = (float)_micBuf[i];
        sum += s * s;
    }
    float rms = sqrtf(sum / MIC_BUF_SIZE);
    _lastRms = rms;

    // Evento puntual (excitación)
    _state.onNoise(rms);

    // Ruido sostenido (contrariado)
    if (rms > NOISE_SUSTAINED_THRESHOLD) {
        if (_noiseSustainedStart == 0) {
            _noiseSustainedStart = now;
        } else if (!_noiseSustainedActive &&
                   (now - _noiseSustainedStart) >= NOISE_SUSTAINED_MS) {
            _noiseSustainedActive = true;
            _state.onSustainedNoise(true);
        }
    } else {
        if (_noiseSustainedActive) {
            _noiseSustainedActive = false;
            _state.onSustainedNoise(false);
        }
        _noiseSustainedStart = 0;
    }
}

// ─── Botón ────────────────────────────────────────────────────────────────────

void GotchiSensors::_updateButton() {
    if (M5.BtnA.wasPressed()) {
        _state.onButtonClick();
    }
}
