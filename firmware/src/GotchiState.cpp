#include "GotchiState.h"
#include <Arduino.h>

GotchiState::GotchiState()
    : _mood(Mood::HAPPY), _moodChanged(true),
      _hunger(80), _thirst(80), _energy(90), _steps(0),
      _phoneBatteryWarn(false),
      _hour(12), _tempC(20),
      _isDizzy(false), _isExcited(false), _isLaughing(false),
      _isAngry(false), _isStartled(false), _isAnnoyed(false),
      _inducedSleep(false), _noiseActive(false),
      _dizzyUntil(0), _excitedUntil(0), _laughingUntil(0),
      _angryUntil(0), _startledUntil(0), _annoyedUntil(0),
      _inducedSleepUntil(0), _lastStatUpdate(0),
      _petCount(0), _petWindowStart(0), _clickHead(0)
{
    memset(_clickTimes, 0, sizeof(_clickTimes));
}

// ─── Update (llamar cada loop) ────────────────────────────────────────────────

void GotchiState::update() {
    unsigned long now = millis();
    if (now - _lastStatUpdate < 1000) return;

    float secs = (now - _lastStatUpdate) / 1000.0f;
    _lastStatUpdate = now;

    // ── Expirar estados temporales ─────────────────────────────────────────
    if (_isDizzy     && now >= _dizzyUntil)     _isDizzy     = false;
    if (_isExcited   && now >= _excitedUntil)   _isExcited   = false;
    if (_isLaughing  && now >= _laughingUntil)  _isLaughing  = false;
    if (_isAngry     && now >= _angryUntil)     _isAngry     = false;
    if (_isStartled  && now >= _startledUntil)  _isStartled  = false;
    if (_isAnnoyed   && now >= _annoyedUntil)   _isAnnoyed   = false;
    if (_inducedSleep && now >= _inducedSleepUntil) _inducedSleep = false;

    // Limpiar ventana de petting si expiró
    if (_petCount > 0 && (now - _petWindowStart) > PET_WINDOW_MS) {
        _petCount = 0;
    }

    // ── Decaimiento de stats ───────────────────────────────────────────────
    // Hambre y sed se drenan PRINCIPALMENTE por pasos (en onStep).
    // Aquí solo el decaimiento de fondo muy suave (inactividad total).
    // Si hace calor, la sed baja un poco más rápido.
    float thirstMult = (_tempC > 28) ? 1.5f : 1.0f;

    float hungerDecay = 0.0008f * secs;             // fondo: vacío en ~35h
    float thirstDecay = 0.0012f * secs * thirstMult; // fondo: vacío en ~23h (15h si calor)
    float energyDecay = 0.004f  * secs;             // vacío en ~7h sin movimiento

    // Si está durmiendo la energía SE RECUPERA en vez de bajar
    if (_mood == Mood::SLEEPING) {
        float recovery = 0.015f * secs;             // recarga completa en ~1.8h
        if (_isNight()) recovery *= 1.5f;           // más eficiente de noche
        _energy = (uint8_t)min(100, (int)_energy + (int)(recovery + 0.5f));
    } else {
        _energy = (uint8_t)max(0, (int)_energy - (int)(energyDecay + 0.5f));
    }

    _hunger = (uint8_t)max(0, (int)_hunger - (int)(hungerDecay + 0.5f));
    _thirst = (uint8_t)max(0, (int)_thirst - (int)(thirstDecay + 0.5f));

    _recalcMood();
}

// ─── Acciones del usuario (desde app) ────────────────────────────────────────

void GotchiState::feed() {
    _hunger = (uint8_t)min(100, (int)_hunger + 40);
    _isAngry = false;
    _isAnnoyed = false;
    _inducedSleep = false;
    _recalcMood();
}

void GotchiState::drink() {
    _thirst = (uint8_t)min(100, (int)_thirst + 50);
    _inducedSleep = false;
    _recalcMood();
}

void GotchiState::pet() {
    _energy = (uint8_t)min(100, (int)_energy + 5);
    if (!_isAngry && !_isDizzy) {
        _isLaughing    = true;
        _laughingUntil = millis() + 2000;
    }
    _inducedSleep = false;
    _recalcMood();
}

// ─── Eventos de sensores ──────────────────────────────────────────────────────

void GotchiState::onNoise(float rmsLevel) {
    if (rmsLevel > NOISE_THRESHOLD && !_isAngry && !_isDizzy && !_isAnnoyed) {
        _isExcited    = true;
        unsigned long dur = (unsigned long)constrain(rmsLevel / 100.0f, 1.0f, 5.0f) * 1000;
        _excitedUntil = millis() + dur;
        _recalcMood();
    }
}

void GotchiState::onSustainedNoise(bool active) {
    if (active == _noiseActive) return;
    _noiseActive = active;

    if (active && !_isAngry && !_isDizzy && !_isStartled) {
        _isAnnoyed    = true;
        _annoyedUntil = millis() + 6000;  // contrariado 6s mientras dura el ruido
        _isExcited    = false;
        _recalcMood();
    } else if (!active && _isAnnoyed) {
        // El ruido paró → se relaja gradualmente (no cortar inmediatamente)
        _annoyedUntil = millis() + 3000;  // 3s más de resaca
    }
}

void GotchiState::onDizzy(float gyroMagnitude) {
    if (gyroMagnitude > DIZZY_THRESHOLD && !_isStartled) {
        _isDizzy    = true;
        _dizzyUntil = millis() + 4000;
        _isExcited  = false;
        _isLaughing = false;
        _recalcMood();
    }
}

void GotchiState::onStep() {
    if (_steps < 65535) _steps++;

    // Hunger y thirst bajan PRINCIPALMENTE con pasos
    if (_steps % 100 == 0) _hunger = (uint8_t)max(0, (int)_hunger - 1);
    if (_steps % 80  == 0) _thirst = (uint8_t)max(0, (int)_thirst - 1);

    // Cada 500 pasos −5 energía (ejercicio)
    if (_steps % 500 == 0) _energy = (uint8_t)max(0, (int)_energy - 5);

    // Movimiento cancela sueño inducido
    _inducedSleep = false;
}

void GotchiState::onFall() {
    // Caída brusca → sobresaltado si despierto, despertar sobresaltado si dormía
    _isStartled    = true;
    _startledUntil = millis() + 4000;
    _isLaughing    = false;
    _isExcited     = false;
    _inducedSleep  = false;
    _recalcMood();
}

void GotchiState::onShake(bool strong) {
    _inducedSleep = false;

    if (_mood == Mood::SLEEPING) {
        // Despertar por sacudida
        if (strong) {
            _isStartled    = true;
            _startledUntil = millis() + 3000;
        }
        // Si es suave simplemente sale del sueño inducido → TIRED o lo que toque
        _energy = (uint8_t)max(0, (int)_energy - 5); // cuesta energía despertarse
    } else if (strong) {
        // Sacudida fuerte estando despierto → también sobresalta
        _isStartled    = true;
        _startledUntil = millis() + 2000;
        _isLaughing    = false;
    }
    _recalcMood();
}

void GotchiState::onFaceDown30s() {
    if (!_isAngry) {
        _isAngry    = true;
        _angryUntil = millis() + 8000;
        _recalcMood();
    }
}

void GotchiState::onFaceUp5min() {
    if (!_isExcited && !_isAngry && !_isStartled) {
        _inducedSleep    = true;
        _inducedSleepUntil = millis() + 600000UL; // hasta 10min o que haya actividad
        _recalcMood();
    }
}

void GotchiState::onInactivity5min() {
    if (!_isExcited && !_isAngry && !_isStartled) {
        _inducedSleep    = true;
        _inducedSleepUntil = millis() + 600000UL;
        _recalcMood();
    }
}

void GotchiState::onButtonClick() {
    unsigned long now = millis();
    _inducedSleep = false;

    // Detectar rage: muchos clicks muy rápidos
    _clickTimes[_clickHead] = now;
    _clickHead = (_clickHead + 1) % CLICK_HISTORY;

    int recent = 0;
    for (int i = 0; i < CLICK_HISTORY; i++) {
        if (_clickTimes[i] > 0 && (now - _clickTimes[i]) < RAGE_WINDOW_MS) recent++;
    }

    if (recent >= RAGE_THRESHOLD) {
        // Rage: muchos clicks muy rápidos → ANGRY
        _isAngry    = true;
        _angryUntil = now + RAGE_DURATION_MS;
        _isLaughing = false;
        _isAnnoyed  = false;
        _petCount   = 0;
        _recalcMood();
        return;
    }

    // Lógica de petting en ventana más larga
    if (_petCount == 0 || (now - _petWindowStart) > PET_WINDOW_MS) {
        _petCount      = 1;
        _petWindowStart = now;
    } else {
        _petCount++;
    }

    if (_petCount > PET_MAX) {
        // Demasiados mimos → se agobia (ANNOYED moderado)
        _isAnnoyed    = true;
        _annoyedUntil = now + 5000;
        _isLaughing   = false;
        _petCount     = 0;  // reset para no seguir acumulando
    } else if (!_isAngry && !_isDizzy && !_isAnnoyed) {
        // Caricia normal → reír un momento
        _isLaughing    = true;
        _laughingUntil = now + 2500;
        _energy = (uint8_t)min(100, (int)_energy + 1);
    }

    _recalcMood();
}

void GotchiState::onPhoneBattery(uint8_t level, bool /*charging*/) {
    bool warn = (level < 20);
    if (warn != _phoneBatteryWarn) {
        _phoneBatteryWarn = warn;
        _recalcMood();
    }
}

void GotchiState::onContext(uint8_t hour, int8_t tempC) {
    _hour  = hour;
    _tempC = tempC;
    // No recalcMood inmediato; se aplicará en el siguiente update()
}

// ─── Lógica interna ───────────────────────────────────────────────────────────

void GotchiState::_recalcMood() {
    Mood newMood;

    // Prioridad: SCARED → STARTLED → ANGRY → ANNOYED → DIZZY
    //          → LAUGHING → EXCITED → SLEEPING → TIRED → HUNGRY → THIRSTY → SAD → HAPPY

    if (_isStartled) {
        newMood = Mood::STARTLED;
    } else if (_isAngry) {
        newMood = Mood::ANGRY;
    } else if (_isAnnoyed) {
        newMood = Mood::ANNOYED;
    } else if (_isDizzy) {
        newMood = Mood::DIZZY;
    } else if (_isLaughing) {
        newMood = Mood::LAUGHING;
    } else if (_isExcited) {
        newMood = Mood::EXCITED;
    } else if (_energy < 15 || _inducedSleep ||
               (_isNight() && _energy < 40 && !_isExcited)) {
        // Duerme si: sin energía, inducido por posición/inactividad,
        // o es de noche y está bastante cansado
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
