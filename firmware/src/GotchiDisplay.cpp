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
      _warnHunger(false), _warnThirst(false), _warnEnergy(false),
      _lifeState(LifeState::IDLE),
      _stateUntil(0), _nextGlanceMs(0), _soundReactUntil(0),
      _gazeH(0), _gazeV(0), _targetGazeH(0), _targetGazeV(0),
      _lastMood(255),
      _moodFlashUntil(0)
{
    memset(_alertText, 0, sizeof(_alertText));
}

// ─── begin ────────────────────────────────────────────────────────────────────
void GotchiDisplay::begin() {
    _instance = this;

    M5.Display.setBrightness(180);
    M5.Display.fillScreen(TFT_BLACK);

    // ── Face escalado a 128×128 ──────────────────────────────────────────
    // El Face por defecto usa coords para 320×240.
    // Factores: x *= 0.40 (128/320), y *= 0.533 (128/240)
    //
    //  Componente      original (top, left)    → escalado
    //  Mouth(50,90,4,60)  @ (148, 163)         → (79, 65)
    //  Eye  r=8           @ (93,  90) / (96, 230) → (50,36) / (51,92)
    //  Eyeblow w=32       @ (67,  96) / (72, 230) → (36,38) / (38,92)

    // Crear face y avatar aquí (después de M5.begin()) para evitar que el
    // constructor de M5Canvas se ejecute antes de que el hardware esté
    // inicializado. Pasamos face128 directamente al constructor de Avatar
    // para evitar cualquier ventana con face==nullptr.
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
    _avatar->setColorPalette(_neutralPalette());  // arrancar en negro con cara blanca

    // Arrancar primero (colorDepth=16 para display color), luego addTask para
    // que isDrawing()==true cuando la tarea de comportamiento empiece.
    _avatar->start(16);
    _avatar->addTask(_behaviorTask, "gotchi_life", 4096, 1);

    _setLED(_moodToLED(Mood::HAPPY));
}

// ─── update (main loop) ───────────────────────────────────────────────────────
void GotchiDisplay::update(const GotchiStats& stats, float ax, float ay, float micRms) {
    // Actualizar datos compartidos (leídos por la tarea de Avatar)
    _imuAx   = ax;
    _imuAy   = ay;
    _micRms  = micRms;
    _currentMood = (uint8_t)stats.mood;

    // Aplicar estilo si cambió el mood
    if (_lastMood != (uint8_t)stats.mood) {
        _lastMood = (uint8_t)stats.mood;
        _applyMoodStyle(stats.mood);
    }

    // ── Alertas de stats < 20% ────────────────────────────────────────────
    unsigned long now = millis();

    // Resetear flags cuando el stat se recupera
    if (stats.hunger >= 30) _warnHunger = false;
    if (stats.thirst >= 30) _warnThirst = false;
    if (stats.energy >= 30) _warnEnergy = false;

    if (!_alertPending && now >= _alertUntil) {
        if (stats.hunger < 20 && !_warnHunger) {
            _warnHunger = true;
            strncpy(_alertText, "Tengo hambre...", sizeof(_alertText) - 1);
            _alertPending = true;
            _alertUntil = now + 4000;
        } else if (stats.thirst < 20 && !_warnThirst) {
            _warnThirst = true;
            strncpy(_alertText, "Tengo sed!", sizeof(_alertText) - 1);
            _alertPending = true;
            _alertUntil = now + 4000;
        } else if (stats.energy < 20 && !_warnEnergy &&
                   stats.mood != Mood::SLEEPING) {
            _warnEnergy = true;
            strncpy(_alertText, "Estoy agotado...", sizeof(_alertText) - 1);
            _alertPending = true;
            _alertUntil = now + 4000;
        }
    }

    // Limpiar balloon cuando expire
    if (now >= _alertUntil && _alertUntil > 0) {
        _avatar->setSpeechText("");
        _alertUntil = 0;
    }
}

// ─── _applyMoodStyle ─────────────────────────────────────────────────────────
void GotchiDisplay::_applyMoodStyle(Mood m) {
    _avatar->setExpression(_moodToExpression(m));

    // Paleta neutra + acento del mood: icono de efecto en color + borde 0.5s
    ColorPalette cp = _neutralPalette();
    uint16_t accent = _moodAccent565(m);
    cp.set(COLOR_SECONDARY, accent);   // color del icono manga (permanente)
    cp.set(COLOR_BORDER,    accent);   // borde inferior (sólo 0.5 s)
    _avatar->setColorPalette(cp);

    _moodFlashUntil = millis() + 500;
    _setLED(_moodToLED(m));
}

// ─── Tarea de comportamiento autónomo ────────────────────────────────────────
// Corre en FreeRTOS, ~50 ms por frame
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

    // ── 0. Quitar borde tras el flash (0.5s); mantener color del icono ───
    if (_moodFlashUntil > 0 && now >= _moodFlashUntil) {
        _moodFlashUntil = 0;
        ColorPalette cp = _neutralPalette();
        // Mantener el acento de color en el icono manga
        cp.set(COLOR_SECONDARY, _moodAccent565((Mood)_currentMood));
        // COLOR_BORDER queda a negro (default en _neutralPalette) → sin borde
        av->setColorPalette(cp);
    }

    // ── 1. Procesar alertas pendientes ────────────────────────────────────
    if (_alertPending) {
        av->setSpeechText(_alertText);
        _alertPending = false;
    }

    // ── 2. Boca según mood ────────────────────────────────────────────────
    if (mood == Mood::SLEEPING) {
        // Respiración: seno lento en la boca
        float breath = 0.05f + 0.04f * sinf((float)now / 2800.0f);
        av->setMouthOpenRatio(breath);
    } else {
        float ratio = _moodMouthRatio(mood);
        // Pequeño pulso en moods excitados
        if (mood == Mood::EXCITED || mood == Mood::LAUGHING) {
            ratio += 0.1f * sinf((float)now / 400.0f);
        }
        av->setMouthOpenRatio(constrain(ratio, 0.0f, 1.0f));
    }

    // ── 3. Gaze autónomo: máquina de estados ─────────────────────────────
    bool isSleeping = (mood == Mood::SLEEPING);

    // Transición de estado
    if (now >= _stateUntil) {
        if (isSleeping) {
            _lifeState   = LifeState::SLEEPY_DRIFT;
            _stateUntil  = now + random(4000, 8000);
            // Gaze muy suave en sleep
            _targetGazeH = (random(100) / 100.0f - 0.5f) * 0.2f;
            _targetGazeV = 0.1f + (random(100) / 100.0f) * 0.1f; // ojos hacia abajo
        } else {
            // Seleccionar próximo estado aleatorio
            int r = random(10);
            if (r < 5) {
                // IDLE: mirada al frente con pequeña deriva
                _lifeState   = LifeState::IDLE;
                _targetGazeH = (random(100) / 100.0f - 0.5f) * 0.15f;
                _targetGazeV = (random(100) / 100.0f - 0.5f) * 0.1f;
                _stateUntil  = now + random(2000, 5000);
            } else if (r < 8) {
                // GLANCING: mirar a un lado
                _lifeState   = LifeState::GLANCING;
                _targetGazeH = (random(2) ? 1.0f : -1.0f) * (0.4f + random(40) / 100.0f);
                _targetGazeV = (random(100) / 100.0f - 0.5f) * 0.3f;
                _stateUntil  = now + random(800, 2000);
            } else {
                // CURIOUS: mirar con interés (arriba ligeramente)
                _lifeState   = LifeState::CURIOUS;
                _targetGazeH = (random(100) / 100.0f - 0.5f) * 0.5f;
                _targetGazeV = -0.2f - random(20) / 100.0f; // ojos arriba
                _stateUntil  = now + random(1200, 2500);
            }
        }
    }

    // ── 4. Reacción al sonido ─────────────────────────────────────────────
    float rms = _micRms;
    if (!isSleeping && rms > 700.0f && now > _soundReactUntil) {
        // Oyó algo: mirar hacia donde "viene" el sonido (aleatorio, único mic)
        _lifeState        = LifeState::SOUND_REACT;
        _targetGazeH      = (random(2) ? 0.8f : -0.8f);
        _targetGazeV      = -0.15f;  // ligeramente hacia arriba (curioso)
        _soundReactUntil  = now + 2500;
        _stateUntil       = now + 2000;
    }

    // ── 5. Influencia del IMU en el gaze ─────────────────────────────────
    // El acelerómetro aporta un leve desplazamiento de los ojos
    // al inclinar el dispositivo (sensación de gravedad en los ojos)
    float imuInfluenceH = constrain((float)_imuAx * 0.4f, -0.6f, 0.6f);
    float imuInfluenceV = constrain(-(float)_imuAy * 0.25f, -0.4f, 0.4f);

    // ── 6. Suavizado hacia target (lerp) ──────────────────────────────────
    float speed = isSleeping ? 0.04f : 0.12f;
    float finalH = _targetGazeH + imuInfluenceH * 0.35f;
    float finalV = _targetGazeV + imuInfluenceV * 0.35f;
    _gazeH += (finalH - _gazeH) * speed;
    _gazeV += (finalV - _gazeV) * speed;

    float gh = constrain(_gazeH, -1.0f, 1.0f);
    float gv = constrain(_gazeV, -1.0f, 1.0f);

    av->setRightGaze(gv, gh);
    av->setLeftGaze(gv, gh);
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
        case Mood::TIRED:    return Expression::Sleepy;
        case Mood::SLEEPING: return Expression::Sleepy;
        case Mood::HUNGRY:   return Expression::Sad;
        case Mood::THIRSTY:  return Expression::Sad;
        case Mood::SAD:      return Expression::Sad;
        case Mood::ANGRY:    return Expression::Angry;
        case Mood::ANNOYED:  return Expression::Angry;
        case Mood::SCARED:   return Expression::Doubt;
        case Mood::STARTLED: return Expression::Doubt;
        case Mood::DIZZY:    return Expression::Doubt;
        default:             return Expression::Neutral;
    }
}

ColorPalette GotchiDisplay::_moodToColorPalette(Mood m) {
    ColorPalette cp;
    // COLOR_PRIMARY = color de cara, COLOR_BACKGROUND = fondo,
    // COLOR_SECONDARY = color secundario (cejas, boca, etc.)
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
        case Mood::TIRED:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(210, 195, 160));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565( 50,  60,  90));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,  20,  20));
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
        case Mood::THIRSTY:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(200, 230, 255));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565(  0, 100, 180));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,  20,  20));
            break;
        case Mood::SAD:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(190, 210, 255));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565( 20,  60, 160));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 20,  20,  40));
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
        case Mood::SCARED:
            cp.set(COLOR_PRIMARY,    (uint16_t)M5.Display.color565(235, 235, 200));
            cp.set(COLOR_BACKGROUND, (uint16_t)M5.Display.color565( 15,  15,  30));
            cp.set(COLOR_SECONDARY,  (uint16_t)M5.Display.color565( 10,  10,  20));
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
        case Mood::TIRED:    return 0x000022;
        case Mood::SLEEPING: return 0x050010;
        case Mood::HUNGRY:   return 0x221100;
        case Mood::THIRSTY:  return 0x001122;
        case Mood::SAD:      return 0x000022;
        case Mood::ANGRY:    return 0x330000;
        case Mood::ANNOYED:  return 0x110800;
        case Mood::SCARED:   return 0x100010;
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
    cp.set(COLOR_SECONDARY,          black);  // sin icono hasta que cambie el mood
    cp.set(COLOR_BORDER,             black);  // sin borde
    cp.set(COLOR_BALLOON_FOREGROUND, white);
    cp.set(COLOR_BALLOON_BACKGROUND, M5.Display.color565(30, 30, 30));
    return cp;
}

uint16_t GotchiDisplay::_moodAccent565(Mood m) {
    // Colores vivos para iconos manga y borde inferior
    switch (m) {
        case Mood::HAPPY:    return M5.Display.color565(  0, 220,  80);  // verde
        case Mood::LAUGHING: return M5.Display.color565( 80, 255,  50);  // lima
        case Mood::EXCITED:  return M5.Display.color565(255, 220,   0);  // amarillo
        case Mood::TIRED:    return M5.Display.color565( 80, 100, 200);  // azul medio
        case Mood::SLEEPING: return M5.Display.color565( 50,  80, 200);  // azul oscuro
        case Mood::HUNGRY:   return M5.Display.color565(255, 120,   0);  // naranja
        case Mood::THIRSTY:  return M5.Display.color565(  0, 150, 255);  // azul agua
        case Mood::SAD:      return M5.Display.color565( 80, 120, 255);  // azul
        case Mood::ANGRY:    return M5.Display.color565(255,  30,  30);  // rojo
        case Mood::ANNOYED:  return M5.Display.color565(200,  80,   0);  // naranja oscuro
        case Mood::SCARED:   return M5.Display.color565(180,  80, 255);  // violeta
        case Mood::STARTLED: return M5.Display.color565(255,  80,   0);  // naranja fuerte
        case Mood::DIZZY:    return M5.Display.color565(200,  50, 255);  // púrpura
        default:             return M5.Display.color565(200, 200, 200);
    }
}

float GotchiDisplay::_moodMouthRatio(Mood m) {
    switch (m) {
        case Mood::SCARED:
        case Mood::STARTLED: return 0.75f;
        case Mood::LAUGHING: return 0.80f;
        case Mood::EXCITED:  return 0.55f;
        case Mood::ANGRY:    return 0.30f;
        case Mood::ANNOYED:  return 0.15f;
        case Mood::HAPPY:    return 0.25f;
        case Mood::SAD:
        case Mood::HUNGRY:
        case Mood::THIRSTY:  return 0.10f;
        default:             return 0.05f;
    }
}
