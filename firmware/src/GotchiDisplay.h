#pragma once
#include <M5Unified.h>
#include <Avatar.h>
#include <Face.h>
#include <Eye.h>
#include <Mouth.h>
#include <Eyeblow.h>
#include "GotchiState.h"

using namespace m5avatar;

// ─── Estados de animación autónoma ───────────────────────────────────────────

enum class LifeState : uint8_t {
    IDLE,          // quieto, respiración lenta
    GLANCING,      // mirando en una dirección
    CURIOUS,       // ojos ligeramente abiertos, mirando algo
    SOUND_REACT,   // oyó algo, busca con la mirada
    SLEEPY_DRIFT,  // durmiendo, movimiento mínimo
};

// ─── GotchiDisplay ────────────────────────────────────────────────────────────

class GotchiDisplay {
public:
    GotchiDisplay();
    void begin();

    // Llamar desde main loop cada ciclo
    void update(const GotchiStats& stats, float ax, float ay, float micRms);

private:
    Avatar* _avatar;  // puntero: se crea en begin() para evitar init estática antes de M5.begin()

    // ── Estado compartido (hilo Avatar ↔ hilo principal) ──────────────────
    // volatile garantiza lecturas actualizadas sin cache; valores escalares
    // de 32-bit son atómicos en ESP32-S3
    volatile float   _imuAx;        // acelerómetro X  (-1..1 g)
    volatile float   _imuAy;        // acelerómetro Y  (-1..1 g)
    volatile float   _micRms;       // nivel mic (0..2000)
    volatile uint8_t _currentMood;  // Mood como uint8_t

    // ── Alertas de stat (balloon text) ───────────────────────────────────
    volatile bool    _alertPending;
    char             _alertText[40];
    unsigned long    _alertUntil;

    // Umbral ya notificado para no repetir
    bool _warnHunger;
    bool _warnHappiness;
    bool _warnEnergy;
    bool _warnSick;

    // Flash de atención (pantalla + LED)
    volatile bool   _needsAttention;
    unsigned long   _lastAttentionFlash;  // última vez que se hizo flash
    volatile int    _flashCount;          // transiciones restantes (6 = 3 destellos)
    volatile bool   _flashPhase;          // true=brillante, false=oscuro
    volatile unsigned long _flashNext;    // cuándo ejecutar la próxima transición

    // ── Animación autónoma (corre en tarea FreeRTOS del Avatar) ──────────
    LifeState     _lifeState;
    unsigned long _stateUntil;     // cuándo termina el estado actual
    unsigned long _nextGlanceMs;   // próxima mirada aleatoria
    unsigned long _soundReactUntil;

    float _gazeH;        // gaze actual suavizado (horizontal, -1..1)
    float _gazeV;        // gaze actual suavizado (vertical,   -1..1)
    float _targetGazeH;  // objetivo del gaze
    float _targetGazeV;

    // Última paleta aplicada (para no redibujar si no cambió mood)
    uint8_t _lastMood;

    // Flash de color al cambiar de mood (luego vuelve a fondo negro)
    volatile unsigned long _moodFlashUntil;

    // Singleton para el callback estático
    static GotchiDisplay* _instance;
    static void _behaviorTask(void* driveCtx);

    // Ejecuta un frame de la animación autónoma
    void _tick(Avatar* av);

    // Helpers
    void _applyMoodStyle(Mood m);
    void _setLED(uint32_t rgb);

    static Expression   _moodToExpression(Mood m);
    static ColorPalette _moodToColorPalette(Mood m);
    static ColorPalette _neutralPalette();
    static uint16_t     _moodAccent565(Mood m);
    static uint32_t     _moodToLED(Mood m);
    static float        _moodMouthRatio(Mood m);
    static void         _animateLED(Mood m, unsigned long now);
};
