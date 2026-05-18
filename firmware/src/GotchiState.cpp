#include "GotchiState.h"
#include <Arduino.h>

GotchiState::GotchiState()
    : _type(GotchiType::FELIZ),
      _mood(Mood::HAPPY), _moodChanged(true),
      _hunger(80), _happiness(80), _energy(80),
      _isSick(false), _isDead(false), _serverSleeping(false), _needsAttentionFlag(false), _needsClean(false),
      _isDizzy(false), _isExcited(false), _isLaughing(false),
      _isAngry(false), _isStartled(false), _isAnnoyed(false),
      _inducedSleep(false), _noiseActive(false),
      _dizzyUntil(0), _excitedUntil(0), _laughingUntil(0),
      _angryUntil(0), _startledUntil(0), _annoyedUntil(0),
      _inducedSleepUntil(0), _lastInteractionTime(0),
      _petCount(0), _petWindowStart(0), _clickHead(0)
{
    memset(_clickTimes, 0, sizeof(_clickTimes));
}

// ─── Update ───────────────────────────────────────────────────────────────────

void GotchiState::update() {
    unsigned long now = millis();

    if (_isDizzy     && now >= _dizzyUntil)         _isDizzy     = false;
    if (_isExcited   && now >= _excitedUntil)       _isExcited   = false;
    if (_isLaughing  && now >= _laughingUntil)      _isLaughing  = false;
    if (_isAngry     && now >= _angryUntil)         _isAngry     = false;
    if (_isStartled  && now >= _startledUntil)      _isStartled  = false;
    if (_isAnnoyed   && now >= _annoyedUntil)       _isAnnoyed   = false;
    if (_inducedSleep && now >= _inducedSleepUntil) _inducedSleep = false;

    if (_petCount > 0 && (now - _petWindowStart) > PET_WINDOW_MS) _petCount = 0;

    _recalcMood();
}

// ─── Vitals desde servidor ────────────────────────────────────────────────────

void GotchiState::setVitals(int hunger, int happiness, int energy,
                             bool sick, bool dead, bool sleeping, bool needsClean) {
    _hunger              = (uint8_t)constrain(hunger,    0, 100);
    _happiness           = (uint8_t)constrain(happiness, 0, 100);
    _energy              = (uint8_t)constrain(energy,    0, 100);
    _isSick              = sick;
    _isDead              = dead;
    _serverSleeping      = sleeping;
    _needsClean          = needsClean;
    _needsAttentionFlag  = (needsClean || sick || dead || hunger < 40);
    _recalcMood();
}

// ─── Affinity helpers (per-type behavior) ─────────────────────────────────────

unsigned long GotchiState::_laughDur() const {
    switch (_type) {
        case GotchiType::FELIZ:    return 3500;  // loves petting
        case GotchiType::GLOTON:   return 1500;  // not into social
        case GotchiType::DORMILON: return 2500;
        case GotchiType::ACTIVO:   return 1500;  // quick, restless
        default:                   return 2000;
    }
}

unsigned long GotchiState::_angryDur() const {
    switch (_type) {
        case GotchiType::FELIZ:    return 5000;   // forgives fast
        case GotchiType::GLOTON:   return 10000;  // stubborn
        case GotchiType::ACTIVO:   return 6000;   // shakes it off
        default:                   return 8000;
    }
}

unsigned long GotchiState::_startledDur() const {
    switch (_type) {
        case GotchiType::DORMILON: return 6000;  // very disrupted when woken
        case GotchiType::ACTIVO:   return 2000;  // shakes it off fast
        default:                   return 4000;
    }
}

float GotchiState::_noiseThresh() const {
    switch (_type) {
        case GotchiType::GLOTON: return 350.0f;  // more easily excited
        case GotchiType::ACTIVO: return 250.0f;  // loves sounds
        default:                 return NOISE_THRESHOLD;
    }
}

unsigned long GotchiState::_excitedDur(float rms) const {
    unsigned long base = (unsigned long)constrain(rms / 100.0f, 1.0f, 5.0f) * 1000;
    switch (_type) {
        case GotchiType::GLOTON: return base * 3 / 2;  // 1.5x
        case GotchiType::ACTIVO: return base * 2;       // 2x
        default:                 return base;
    }
}

int GotchiState::_petMaxCount() const {
    switch (_type) {
        case GotchiType::FELIZ:    return 8;  // loves petting
        case GotchiType::GLOTON:   return 3;  // impatient
        case GotchiType::DORMILON: return 6;  // easy-going
        case GotchiType::ACTIVO:   return 4;  // restless
        default:                   return PET_MAX;
    }
}

unsigned long GotchiState::_boredMs() const {
    switch (_type) {
        case GotchiType::FELIZ:    return 240000UL;  // 4 min — patient
        case GotchiType::DORMILON: return 120000UL;  // 2 min — falls asleep fast
        case GotchiType::ACTIVO:   return  90000UL;  // 1.5 min — needs stimulation
        default:                   return BORED_TIMEOUT_MS;
    }
}

// ─── Acciones del usuario ─────────────────────────────────────────────────────

void GotchiState::_touchInteraction() {
    _lastInteractionTime = millis();
}

void GotchiState::pet() {
    _touchInteraction();
    if (!_isAngry && !_isDizzy && !_isDead) {
        _isLaughing    = true;
        _laughingUntil = millis() + _laughDur();
    }
    _inducedSleep = false;
    _recalcMood();
}

// ─── Eventos de sensores ──────────────────────────────────────────────────────

void GotchiState::onNoise(float rmsLevel) {
    // DORMILON: any noise while sleeping triggers annoyance
    if (_type == GotchiType::DORMILON && (_inducedSleep || _serverSleeping)
            && rmsLevel > 200.0f && !_isAngry && !_isDead) {
        _isAnnoyed    = true;
        _annoyedUntil = millis() + 5000;
        _isExcited    = false;
        _recalcMood();
        return;
    }

    if (rmsLevel > _noiseThresh() && !_isAngry && !_isDizzy && !_isAnnoyed && !_isDead) {
        _touchInteraction();
        _isExcited    = true;
        _excitedUntil = millis() + _excitedDur(rmsLevel);
        _recalcMood();
    }
}

void GotchiState::onSustainedNoise(bool active) {
    if (active == _noiseActive) return;
    _noiseActive = active;

    if (active && !_isAngry && !_isDizzy && !_isStartled && !_isDead) {
        // DORMILON: sustained noise while sleeping is especially disruptive
        unsigned long dur = (_type == GotchiType::DORMILON && (_inducedSleep || _serverSleeping))
                            ? 10000UL : 6000UL;
        _isAnnoyed    = true;
        _annoyedUntil = millis() + dur;
        _isExcited    = false;
        _recalcMood();
    } else if (!active && _isAnnoyed) {
        _annoyedUntil = millis() + 3000;
    }
}

void GotchiState::onDizzy(float gyroMagnitude) {
    // ACTIVO is harder to dizzy — used to movement
    float thresh = (_type == GotchiType::ACTIVO) ? DIZZY_THRESHOLD * 1.6f : DIZZY_THRESHOLD;
    if (gyroMagnitude > thresh && !_isStartled && !_isDead) {
        _isDizzy    = true;
        _dizzyUntil = millis() + 4000;
        _isExcited  = false;
        _isLaughing = false;
        _recalcMood();
    }
}

void GotchiState::onFall() {
    if (_isDead) return;
    _touchInteraction();
    _isStartled    = true;
    _startledUntil = millis() + _startledDur();
    _isLaughing    = false;
    _isExcited     = false;
    _inducedSleep  = false;
    _recalcMood();
}

void GotchiState::onShake(bool strong) {
    if (_isDead) return;
    _touchInteraction();
    _inducedSleep = false;

    if (_mood == Mood::SLEEPING) {
        // DORMILON wakes up very startled; ACTIVO shakes it off faster
        if (strong || _type == GotchiType::DORMILON) {
            _isStartled    = true;
            _startledUntil = millis() + _startledDur();
        }
    } else if (strong) {
        if (_type == GotchiType::ACTIVO) {
            // ACTIVO loves rough movement — gets excited instead of startled
            _isExcited    = true;
            _excitedUntil = millis() + 4000;
            _isLaughing   = false;
        } else {
            _isStartled    = true;
            _startledUntil = millis() + 2000;
            _isLaughing    = false;
        }
    } else if (_type == GotchiType::ACTIVO) {
        // Gentle shake also excites ACTIVO
        _isExcited    = true;
        _excitedUntil = millis() + 2000;
    }
    _recalcMood();
}

void GotchiState::onFaceDown30s() {
    if (!_isAngry && !_isDead) {
        _isAngry    = true;
        _angryUntil = millis() + _angryDur();
        _recalcMood();
    }
}

void GotchiState::onFaceUp5min() {
    if (!_isExcited && !_isAngry && !_isStartled && !_isDead) {
        _inducedSleep      = true;
        _inducedSleepUntil = millis() + 600000UL;
        _recalcMood();
    }
}

void GotchiState::onInactivity5min() {
    if (!_isExcited && !_isAngry && !_isStartled && !_isDead) {
        _inducedSleep      = true;
        _inducedSleepUntil = millis() + 600000UL;
        _recalcMood();
    }
}

void GotchiState::onButtonClick() {
    if (_isDead) return;
    unsigned long now = millis();
    _touchInteraction();
    _inducedSleep = false;

    _clickTimes[_clickHead] = now;
    _clickHead = (_clickHead + 1) % CLICK_HISTORY;

    int recent = 0;
    for (int i = 0; i < CLICK_HISTORY; i++) {
        if (_clickTimes[i] > 0 && (now - _clickTimes[i]) < RAGE_WINDOW_MS) recent++;
    }

    if (recent >= RAGE_THRESHOLD) {
        _isAngry    = true;
        _angryUntil = now + RAGE_DURATION_MS;
        _isLaughing = false;
        _isAnnoyed  = false;
        _petCount   = 0;
        _recalcMood();
        return;
    }

    if (_petCount == 0 || (now - _petWindowStart) > PET_WINDOW_MS) {
        _petCount       = 1;
        _petWindowStart = now;
    } else {
        _petCount++;
    }

    if (_petCount > _petMaxCount()) {
        _isAnnoyed    = true;
        _annoyedUntil = now + 5000;
        _isLaughing   = false;
        _petCount     = 0;
    } else if (!_isAngry && !_isDizzy && !_isAnnoyed) {
        _isLaughing    = true;
        _laughingUntil = now + _laughDur();
    }

    _recalcMood();
}

// ─── Lógica interna ───────────────────────────────────────────────────────────

void GotchiState::_recalcMood() {
    unsigned long now    = millis();
    bool isBored = (_lastInteractionTime > 0) &&
                   (now - _lastInteractionTime > _boredMs());

    Mood newMood;

    if      (_isDead)                           newMood = Mood::DEAD;
    else if (_isSick)                           newMood = Mood::SICK;
    else if (_isStartled)                       newMood = Mood::STARTLED;
    else if (_isAngry)                          newMood = Mood::ANGRY;
    else if (_isAnnoyed)                        newMood = Mood::ANNOYED;
    else if (_isDizzy)                          newMood = Mood::DIZZY;
    else if (_isLaughing)                       newMood = Mood::LAUGHING;
    else if (_isExcited)                        newMood = Mood::EXCITED;
    else if (_inducedSleep || _serverSleeping)  newMood = Mood::SLEEPING;
    else if (_hunger < 25)                      newMood = Mood::HUNGRY;
    else if (_happiness < 25)                   newMood = Mood::SAD;
    else if (isBored)                           newMood = Mood::BORED;
    else                                        newMood = Mood::HAPPY;

    if (newMood != _mood) {
        _mood        = newMood;
        _moodChanged = true;
    }
}

GotchiStats GotchiState::getStats() const {
    return { _mood, _hunger, _happiness, _energy, _needsAttentionFlag, _needsClean, _isSick };
}
