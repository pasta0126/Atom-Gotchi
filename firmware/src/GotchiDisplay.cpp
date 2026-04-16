#include "GotchiDisplay.h"
#include <math.h>

// ─── Singleton ────────────────────────────────────────────────────────────────
GotchiDisplay* GotchiDisplay::_instance = nullptr;

// ─── Constructor ─────────────────────────────────────────────────────────────
GotchiDisplay::GotchiDisplay()
    : _avatar(nullptr),
      _imuAx(0), _imuAy(0), _micRms(0),
      _currentMood((uint8_t)Mood::HAPPY),
      _alertPending(false), _alertUntil(0),
      _warnHunger(false), _warnHappiness(false), _warnEnergy(false), _warnSick(false),
      _lifeState(LifeState::IDLE),
      _stateUntil(0), _nextGlanceMs(0), _soundReactUntil(0),
      _gazeH(0), _gazeV(0), _targetGazeH(0), _targetGazeV(0),
      _lastMood(255),
      _moodFlashUntil(0),
      _needsAttention(false), _lastAttentionFlash(0),
      _flashCount(0), _flashPhase(false), _flashNext(0)
{
    memset(_alertText, 0, sizeof(_alertText));
}

// ─── begin ────────────────────────────────────────────────────────────────────
void GotchiDisplay::begin() {
    _instance = this;

    M5.Display.setBrightness(180);
    M5.Display.fillScreen(TFT_BLACK);

    auto* face128 = new Face(
        new Mouth(20, 36, 2, 24),   new BoundingRect(68, 65),
        new Eye(4, false),          new BoundingRect(50, 36),
        new Eye(4, true),           new BoundingRect(51, 92),
        new Eyeblow(13, 0, false),  new BoundingRect(36, 38),
        new Eyeblow(13, 0, true),   new BoundingRect(38, 92),
        new BoundingRect(0, 0, 128, 128),
        new M5Canvas(&M5.Display),
        new M5Canvas(&M5.Display)
    );

    _avatar = new Avatar(face128);
    _avatar->setColorPalette(_neutralPalette());

    _avatar->start(16);
    _avatar->addTask(_behaviorTask, "gotchi_life", 4096, 1);

    _setLED(_moodToLED(Mood::HAPPY));
}

// ─── update (main loop) ───────────────────────────────────────────────────────
void GotchiDisplay::update(const GotchiStats& stats, float ax, float ay, float micRms) {
    _imuAx       = ax;
    _imuAy       = ay;
    _micRms      = micRms;
    _currentMood = (uint8_t)stats.mood;

    if (_lastMood != (uint8_t)stats.mood) {
        _lastMood = (uint8_t)stats.mood;
        _applyMoodStyle(stats.mood);
    }

    // ── Alertas de stats bajos ────────────────────────────────────────────
    unsigned long now = millis();

    _needsAttention = stats.needsAttention;

    if (stats.hunger    >= 30) _warnHunger    = false;
    if (stats.happiness >= 30) _warnHappiness = false;
    if (stats.energy    >= 30) _warnEnergy    = false;
    if (stats.mood != Mood::SICK) _warnSick   = false;

    if (!_alertPending && now >= _alertUntil) {
        if (stats.mood == Mood::DEAD) {
            strncpy(_alertText, "Game over...", sizeof(_alertText) - 1);
            _alertPending = true;
            _alertUntil   = now + 5000;
        } else if (stats.mood == Mood::SICK && !_warnSick) {
            _warnSick = true;
            strncpy(_alertText, "Me siento mal...", sizeof(_alertText) - 1);
            _alertPending = true;
            _alertUntil   = now + 4000;
        } else if (stats.hunger < 20 && !_warnHunger) {
            _warnHunger = true;
            strncpy(_alertText, "Tengo hambre...", sizeof(_alertText) - 1);
            _alertPending = true;
            _alertUntil   = now + 4000;
        } else if (stats.happiness < 20 && !_warnHappiness) {
            _warnHappiness = true;
            strncpy(_alertText, "Estoy triste...", sizeof(_alertText) - 1);
            _alertPending = true;
            _alertUntil   = now + 4000;
        } else if (stats.energy < 20 && !_warnEnergy &&
                   stats.mood != Mood::SLEEPING) {
            _warnEnergy = true;
            strncpy(_alertText, "Estoy agotado...", sizeof(_alertText) - 1);
            _alertPending = true;
            _alertUntil   = now + 4000;
        }
    }

    if (now >= _alertUntil && _alertUntil > 0) {
        _avatar->setSpeechText("");
        _alertUntil = 0;
    }
}

// ─── _applyMoodStyle ─────────────────────────────────────────────────────────
void GotchiDisplay::_applyMoodStyle(Mood m) {
    _avatar->setExpression(_moodToExpression(m));

    ColorPalette cp = _neutralPalette();
    uint16_t accent = _moodAccent565(m);
    cp.set(COLOR_SECONDARY, accent);
    cp.set(COLOR_BORDER,    accent);
    _avatar->setColorPalette(cp);

    _moodFlashUntil = millis() + 500;
    _setLED(_moodToLED(m));
}

// ─── Tarea de comportamiento autónomo ────────────────────────────────────────
void GotchiDisplay::_behaviorTask(void* arg) {
    DriveContext* ctx = (DriveContext*)arg;
    Avatar* av = ctx->getAvatar();

    for (;;) {
        if (av->isDrawing() && _instance) {
            _instance->_tick(av);
        }
        delay(50);
    }
}

// ─── _tick: un frame de vida autónoma ────────────────────────────────────────
void GotchiDisplay::_tick(Avatar* av) {
    unsigned long now = millis();
    Mood mood = (Mood)_currentMood;

    // ── 0. Quitar borde tras el flash de mood (0.5s) ─────────────────────
    if (_moodFlashUntil > 0 && now >= _moodFlashUntil) {
        _moodFlashUntil = 0;
        ColorPalette cp = _neutralPalette();
        cp.set(COLOR_SECONDARY, _moodAccent565((Mood)_currentMood));
        av->setColorPalette(cp);
    }

    // ── 1. Procesar alertas pendientes ────────────────────────────────────
    if (_alertPending) {
        av->setSpeechText(_alertText);
        _alertPending = false;
    }

    // ── 2. Boca según mood ────────────────────────────────────────────────
    if (mood == Mood::SLEEPING) {
        float breath = 0.05f + 0.04f * sinf((float)now / 2800.0f);
        av->setMouthOpenRatio(breath);
    } else if (mood == Mood::DEAD) {
        av->setMouthOpenRatio(0.0f);
    } else {
        float ratio = _moodMouthRatio(mood);
        if (mood == Mood::EXCITED || mood == Mood::LAUGHING) {
            ratio += 0.1f * sinf((float)now / 400.0f);
        }
        av->setMouthOpenRatio(constrain(ratio, 0.0f, 1.0f));
    }

    // ── 3. Gaze autónomo ──────────────────────────────────────────────────
    bool isSleeping = (mood == Mood::SLEEPING || mood == Mood::DEAD);

    if (now >= _stateUntil) {
        if (isSleeping) {
            _lifeState   = LifeState::SLEEPY_DRIFT;
            _stateUntil  = now + random(4000, 8000);
            _targetGazeH = (random(100) / 100.0f - 0.5f) * 0.2f;
            _targetGazeV = 0.1f + (random(100) / 100.0f) * 0.1f;
        } else {
            int r = random(10);
            if (r < 5) {
                _lifeState   = LifeState::IDLE;
                _targetGazeH = (random(100) / 100.0f - 0.5f) * 0.15f;
                _targetGazeV = (random(100) / 100.0f - 0.5f) * 0.1f;
                _stateUntil  = now + random(2000, 5000);
            } else if (r < 8) {
                _lifeState   = LifeState::GLANCING;
                _targetGazeH = (random(2) ? 1.0f : -1.0f) * (0.4f + random(40) / 100.0f);
                _targetGazeV = (random(100) / 100.0f - 0.5f) * 0.3f;
                _stateUntil  = now + random(800, 2000);
            } else {
                _lifeState   = LifeState::CURIOUS;
                _targetGazeH = (random(100) / 100.0f - 0.5f) * 0.5f;
                _targetGazeV = -0.2f - random(20) / 100.0f;
                _stateUntil  = now + random(1200, 2500);
            }
        }
    }

    // ── 4. Reacción al sonido ─────────────────────────────────────────────
    float rms = _micRms;
    if (!isSleeping && rms > 700.0f && now > _soundReactUntil) {
        _lifeState        = LifeState::SOUND_REACT;
        _targetGazeH      = (random(2) ? 0.8f : -0.8f);
        _targetGazeV      = -0.15f;
        _soundReactUntil  = now + 2500;
        _stateUntil       = now + 2000;
    }

    // ── 5. Influencia del IMU en el gaze ─────────────────────────────────
    float imuInfluenceH = constrain((float)_imuAx * 0.4f, -0.6f, 0.6f);
    float imuInfluenceV = constrain(-(float)_imuAy * 0.25f, -0.4f, 0.4f);

    // ── 6. Suavizado hacia target ─────────────────────────────────────────
    float speed = isSleeping ? 0.04f : 0.12f;
    float finalH = _targetGazeH + imuInfluenceH * 0.35f;
    float finalV = _targetGazeV + imuInfluenceV * 0.35f;
    _gazeH += (finalH - _gazeH) * speed;
    _gazeV += (finalV - _gazeV) * speed;

    float gh = constrain(_gazeH, -1.0f, 1.0f);
    float gv = constrain(_gazeV, -1.0f, 1.0f);

    av->setRightGaze(gv, gh);
    av->setLeftGaze(gv, gh);

    // ── 7. Animación LED ──────────────────────────────────────────────────
    _animateLED(mood, now);

    // ── 8. Flash de pantalla para llamar la atención ──────────────────────
    // Activa cada 30 s cuando el gotchi necesita cuidado y no está durmiendo
    if (_needsAttention && !isSleeping && _flashCount == 0) {
        if ((now - _lastAttentionFlash) > 30000UL) {
            _lastAttentionFlash = now;
            _flashCount = 6;   // 3 destellos = 6 transiciones brillante/oscuro
            _flashPhase = true;
            _flashNext  = now;
        }
    }

    if (_flashCount > 0 && now >= _flashNext) {
        M5.Display.setBrightness(_flashPhase ? 255 : 20);
        _flashPhase = !_flashPhase;
        _flashCount--;
        _flashNext = now + 130;
        if (_flashCount == 0) {
            M5.Display.setBrightness(180); // restaurar brillo normal
        }
    }
}

// ─── LED ──────────────────────────────────────────────────────────────────────
void GotchiDisplay::_setLED(uint32_t rgb) {
    M5.Led.setColor(0, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)(rgb));
}

// ─── Mapeos mood → Avatar ─────────────────────────────────────────────────────

Expression GotchiDisplay::_moodToExpression(Mood m) {
    switch (m) {
        case Mood::HAPPY:    return Expression::Happy;
        case Mood::LAUGHING: return Expression::Happy;
        case Mood::EXCITED:  return Expression::Happy;
        case Mood::SLEEPING: return Expression::Sleepy;
        case Mood::BORED:    return Expression::Sleepy;
        case Mood::HUNGRY:   return Expression::Sad;
        case Mood::SAD:      return Expression::Sad;
        case Mood::SICK:     return Expression::Doubt;
        case Mood::DEAD:     return Expression::Neutral;
        case Mood::ANGRY:    return Expression::Angry;
        case Mood::ANNOYED:  return Expression::Angry;
        case Mood::STARTLED: return Expression::Doubt;
        case Mood::DIZZY:    return Expression::Doubt;
        default:             return Expression::Neutral;
    }
}

ColorPalette GotchiDisplay::_moodToColorPalette(Mood m) {
    ColorPalette cp;
    switch (m) {
        case Mood::HAPPY:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(255, 220,  50));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(100, 200, 120));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 30,  30,  30));
            break;
        case Mood::LAUGHING:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(255, 230,  80));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(150, 255,  70));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,  20,  20));
            break;
        case Mood::EXCITED:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(255, 240, 100));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(255, 220,   0));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,  20,  20));
            break;
        case Mood::BORED:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(180, 180, 200));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565( 30,  30,  60));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,  20,  30));
            break;
        case Mood::SLEEPING:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(185, 200, 220));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(  5,   5,  30));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 15,  15,  50));
            break;
        case Mood::HUNGRY:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(255, 220,  80));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(200,  80,   0));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,  20,  20));
            break;
        case Mood::SAD:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(190, 210, 255));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565( 20,  60, 160));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,  20,  40));
            break;
        case Mood::SICK:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(190, 230, 160));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565( 20,  60,  20));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 10,  30,  10));
            break;
        case Mood::DEAD:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565( 80,  80,  80));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(  5,   5,   5));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,  20,  20));
            break;
        case Mood::ANGRY:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(255, 170, 120));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(190,   0,   0));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 10,   0,   0));
            break;
        case Mood::ANNOYED:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(215, 180, 140));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565( 70,  30,   0));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 15,   8,   0));
            break;
        case Mood::STARTLED:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(255, 230, 200));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(220,  80,   0));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,   5,   0));
            break;
        case Mood::DIZZY:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(230, 190, 255));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565( 50,   0,  80));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 80,  20, 120));
            break;
        default:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(255, 220,  50));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(100, 200, 120));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 30,  30,  30));
            break;
    }
    return cp;
}

uint32_t GotchiDisplay::_moodToLED(Mood m) {
    switch (m) {
        case Mood::HAPPY:    return 0x003300;
        case Mood::LAUGHING: return 0x006600;
        case Mood::EXCITED:  return 0x333300;
        case Mood::BORED:    return 0x000011;
        case Mood::SLEEPING: return 0x050010;
        case Mood::HUNGRY:   return 0x221100;
        case Mood::SAD:      return 0x000022;
        case Mood::SICK:     return 0x002200;
        case Mood::DEAD:     return 0x000000;
        case Mood::ANGRY:    return 0x330000;
        case Mood::ANNOYED:  return 0x110800;
        case Mood::STARTLED: return 0x220800;
        case Mood::DIZZY:    return 0x110022;
        default:             return 0x001100;
    }
}

ColorPalette GotchiDisplay::_neutralPalette() {
    ColorPalette cp;
    uint16_t white = M5.Display.color565(240, 240, 240);
    uint16_t black = M5.Display.color565(  0,   0,   0);
    cp.set(COLOR_PRIMARY,            white);
    cp.set(COLOR_BACKGROUND,         black);
    cp.set(COLOR_SECONDARY,          black);
    cp.set(COLOR_BORDER,             black);
    cp.set(COLOR_BALLOON_FOREGROUND, white);
    cp.set(COLOR_BALLOON_BACKGROUND, M5.Display.color565(30, 30, 30));
    return cp;
}

uint16_t GotchiDisplay::_moodAccent565(Mood m) {
    switch (m) {
        case Mood::HAPPY:    return M5.Display.color565(  0, 220,  80);
        case Mood::LAUGHING: return M5.Display.color565( 80, 255,  50);
        case Mood::EXCITED:  return M5.Display.color565(255, 220,   0);
        case Mood::BORED:    return M5.Display.color565( 80,  80, 200);
        case Mood::SLEEPING: return M5.Display.color565( 50,  80, 200);
        case Mood::HUNGRY:   return M5.Display.color565(255, 120,   0);
        case Mood::SAD:      return M5.Display.color565( 80, 120, 255);
        case Mood::SICK:     return M5.Display.color565(150, 220,   0);
        case Mood::DEAD:     return M5.Display.color565( 50,  50,  50);
        case Mood::ANGRY:    return M5.Display.color565(255,  30,  30);
        case Mood::ANNOYED:  return M5.Display.color565(200,  80,   0);
        case Mood::STARTLED: return M5.Display.color565(255,  80,   0);
        case Mood::DIZZY:    return M5.Display.color565(200,  50, 255);
        default:             return M5.Display.color565(200, 200, 200);
    }
}

float GotchiDisplay::_moodMouthRatio(Mood m) {
    switch (m) {
        case Mood::STARTLED: return 0.75f;
        case Mood::LAUGHING: return 0.80f;
        case Mood::EXCITED:  return 0.55f;
        case Mood::ANGRY:    return 0.30f;
        case Mood::ANNOYED:  return 0.15f;
        case Mood::HAPPY:    return 0.25f;
        case Mood::SICK:     return 0.20f;
        case Mood::HUNGRY:
        case Mood::SAD:      return 0.10f;
        case Mood::DEAD:     return 0.00f;
        default:             return 0.05f;
    }
}

// ─── LED animado según mood ───────────────────────────────────────────────────
// Corre en la tarea FreeRTOS cada 50 ms
void GotchiDisplay::_animateLED(Mood m, unsigned long now) {
    float t = (float)now;

    switch (m) {
        case Mood::DEAD: {
            // Parpadeo lento rojo apagado — peligro mínimo
            bool on = (now / 800) % 2;
            M5.Led.setColor(0, on ? 25 : 0, 0, 0);
            break;
        }
        case Mood::SICK: {
            // Pulso verde enfermizo
            float p = (sinf(t / 600.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, 0, (uint8_t)(30.0f * p), 0);
            break;
        }
        case Mood::HUNGRY: {
            // Pulso naranja medio
            float p = (sinf(t / 700.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, (uint8_t)(60.0f * p), (uint8_t)(15.0f * p), 0);
            break;
        }
        case Mood::SAD: {
            // Pulso azul suave
            float p = (sinf(t / 900.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, 0, 0, (uint8_t)(40.0f * p));
            break;
        }
        case Mood::SLEEPING: {
            // Respiración azul muy lenta
            float p = (sinf(t / 2500.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, 0, 0, (uint8_t)(18.0f * p));
            break;
        }
        case Mood::LAUGHING: {
            // Pulso verde brillante rápido
            float p = (sinf(t / 180.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, 0, (uint8_t)(80.0f * p), (uint8_t)(20.0f * p));
            break;
        }
        case Mood::EXCITED: {
            // Pulso amarillo rápido
            float p = (sinf(t / 220.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, (uint8_t)(70.0f * p), (uint8_t)(50.0f * p), 0);
            break;
        }
        case Mood::ANGRY: {
            // Parpadeo rojo agresivo
            bool on = (now / 150) % 2;
            M5.Led.setColor(0, on ? 80 : 10, 0, 0);
            break;
        }
        case Mood::STARTLED: {
            // Parpadeo naranja rápido
            bool on = (now / 100) % 2;
            M5.Led.setColor(0, on ? 70 : 0, on ? 20 : 0, 0);
            break;
        }
        case Mood::ANNOYED: {
            // Pulso naranja oscuro lento
            float p = (sinf(t / 500.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, (uint8_t)(40.0f * p), (uint8_t)(10.0f * p), 0);
            break;
        }
        case Mood::DIZZY: {
            // Cicla entre lila y apagado
            float p = (sinf(t / 300.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, (uint8_t)(30.0f * p), 0, (uint8_t)(50.0f * p));
            break;
        }
        case Mood::BORED: {
            // Latido muy lento azul tenue
            float p = (sinf(t / 3000.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, 0, 0, (uint8_t)(12.0f * p));
            break;
        }
        default: // HAPPY
        {
            // Pulso verde suave — "todo bien"
            float p = (sinf(t / 1800.0f) + 1.0f) * 0.5f;
            M5.Led.setColor(0, 0, (uint8_t)(20.0f + 15.0f * p), 0);
            break;
        }
    }
}
