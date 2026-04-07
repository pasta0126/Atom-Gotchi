#include "GotchiDisplay.h"

// Colores básicos (RGB565 pero M5GFX acepta RGB888 con color24to16 automático)
#define C_WHITE    0xFFFFFF
#define C_BLACK    0x000000
#define C_YELLOW   0xFFE000
#define C_RED      0xFF2020
#define C_BLUE     0x2060FF
#define C_GREEN    0x30CC30
#define C_PURPLE   0xAA44FF
#define C_ORANGE   0xFF8000
#define C_CYAN     0x00CCFF
#define C_DARKBLUE 0x102060
#define C_GRAY     0x888888
#define C_PINK     0xFF6688

GotchiDisplay::GotchiDisplay()
    : _lastMood(Mood::HAPPY), _moodChanged(true),
      _lastDraw(0), _blinkOpen(true), _nextBlink(3000)
{}

void GotchiDisplay::begin() {
    M5.Display.setBrightness(180);
    M5.Display.setTextFont(0);
    M5.Display.setTextSize(1);
    _setLED(C_GREEN);
}

// ─── Update principal ─────────────────────────────────────────────────────────

void GotchiDisplay::update(const GotchiStats& stats) {
    unsigned long now = millis();
    if (now - _lastDraw < 100) return; // 10 fps máx
    _lastDraw = now;

    // Gestión de parpadeo
    if (now >= _nextBlink) {
        _blinkOpen = !_blinkOpen;
        _nextBlink = now + (_blinkOpen ? random(3000, 6000) : 180);
    }

    bool redraw = (stats.mood != _lastMood);
    _lastMood = stats.mood;

    if (redraw || !_blinkOpen) {
        _drawFace(stats.mood, _blinkOpen);
        _drawStatusBar(stats.hunger, stats.thirst, stats.energy);
        if (stats.phoneBatteryWarn) _drawBatteryWarn(100, 4);
        M5.Display.display();
    }
}

// ─── Despachador de caras ─────────────────────────────────────────────────────

void GotchiDisplay::_drawFace(Mood mood, bool blink) {
    switch (mood) {
        case Mood::HAPPY:    _drawHappy(blink);    _setLED(C_GREEN);   break;
        case Mood::TIRED:    _drawTired(blink);    _setLED(C_BLUE);    break;
        case Mood::HUNGRY:   _drawHungry(blink);   _setLED(C_ORANGE);  break;
        case Mood::THIRSTY:  _drawThirsty(blink);  _setLED(C_CYAN);    break;
        case Mood::DIZZY:    _drawDizzy();          _setLED(C_PURPLE);  break;
        case Mood::EXCITED:  _drawExcited(blink);  _setLED(C_YELLOW);  break;
        case Mood::SCARED:   _drawScared(blink);   _setLED(C_RED);     break;
        case Mood::LAUGHING: _drawLaughing();       _setLED(C_GREEN);   break;
        case Mood::ANGRY:    _drawAngry(blink);    _setLED(C_RED);     break;
        case Mood::SAD:      _drawSad(blink);      _setLED(C_BLUE);    break;
        case Mood::SLEEPING: _drawSleeping();       _setLED(0x110022);  break;
    }
}

// ─── Primitivos ───────────────────────────────────────────────────────────────

void GotchiDisplay::_background(uint32_t color) {
    M5.Display.fillScreen(color);
}

void GotchiDisplay::_drawFaceCircle(uint32_t color) {
    M5.Display.fillCircle(CX, FY, FR, color);
    M5.Display.drawCircle(CX, FY, FR, C_BLACK);
}

void GotchiDisplay::_eyeOpen(int x, int y, uint32_t sclera, uint32_t pupil) {
    M5.Display.fillCircle(x, y, 8, sclera);
    M5.Display.fillCircle(x + 1, y + 1, 4, pupil);
}

void GotchiDisplay::_eyeHalf(int x, int y, uint32_t sclera, uint32_t pupil, uint32_t bg) {
    M5.Display.fillCircle(x, y, 8, sclera);
    M5.Display.fillRect(x - 9, y - 10, 19, 10, bg); // tapa mitad superior
    M5.Display.fillCircle(x + 1, y + 2, 4, pupil);
}

void GotchiDisplay::_eyeClosed(int x, int y, uint32_t color) {
    M5.Display.drawLine(x - 7, y, x + 7, y, color);
    M5.Display.drawLine(x - 7, y + 1, x + 7, y + 1, color);
}

void GotchiDisplay::_eyeWide(int x, int y, uint32_t sclera, uint32_t pupil) {
    M5.Display.fillCircle(x, y, 11, sclera);
    M5.Display.fillCircle(x + 1, y + 2, 5, pupil);
    M5.Display.drawCircle(x, y, 11, C_BLACK);
}

void GotchiDisplay::_eyeHappy(int x, int y, uint32_t color) {
    // Forma ^ (ojo feliz tipo anime)
    for (int t = 0; t < 2; t++) {
        M5.Display.drawLine(x - 7, y + 4 + t, x, y - 2 + t, color);
        M5.Display.drawLine(x, y - 2 + t, x + 7, y + 4 + t, color);
    }
}

void GotchiDisplay::_eyeAngry(int x, int y, uint32_t sclera, uint32_t pupil, bool left) {
    _eyeOpen(x, y, sclera, pupil);
    // Ceja inclinada
    if (left) {
        M5.Display.drawLine(x - 6, y - 8, x + 4, y - 13, C_BLACK);
        M5.Display.drawLine(x - 6, y - 9, x + 4, y - 14, C_BLACK);
    } else {
        M5.Display.drawLine(x - 4, y - 13, x + 6, y - 8, C_BLACK);
        M5.Display.drawLine(x - 4, y - 14, x + 6, y - 9, C_BLACK);
    }
}

void GotchiDisplay::_eyeDizzy(int x, int y, uint32_t color) {
    // Forma X
    M5.Display.drawLine(x - 6, y - 6, x + 6, y + 6, color);
    M5.Display.drawLine(x + 6, y - 6, x - 6, y + 6, color);
    M5.Display.drawLine(x - 7, y - 5, x + 5, y + 7, color);
    M5.Display.drawLine(x + 7, y - 5, x - 5, y + 7, color);
}

void GotchiDisplay::_eyeStar(int x, int y, uint32_t color) {
    M5.Display.drawLine(x, y - 9, x, y + 9, color);
    M5.Display.drawLine(x - 9, y, x + 9, y, color);
    M5.Display.drawLine(x - 6, y - 6, x + 6, y + 6, color);
    M5.Display.drawLine(x + 6, y - 6, x - 6, y + 6, color);
}

// Boca: ángulos M5GFX: 0=arriba(12h), 90=derecha, 180=abajo, 270=izquierda
void GotchiDisplay::_mouthSmile(int cx, int cy) {
    // Centro encima de la boca, arco en parte inferior = sonrisa
    M5.Display.fillArc(cx, cy - 8, 12, 17, 130, 230, C_BLACK);
}

void GotchiDisplay::_mouthFrown(int cx, int cy) {
    // Centro debajo de la boca, arco en parte superior = mueca
    M5.Display.fillArc(cx, cy + 8, 12, 17, 310, 50, C_BLACK);
}

void GotchiDisplay::_mouthOpen(int cx, int cy) {
    M5.Display.fillEllipse(cx, cy, 12, 9, C_BLACK);
}

void GotchiDisplay::_mouthGrin(int cx, int cy) {
    // Sonrisa ancha con dientes
    M5.Display.fillRect(cx - 13, cy - 5, 26, 12, C_BLACK);
    M5.Display.fillRect(cx - 12, cy - 4, 24, 7, C_WHITE);
    // Separadores dientes
    M5.Display.drawLine(cx - 4, cy - 4, cx - 4, cy + 3, C_BLACK);
    M5.Display.drawLine(cx + 4, cy - 4, cx + 4, cy + 3, C_BLACK);
}

void GotchiDisplay::_mouthTongue(int cx, int cy) {
    M5.Display.fillEllipse(cx, cy, 10, 7, C_BLACK);
    M5.Display.fillEllipse(cx, cy + 4, 7, 5, C_PINK);
}

void GotchiDisplay::_mouthWavy(int cx, int cy) {
    // Boca ondulada (hambriento)
    for (int i = -12; i <= 12; i += 4) {
        int y0 = cy + (i % 8 == 0 ? -2 : 2);
        int y1 = cy + ((i + 4) % 8 == 0 ? -2 : 2);
        M5.Display.drawLine(cx + i, y0, cx + i + 4, y1, C_BLACK);
        M5.Display.drawLine(cx + i, y0 + 1, cx + i + 4, y1 + 1, C_BLACK);
    }
}

// ─── Extras ───────────────────────────────────────────────────────────────────

void GotchiDisplay::_drawTear(int x, int y) {
    M5.Display.fillCircle(x, y, 3, C_CYAN);
    M5.Display.fillTriangle(x - 3, y, x + 3, y, x, y + 6, C_CYAN);
}

void GotchiDisplay::_drawSweat(int x, int y) {
    M5.Display.fillCircle(x, y, 3, C_CYAN);
    M5.Display.fillTriangle(x - 2, y, x + 2, y, x, y + 5, C_CYAN);
}

void GotchiDisplay::_drawZzz(int x, int y) {
    M5.Display.setTextColor(C_WHITE);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(x, y);
    M5.Display.print("z");
    M5.Display.setCursor(x + 5, y - 5);
    M5.Display.print("Z");
    M5.Display.setCursor(x + 11, y - 10);
    M5.Display.print("Z");
}

void GotchiDisplay::_drawBatteryWarn(int x, int y) {
    // Icono batería pequeño rojo en esquina
    M5.Display.fillRect(x, y, 18, 10, C_RED);
    M5.Display.fillRect(x + 18, y + 3, 3, 4, C_RED);
    M5.Display.fillRect(x + 1, y + 1, 5, 8, C_BLACK); // barra casi vacía
    M5.Display.setTextColor(C_WHITE);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(x + 7, y + 2);
    M5.Display.print("!");
}

void GotchiDisplay::_drawStatusBar(uint8_t hunger, uint8_t thirst, uint8_t energy) {
    int barW = 36;
    int barH = 6;
    int y = BAR_Y;

    // Fondo oscuro
    M5.Display.fillRect(0, y - 8, 128, 28, 0x111111);

    // Iconos y barras: [H] [T] [E]
    const char* labels[] = { "H", "T", "E" };
    uint8_t     vals[]   = { hunger, thirst, energy };
    uint32_t    colors[] = { C_ORANGE, C_CYAN, C_GREEN };

    for (int i = 0; i < 3; i++) {
        int x = 4 + i * 42;
        M5.Display.setTextColor(colors[i]);
        M5.Display.setTextSize(1);
        M5.Display.setCursor(x, y - 6);
        M5.Display.print(labels[i]);

        // Fondo barra
        M5.Display.fillRect(x, y + 1, barW, barH, 0x333333);
        // Relleno
        int fill = (vals[i] * barW) / 100;
        M5.Display.fillRect(x, y + 1, fill, barH, colors[i]);
    }
}

// ─── LED RGB (M5Atom LED único) ───────────────────────────────────────────────

void GotchiDisplay::_setLED(uint32_t rgb) {
    // M5Unified Atom: M5.dis.drawpix(index, color)
    M5.dis.drawpix(0, rgb);
    M5.dis.show();
}

// ─── Caras ────────────────────────────────────────────────────────────────────

void GotchiDisplay::_drawHappy(bool blink) {
    _background(0x88DDAA);
    _drawFaceCircle(C_YELLOW);
    if (!blink) {
        _eyeOpen(LEX, EY, C_WHITE, C_BLACK);
        _eyeOpen(REX, EY, C_WHITE, C_BLACK);
    } else {
        _eyeClosed(LEX, EY, C_BLACK);
        _eyeClosed(REX, EY, C_BLACK);
    }
    _mouthSmile(CX, MY);
}

void GotchiDisplay::_drawTired(bool blink) {
    _background(0x334466);
    _drawFaceCircle(0xDDCCAA);
    if (!blink) {
        _eyeHalf(LEX, EY, C_WHITE, C_BLACK, 0xDDCCAA);
        _eyeHalf(REX, EY, C_WHITE, C_BLACK, 0xDDCCAA);
    } else {
        _eyeClosed(LEX, EY, C_BLACK);
        _eyeClosed(REX, EY, C_BLACK);
    }
    _mouthFrown(CX, MY - 2);
}

void GotchiDisplay::_drawHungry(bool blink) {
    _background(0xCC6600);
    _drawFaceCircle(C_YELLOW);
    if (!blink) {
        _eyeOpen(LEX, EY, C_WHITE, C_BLACK);
        _eyeOpen(REX, EY, C_WHITE, C_BLACK);
    } else {
        _eyeClosed(LEX, EY, C_BLACK);
        _eyeClosed(REX, EY, C_BLACK);
    }
    _mouthWavy(CX, MY);
    // Gota de baba
    M5.Display.fillCircle(CX + 10, MY + 10, 3, C_WHITE);
}

void GotchiDisplay::_drawThirsty(bool blink) {
    _background(0x0077AA);
    _drawFaceCircle(C_YELLOW);
    if (!blink) {
        _eyeOpen(LEX, EY, C_WHITE, C_BLACK);
        _eyeOpen(REX, EY, C_WHITE, C_BLACK);
    } else {
        _eyeClosed(LEX, EY, C_BLACK);
        _eyeClosed(REX, EY, C_BLACK);
    }
    _mouthTongue(CX, MY);
}

void GotchiDisplay::_drawDizzy() {
    _background(0x330055);
    _drawFaceCircle(0xEEBBFF);
    _eyeDizzy(LEX, EY, C_BLACK);
    _eyeDizzy(REX, EY, C_BLACK);
    // Espirales alrededor de la cabeza
    for (int a = 0; a < 360; a += 45) {
        float rad = a * DEG_TO_RAD;
        int sx = CX + (int)((FR + 6) * cosf(rad));
        int sy = FY + (int)((FR + 6) * sinf(rad));
        M5.Display.fillCircle(sx, sy, 2, C_PURPLE);
    }
    _mouthFrown(CX, MY);
}

void GotchiDisplay::_drawExcited(bool blink) {
    _background(0xFFDD00);
    _drawFaceCircle(C_YELLOW);
    if (!blink) {
        _eyeStar(LEX, EY, C_BLACK);
        _eyeStar(REX, EY, C_BLACK);
    } else {
        _eyeClosed(LEX, EY, C_BLACK);
        _eyeClosed(REX, EY, C_BLACK);
    }
    _mouthOpen(CX, MY);
    // Destellos
    M5.Display.fillCircle(CX - FR - 4, FY - 10, 3, C_WHITE);
    M5.Display.fillCircle(CX + FR + 4, FY - 10, 3, C_WHITE);
}

void GotchiDisplay::_drawScared(bool blink) {
    _background(0x111122);
    _drawFaceCircle(0xEEEECC);
    if (!blink) {
        _eyeWide(LEX, EY, C_WHITE, C_BLACK);
        _eyeWide(REX, EY, C_WHITE, C_BLACK);
    } else {
        _eyeClosed(LEX, EY, C_BLACK);
        _eyeClosed(REX, EY, C_BLACK);
    }
    _mouthOpen(CX, MY + 2);
    _drawSweat(CX + FR - 5, FY - 20);
}

void GotchiDisplay::_drawLaughing() {
    _background(0x99FF44);
    _drawFaceCircle(C_YELLOW);
    _eyeHappy(LEX, EY, C_BLACK);
    _eyeHappy(REX, EY, C_BLACK);
    _mouthGrin(CX, MY);
    // Líneas de carcajada
    M5.Display.drawLine(CX - FR - 8, FY - 5, CX - FR - 2, FY - 2, C_YELLOW);
    M5.Display.drawLine(CX + FR + 2, FY - 5, CX + FR + 8, FY - 2, C_YELLOW);
}

void GotchiDisplay::_drawAngry(bool blink) {
    _background(0xCC0000);
    _drawFaceCircle(0xFFBB88);
    if (!blink) {
        _eyeAngry(LEX, EY, C_WHITE, C_BLACK, true);
        _eyeAngry(REX, EY, C_WHITE, C_BLACK, false);
    } else {
        _eyeClosed(LEX, EY, C_BLACK);
        _eyeClosed(REX, EY, C_BLACK);
    }
    _mouthFrown(CX, MY);
    // Venas en la frente
    M5.Display.drawLine(CX - 8, FY - FR + 8, CX - 4, FY - FR + 14, C_RED);
    M5.Display.drawLine(CX + 4, FY - FR + 8, CX + 8, FY - FR + 14, C_RED);
}

void GotchiDisplay::_drawSad(bool blink) {
    _background(0x1144AA);
    _drawFaceCircle(0xCCDDFF);
    if (!blink) {
        _eyeOpen(LEX, EY, C_WHITE, C_BLACK);
        _eyeOpen(REX, EY, C_WHITE, C_BLACK);
    } else {
        _eyeClosed(LEX, EY, C_BLACK);
        _eyeClosed(REX, EY, C_BLACK);
    }
    _mouthFrown(CX, MY);
    _drawTear(LEX + 2, EY + 12);
    _drawTear(REX + 2, EY + 12);
}

void GotchiDisplay::_drawSleeping() {
    _background(0x050520);
    _drawFaceCircle(0xBBCCEE);
    _eyeClosed(LEX, EY, C_BLACK);
    _eyeClosed(REX, EY, C_BLACK);
    _mouthSmile(CX, MY - 2);
    _drawZzz(CX + 15, FY - FR - 5);
    // Burbuja de ronquido
    M5.Display.drawCircle(CX + 25, FY - FR - 10, 6, C_GRAY);
}
