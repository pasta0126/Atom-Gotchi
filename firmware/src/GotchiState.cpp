#include "GotchiState.h"
#include <Arduino.h>

GotchiState::GotchiState()
    : _mood(Mood::HAPPY), _moodChanged(true),
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

// ─── Acciones del usuario ─────────────────────────────────────────────────────

void GotchiState::_touchInteraction() {
    _lastInteractionTime = millis();
}

void GotchiState::pet() {
    _touchInteraction();
    if (!_isAngry && !_isDizzy && !_isDead) {
        _isLaughing    = true;
        _laughingUntil = millis() + 2000;
    }
    _inducedSleep = false;
    _recalcMood();
}

// ─── Eventos de sensores ──────────────────────────────────────────────────────

void GotchiState::onNoise(float rmsLevel) {
    if (rmsLevel > NOISE_THRESHOLD && !_isAngry && !_isDizzy && !_isAnnoyed && !_isDead) {
        _touchInteraction();
        _isExcited    = true;
        unsigned long dur = (unsigned long)constrain(rmsLevel / 100.0f, 1.0f, 5.0f) * 1000;
        _excitedUntil = millis() + dur;
        _recalcMood();
    }
}

void GotchiState::onSustainedNoise(bool active) {
    if (active == _noiseActive) return;
    _noiseActive = active;

    if (active && !_isAngry && !_isDizzy && !_isStartled && !_isDead) {
        _isAnnoyed    = true;
        _annoyedUntil = millis() + 6000;
        _isExcited    = false;
        _recalcMood();
    } else if (!active && _isAnnoyed) {
        _annoyedUntil = millis() + 3000;
    }
}

void GotchiState::onDizzy(float gyroMagnitude) {
    if (gyroMagnitude > DIZZY_THRESHOLD && !_isStartled && !_isDead) {
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
    _startledUntil = millis() + 4000;
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
        if (strong) {
            _isStartled    = true;
            _startledUntil = millis() + 3000;
        }
    } else if (strong) {
        _isStartled    = true;
        _startledUntil = millis() + 2000;
        _isLaughing    = false;
    }
    _recalcMood();
}

void GotchiState::onFaceDown30s() {
    if (!_isAngry && !_isDead) {
        _isAngry    = true;
        _angryUntil = millis() + 8000;
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

    if (_petCount > PET_MAX) {
        _isAnnoyed    = true;
        _annoyedUntil = now + 5000;
        _isLaughing   = false;
        _petCount     = 0;
    } else if (!_isAngry && !_isDizzy && !_isAnnoyed) {
        _isLaughing    = true;
        _laughingUntil = now + 2500;
    }

    _recalcMood();
}

// ─── Lógica interna ───────────────────────────────────────────────────────────

void GotchiState::_recalcMood() {
    unsigned long now    = millis();
    bool isBored = (_lastInteractionTime > 0) &&
                   (now - _lastInteractionTime > BORED_TIMEOUT_MS);

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
