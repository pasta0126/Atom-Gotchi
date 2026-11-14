// Gotchi de actividad de Claude Code para AtomS3R (128x128).
// El servidor agrega los hooks de Claude en un estado (GET /api/claude) que una tarea
// WiFi consulta cada ~1 s. Override manual: pulsar pantalla = siguiente estado,
// serie '0'-'7' fija estado; el override caduca a los 10 s. 'a' cancela el override.
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include "../src/credentials.h"

enum State : uint8_t { S_IDLE, S_ALERT, S_WORK, S_WAIT, S_DONE, S_ERROR, S_BORED, S_SLEEP, S_COUNT };
static const char* STATE_NAME[S_COUNT] = {"idle", "alert", "work", "wait", "done", "error", "bored", "sleep"};

// Pose continua: todo se interpola hacia el objetivo.
struct Pose {
    float w, h, gap;     // tamaño de ojo y separación
    float lidTop;        // 0..1 párpado superior
    float tilt;          // + enfadado/concentrado, - triste
    float lidBot;        // 0..1 párpado inferior (ojos felices ^ ^)
    float pupil;         // 0..1
    float yoff;          // desplazamiento vertical de la cara
    float r, g, b;       // color del ojo
};

static const Pose POSES[S_COUNT] = {
    //  w    h   gap  lidT  tilt  lidB pupil yoff    r    g    b
    {  34,  46,  20,  0.00,  0.00, 0.00, 0.55,  0,  130, 220, 255},  // idle
    {  38,  58,  16,  0.00,  0.00, 0.00, 0.35, -2,  210, 245, 255},  // alert
    {  36,  40,  18,  0.22,  0.40, 0.00, 0.50,  0,  255, 140,  95},  // work
    {  40,  58,  14,  0.00, -0.20, 0.00, 0.78, -2,  255, 214,  90},  // wait
    {  36,  42,  18,  0.00,  0.00, 0.60, 0.50,  0,  120, 255, 170},  // done
    {  34,  44,  20,  0.18, -0.75, 0.00, 0.45,  6,  165, 150, 255},  // error
    {  36,  38,  20,  0.45,  0.00, 0.00, 0.50,  4,  150, 170, 195},  // bored
    {  36,   6,  20,  0.00,  0.00, 0.00, 0.00,  8,  100, 110, 175},  // sleep
};

static M5Canvas canvas(&M5.Display);
static Pose cur = POSES[S_IDLE];
static State state = S_IDLE;
static uint32_t stateSince = 0;
static uint32_t overrideUntil = 0;          // override manual (debug)
static volatile int8_t remoteState = -1;    // lo escribe la tarea WiFi
static volatile bool wifiUp = false;

static float gazeX = 0, gazeY = 0;          // actual
static float gazeTX = 0, gazeTY = 0;        // objetivo
static uint32_t nextSaccade = 0;
static uint32_t nextBlink = 2000, blinkStart = 0;
static bool blinking = false;

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float approach(float c, float t, float k, float dt) { return c + (t - c) * (1.0f - expf(-k * dt)); }

static void setState(State s) {
    state = s;
    stateSince = millis();
    Serial.printf("state -> %s\n", STATE_NAME[s]);
}

static void updateGaze(uint32_t now, float t) {
    switch (state) {
        case S_IDLE:
        case S_BORED:
            if (now >= nextSaccade) {
                gazeTX = (random(-100, 101)) / 100.0f * (state == S_BORED ? 0.4f : 0.8f);
                gazeTY = (random(-60, 61)) / 100.0f * 0.6f + (state == S_BORED ? 0.3f : 0);
                nextSaccade = now + random(900, 3200);
            }
            break;
        case S_WORK:  // lectura: barrido rápido izquierda-derecha con deriva vertical
            gazeTX = sinf(t * 3.2f) * 0.85f;
            gazeTY = sinf(t * 0.9f) * 0.25f + 0.1f;
            break;
        case S_WAIT:  gazeTX = 0; gazeTY = -0.05f; break;  // te mira a ti
        case S_ALERT: gazeTX = 0; gazeTY = 0; break;
        case S_DONE:  gazeTX = 0; gazeTY = -0.1f; break;
        case S_ERROR: gazeTX = 0; gazeTY = 0.7f; break;
        case S_SLEEP: gazeTX = 0; gazeTY = 0.5f; break;
        default: break;
    }
}

static float blinkFactor(uint32_t now) {
    if (state == S_SLEEP) return 0;
    if (!blinking && now >= nextBlink) { blinking = true; blinkStart = now; }
    if (!blinking) return 0;
    float p = (now - blinkStart) / 150.0f;   // 150 ms
    if (p >= 1) { blinking = false; nextBlink = now + random(2000, 6000); return 0; }
    return p < 0.5f ? p * 2 : (1 - p) * 2;
}

static uint16_t rgb(float r, float g, float b) {
    return M5.Display.color565((uint8_t)constrain(r, 0, 255), (uint8_t)constrain(g, 0, 255), (uint8_t)constrain(b, 0, 255));
}

static void drawEye(int idx, float cx, float cy, float w, float h, const Pose& p, float t, uint16_t bg) {
    const int cxi = (int)cx, cyi = (int)cy;
    const int x0 = cxi - (int)(w / 2), top = cyi - (int)(h / 2);
    canvas.setClipRect(x0 - 6, 0, (int)w + 12, 128);

    // halo suave
    uint16_t glow = rgb(p.r * 0.16f + 6, p.g * 0.16f + 8, p.b * 0.16f + 18);
    int rad = (int)(fminf(w, h) / 2);
    canvas.fillRoundRect(x0 - 4, top - 4, (int)w + 8, (int)h + 8, rad + 4, glow);
    // cuerpo
    canvas.fillRoundRect(x0, top, (int)w, (int)h, rad, rgb(p.r, p.g, p.b));

    // pupila + brillos
    if (p.pupil > 0.05f && h > 14) {
        float pr = p.pupil * fminf(w, h) * 0.42f;
        float px = cx + gazeX * (w * 0.5f - pr) * 0.8f;
        float py = cy + gazeY * (h * 0.5f - pr) * 0.8f;
        canvas.fillCircle((int)px, (int)py, (int)pr, rgb(14, 16, 30));
        canvas.fillCircle((int)(px - pr * 0.35f), (int)(py - pr * 0.4f), (int)fmaxf(2, pr * 0.28f), TFT_WHITE);
        canvas.fillCircle((int)(px + pr * 0.35f), (int)(py + pr * 0.35f), (int)fmaxf(1, pr * 0.12f), TFT_WHITE);
    }

    // párpado superior con inclinación (lado interno: izquierdo→derecha, derecho→izquierda)
    float inner = (idx == 0) ? 1.0f : -1.0f;
    float covIn  = clamp01(p.lidTop + p.tilt * 0.5f);
    float covOut = clamp01(p.lidTop - p.tilt * 0.5f);
    if (covIn > 0.01f || covOut > 0.01f) {
        float xl = x0 - 6, xr = x0 + w + 6;
        float yIn  = top + covIn  * h;
        float yOut = top + covOut * h;
        float yl = (inner > 0) ? yOut : yIn;   // borde izquierdo
        float yr = (inner > 0) ? yIn  : yOut;  // borde derecho
        float yt = top - 8;
        canvas.fillTriangle(xl, yt, xr, yt, xl, yl, bg);
        canvas.fillTriangle(xr, yt, xr, yr, xl, yl, bg);
    }
    // párpado inferior curvo (felicidad ^ ^)
    if (p.lidBot > 0.02f) {
        float R = w * 0.85f;
        canvas.fillCircle(cxi, (int)(cy + h / 2 + R - p.lidBot * h), (int)R, bg);
    }
    canvas.clearClipRect();
}

static void drawStar(int x, int y, int s, uint16_t c) {
    canvas.drawFastHLine(x - s, y, 2 * s + 1, c);
    canvas.drawFastVLine(x, y - s, 2 * s + 1, c);
    canvas.drawLine(x - s / 2, y - s / 2, x + s / 2, y + s / 2, c);
    canvas.drawLine(x - s / 2, y + s / 2, x + s / 2, y - s / 2, c);
}

static void drawEffects(float t, const Pose& p) {
    switch (state) {
        case S_WORK:  // puntos "pensando"
            for (int i = 0; i < 3; i++) {
                float ph = fmodf(t * 2.2f - i * 0.25f, 1.0f);
                int r = 2 + (int)(2.5f * sinf(ph * 3.14159f));
                canvas.fillCircle(52 + i * 12, 112, r, rgb(255, 160, 120));
            }
            break;
        case S_WAIT: {  // signo de atención que rebota
            int by = 18 + (int)(3 * sinf(t * 7));
            canvas.setTextColor(rgb(255, 230, 120));
            canvas.setTextSize(3);
            canvas.drawString("?", 108, by);
            break;
        }
        case S_DONE:  // chispas
            for (int i = 0; i < 4; i++) {
                float ph = fmodf(t * 1.3f + i * 0.25f, 1.0f);
                int s = (int)(5 * sinf(ph * 3.14159f));
                if (s < 1) continue;
                static const int8_t sx[4] = {14, 112, 22, 104}, sy[4] = {20, 26, 100, 96};
                drawStar(sx[i], sy[i], s, rgb(255, 255, 200));
            }
            break;
        case S_ERROR: {  // lágrima
            float ph = fmodf(t * 0.8f, 1.0f);
            int ty = 76 + (int)(ph * 36);
            if (ph < 0.9f) canvas.fillCircle(38, ty, 3, rgb(140, 190, 255));
            break;
        }
        case S_SLEEP:  // zzz
            for (int i = 0; i < 3; i++) {
                float ph = fmodf(t * 0.5f + i * 0.33f, 1.0f);
                canvas.setTextColor(rgb(150 * (1 - ph) + 20, 160 * (1 - ph) + 20, 230 * (1 - ph) + 30));
                canvas.setTextSize(1 + i);
                canvas.drawString("z", 84 + (int)(ph * 26), 56 - (int)(ph * 34));
            }
            break;
        default: break;
    }
}

static int parseState(const String& b) {
    for (int i = 0; i < S_COUNT; i++) if (b == STATE_NAME[i]) return i;
    return -1;
}

// Tarea WiFi (core 0): no bloquea el render. Si el servidor no responde 5 veces seguidas,
// el gotchi pasa a "bored" en vez de quedarse congelado en el último estado.
static void netTask(void*) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) Serial.printf("scan: '%s' ch%d rssi%d auth%d\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i));
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int fails = 0, waited = 0;
    // HTTPS (Traefik) con conexión reutilizada: evita un handshake TLS por petición.
    // setInsecure(): no valida el certificado (suficiente para un indicador de actividad).
    const bool https = String(API_URL).startsWith("https");
    WiFiClientSecure secure; secure.setInsecure();
    WiFiClient plain;
    HTTPClient http;
    http.setReuse(true);
    for (;;) {
        wifiUp = WiFi.status() == WL_CONNECTED;
        if (!wifiUp) {
            if (++waited % 3 == 0) Serial.printf("wifi status=%d (ssid len=%d)\n", (int)WiFi.status(), (int)strlen(WIFI_SSID));
            vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        http.setConnectTimeout(3000);
        http.setTimeout(2500);
        const String url = String(API_URL) + "/api/claude";
        if (https) http.begin(secure, url); else http.begin(plain, url);
        int code = http.GET();
        if (code == 200) {
            String b = http.getString(); b.trim();
            int s = parseState(b);
            if (s >= 0) { remoteState = s; fails = 0; }
        } else if (++fails >= 5) {
            remoteState = S_BORED;
        }
        if (code <= 0) http.end();   // conexión rota: forzar reconexión; si no, se reutiliza
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static uint32_t lastFrame = 0, accRender = 0, accPush = 0, frames = 0, lastReport = 0;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setBrightness(140);
    canvas.setColorDepth(16);
    canvas.createSprite(128, 128);
    randomSeed(esp_random());
    setState(S_IDLE);
    xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
    lastFrame = lastReport = millis();
}

void loop() {
    M5.update();
    uint32_t now = millis();
    float dt = (now - lastFrame) / 1000.0f;
    lastFrame = now;
    float t = now / 1000.0f;

    if (M5.BtnA.wasClicked()) { overrideUntil = now + 10000; setState((State)((state + 1) % S_COUNT)); }
    while (Serial.available()) {
        int c = Serial.read();
        if (c >= '0' && c < '0' + S_COUNT) { overrideUntil = now + 10000; setState((State)(c - '0')); }
        else if (c == 'a') { overrideUntil = 0; }
    }
    if (now >= overrideUntil) {
        int rs = remoteState;
        if (rs >= 0 && rs != state) setState((State)rs);
    }

    // interpolar pose
    const Pose& tg = POSES[state];
    float* cp = (float*)&cur; const float* tp = (const float*)&tg;
    for (size_t i = 0; i < sizeof(Pose) / sizeof(float); i++) cp[i] = approach(cp[i], tp[i], 9.0f, dt);

    updateGaze(now, t);
    gazeX = approach(gazeX, gazeTX, state == S_WORK ? 30.0f : 16.0f, dt);
    gazeY = approach(gazeY, gazeTY, 16.0f, dt);

    uint32_t r0 = micros();
    uint16_t bg = rgb(8, 10, 22);
    canvas.fillScreen(bg);

    float breathe = 1.0f + 0.025f * sinf(t * 2.0f);
    float b = blinkFactor(now);
    float h = fmaxf(4.0f, cur.h * breathe * (1.0f - b));
    float cy = 62 + cur.yoff;
    float dx = (cur.w + cur.gap) / 2.0f;
    drawEye(0, 64 - dx, cy, cur.w, h, cur, t, bg);
    drawEye(1, 64 + dx, cy, cur.w, h, cur, t, bg);
    drawEffects(t, cur);
    uint32_t r1 = micros();
    canvas.pushSprite(0, 0);
    M5.Display.waitDisplay();
    uint32_t r2 = micros();

    accRender += r1 - r0; accPush += r2 - r1; frames++;
    if (now - lastReport >= 2000) {
        Serial.printf("fps=%.1f render=%.1fms push=%.1fms heap=%u wifi=%d state=%s\n", frames * 1000.0f / (now - lastReport),
                      accRender / 1000.0f / frames, accPush / 1000.0f / frames, ESP.getFreeHeap(), (int)wifiUp, STATE_NAME[state]);
        accRender = accPush = frames = 0; lastReport = now;
    }
    // sin límite artificial: medimos el techo; limitar a 60 fps
    int32_t spare = 16 - (int32_t)((micros() - r0) / 1000);
    if (spare > 0) delay(spare);
}
