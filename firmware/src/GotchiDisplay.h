#pragma once
#include <M5Unified.h>
#include "GotchiState.h"

class GotchiDisplay {
public:
    GotchiDisplay();
    void begin();
    void update(const GotchiStats& stats);

private:
    // ── Caras ──────────────────────────────────────
    void _drawFace(Mood mood, bool blink);
    void _drawHappy(bool blink);
    void _drawTired(bool blink);
    void _drawHungry(bool blink);
    void _drawThirsty(bool blink);
    void _drawDizzy();
    void _drawExcited(bool blink);
    void _drawScared(bool blink);
    void _drawLaughing();
    void _drawAngry(bool blink);
    void _drawSad(bool blink);
    void _drawSleeping();

    // ── Primitivos de cara ─────────────────────────
    void _background(uint32_t color);
    void _drawFaceCircle(uint32_t color);
    void _eyeOpen(int x, int y, uint32_t sclera, uint32_t pupil);
    void _eyeHalf(int x, int y, uint32_t sclera, uint32_t pupil, uint32_t bgColor);
    void _eyeClosed(int x, int y, uint32_t color);
    void _eyeWide(int x, int y, uint32_t sclera, uint32_t pupil);
    void _eyeHappy(int x, int y, uint32_t color);        // forma ^
    void _eyeAngry(int x, int y, uint32_t sclera, uint32_t pupil, bool left);
    void _eyeDizzy(int x, int y, uint32_t color);        // forma X
    void _eyeStar(int x, int y, uint32_t color);         // forma *
    void _mouthSmile(int cx, int cy);
    void _mouthFrown(int cx, int cy);
    void _mouthOpen(int cx, int cy);
    void _mouthGrin(int cx, int cy);
    void _mouthTongue(int cx, int cy);
    void _mouthWavy(int cx, int cy);                     // hambriento

    // ── Extras ─────────────────────────────────────
    void _drawTear(int x, int y);
    void _drawSweat(int x, int y);
    void _drawZzz(int x, int y);
    void _drawBatteryWarn(int x, int y);
    void _drawStatusBar(uint8_t hunger, uint8_t thirst, uint8_t energy);
    void _setLED(uint32_t rgb);

    // ── Estado interno ─────────────────────────────
    Mood          _lastMood;
    bool          _moodChanged;
    unsigned long _lastDraw;
    bool          _blinkOpen;
    unsigned long _nextBlink;

    // Coordenadas base de la cara (pantalla 128×128)
    static constexpr int CX = 64;  // centro horizontal
    static constexpr int FY = 55;  // centro cara vertical
    static constexpr int FR = 42;  // radio cara
    static constexpr int LEX = 46; // ojo izquierdo X
    static constexpr int REX = 82; // ojo derecho X
    static constexpr int EY  = 46; // ojos Y
    static constexpr int MY  = 70; // boca Y
    static constexpr int BAR_Y = 106; // barra estado
};
