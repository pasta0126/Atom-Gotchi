#include "GotchiState.h"
#include <Arduino.h>

GotchiState::GotchiState()
    : _mood(Mood::HAPPY), _moodChanged(true),
      _hunger(80), _thirst(80), _energy(90), _steps(0),
      _btConnected(false), _phoneBatteryWarn(false),
      _isDizzy(false), _isExcited(false), _isLaughing(false), _isAngry(false),
      _dizzyUntil(0), _excitedUntil(0), _laughingUntil(0), _angryUntil(0),
      _lastStatUpdate(0), _clickHead(0)
{
    memset(_clickTimes, 0, sizeof(_clickTimes));
}

// ─── Update (llamar cada loop) ────────────────────────────────────────────────

void GotchiState::update() {
    unsigned long now = millis();

    // Actualizar stats cada segundo
    if (now - _lastStatUpdate >= 1000) {
        unsigned long elapsed = now - _lastStatUpdate;
        _lastStatUpdate = now;

        // Decaimiento por tiempo (unidades por minuto → por segundo)
        float secs = elapsed / 1000.0f;
        float hungerDecay = 0.8f * secs;   // vacío en ~2h
        float thirstDecay = 1.5f * secs;   // vacío en ~1h
        float energyDecay = 0.2f * secs;   // decae lento sin movimiento

        _hunger = (uint8_t)max(0, (int)_hunger - (int)(hungerDecay + 0.5f));
        _thirst = (uint8_t)max(0, (int)_thirst - (int)(thirstDecay + 0.5f));
        _energy = (uint8_t)max(0, (int)_energy - (int)(energyDecay + 0.5f));

        // Expirar estados temporales
        if (_isDizzy    && now >= _dizzyUntil)    _isDizzy    = false;
        if (_isExcited  && now >= _excitedUntil)  _isExcited  = false;
        if (_isLaughing && now >= _laughingUntil) _isLaughing = false;
        if (_isAngry    && now >= _angryUntil)    _isAngry    = false;

        _recalcMood();
    }
}

// ─── Acciones del usuario (desde app) ────────────────────────────────────────

void GotchiState::feed() {
    _hunger = min(100, (int)_hunger + 40);
    _isAngry = false;
    _recalcMood();
}

void GotchiState::drink() {
    _thirst = min(100, (int)_thirst + 50);
    _recalcMood();
}

void GotchiState::pet() {
    // Darle cariño lo anima un poco
    _energy = min(100, (int)_energy + 5);
    if (!_isAngry && !_isDizzy) {
        _isLaughing = true;
        _laughingUntil = millis() + 2000;
    }
    _recalcMood();
}

// ─── Eventos de sensores ──────────────────────────────────────────────────────

void GotchiState::onNoise(float rmsLevel) {
    if (rmsLevel > NOISE_THRESHOLD && !_isAngry && !_isDizzy) {
        _isExcited = true;
        // Más ruido = más tiempo emocionado (hasta 5s)
        unsigned long duration = (unsigned long)constrain(rmsLevel / 100.0f, 1.0f, 5.0f) * 1000;
        _excitedUntil = millis() + duration;
        _recalcMood();
    }
}

void GotchiState::onDizzy(float gyroMagnitude) {
    if (gyroMagnitude > DIZZY_THRESHOLD) {
        _isDizzy = true;
        _dizzyUntil = millis() + 4000;
        _isExcited = false;
        _isLaughing = false;
        _recalcMood();
    }
}

void GotchiState::onStep() {
    if (_steps < 65535) _steps++;
    // Cada 500 pasos cuesta 5 de energía
    if (_steps % 500 == 0) {
        _energy = (uint8_t)max(0, (int)_energy - 5);
    }
}

void GotchiState::onButtonClick() {
    unsigned long now = millis();

    // Guardar timestamp del click
    _clickTimes[_clickHead] = now;
    _clickHead = (_clickHead + 1) % CLICK_HISTORY;

    // Contar clicks recientes
    int recent = 0;
    for (int i = 0; i < CLICK_HISTORY; i++) {
        if (_clickTimes[i] > 0 && (now - _clickTimes[i]) < RAGE_WINDOW_MS) {
            recent++;
        }
    }

    if (recent >= RAGE_THRESHOLD) {
        _isAngry    = true;
        _isLaughing = false;
        _angryUntil = now + RAGE_DURATION_MS;
    } else if (!_isAngry) {
        _isLaughing    = true;
        _laughingUntil = now + 2500;
    }

    _recalcMood();
}

void GotchiState::onBTConnect() {
    if (!_btConnected) {
        _btConnected = true;
        if (_mood == Mood::SCARED) {
            _recalcMood();
        }
    }
}

void GotchiState::onBTDisconnect() {
    _btConnected = false;
    _isExcited   = false;
    _isLaughing  = false;
    _recalcMood();
}

void GotchiState::onPhoneBattery(uint8_t level, bool /*charging*/) {
    bool warn = (level < 20);
    if (warn != _phoneBatteryWarn) {
        _phoneBatteryWarn = warn;
        _recalcMood();
    }
}

// ─── Lógica interna ───────────────────────────────────────────────────────────

void GotchiState::_recalcMood() {
    Mood newMood;

    if (!_btConnected) {
        newMood = Mood::SCARED;
    } else if (_isAngry) {
        newMood = Mood::ANGRY;
    } else if (_isDizzy) {
        newMood = Mood::DIZZY;
    } else if (_isLaughing) {
        newMood = Mood::LAUGHING;
    } else if (_isExcited) {
        newMood = Mood::EXCITED;
    } else if (_energy < 15) {
        newMood = Mood::SLEEPING;
    } else if (_energy < 30) {
        newMood = Mood::TIRED;
    } else if (_hunger < 20) {
        newMood = Mood::HUNGRY;
    } else if (_thirst < 20) {
        newMood = Mood::THIRSTY;
    } else if (_hunger < 35 || _thirst < 35 || _phoneBatteryWarn) {
        newMood = Mood::SAD;
    } else {
        newMood = Mood::HAPPY;
    }

    if (newMood != _mood) {
        _mood        = newMood;
        _moodChanged = true;
    }
}

GotchiStats GotchiState::getStats() const {
    return { _mood, _hunger, _thirst, _energy, _steps, _phoneBatteryWarn };
}
