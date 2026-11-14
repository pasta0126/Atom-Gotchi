// Gotchi de actividad de Claude Code para AtomS3R (128x128).
//
// El servidor agrega los hooks de Claude en un estado (GET /api/claude, líneas clave=valor)
// que una tarea WiFi consulta cada ~1 s. Encima del estado hay:
//   - personalidad curiosa: micro-animaciones aleatorias en idle/bored (behaviors)
//   - reacciones al IMU (agitar, golpecito, caída) y al botón
//   - efectos según la herramienta que Claude usa (leer, buscar, editar, bash, web, subagente)
//   - aviso de "tarea terminada" con el nombre del proyecto: borde que respira hasta pulsar el botón
//   - fondo de color según el estado (sin sonido ni destellos bruscos)
//
// Debug por serie: '0'-'7' fija estado (10 s), 'a' cancela override, 'T' cicla herramienta,
// 'd' simula tarea terminada, 'k' mareo, 'h' caricia, 's' susto, 'b' siguiente micro-animación.
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <math.h>
#include "credentials.h"

#ifndef OTA_PASS
#define OTA_PASS ""
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Estados y poses
// ─────────────────────────────────────────────────────────────────────────────
enum State : uint8_t { S_IDLE, S_ALERT, S_WORK, S_WAIT, S_DONE, S_ERROR, S_BORED, S_SLEEP, S_COUNT };
static const char* STATE_NAME[S_COUNT] = {"idle", "alert", "work", "wait", "done", "error", "bored", "sleep"};

enum Tool : uint8_t { T_NONE, T_READ, T_SEARCH, T_EDIT, T_BASH, T_WEB, T_AGENT, T_OTHER, T_COUNT };
static const char* TOOL_NAME[T_COUNT] = {"", "read", "search", "edit", "bash", "web", "agent", "other"};

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
    {  36,  40,  18,  0.10,  0.04, 0.12, 0.50,  0,  255, 150, 105},  // work (concentrado, no enfadado)
    {  40,  58,  14,  0.00, -0.20, 0.00, 0.78, -2,  255, 214,  90},  // wait
    {  36,  42,  18,  0.00,  0.00, 0.60, 0.50,  0,  120, 255, 170},  // done
    {  34,  44,  20,  0.18, -0.75, 0.00, 0.45,  6,  165, 150, 255},  // error
    {  36,  38,  20,  0.45,  0.00, 0.00, 0.50,  4,  150, 170, 195},  // bored
    {  36,   6,  20,  0.00,  0.00, 0.00, 0.00,  8,  100, 110, 175},  // sleep
};

// ─────────────────────────────────────────────────────────────────────────────
// Datos remotos (los escribe la tarea WiFi en core 0, los lee el loop en core 1)
// ─────────────────────────────────────────────────────────────────────────────
#define MAX_SESS 6
struct Sess { char id[8]; char name[24]; uint8_t state; uint8_t tool; long done; };
struct Remote {
    int8_t  state = -1;       // -1 = sin datos todavía
    uint8_t tool = T_NONE;
    int8_t  mood = 0;         // -2..2
    uint8_t nList = 0;
    Sess    list[MAX_SESS];
};
static Remote remote;   // escrito por netTask
static Remote R;        // copia del frame actual (loop)
static portMUX_TYPE remoteMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool wifiUp = false;
static volatile int otaPct = -1;

static int indexOf(const char* const* names, int n, const String& v) {
    for (int i = 0; i < n; i++) if (v == names[i]) return i;
    return -1;
}

static void parseBody(const String& body, Remote& r) {
    if (body.indexOf('=') < 0) {            // formato antiguo: una sola palabra
        int s = indexOf(STATE_NAME, S_COUNT, body);
        if (s >= 0) r.state = s;
        return;
    }
    r.nList = 0;
    int pos = 0;
    while (pos < (int)body.length()) {
        int nl = body.indexOf('\n', pos);
        if (nl < 0) nl = body.length();
        String line = body.substring(pos, nl);
        pos = nl + 1;
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        String k = line.substring(0, eq), v = line.substring(eq + 1);
        v.trim();
        if (k == "state") { int s = indexOf(STATE_NAME, S_COUNT, v); if (s >= 0) r.state = s; }
        else if (k == "tool") { int t = indexOf(TOOL_NAME, T_COUNT, v); r.tool = t >= 0 ? t : T_NONE; }
        else if (k == "mood") r.mood = constrain(v.toInt(), -2, 2);
        else if (k == "s" && r.nList < MAX_SESS) {   // id|nombre|estado|tareas|herramienta
            Sess& x = r.list[r.nList];
            int a = v.indexOf('|'), b = v.indexOf('|', a + 1), c = v.indexOf('|', b + 1), d = v.indexOf('|', c + 1);
            if (a < 0 || b < 0 || c < 0) continue;
            strlcpy(x.id, v.substring(0, a).c_str(), sizeof(x.id));
            strlcpy(x.name, v.substring(a + 1, b).c_str(), sizeof(x.name));
            int st = indexOf(STATE_NAME, S_COUNT, v.substring(b + 1, c)); x.state = st >= 0 ? st : S_IDLE;
            x.done = v.substring(c + 1, d < 0 ? v.length() : d).toInt();
            int tl = d < 0 ? -1 : indexOf(TOOL_NAME, T_COUNT, v.substring(d + 1)); x.tool = tl >= 0 ? tl : T_NONE;
            r.nList++;
        }
    }
}

// Tarea WiFi: no bloquea el render. Si el servidor no responde 5 veces seguidas, el gotchi
// pasa a "bored" en vez de quedarse congelado en el último estado.
static void netTask(void*) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int fails = 0, waited = 0;
    // HTTPS (Traefik) con conexión reutilizada: evita un handshake TLS por petición.
    // setInsecure(): no valida el certificado (suficiente para un indicador de actividad).
    const bool https = String(API_URL).startsWith("https");
    WiFiClientSecure secure; secure.setInsecure();
    WiFiClient plain;
    HTTPClient http;
    http.setReuse(true);
    bool otaStarted = false;
    for (;;) {
        wifiUp = WiFi.status() == WL_CONNECTED;
        if (!wifiUp) {
            if (++waited % 3 == 0) Serial.printf("wifi status=%d\n", (int)WiFi.status());
            vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        if (!otaStarted) {   // OTA: flashear por WiFi sin cable (protegido con OTA_PASS)
            ArduinoOTA.setHostname("atom-gotchi");
            if (OTA_PASS[0]) ArduinoOTA.setPassword(OTA_PASS);
            ArduinoOTA.onStart([]() { otaPct = 0; });
            ArduinoOTA.onProgress([](unsigned int p, unsigned int t) { otaPct = (int)(p * 100 / t); });
            ArduinoOTA.onEnd([]() { otaPct = 100; });
            ArduinoOTA.onError([](ota_error_t) { otaPct = -1; });
            ArduinoOTA.begin();
            configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");   // hora local
            otaStarted = true;
        }
        http.setConnectTimeout(3000);
        http.setTimeout(2500);
        const String url = String(API_URL) + "/api/claude";
        if (https) http.begin(secure, url); else http.begin(plain, url);
        int code = http.GET();
        if (code == 200) {
            Remote r;
            portENTER_CRITICAL(&remoteMux); r = remote; portEXIT_CRITICAL(&remoteMux);
            r.tool = T_NONE;
            parseBody(http.getString(), r);
            portENTER_CRITICAL(&remoteMux); remote = r; portEXIT_CRITICAL(&remoteMux);
            fails = 0;
        } else if (++fails >= 5) {
            portENTER_CRITICAL(&remoteMux); remote.state = S_BORED; remote.tool = T_NONE; portEXIT_CRITICAL(&remoteMux);
        }
        if (code <= 0) http.end();   // conexión rota: forzar reconexión; si no, se reutiliza
        for (int i = 0; i < 10; i++) { ArduinoOTA.handle(); vTaskDelay(pdMS_TO_TICKS(100)); }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Estado de animación
// ─────────────────────────────────────────────────────────────────────────────
static M5Canvas canvas(&M5.Display);
static Pose cur = POSES[S_IDLE];
static State state = S_IDLE;
enum UI : uint8_t { UI_FACE, UI_LIST };
static UI ui = UI_FACE;
static Tool curTool = T_NONE;
static int mood = 0;
static uint32_t stateSince = 0;
static uint32_t overrideUntil = 0;       // override manual (debug)
static int toolOverride = -1;

static float bgCur[3] = {8, 10, 22};
static void bgTarget(float* o, uint32_t now, bool pend) {
    static const float B[S_COUNT][3] = {
        {8, 10, 22},    // idle: azul noche
        {10, 15, 30},   // alert
        {28, 14, 10},   // work: cálido
        {32, 24, 6},    // wait: ámbar
        {6, 30, 18},    // done: verde
        {20, 10, 34},   // error: violeta
        {15, 15, 20},   // bored: gris
        {3, 4, 10},     // sleep: casi negro
    };
    for (int i = 0; i < 3; i++) o[i] = B[state][i];
    if (pend) {   // pendiente de ver: el fondo respira hacia verde
        float k = 0.5f - 0.5f * cosf(now / 700.0f);
        static const float G[3] = {6, 38, 22};
        for (int i = 0; i < 3; i++) o[i] += (G[i] - o[i]) * k * 0.8f;
    }
}

static float gazeX = 0, gazeY = 0, gazeTX = 0, gazeTY = 0;
static uint32_t nextSaccade = 0;
static uint32_t nextBlink = 2000, blinkStart = 0;
static bool blinking = false;

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float approach(float c, float t, float k, float dt) { return c + (t - c) * (1.0f - expf(-k * dt)); }
static inline float envelope(float p) { return sinf(3.14159265f * clamp01(p)); }   // 0→1→0

static void setState(State s) {
    state = s;
    stateSince = millis();
    Serial.printf("state -> %s\n", STATE_NAME[s]);
}

// Efectos por-frame que modifican la pose sin tocar su interpolación
struct Fx {
    float scale[2] = {1, 1};      // multiplicador de altura por ojo
    float lidAdd[2] = {0, 0};     // párpado superior extra por ojo
    float dy[2] = {0, 0};         // desplazamiento vertical por ojo
    float ox = 0, oy = 0;         // desplazamiento de toda la cara
    float pupilMul = 1;
    float botAdd = 0;             // párpado inferior extra (sonrisa ^ ^)
    bool  gazeOv = false;  float gx = 0, gy = 0;
    bool  dizzy = false, sparkle = false;
    float yawn = 0;               // 0..1 apertura de la boca
    float blush = 0;              // 0..1 mejillas sonrojadas
};
static Fx fxRaw;
static float fxSm[10];   // suavizado de: scale0,1 lid0,1 dy0,1 ox oy pupilMul botAdd

// Micro-animaciones (personalidad curiosa)
enum Beh : uint8_t { B_NONE, B_LOOK, B_TILT, B_WINK, B_DBLINK, B_INSPECT, B_BUTTERFLY, B_YAWN, B_WIGGLE, B_SPARKLE, B_LAUGH, B_SHY, B_EYEROLL, B_NOD, B_COUNT };
static const char* BEH_NAME[B_COUNT] = {"none", "look", "tilt", "wink", "dblink", "inspect", "butterfly", "yawn", "wiggle", "sparkle", "laugh", "shy", "eyeroll", "nod"};
static const uint16_t BEH_MS[B_COUNT] = {0, 2800, 1900, 500, 420, 1700, 3200, 2300, 900, 1800, 1400, 2200, 1600, 2600};
static Beh beh = B_NONE;
static uint32_t behStart = 0, nextBeh = 3000;
static float behA = 0, behB = 0, behC = 0, behD = 0;   // parámetros aleatorios por ejecución
static int behForce = -1;
static int behCycle = 0;

static float frand(float a, float b) { return a + (b - a) * (random(0, 10001) / 10000.0f); }

static void startBehavior(Beh b) {
    beh = b; behStart = millis();
    behA = frand(-1, 1); behB = frand(-1, 1); behC = frand(-1, 1); behD = frand(-1, 1);
    Serial.printf("behavior: %s\n", BEH_NAME[b]);
}

static Beh pickBehavior() {
    // pesos según estado y humor; "curioso": mira alrededor, ladea la cabeza, persigue luciérnagas
    struct W { Beh b; int w; };
    W idle[] = {{B_LOOK, 4}, {B_TILT, 4}, {B_BUTTERFLY, 3}, {B_INSPECT, 3}, {B_WINK, 1}, {B_DBLINK, 2}, {B_WIGGLE, 1},
                {B_SPARKLE, mood >= 1 ? 4 : 0}, {B_LAUGH, mood >= 1 ? 3 : 1}, {B_SHY, 1}, {B_EYEROLL, mood <= -1 ? 3 : 1}};
    W bored[] = {{B_LOOK, 2}, {B_YAWN, 4}, {B_DBLINK, 1}, {B_TILT, 1}, {B_NOD, 4}, {B_EYEROLL, 1}};
    W* t = state == S_BORED ? bored : idle;
    int n = state == S_BORED ? 6 : 11, total = 0;
    for (int i = 0; i < n; i++) total += t[i].w;
    int r = random(0, total);
    for (int i = 0; i < n; i++) { if ((r -= t[i].w) < 0) return t[i].b; }
    return B_LOOK;
}

static void computeBehavior(Fx& fx, uint32_t now, float t) {
    if (state != S_IDLE && state != S_BORED) { beh = B_NONE; nextBeh = now + 2500; return; }
    if (beh == B_NONE) {
        if (behForce >= 0) { startBehavior((Beh)behForce); behForce = -1; }
        else if (now >= nextBeh) startBehavior(pickBehavior());
        else return;
    }
    float p = (now - behStart) / (float)BEH_MS[beh];
    if (p >= 1) { beh = B_NONE; nextBeh = now + random(2200, 6500); return; }
    float e = envelope(p);
    int side = behA > 0 ? 1 : 0;
    switch (beh) {
        case B_LOOK: {   // 3 puntos de interés consecutivos
            static const float px[3] = {0.9f, -0.8f, 0.2f}, py[3] = {-0.2f, 0.1f, -0.7f};
            int i = (int)(p * 3); if (i > 2) i = 2;
            float sx = behA > 0 ? 1 : -1;
            fx.gazeOv = true; fx.gx = px[i] * sx; fx.gy = py[i] + behB * 0.2f;
            break; }
        case B_TILT:     // ladea la "cabeza": un ojo más grande y más alto, el otro entrecerrado
            fx.scale[side] = 1 + 0.18f * e;  fx.scale[1 - side] = 1 - 0.12f * e;
            fx.dy[side] = -4 * e;            fx.dy[1 - side] = 3 * e;
            fx.lidAdd[1 - side] = 0.15f * e;
            fx.gazeOv = true; fx.gx = (side ? 1 : -1) * 0.6f; fx.gy = -0.5f;
            break;
        case B_WINK:
            fx.scale[side] = 1 - 0.93f * e;  fx.botAdd = 0.1f * e; fx.pupilMul = 1.1f;
            break;
        case B_DBLINK: {
            float k = fabsf(sinf(6.2831853f * p));
            fx.scale[0] = fx.scale[1] = 1 - 0.93f * k;
            break; }
        case B_INSPECT:  // "¿qué es eso?": entrecierra y se acerca a una esquina
            fx.lidAdd[0] = fx.lidAdd[1] = 0.3f * e; fx.pupilMul = 1 - 0.3f * e;
            fx.gazeOv = true; fx.gx = (behA > 0 ? 1 : -1) * 0.9f; fx.gy = behB * 0.5f;
            fx.ox = (behA > 0 ? 3 : -3) * e;
            break;
        case B_BUTTERFLY:  // sigue una luciérnaga imaginaria
            fx.gazeOv = true;
            fx.gx = sinf(6.2831853f * p * 1.5f + behA * 3) * 0.85f;
            fx.gy = sinf(6.2831853f * p * 2.3f + behB * 3) * 0.5f;
            fx.pupilMul = 1.25f;
            break;
        case B_YAWN:
            fx.scale[0] = fx.scale[1] = 1 - 0.75f * e; fx.oy = -2 * e; fx.yawn = e;
            fx.gazeOv = true; fx.gx = 0; fx.gy = -0.3f;
            break;
        case B_WIGGLE:
            fx.ox = sinf(t * 55) * 2.5f * (1 - p); fx.pupilMul = 1.2f; fx.scale[0] = fx.scale[1] = 1 + 0.08f * e;
            break;
        case B_SPARKLE:
            fx.sparkle = e > 0.1f; fx.pupilMul = 1.2f; fx.botAdd = 0.2f * e;
            break;
        case B_LAUGH:   // risita: ojos ^ ^ que tiemblan
            fx.botAdd = 0.62f * fminf(1.0f, e * 2.5f); fx.ox = sinf(t * 38) * 2.0f * e; fx.oy = -1.5f * e;
            break;
        case B_SHY:     // tímido: aparta la mirada, se encoge y se sonroja
            fx.gazeOv = true; fx.gx = (behA > 0 ? 1 : -1) * 0.9f; fx.gy = 0.55f;
            fx.scale[0] = fx.scale[1] = 1 - 0.15f * e; fx.botAdd = 0.2f * e; fx.blush = e; fx.pupilMul = 0.9f;
            break;
        case B_EYEROLL: {   // pone los ojos en blanco (una vuelta)
            float a = 6.2831853f * p * (behA > 0 ? 1 : -1);
            fx.gazeOv = true; fx.gx = sinf(a) * 0.85f; fx.gy = -cosf(a) * 0.7f;
            fx.lidAdd[0] = fx.lidAdd[1] = 0.12f * e;
            break; }
        case B_NOD:     // cabecea de sueño: se le cierran los ojos y se sobresalta
            if (p < 0.62f) { float q = p / 0.62f; fx.scale[0] = fx.scale[1] = 1 - 0.7f * q * q; fx.oy = 3 * q; fx.gazeOv = true; fx.gx = 0; fx.gy = 0.6f; }
            else { float q = (p - 0.62f) / 0.38f; fx.scale[0] = fx.scale[1] = 1 + 0.15f * (1 - q); fx.pupilMul = 0.8f; }
            break;
        default: break;
    }
}

// Reacciones físicas (IMU / botón): se superponen al estado
enum React : uint8_t { R_NONE, R_DIZZY, R_PET, R_STARTLE, R_GREET };
static const uint16_t REACT_MS[5] = {0, 3000, 2200, 1200, 2800};
static React react = R_NONE;
static uint32_t reactStart = 0;

static void startReaction(React r) {
    react = r; reactStart = millis();
    Serial.printf("reaction: %d\n", (int)r);
}

static void computeReaction(Fx& fx, uint32_t now, float t) {
    if (react == R_NONE) return;
    float p = (now - reactStart) / (float)REACT_MS[react];
    if (p >= 1) { react = R_NONE; return; }
    float e = envelope(p);
    switch (react) {
        case R_DIZZY:
            fx.dizzy = true;
            fx.ox = sinf(t * 14) * 3 * (1 - p); fx.oy = cosf(t * 11) * 2 * (1 - p);
            fx.gazeOv = true; fx.gx = cosf(t * 9) * 0.8f; fx.gy = sinf(t * 9) * 0.8f;
            break;
        case R_PET:
            fx.botAdd = 0.65f * fminf(1.0f, e * 2.5f); fx.pupilMul = 1.15f; fx.oy = 2 * e; fx.blush = e;
            fx.gazeOv = true; fx.gx = 0; fx.gy = 0;
            break;
        case R_GREET:   // buenos días: ojos felices con destellos
            fx.sparkle = e > 0.15f; fx.botAdd = 0.45f * e; fx.pupilMul = 1.15f; fx.oy = -1.5f * e;
            fx.gazeOv = true; fx.gx = 0; fx.gy = -0.2f;
            break;
        case R_STARTLE:
            fx.scale[0] = fx.scale[1] = 1 + 0.25f * (1 - p); fx.pupilMul = 0.55f; fx.dy[0] = fx.dy[1] = -3 * (1 - p);
            fx.gazeOv = true; fx.gx = 0; fx.gy = 0;
            break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mirada
// ─────────────────────────────────────────────────────────────────────────────
static void updateGaze(uint32_t now, float t, const Fx& fx) {
    if (fx.gazeOv) { gazeTX = fx.gx; gazeTY = fx.gy; return; }
    switch (state) {
        case S_IDLE:
        case S_BORED:
            if (now >= nextSaccade) {
                gazeTX = frand(-1, 1) * (state == S_BORED ? 0.4f : 0.8f);
                gazeTY = frand(-0.36f, 0.36f) + (state == S_BORED ? 0.3f : 0);
                nextSaccade = now + random(900, 3200);
            }
            break;
        case S_WORK:
            switch (curTool) {
                case T_READ: {   // lectura: barrido en "zig-zag" bajando por la página
                    float ph = fmodf(t / 2.2f, 1.0f);
                    gazeTX = (ph < 0.5f ? ph * 2 : 2 - ph * 2) * 1.7f - 0.85f;
                    gazeTY = -0.2f + 0.5f * fmodf(t / 6.6f, 1.0f);
                    break; }
                case T_SEARCH: gazeTX = cosf(t * 2.2f) * 0.85f; gazeTY = sinf(t * 2.2f) * 0.5f; break;
                case T_EDIT:   gazeTX = sinf(t * 9) * 0.12f; gazeTY = 0.55f; break;
                case T_BASH:   gazeTX = sinf(t * 2.5f) * 0.5f; gazeTY = 0.15f; break;
                case T_WEB:    gazeTX = cosf(t * 1.4f) * 0.8f; gazeTY = sinf(t * 1.4f) * 0.45f - 0.1f; break;
                case T_AGENT:  gazeTX = sinf(t * 5) * 0.2f; gazeTY = sinf(t * 7) * 0.12f; break;
                default:       gazeTX = sinf(t * 3.2f) * 0.85f; gazeTY = sinf(t * 0.9f) * 0.25f + 0.1f; break;
            }
            break;
        case S_WAIT:  gazeTX = 0; gazeTY = -0.05f; break;   // te mira a ti
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

// Pose objetivo = pose del estado + ajustes por herramienta y por humor
static Pose targetPose() {
    Pose p = POSES[state];
    if (state == S_WORK) {
        switch (curTool) {
            case T_READ:   p.lidTop = 0.08f; p.tilt = 0.00f; p.h = 42; break;
            case T_SEARCH: p.lidTop = 0.00f; p.tilt = 0.00f; p.lidBot = 0.0f; p.h = 46; p.w = 38; p.pupil = 0.45f; break;
            case T_EDIT:   p.lidTop = 0.20f; p.tilt = 0.05f; p.h = 38; break;
            case T_BASH:   p.lidTop = 0.18f; p.tilt = 0.06f; p.r = 110; p.g = 255; p.b = 150; break;
            case T_WEB:    p.lidTop = 0.05f; p.h = 44; p.r = 110; p.g = 200; p.b = 255; break;
            case T_AGENT:  p.w = 40; p.h = 46; p.lidTop = 0.05f; p.r = 200; p.g = 150; p.b = 255; break;
            default: break;
        }
    } else if (state == S_IDLE || state == S_BORED) {
        if (mood >= 1)      { p.lidBot += 0.14f * mood; p.r += 25; p.g += 10; }          // contento: sonrisa suave
        else if (mood <= -1) { p.lidTop += 0.10f * -mood; p.tilt -= 0.25f * -mood; p.r *= 0.8f; p.g *= 0.8f; }   // alicaído
    }
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dibujo
// ─────────────────────────────────────────────────────────────────────────────
static uint16_t rgb(float r, float g, float b) {
    return M5.Display.color565((uint8_t)constrain(r, 0, 255), (uint8_t)constrain(g, 0, 255), (uint8_t)constrain(b, 0, 255));
}

static void drawStar(int x, int y, int s, uint16_t c) {
    canvas.drawFastHLine(x - s, y, 2 * s + 1, c);
    canvas.drawFastVLine(x, y - s, 2 * s + 1, c);
    canvas.drawLine(x - s / 2, y - s / 2, x + s / 2, y + s / 2, c);
    canvas.drawLine(x - s / 2, y + s / 2, x + s / 2, y - s / 2, c);
}

static void drawHeart(int x, int y, int s, uint16_t c) {
    canvas.fillCircle(x - s / 2, y, s / 2 + 1, c);
    canvas.fillCircle(x + s / 2, y, s / 2 + 1, c);
    canvas.fillTriangle(x - s - 1, y + 1, x + s + 1, y + 1, x, y + s + 2, c);
}

static void drawEye(int idx, float cx, float cy, float w, float h, const Pose& p, float lidTopExtra, float botExtra,
                    float pupilMul, bool dizzy, bool sparkle, float t, uint16_t bg) {
    const int cxi = (int)cx, cyi = (int)cy;
    const int x0 = cxi - (int)(w / 2), top = cyi - (int)(h / 2);
    canvas.setClipRect(x0 - 6, 0, (int)w + 12, 128);

    // halo suave + cuerpo
    uint16_t glow = rgb(p.r * 0.16f + 6, p.g * 0.16f + 8, p.b * 0.16f + 18);
    int rad = (int)(fminf(w, h) / 2);
    canvas.fillRoundRect(x0 - 4, top - 4, (int)w + 8, (int)h + 8, rad + 4, glow);
    canvas.fillRoundRect(x0, top, (int)w, (int)h, rad, rgb(p.r, p.g, p.b));

    // pupila + brillos
    float pup = p.pupil * pupilMul;
    if (pup > 0.05f && h > 14) {
        float pr = pup * fminf(w, h) * 0.42f;
        float px = cx + gazeX * (w * 0.5f - pr) * 0.8f;
        float py = cy + gazeY * (h * 0.5f - pr) * 0.8f;
        canvas.fillCircle((int)px, (int)py, (int)pr, rgb(14, 16, 30));
        if (dizzy) {   // espiral
            float a0 = t * 9 * (idx ? -1 : 1);
            int lx = (int)px, ly = (int)py;
            for (float a = 0; a < 12.0f; a += 0.35f) {
                float r = a * 0.85f;
                int nx = (int)(px + cosf(a + a0) * r), ny = (int)(py + sinf(a + a0) * r);
                canvas.drawLine(lx, ly, nx, ny, rgb(p.r, p.g, p.b));
                lx = nx; ly = ny;
            }
        } else if (sparkle) {
            drawStar((int)px, (int)py, (int)fmaxf(3, pr * 0.8f + 1.5f * sinf(t * 9)), TFT_WHITE);
        } else {
            canvas.fillCircle((int)(px - pr * 0.35f), (int)(py - pr * 0.4f), (int)fmaxf(2, pr * 0.28f), TFT_WHITE);
            canvas.fillCircle((int)(px + pr * 0.35f), (int)(py + pr * 0.35f), (int)fmaxf(1, pr * 0.12f), TFT_WHITE);
        }
    }

    // párpado superior con inclinación (lado interno: ojo 0 → derecha, ojo 1 → izquierda)
    float inner = (idx == 0) ? 1.0f : -1.0f;
    float lt = clamp01(p.lidTop + lidTopExtra);
    float covIn  = clamp01(lt + p.tilt * 0.5f);
    float covOut = clamp01(lt - p.tilt * 0.5f);
    if (covIn > 0.01f || covOut > 0.01f) {
        float xl = x0 - 6, xr = x0 + w + 6;
        float yIn  = top + covIn  * h;
        float yOut = top + covOut * h;
        float yl = (inner > 0) ? yOut : yIn;
        float yr = (inner > 0) ? yIn  : yOut;
        float yt = top - 8;
        canvas.fillTriangle(xl, yt, xr, yt, xl, yl, bg);
        canvas.fillTriangle(xr, yt, xr, yr, xl, yl, bg);
    }
    // párpado inferior curvo (sonrisa ^ ^)
    float lb = clamp01(p.lidBot + botExtra);
    if (lb > 0.02f) {
        float R = w * 0.85f;
        canvas.fillCircle(cxi, (int)(cy + h / 2 + R - lb * h), (int)R, bg);
    }
    canvas.clearClipRect();
}

// Efectos de ambiente por estado/herramienta (zona inferior y esquinas)
static void drawEffects(float t, bool bottomFree) {
    switch (state) {
        case S_WORK:
            if (!bottomFree) break;
            switch (curTool) {
                case T_READ:   // líneas de texto que se van revelando
                    for (int i = 0; i < 3; i++) {
                        float ph = fmodf(t * 0.8f - i * 0.25f, 1.0f); if (ph < 0) ph += 1;
                        canvas.fillRoundRect(38, 104 + i * 6, (int)(52 * clamp01(ph * 1.4f)), 3, 1, rgb(255, 190, 150));
                    }
                    break;
                case T_SEARCH: {  // lupa que barre
                    int mx = 64 + (int)(sinf(t * 2.2f) * 30);
                    canvas.drawCircle(mx, 110, 7, rgb(255, 230, 160)); canvas.drawCircle(mx, 110, 6, rgb(255, 230, 160));
                    canvas.drawLine(mx + 5, 115, mx + 11, 121, rgb(255, 230, 160));
                    break; }
                case T_EDIT: {   // línea que se escribe + cursor
                    int len = (int)fmodf(t * 28, 52);
                    canvas.fillRoundRect(38, 112, len, 3, 1, rgb(255, 170, 120));
                    if (((int)(t * 4)) & 1) canvas.fillRect(40 + len, 108, 2, 11, rgb(255, 255, 255));
                    break; }
                case T_BASH: {   // prompt >_
                    canvas.setFont(&fonts::Font2); canvas.setTextSize(1);
                    canvas.setTextColor(rgb(120, 255, 160));
                    canvas.drawString(((int)(t * 2.5f)) & 1 ? ">_" : "> ", 54, 104);
                    break; }
                case T_WEB: {    // globo giratorio
                    canvas.drawCircle(64, 112, 9, rgb(140, 210, 255));
                    canvas.drawFastHLine(55, 112, 19, rgb(140, 210, 255));
                    int ew = (int)(fabsf(cosf(t * 2)) * 8) + 1;
                    canvas.drawEllipse(64, 112, ew, 9, rgb(140, 210, 255));
                    break; }
                case T_AGENT:    // subagentes orbitando
                    for (int i = 0; i < 3; i++) {
                        float a = t * 3 + i * 2.0944f;
                        canvas.fillCircle(64 + (int)(cosf(a) * 13), 112 + (int)(sinf(a) * 5), 3, rgb(215, 170, 255));
                    }
                    break;
                default:
                    for (int i = 0; i < 3; i++) {
                        float ph = fmodf(t * 2.2f - i * 0.25f, 1.0f); if (ph < 0) ph += 1;
                        canvas.fillCircle(52 + i * 12, 112, 2 + (int)(2.5f * sinf(ph * 3.14159f)), rgb(255, 160, 120));
                    }
            }
            break;
        case S_WAIT: {
            int by = 18 + (int)(3 * sinf(t * 7));
            canvas.setFont(&fonts::Font4); canvas.setTextSize(1);
            canvas.setTextColor(rgb(255, 230, 120));
            canvas.drawString("?", 108, by);
            break; }
        case S_DONE:
            for (int i = 0; i < 4; i++) {
                float ph = fmodf(t * 1.3f + i * 0.25f, 1.0f);
                int s = (int)(5 * sinf(ph * 3.14159f));
                if (s < 1) continue;
                static const int8_t sx[4] = {14, 112, 22, 104}, sy[4] = {20, 26, 100, 96};
                drawStar(sx[i], sy[i], s, rgb(255, 255, 200));
            }
            break;
        case S_ERROR: {
            float ph = fmodf(t * 0.8f, 1.0f);
            if (ph < 0.9f) canvas.fillCircle(38, 76 + (int)(ph * 36), 3, rgb(140, 190, 255));
            break; }
        case S_SLEEP:
            canvas.setFont(&fonts::Font0);
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

static void drawOverlays(float t, uint32_t now) {
    if (react == R_PET) {   // corazones que suben
        float p = (now - reactStart) / (float)REACT_MS[R_PET];
        for (int i = 0; i < 3; i++) {
            float ph = clamp01(p * 1.3f - i * 0.18f);
            if (ph <= 0 || ph >= 1) continue;
            drawHeart(34 + i * 30 + (int)(sinf(ph * 8 + i) * 4), 92 - (int)(ph * 60), 3 + (i & 1), rgb(255, 110 + 40 * ph, 150));
        }
    } else if (react == R_GREET) {   // sol saliendo con rayos
        float p = (now - reactStart) / (float)REACT_MS[R_GREET];
        int sy = 40 - (int)(22 * clamp01(p * 3));
        canvas.fillCircle(106, sy, 8, rgb(255, 220, 100));
        for (int k = 0; k < 8; k++) {
            float a = t * 1.5f + k * 0.785f;
            canvas.drawLine(106 + (int)(cosf(a) * 12), sy + (int)(sinf(a) * 12), 106 + (int)(cosf(a) * 17), sy + (int)(sinf(a) * 17), rgb(255, 230, 130));
        }
    } else if (react == R_STARTLE) {
        canvas.setFont(&fonts::Font4); canvas.setTextColor(rgb(255, 240, 150));
        canvas.drawString("!", 108, 14);
    } else if (react == R_DIZZY) {
        for (int i = 0; i < 3; i++) {
            float a = t * 6 + i * 2.0944f;
            drawStar(64 + (int)(cosf(a) * 40), 18 + (int)(sinf(a) * 5), 3, rgb(255, 240, 150));
        }
    }
    if (beh == B_BUTTERFLY) {   // la luciérnaga que persigue la mirada
        int fx = 64 + (int)(gazeTX * 52), fy = 62 + (int)(gazeTY * 40);
        drawStar(fx, fy, 2 + ((int)(t * 10) & 1), rgb(255, 250, 170));
    }
    if (fxRaw.blush > 0.02f) {   // mejillas
        uint16_t c = rgb(bgCur[0] + (255 - bgCur[0]) * 0.55f * fxRaw.blush, bgCur[1] + (110 - bgCur[1]) * 0.55f * fxRaw.blush,
                         bgCur[2] + (150 - bgCur[2]) * 0.55f * fxRaw.blush);
        canvas.fillEllipse(26, 88, 9, 4, c); canvas.fillEllipse(102, 88, 9, 4, c);
    }
    if (fxRaw.yawn > 0.02f) {   // bostezo
        int ry = (int)(2 + 9 * fxRaw.yawn), rx = (int)(3 + 5 * fxRaw.yawn);
        canvas.fillEllipse(64, 100, rx, ry, rgb(40, 20, 40));
        canvas.drawEllipse(64, 100, rx, ry, rgb(150, 120, 170));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Aviso de tarea terminada (pendiente hasta pulsar el botón) y flashes
// ─────────────────────────────────────────────────────────────────────────────
static bool pending = false;     // hay tareas terminadas sin confirmar
static uint32_t pendingSince = 0;
static int curBrightness = -1;

static void setBrightnessIfChanged(int v) {
    if (v != curBrightness) { curBrightness = v; M5.Display.setBrightness(v); }
}

// Seguimiento por sesión de las tareas terminadas: "seen" = última vista, "acked" = última confirmada.
struct Track { char id[8]; long seen; long acked; bool used; };
static Track tracks[8];
static int pend[MAX_SESS];       // índices de R.list con tareas sin confirmar
static int nPend = 0;
static char lastAlertId[8] = "";     // sesión cuyo aviso es el más reciente

static int findSess(const char* id) {
    for (int i = 0; i < R.nList; i++) if (!strcmp(R.list[i].id, id)) return i;
    return -1;
}

static Track* trackFor(const char* id, long doneNow) {
    for (auto& t : tracks) if (t.used && !strcmp(t.id, id)) return &t;
    for (auto& t : tracks) if (!t.used || findSess(t.id) < 0) {    // hueco libre o de una sesión que ya no existe
        strlcpy(t.id, id, sizeof(t.id)); t.seen = t.acked = doneNow; t.used = true;   // línea base: sin aviso retroactivo
        return &t;
    }
    return nullptr;
}

static void computePending(uint32_t now) {
    nPend = 0;
    for (int i = 0; i < R.nList; i++) {
        Track* t = trackFor(R.list[i].id, R.list[i].done);
        if (!t) continue;
        long d = R.list[i].done;
        if (d < t->seen) { t->seen = t->acked = d; }                   // el servidor se reinició
        if (d > t->seen) { t->seen = d; pendingSince = now; strlcpy(lastAlertId, R.list[i].id, sizeof(lastAlertId)); Serial.printf("done alert: %s\n", R.list[i].name); }
        if (d > t->acked && nPend < MAX_SESS) pend[nPend++] = i;
    }
    pending = nPend > 0;
}

static void ackAll() {   // un click confirma todas las pendientes
    for (int q = 0; q < nPend; q++) {
        Track* t = trackFor(R.list[pend[q]].id, R.list[pend[q]].done);
        if (t) t->acked = R.list[pend[q]].done;
    }
}

// Borde que "respira": más vivo los primeros 12 s tras terminar, luego más lento y tenue.
// En espera de permiso, pulso ámbar muy suave. Nunca destellos bruscos.
static void drawBorder(uint32_t now) {
    float k; float ar, ag, ab;
    if (pending) {
        uint32_t age = now - pendingSince;
        float per = age < 12000 ? 1500.0f : 2800.0f;
        k = 0.5f - 0.5f * cosf(6.2831853f * age / per);
        k = (age < 12000 ? 0.25f : 0.12f) + (age < 12000 ? 0.75f : 0.45f) * k;
        ar = 90; ag = 230; ab = 150;
    } else if (state == S_WAIT) {
        k = 0.10f + 0.40f * (0.5f - 0.5f * cosf(6.2831853f * now / 2400.0f));
        ar = 255; ag = 200; ab = 80;
    } else return;
    uint16_t c = rgb(bgCur[0] + (ar - bgCur[0]) * k, bgCur[1] + (ag - bgCur[1]) * k, bgCur[2] + (ab - bgCur[2]) * k);
    for (int i = 0; i < 2; i++) canvas.drawRoundRect(i, i, 128 - 2 * i, 128 - 2 * i, 10, c);
}

// Texto abreviado con ".." para que quepa en maxW píxeles (usa la fuente activa del canvas)
static void fitText(char* buf, int maxW) {
    while (canvas.textWidth(buf) > maxW && strlen(buf) > 3) { buf[strlen(buf) - 1] = 0; buf[strlen(buf) - 1] = '.'; }
}

static void drawBanner(float t) {
    if (!pending || ui == UI_LIST) return;
    int show = pend[0];                                        // la sesión que terminó más recientemente
    for (int q = 0; q < nPend; q++) if (!strcmp(R.list[pend[q]].id, lastAlertId)) show = pend[q];
    char buf[40];
    const char* nm = R.list[show].name;
    strlcpy(buf, nm[0] ? nm : "listo", sizeof(buf));
    canvas.setFont(&fonts::Font2); canvas.setTextSize(1);
    fitText(buf, nPend > 1 ? 66 : 96);
    char badge[8] = ""; if (nPend > 1) snprintf(badge, sizeof(badge), "+%d", nPend - 1);
    int bw = badge[0] ? canvas.textWidth(badge) + 6 : 0;
    int tw = canvas.textWidth(buf) + 16 + bw;
    int x = 64 - tw / 2;
    canvas.fillRoundRect(x - 3, 104, tw + 6, 19, 5, rgb(10, 50, 30));
    canvas.drawLine(x + 2, 114, x + 5, 118, rgb(120, 255, 170)); canvas.drawLine(x + 5, 118, x + 11, 108, rgb(120, 255, 170));
    canvas.setTextColor(rgb(220, 255, 235));
    canvas.drawString(buf, x + 15, 105);
    if (badge[0]) { canvas.setTextColor(rgb(120, 200, 160)); canvas.drawString(badge, x + 15 + canvas.textWidth(buf) + 6, 105); }
}

// "waiting" con tres puntos animados mientras Claude espera una respuesta tuya
static void drawWaitMsg(float t) {
    if (state != S_WAIT || pending || ui == UI_LIST) return;
    canvas.setFont(&fonts::Font2); canvas.setTextSize(1); canvas.setTextColor(rgb(255, 225, 150));
    int full = canvas.textWidth("waiting..."), x = 64 - full / 2;
    canvas.drawString("waiting", x, 106);
    int dx = x + canvas.textWidth("waiting"), dots = 1 + ((int)(t * 2.5f) % 3);
    for (int i = 0; i < dots; i++) canvas.fillCircle(dx + 3 + i * 6, 119, 1, rgb(255, 225, 150));
}

// ─────────────────────────────────────────────────────────────────────────────
// Selector de sesiones (lista) y seguimiento de una sesión concreta
// ─────────────────────────────────────────────────────────────────────────────
static char pinnedId[8] = "";           // "" = automático (agregado de todas)
static int listCursor = 0;
static uint32_t listLastInput = 0;
static uint32_t toastUntil = 0;
static char toastText[32] = "";

static void showToast(const char* txt, uint32_t now) { strlcpy(toastText, txt, sizeof(toastText)); toastUntil = now + 2000; }

static uint16_t stateDot(int st) {
    switch (st) {
        case S_WORK: return rgb(255, 150, 105);  case S_WAIT: return rgb(255, 200, 80);
        case S_DONE: return rgb(120, 255, 170);  case S_ERROR: return rgb(165, 150, 255);
        case S_BORED: return rgb(150, 170, 195); case S_SLEEP: return rgb(100, 110, 175);
        default: return rgb(130, 220, 255);
    }
}

static void drawList(uint32_t now) {
    int n = R.nList + 1;                            // fila 0 = automático
    int vis = n < 7 ? n : 7;
    int start = listCursor - 3; if (start > n - vis) start = n - vis; if (start < 0) start = 0;
    canvas.setFont(&fonts::Font2); canvas.setTextSize(1);
    for (int k = 0; k < vis; k++) {
        int idx = start + k, y = 5 + k * 17;
        bool sel = idx == listCursor;
        if (sel) { canvas.fillRoundRect(4, y - 1, 120, 17, 6, rgb(28, 42, 68)); canvas.drawRoundRect(4, y - 1, 120, 17, 6, rgb(110, 160, 230)); }
        char buf[28];
        bool pinned = idx == 0 ? pinnedId[0] == 0 : !strcmp(R.list[idx - 1].id, pinnedId);
        if (idx == 0) { strlcpy(buf, "auto", sizeof(buf)); canvas.drawCircle(13, y + 8, 4, TFT_WHITE); }
        else { strlcpy(buf, R.list[idx - 1].name, sizeof(buf)); canvas.fillCircle(13, y + 8, 4, stateDot(R.list[idx - 1].state)); }
        fitText(buf, 78);
        canvas.setTextColor(sel ? TFT_WHITE : rgb(170, 185, 210));
        canvas.drawString(buf, 24, y);
        if (pinned) canvas.fillTriangle(110, y + 4, 110, y + 12, 117, y + 8, rgb(255, 220, 120));   // ▶ seguida
        if (idx > 0) {                                                                                     // ✓ sin ver
            for (int q = 0; q < nPend; q++) if (pend[q] == idx - 1) { canvas.drawLine(98, y + 9, 100, y + 12, rgb(120, 255, 170)); canvas.drawLine(100, y + 12, 104, y + 5, rgb(120, 255, 170)); }
        }
    }
}

static const float COVER_MAX = 91.0f;                      // radio que cubre las esquinas de 128x128 desde el centro
static float coverR = 0;                                   // radio actual del círculo
static uint32_t fadeAt = 0, clickAt = 0;
static uint16_t fadeColor = 0;

// Color de la acción de pulsación larga: azul = abrir opciones, verde = aceptar la selección
static uint16_t coverColor() { return ui == UI_LIST ? rgb(60, 200, 125) : rgb(75, 125, 225); }

// Pulsación larga: un círculo crece desde el centro hasta cubrir toda la pantalla (la acción se dispara al llenarla).
// Al soltar antes, se retira rápidamente. Tras completarse se desvanece revelando la nueva pantalla.
// Cada click corto deja además un destello verde sutil.
static void drawCover(uint32_t now) {
    if (coverR > 1) {
        uint16_t c = coverColor();
        canvas.fillCircle(64, 64, (int)coverR, c);
        canvas.drawCircle(64, 64, (int)coverR, TFT_WHITE);
        if (coverR > 3) canvas.drawCircle(64, 64, (int)coverR - 1, TFT_WHITE);
    }
    if (now - fadeAt < 320) canvas.fillRectAlpha(0, 0, 128, 128, (uint8_t)(255 * (1.0f - (now - fadeAt) / 320.0f)), fadeColor);
    if (now - clickAt < 280) canvas.fillRectAlpha(0, 0, 128, 128, (uint8_t)(70 * (1.0f - (now - clickAt) / 280.0f)), rgb(90, 255, 160));
}

static void drawToast(uint32_t now) {
    if (now >= toastUntil) return;
    char buf[32]; strlcpy(buf, toastText, sizeof(buf));
    canvas.setFont(&fonts::Font2); canvas.setTextSize(1);
    fitText(buf, 100);
    int tw = canvas.textWidth(buf) + 14;
    canvas.fillRoundRect(64 - tw / 2, 4, tw, 19, 6, rgb(20, 30, 50));
    canvas.drawRoundRect(64 - tw / 2, 4, tw, 19, 6, rgb(110, 160, 230));
    canvas.setTextColor(TFT_WHITE); canvas.drawCenterString(buf, 64, 6);
}

// Botón: click corto y pulsación larga (con anillo). Sin triple click para no añadir latencia.
static uint32_t pressAt = 0, lastBtnAt = 0;
static bool btnDown = false, longFired = false;
static float ring = 0;
static const uint32_t RING_START = 150, RING_END = 750;   // el círculo aparece a los 150 ms y cubre la pantalla a los 750 ms

// ── Depuración del botón: pantalla que muestra lo que lee el firmware (serie 'x' o -DBUTTON_DEBUG) ──
struct BtnEvt { char kind[8]; uint32_t held; };
static BtnEvt btnHist[4];
static uint32_t nClick = 0, nLong = 0, nPress = 0, lastHeld = 0;
#ifdef BUTTON_DEBUG
static bool dbgScreen = true;
#else
static bool dbgScreen = false;
#endif
static void dbgPush(const char* kind, uint32_t held) {
    for (int i = 3; i > 0; i--) btnHist[i] = btnHist[i - 1];
    strlcpy(btnHist[0].kind, kind, sizeof(btnHist[0].kind)); btnHist[0].held = held;
}

static void drawBtnDebug(uint32_t now) {
    bool down = M5.BtnA.isPressed();
    int raw = digitalRead(41);                       // GPIO del botón (activo en bajo)
    canvas.setFont(&fonts::Font2); canvas.setTextSize(1);
    canvas.setTextColor(rgb(150, 170, 200)); canvas.drawString("BTN DEBUG", 8, 3);
    canvas.fillCircle(20, 40, 13, down ? rgb(90, 255, 160) : rgb(25, 30, 45));
    canvas.drawCircle(20, 40, 13, down ? (uint16_t)TFT_WHITE : rgb(70, 80, 100));
    uint32_t held = down ? now - pressAt : lastHeld;
    char b[16]; snprintf(b, sizeof(b), "%u", (unsigned)held);
    canvas.setFont(&fonts::Font4); canvas.setTextColor(down ? (uint16_t)TFT_WHITE : rgb(120, 135, 160));
    canvas.drawString(b, 42, 26);
    int w = canvas.textWidth(b);
    canvas.setFont(&fonts::Font2); canvas.drawString("ms", 46 + w, 33);
    bool mismatch = down != (raw == 0);
    char l[32]; snprintf(l, sizeof(l), "gpio41=%d btnA=%d", raw, (int)down);
    canvas.setTextColor(mismatch ? rgb(255, 190, 90) : rgb(130, 150, 180)); canvas.drawString(l, 6, 60);
    snprintf(l, sizeof(l), "click %u long %u", (unsigned)nClick, (unsigned)nLong);
    canvas.setTextColor(rgb(170, 190, 220)); canvas.drawString(l, 6, 76);
    for (int i = 0; i < 2; i++) {
        if (!btnHist[i].kind[0]) break;
        snprintf(l, sizeof(l), "%s %ums", btnHist[i].kind, (unsigned)btnHist[i].held);
        canvas.setTextColor(btnHist[i].kind[0] == 'L' ? rgb(110, 170, 255) : rgb(120, 255, 170)); canvas.drawString(l, 6, 94 + i * 16);
    }
}

static bool longAvailable() { return dbgScreen || ui == UI_LIST || R.nList >= 2; }

// click: en la lista mueve el cursor; con avisos pendientes los confirma todos; si no, caricia
static void doClick(uint32_t now) {
    clickAt = now;                                                      // destello verde: el click se procesó
    if (ui == UI_FACE && state != S_SLEEP) { blinking = true; blinkStart = now; }   // y un parpadeo
    if (dbgScreen) return;
    if (ui == UI_LIST) { listCursor = (listCursor + 1) % (R.nList + 1); listLastInput = now; return; }
    if (nPend > 0) { ackAll(); startReaction(R_PET); return; }
    startReaction(R_PET);   // boop
}

// pulsación larga (anillo completo): abre la lista o, dentro de ella, selecciona la fila
static void doLong(uint32_t now) {
    fadeAt = now; fadeColor = coverColor(); coverR = 0;                 // el círculo se desvanece al completarse
    if (dbgScreen) return;
    if (ui == UI_LIST) {
        if (listCursor == 0) { pinnedId[0] = 0; showToast("auto", now); }
        else { strlcpy(pinnedId, R.list[listCursor - 1].id, sizeof(pinnedId)); showToast(R.list[listCursor - 1].name, now); }
        Serial.printf("follow: %s\n", pinnedId[0] ? pinnedId : "auto");
        ui = UI_FACE; startReaction(R_PET); return;
    }
    if (R.nList >= 2) {
        ui = UI_LIST; listLastInput = now;
        int pi = pinnedId[0] ? findSess(pinnedId) : -1; listCursor = pi >= 0 ? pi + 1 : 0;
    }
}

// Sin zona muerta: soltar antes de completar el anillo = click; completarlo = pulsación larga.
static void handleButton(uint32_t now) {
    if (M5.BtnA.wasPressed()) { pressAt = now; btnDown = true; longFired = false; lastBtnAt = now; nPress++; Serial.printf("button: pressed #%u (nPend=%d list=%d ui=%d)\n", (unsigned)nPress, nPend, (int)R.nList, (int)ui); }
    ring = 0;
    if (btnDown && M5.BtnA.isPressed() && !longFired && longAvailable() && now - pressAt >= RING_START) {
        ring = clamp01((now - pressAt - RING_START) / (float)(RING_END - RING_START));
        listLastInput = now;   // la lista no se cierra mientras se está pulsando
        if (ring >= 1) { longFired = true; ring = 0; nLong++; dbgPush("LONG", now - pressAt); Serial.printf("button: LONG after %ums\n", (unsigned)(now - pressAt)); doLong(now); }
    }
    if (btnDown && M5.BtnA.wasReleased()) {
        btnDown = false; lastHeld = now - pressAt;
        if (!longFired) { nClick++; dbgPush("click", lastHeld); Serial.printf("button: click (held %ums)\n", (unsigned)lastHeld); doClick(now); }
        else Serial.printf("button: released after long (held %ums)\n", (unsigned)lastHeld);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IMU: agitar → mareo, golpecito → caricia, caída libre → susto
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t spikeTimes[6] = {0};
static int spikeCount = 0;
static uint32_t lastSpike = 0, firstSpikeAt = 0;
static int freefallFrames = 0;
static float lastMag = 1;

static void imuUpdate(uint32_t now) {
    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    lastMag = mag;
    float dm = fabsf(mag - 1.0f);

    // caída libre
    freefallFrames = mag < 0.35f ? freefallFrames + 1 : 0;
    if (freefallFrames == 4 && react != R_STARTLE) { startReaction(R_STARTLE); return; }

    // picos
    if (dm > 0.6f && now - lastSpike > 90) {
        if (spikeCount == 0 || now - firstSpikeAt > 1000) { spikeCount = 0; firstSpikeAt = now; }
        spikeCount++; lastSpike = now;
        if (spikeCount >= 4 && react != R_DIZZY) { spikeCount = 0; startReaction(R_DIZZY); return; }
    }
    // un único golpecito (sin más picos tras 600 ms y sin botón de por medio) = caricia
    if (spikeCount == 1 && now - lastSpike > 600) {
        spikeCount = 0;
        if (now - lastBtnAt > 500 && react == R_NONE) startReaction(R_PET);
    } else if (spikeCount > 1 && now - lastSpike > 1000) spikeCount = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Principal
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t lastFrame = 0, accRender = 0, accPush = 0, frames = 0, lastReport = 0;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    setBrightnessIfChanged(140);
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
    float dt = fminf((now - lastFrame) / 1000.0f, 0.1f);
    lastFrame = now;
    float t = now / 1000.0f;

    // ── entrada: botón ──
    handleButton(now);
    if (ring > 0) coverR = ring * COVER_MAX; else coverR = fmaxf(0.0f, coverR - dt * 700.0f);   // se retira si sueltas a medias
    if (ui == UI_LIST && (now - listLastInput > 10000 || R.nList < 2)) ui = UI_FACE;

    // ── entrada: serie (debug) ──
    while (Serial.available()) {
        int c = Serial.read();
        if (c >= '0' && c < '0' + S_COUNT) { overrideUntil = now + 10000; setState((State)(c - '0')); }
        else if (c == 'x') { dbgScreen = !dbgScreen; Serial.printf("button debug screen: %d\n", (int)dbgScreen); }
        else if (c == 'a') { overrideUntil = 0; toolOverride = -1; }
        else if (c == 'T') { toolOverride = (toolOverride + 1) % T_COUNT; overrideUntil = now + 10000; setState(S_WORK); Serial.printf("tool=%s\n", TOOL_NAME[toolOverride]); }
        else if (c == 'k') startReaction(R_DIZZY);
        else if (c == 'h') startReaction(R_PET);
        else if (c == 's') startReaction(R_STARTLE);
        else if (c == 'b') { behCycle = behCycle % (B_COUNT - 1) + 1; behForce = behCycle; beh = B_NONE; state = S_IDLE; overrideUntil = now + 10000; }
    }

    // ── hora local (NTP): noche = horas de silencio ──
    static int hourNow = -1, todayYday = -1, lastGreetDay = -1;
    static uint32_t lastTimeCheck = 0;
    if (now - lastTimeCheck > 1000) {
        lastTimeCheck = now;
        time_t tt = time(nullptr);
        if (tt > 1700000000) { tm ti; localtime_r(&tt, &ti); hourNow = ti.tm_hour; todayYday = ti.tm_yday; }
    }
    const bool night = hourNow >= 0 && (hourNow >= 23 || hourNow < 7);

    // ── datos remotos ──
    portENTER_CRITICAL(&remoteMux); R = remote; portEXIT_CRITICAL(&remoteMux);
    const Remote& r = R;
    mood = r.mood;
    computePending(now);
    if (now >= overrideUntil) {
        toolOverride = -1;
        int rs = r.state; Tool rt = (Tool)r.tool;
        if (pinnedId[0]) {   // seguir una sesión concreta (salvo que otra te necesite)
            int pi = findSess(pinnedId);
            if (pi < 0) { pinnedId[0] = 0; showToast("auto", now); }
            else {
                rs = r.list[pi].state; rt = (Tool)r.list[pi].tool;
                for (int j = 0; j < r.nList; j++) if (j != pi && r.list[j].state == S_WAIT) { rs = S_WAIT; rt = T_NONE; }
                if (rs == S_IDLE && (r.state == S_BORED || r.state == S_SLEEP)) rs = r.state;
            }
        }
        if (rs == S_DONE && !pending) rs = S_IDLE;   // ya confirmada: no repetir la cara de "terminado"
        if (night && (rs == S_IDLE || rs == S_BORED)) rs = S_SLEEP;   // de noche duerme en vez de aburrirse
        if (rs >= 0 && rs != state) {
            // saludo: primera actividad de la mañana
            if (hourNow >= 5 && hourNow < 13 && todayYday != lastGreetDay && (rs == S_ALERT || rs == S_WORK) &&
                (state == S_SLEEP || state == S_BORED || state == S_IDLE)) {
                lastGreetDay = todayYday; startReaction(R_GREET); showToast("buenos dias", now);
            }
            setState((State)rs);
        }
        curTool = rt;
    } else if (toolOverride >= 0) curTool = (Tool)toolOverride;

    // ── IMU y animaciones ──
    imuUpdate(now);
    fxRaw = Fx();
    computeBehavior(fxRaw, now, t);
    computeReaction(fxRaw, now, t);
    // (los reflejos del IMU ganan sobre los behaviors: se calculan después y sobrescriben campos)

    // interpolar pose
    Pose tg = targetPose();
    float* cp = (float*)&cur; const float* tp = (const float*)&tg;
    for (size_t i = 0; i < sizeof(Pose) / sizeof(float); i++) cp[i] = approach(cp[i], tp[i], 9.0f, dt);

    // suavizar efectos
    const float fxT[10] = {fxRaw.scale[0], fxRaw.scale[1], fxRaw.lidAdd[0], fxRaw.lidAdd[1], fxRaw.dy[0], fxRaw.dy[1],
                           fxRaw.ox, fxRaw.oy, fxRaw.pupilMul, fxRaw.botAdd};
    static bool fxInit = false;
    if (!fxInit) { for (int i = 0; i < 10; i++) fxSm[i] = fxT[i]; fxInit = true; }
    for (int i = 0; i < 10; i++) fxSm[i] = approach(fxSm[i], fxT[i], 14.0f, dt);

    updateGaze(now, t, fxRaw);
    gazeX = approach(gazeX, gazeTX, (state == S_WORK && !fxRaw.gazeOv) ? 30.0f : 16.0f, dt);
    gazeY = approach(gazeY, gazeTY, 16.0f, dt);

    // brillo: solo se atenúa al dormir
    int bright = night ? 45 : 140;                              // horas de silencio: pantalla tenue
    if (state == S_SLEEP && !pending) bright = night ? 15 : 30;
    setBrightnessIfChanged(bright);

    // ── render ──
    uint32_t r0 = micros();
    float bgT[3]; bgTarget(bgT, now, pending);
    for (int i = 0; i < 3; i++) bgCur[i] = approach(bgCur[i], bgT[i], 3.0f, dt);
    uint16_t bg = rgb(bgCur[0], bgCur[1], bgCur[2]);
    canvas.fillScreen(bg);

    if (dbgScreen) drawBtnDebug(now);
    else if (ui == UI_LIST) drawList(now);
    else {
    float breathe = 1.0f + 0.025f * sinf(t * 2.0f);
    float b = blinkFactor(now);
    float cy = 62 + cur.yoff + fxSm[7];
    float dx = (cur.w + cur.gap) / 2.0f;
    for (int i = 0; i < 2; i++) {
        float s = fxSm[i];
        float h = fmaxf(4.0f, cur.h * breathe * s * (1.0f - b));
        float w = cur.w * (s > 1 ? 1 + (s - 1) * 0.6f : 1.0f);
        float cx = 64 + (i ? dx : -dx) + fxSm[6];
        drawEye(i, cx, cy + fxSm[4 + i], w, h, cur, fxSm[2 + i], fxSm[9], fxSm[8], fxRaw.dizzy, fxRaw.sparkle, t, bg);
    }
    drawEffects(t, !pending);
    drawOverlays(t, now);
    drawBanner(t);
    drawWaitMsg(t);
    }
    drawBorder(now);
    drawToast(now);
    drawCover(now);
    if (otaPct >= 0) {   // progreso de actualización por WiFi
        canvas.fillRect(14, 58, 100, 12, rgb(20, 20, 30)); canvas.fillRect(16, 60, otaPct, 8, rgb(120, 220, 255));
        canvas.setFont(&fonts::Font2); canvas.setTextColor(TFT_WHITE); canvas.drawCenterString("OTA", 64, 40);
    }
    uint32_t r1 = micros();
    canvas.pushSprite(0, 0);
    M5.Display.waitDisplay();
    uint32_t r2 = micros();

    accRender += r1 - r0; accPush += r2 - r1; frames++;
    if (now - lastReport >= 2000) {
        Serial.printf("fps=%.1f render=%.1fms push=%.1fms heap=%u wifi=%d state=%s tool=%s mood=%d hour=%d |a|=%.2f\n",
                      frames * 1000.0f / (now - lastReport), accRender / 1000.0f / frames, accPush / 1000.0f / frames,
                      ESP.getFreeHeap(), (int)wifiUp, STATE_NAME[state], TOOL_NAME[curTool], mood, hourNow, lastMag);
        accRender = accPush = frames = 0; lastReport = now;
    }
    int32_t spare = 16 - (int32_t)((micros() - r0) / 1000);   // ~60 fps
    if (spare > 0) delay(spare);
}
