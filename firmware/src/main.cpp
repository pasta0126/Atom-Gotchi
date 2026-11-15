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
#ifndef API_KEY
#define API_KEY ""
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
struct Remote {
    int8_t  state = -1;       // -1 = sin datos todavía
    uint8_t tool = T_NONE;
    int8_t  mood = 0;         // -2..2
    uint8_t attn = 0;         // el servidor dice: hay una tarea terminada sin confirmar
    long    attnSeq = 0;      // contador de avisos (para no repetir uno ya confirmado aquí)
};
static Remote remote;   // escrito por netTask
static Remote R;        // copia del frame actual (loop)
static portMUX_TYPE remoteMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool wifiUp = false;
static volatile int otaPct = -1;
static volatile bool ackRequested = false;   // el loop pide a netTask que confirme en el servidor

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
        else if (k == "attn") r.attn = v.toInt() != 0;
        else if (k == "attnseq") r.attnSeq = v.toInt();
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
        http.addHeader("X-Api-Key", API_KEY);
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
        if (ackRequested) {          // confirmar la atención pendiente en el servidor
            ackRequested = false;
            const String aurl = String(API_URL) + "/api/ack";
            if (https) http.begin(secure, aurl); else http.begin(plain, aurl);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("X-Api-Key", API_KEY);
            int ac = http.POST("{}");
            Serial.printf("ack -> %d\n", ac);
            if (ac <= 0) http.end();
        }
        for (int i = 0; i < 10; i++) { ArduinoOTA.handle(); vTaskDelay(pdMS_TO_TICKS(100)); }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Estado de animación
// ─────────────────────────────────────────────────────────────────────────────
static M5Canvas canvas(&M5.Display);
static Pose cur = POSES[S_IDLE];
static State state = S_IDLE;
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
    bool  brow = false;           // dibujar cejas
    float browY[2] = {0, 0};      // + baja / - sube (px)
    float browAng[2] = {0, 0};    // + enfadado (extremo interno más bajo) / - triste
    uint8_t glyph = 0;            // 0 pupila normal, 1 corazón, 2 estrella
    uint8_t mouth = 0;            // 0 ninguna, 1 sonrisa, 2 puchero, 3 "o", 4 ondulada, 5 sonrisa torcida, 6 recta, 7 risa
    float mouthAmt = 0;
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
static int emoteIdx = 0;

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
enum React : uint8_t { R_NONE, R_DIZZY, R_PET, R_STARTLE, R_GREET, R_ANNOYED };
static const uint16_t REACT_MS[6] = {0, 3000, 2200, 1200, 2800, 2600};
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
        case R_ANNOYED:   // molesto: la pose (cejas, color) la pone targetPose(); aquí el temblor y la mirada fija
            fx.gazeOv = true; fx.gx = 0; fx.gy = 0; fx.ox = sinf(t * 34) * 1.6f * (1 - p); fx.pupilMul = 0.8f;
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
// Emotes de prueba (galería): cada uno combina pose, ojos, cejas, boca y partículas.
// ─────────────────────────────────────────────────────────────────────────────
enum Emote : uint8_t { E_NONE, E_HEART, E_STAR, E_LAUGH, E_CRY, E_SMUG, E_SHOCK, E_IDEA, E_SWEAT, E_YAWN, E_COLD, E_COUNT };
static const char* EMOTE_NAME[E_COUNT] = {"", "corazones", "estrellas", "risa", "llanto", "enguido", "shock", "idea", "sudor", "bostezo", "frio"};
static const uint16_t EMOTE_MS[E_COUNT] = {0, 2600, 2600, 2200, 3400, 2600, 2300, 3000, 2800, 3200, 2800};
static Emote emote = E_NONE;
static uint32_t emoteStart = 0, emoteEndedAt = 0;
static bool gallery = false;
static uint32_t galleryStart = 0;

static void startEmote(Emote e) {
    emote = e; emoteStart = millis();
    Serial.printf("emote: %s\n", EMOTE_NAME[e]);
}

static void computeEmote(Fx& fx, uint32_t now, float t) {
    if (emote == E_NONE) return;
    float p = (now - emoteStart) / (float)EMOTE_MS[emote];
    if (p >= 1) { emote = E_NONE; emoteEndedAt = now; return; }
    float e = envelope(p), in = clamp01(p * 5);   // e: sube y baja; in: entrada rápida
    switch (emote) {
        case E_HEART:
            fx.glyph = 1; fx.botAdd = 0.3f * e; fx.mouth = 1; fx.mouthAmt = 0.8f * e; fx.oy = -1.5f * sinf(t * 9) * e;
            fx.gazeOv = true; fx.gx = 0; fx.gy = 0; fx.blush = 0.7f * e;
            break;
        case E_STAR:
            fx.glyph = 2; fx.botAdd = 0.2f * e; fx.mouth = 7; fx.mouthAmt = 0.35f * e; fx.pupilMul = 1.2f;
            fx.gazeOv = true; fx.gx = 0; fx.gy = 0;
            break;
        case E_LAUGH:
            fx.botAdd = 0.66f * in; fx.ox = sinf(t * 36) * 2.2f * e; fx.oy = sinf(t * 18) * 1.5f * e;
            fx.mouth = 7; fx.mouthAmt = (0.55f + 0.45f * fabsf(sinf(t * 13))) * e;
            break;
        case E_CRY:
            fx.pupilMul = 1.3f; fx.ox = sinf(t * 23) * 0.9f * e; fx.mouth = 2; fx.mouthAmt = 0.9f * in;
            fx.brow = true; fx.browAng[0] = fx.browAng[1] = -4.5f * in; fx.browY[0] = fx.browY[1] = -1;
            fx.gazeOv = true; fx.gx = 0; fx.gy = 0.4f;
            break;
        case E_SMUG:
            fx.lidAdd[0] = 0.45f * in; fx.lidAdd[1] = 0.08f * in; fx.brow = true;
            fx.browY[1] = -5.5f * in; fx.browAng[1] = 1.5f * in; fx.browY[0] = 1.5f * in; fx.browAng[0] = 2.5f * in;
            fx.mouth = 5; fx.mouthAmt = in; fx.pupilMul = 0.9f;
            fx.gazeOv = true; fx.gx = 0.85f * sinf(p * 6.2832f); fx.gy = -0.1f;
            break;
        case E_SHOCK:
            fx.scale[0] = fx.scale[1] = 1 + 0.28f * in; fx.pupilMul = 0.3f; fx.ox = sinf(t * 46) * 1.4f * e;
            fx.brow = true; fx.browY[0] = fx.browY[1] = -8 * in; fx.mouth = 3; fx.mouthAmt = 0.55f * in;
            fx.gazeOv = true; fx.gx = 0; fx.gy = 0;
            break;
        case E_IDEA:
            if (p < 0.45f) {   // pensando
                fx.gazeOv = true; fx.gx = -0.7f; fx.gy = -0.75f; fx.brow = true; fx.browY[1] = -5; fx.browAng[1] = 1; fx.browY[0] = 1;
                fx.mouth = 6; fx.mouthAmt = 1; fx.lidAdd[0] = 0.12f;
            } else {           // ¡eureka!
                float q = clamp01((p - 0.45f) * 8);
                fx.scale[0] = fx.scale[1] = 1 + 0.18f * q; fx.pupilMul = 1.1f; fx.botAdd = 0.2f * q; fx.brow = true;
                fx.browY[0] = fx.browY[1] = -6 * q; fx.mouth = 1; fx.mouthAmt = 0.9f * q; fx.oy = -2 * q;
                fx.gazeOv = true; fx.gx = 0; fx.gy = -0.15f;
            }
            break;
        case E_SWEAT:
            fx.gazeOv = true; fx.gx = 0.85f; fx.gy = 0.25f; fx.scale[0] = fx.scale[1] = 0.92f; fx.blush = 0.8f * e;
            fx.mouth = 4; fx.mouthAmt = 0.55f * e; fx.brow = true; fx.browAng[0] = fx.browAng[1] = -3.5f; fx.browY[0] = fx.browY[1] = -2;
            break;
        case E_YAWN:
            fx.scale[0] = fx.scale[1] = 1 - 0.72f * e; fx.oy = -3 * e; fx.mouth = 3; fx.mouthAmt = e;
            fx.gazeOv = true; fx.gx = 0; fx.gy = -0.3f;
            break;
        case E_COLD:
            fx.ox = sinf(t * 52) * 1.8f * in; fx.scale[0] = fx.scale[1] = 0.9f; fx.mouth = 4; fx.mouthAmt = 0.6f * in;
            fx.brow = true; fx.browAng[0] = fx.browAng[1] = -3.5f; fx.pupilMul = 0.85f;
            break;
        default: break;
    }
}

// Pose base de cada emote (color y forma); se interpola con el muelle
static void emotePose(Pose& q) {
    q = POSES[S_IDLE]; q.lidTop = 0; q.tilt = 0; q.lidBot = 0;
    switch (emote) {
        case E_HEART: q.r = 255; q.g = 205; q.b = 222; q.w = 38; q.h = 44; break;
        case E_STAR:  q.r = 255; q.g = 238; q.b = 170; q.w = 38; q.h = 46; break;
        case E_LAUGH: q.r = 255; q.g = 225; q.b = 140; q.w = 36; q.h = 40; break;
        case E_CRY:   q.r = 150; q.g = 190; q.b = 255; q.w = 36; q.h = 44; q.lidTop = 0.1f; q.pupil = 0.8f; break;
        case E_SMUG:  q.r = 205; q.g = 170; q.b = 255; q.w = 36; q.h = 40; break;
        case E_SHOCK: q.r = 225; q.g = 240; q.b = 255; q.w = 40; q.h = 54; q.pupil = 0.3f; break;
        case E_IDEA:  q.r = 255; q.g = 235; q.b = 150; q.w = 36; q.h = 44; break;
        case E_SWEAT: q.r = 255; q.g = 185; q.b = 170; q.w = 36; q.h = 42; break;
        case E_YAWN:  q.r = 170; q.g = 190; q.b = 230; q.w = 36; q.h = 40; break;
        case E_COLD:  q.r = 170; q.g = 225; q.b = 255; q.w = 36; q.h = 42; break;
        default: break;
    }
}

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
    if (emote != E_NONE) { emotePose(p); return p; }
    if (react == R_ANNOYED) {   // cara de enfado (también si estaba dormido)
        p.h = 38; p.w = 36; p.lidTop = 0.22f; p.tilt = 0.8f; p.lidBot = 0; p.pupil = 0.5f; p.yoff = 2;
        p.r = 255; p.g = 105; p.b = 105;
        return p;
    }
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
                    float pupilMul, bool dizzy, bool sparkle, int glyph, float t, uint16_t bg) {
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
        if (!glyph) canvas.fillCircle((int)px, (int)py, (int)pr, rgb(14, 16, 30));
        if (glyph == 1) {   // ojos de corazón que laten
            float beat = 1.0f + 0.18f * sinf(t * 12);
            drawHeart((int)px, (int)py - 1, (int)fmaxf(3, pr * 0.95f * beat), rgb(235, 40, 95));
        } else if (glyph == 2) {   // ojos de estrella
            int ss = (int)fmaxf(4, pr * 1.2f + 1.5f * sinf(t * 10));
            drawStar((int)px, (int)py, ss, rgb(255, 170, 40)); drawStar((int)px, (int)py, ss - 2, TFT_WHITE);
        } else if (dizzy) {   // espiral
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

static float eyeCx[2], eyeTop[2], eyeW[2];   // geometría del último frame (para cejas)

static void drawBrows() {
    if (!fxRaw.brow) return;
    uint16_t c = rgb(cur.r * 0.9f, cur.g * 0.9f, cur.b * 0.9f);
    for (int i = 0; i < 2; i++) {
        float inner = i == 0 ? 1.0f : -1.0f;
        float y = eyeTop[i] - 7 + fxRaw.browY[i], a = fxRaw.browAng[i];
        canvas.drawWideLine(eyeCx[i] - inner * (eyeW[i] / 2 + 1), y - a, eyeCx[i] + inner * (eyeW[i] / 2 - 2), y + a, 2.0f, c);
    }
}

static void drawMouth(float t) {
    if (!fxRaw.mouth) return;
    uint16_t c = rgb(240, 175, 195), dark = rgb(60, 25, 45);
    float a = fxRaw.mouthAmt;
    switch (fxRaw.mouth) {
        case 1: { float r = 8 + 4 * a; canvas.fillArc(64, 92, r - 2, r, 20, 160, c); break; }          // sonrisa
        case 2: { float r = 8 + 3 * a; canvas.fillArc(64, 112, r - 2, r, 200, 340, c); break; }        // puchero
        case 3: { int ry = (int)(3 + 9 * a), rx = (int)(3 + 3 * a);                                    // "o"
                  canvas.fillEllipse(64, 100, rx, ry, dark); canvas.drawEllipse(64, 100, rx, ry, c); break; }
        case 7: { int ry = (int)(3 + 7 * a), rx = (int)(6 + 9 * a);                                    // risa
                  canvas.fillEllipse(64, 99, rx, ry, dark); canvas.drawEllipse(64, 99, rx, ry, c);
                  canvas.fillEllipse(64, 99 + ry / 2, rx / 2, ry / 2, rgb(230, 100, 120)); break; }
        case 4: {                                                                                      // ondulada
            float px = 50, py = 102;
            for (int k = 1; k <= 6; k++) {
                float x = 50 + k * 4.7f, y = 102 + sinf(k * 1.4f + t * 14) * 2.2f * a;
                canvas.drawWideLine(px, py, x, y, 1.2f, c); px = x; py = y;
            }
            break; }
        case 5: canvas.drawWideLine(54, 100, 66, 102, 1.4f, c); canvas.drawWideLine(66, 102, 77, 96, 1.4f, c); break;   // sonrisa torcida
        case 6: canvas.drawWideLine(56, 101, 72, 101, 1.4f, c); break;                                // recta
        default: break;
    }
}

// Partículas y accesorios propios de cada emote
static void drawEmoteOverlays(float t, uint32_t now) {
    if (emote == E_NONE) return;
    float p = (now - emoteStart) / (float)EMOTE_MS[emote];
    switch (emote) {
        case E_HEART:
            for (int i = 0; i < 4; i++) {
                float ph = fmodf(t * 0.9f + i * 0.25f, 1.0f);
                drawHeart(24 + i * 26 + (int)(sinf(ph * 7 + i) * 4), 92 - (int)(ph * 70), 2 + (i & 1) * 2, rgb(255, 110 + 90 * ph, 150));
            }
            break;
        case E_STAR:
            for (int i = 0; i < 5; i++) {
                float ph = fmodf(t * 1.6f + i * 0.2f, 1.0f); int s = (int)(5 * sinf(ph * 3.14159f));
                static const int8_t sx[5] = {16, 110, 26, 100, 64}, sy[5] = {24, 30, 98, 94, 12};
                if (s >= 1) drawStar(sx[i], sy[i], s, rgb(255, 245, 190));
            }
            break;
        case E_LAUGH:
            for (int i = 0; i < 2; i++) {   // lágrimas de risa que salen despedidas
                float ph = fmodf(t * 2.2f + i * 0.5f, 1.0f); float dir = i ? 1 : -1;
                canvas.fillCircle(64 + (int)(dir * (34 + ph * 20)), 70 + (int)(ph * ph * 26), 3 - (int)(ph * 2), rgb(150, 200, 255));
            }
            break;
        case E_CRY:
            for (int i = 0; i < 2; i++)     // regueros de lágrimas
                for (int k = 0; k < 3; k++) {
                    float ph = fmodf(t * 1.3f + k * 0.33f + i * 0.15f, 1.0f);
                    canvas.fillCircle((int)(eyeCx[i] + (i ? 6 : -6)), (int)(eyeTop[i] + 40 + ph * 34), 2 + (int)((1 - ph) * 2), rgb(130, 185, 255));
                }
            break;
        case E_SMUG:
            drawStar(88, 48, 3 + ((int)(t * 6) & 1), rgb(255, 255, 255));   // brillo en el ojo
            break;
        case E_SHOCK:
            for (int i = 0; i < 3; i++) {   // líneas de impacto sobre la cabeza
                float ang = (i - 1) * 0.5f - 1.5708f;
                canvas.drawWideLine(64 + cosf(ang) * 16, 20 + sinf(ang) * 12, 64 + cosf(ang) * 26, 20 + sinf(ang) * 20, 1.2f, rgb(235, 245, 255));
            }
            break;
        case E_IDEA:
            if (p >= 0.45f) {   // bombilla con rayos y un destello al encenderse
                float q = (p - 0.45f) / 0.55f;
                canvas.fillCircle(64, 13, 8, rgb(255, 235, 110)); canvas.fillRect(60, 21, 9, 4, rgb(170, 170, 180));
                float rl = 4 + 2 * sinf(t * 14);
                for (int k = 0; k < 8; k++) { float a = k * 0.785f; canvas.drawLine(64 + cosf(a) * 12, 13 + sinf(a) * 12, 64 + cosf(a) * (12 + rl), 13 + sinf(a) * (12 + rl), rgb(255, 240, 150)); }
                if (q < 0.12f) canvas.fillRectAlpha(0, 0, 128, 128, (uint8_t)(120 * (1 - q / 0.12f)), TFT_WHITE);
            } else {
                canvas.setFont(&fonts::Font4); canvas.setTextColor(rgb(255, 235, 150));
                canvas.drawString(((int)(t * 3) % 3 == 0) ? "." : ((int)(t * 3) % 3 == 1) ? ".." : "...", 84, 20);
            }
            break;
        case E_SWEAT: {
            float ph = fmodf(t * 0.7f, 1.0f);
            int y = 28 + (int)(ph * 22);
            canvas.fillCircle(102, y + 4, 3, rgb(150, 205, 255)); canvas.fillTriangle(99, y + 3, 105, y + 3, 102, y - 4, rgb(150, 205, 255));
            break; }
        case E_YAWN:
            if (p > 0.7f) { float ph = (p - 0.7f) / 0.3f; canvas.fillCircle(32, 78 + (int)(ph * 14), 2, rgb(150, 200, 255)); }
            break;
        case E_COLD:
            for (int i = 0; i < 6; i++) {
                float ph = fmodf(t * 0.6f + i * 0.17f, 1.0f);
                drawStar(14 + i * 20 + (int)(sinf(ph * 6 + i) * 4), 6 + (int)(ph * 110), 2, rgb(215, 240, 255));
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
    } else if (react == R_ANNOYED) {   // marca de enfado que late + vaho
        int k = 4 + (int)(2 * sinf(now / 90.0f));
        uint16_t c = rgb(255, 90, 90);
        canvas.drawLine(100 - k, 16, 100 - k / 2, 16, c); canvas.drawLine(100 + k / 2, 16, 100 + k, 16, c);
        canvas.drawLine(106, 10 - k / 2, 106, 10, c);     canvas.drawLine(106, 22, 106, 22 + k / 2, c);
        for (int i = 0; i < 2; i++) {
            float ph = fmodf(t * 1.4f + i * 0.5f, 1.0f);
            canvas.fillCircle(30 + i * 68, 100 - (int)(ph * 24), 2 + (int)(ph * 3), rgb(180, 150 - 60 * ph, 150 - 60 * ph));
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

// "waiting" con tres puntos animados mientras Claude espera una respuesta tuya
static void drawWaitMsg(float t) {
    if (state != S_WAIT || pending) return;
    canvas.setFont(&fonts::Font2); canvas.setTextSize(1); canvas.setTextColor(rgb(255, 225, 150));
    int full = canvas.textWidth("waiting..."), x = 64 - full / 2;
    canvas.drawString("waiting", x, 106);
    int dx = x + canvas.textWidth("waiting"), dots = 1 + ((int)(t * 2.5f) % 3);
    for (int i = 0; i < dots; i++) canvas.fillCircle(dx + 3 + i * 6, 119, 1, rgb(255, 225, 150));
}

// Botón: cualquier pulsación (sin distinguir corto/largo) confirma la atención pendiente y responde con un
// destello verde sutil, un parpadeo y una caricia. Lo demás (qué sesión se sigue) se decide por la API.
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t lastBtnAt = 0, clickAt = 0;
static long localAckSeq = 0;      // último aviso confirmado en este dispositivo (evita parpadeos repetidos por latencia)
static uint32_t nPress = 0;
static uint32_t wakeUntil = 0;       // un click despierta al gotchi dormido durante un rato

static void drawClickFlash(uint32_t now) {
    if (now - clickAt < 280) canvas.fillRectAlpha(0, 0, 128, 128, (uint8_t)(70 * (1.0f - (now - clickAt) / 280.0f)), rgb(90, 255, 160));
}

static void handleButton(uint32_t now) {
    if (!M5.BtnA.wasPressed()) return;
    lastBtnAt = now; clickAt = now; nPress++;
    if (gallery) { emoteIdx = emoteIdx % (E_COUNT - 1) + 1; startEmote((Emote)emoteIdx); return; }
    if (state != S_SLEEP) { blinking = true; blinkStart = now; }
    if (state == S_SLEEP) wakeUntil = now + 180000;                    // despertar (3 min)
    if (pending) { localAckSeq = R.attnSeq; ackRequested = true; }
    startReaction(R_PET);
    Serial.printf("button: press #%u (attn=%d seq=%ld)\n", (unsigned)nPress, (int)pending, R.attnSeq);
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
        if (spikeCount >= 4 && react != R_DIZZY && react != R_ANNOYED) {
            spikeCount = 0;
            // dormido o agitado repetidamente en poco tiempo = lo estás molestando
            static uint32_t lastShakeAt = 0;
            bool annoyed = state == S_SLEEP || (lastShakeAt && now - lastShakeAt < 30000);
            lastShakeAt = now;
            startReaction(annoyed ? R_ANNOYED : R_DIZZY);
            return;
        }
    }
    if (spikeCount > 0 && now - lastSpike > 1000) spikeCount = 0;
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

    // ── entrada: serie (debug) ──
    while (Serial.available()) {
        int c = Serial.read();
        if (c >= '0' && c < '0' + S_COUNT) { overrideUntil = now + 10000; setState((State)(c - '0')); }
        else if (c == 'a') { overrideUntil = 0; toolOverride = -1; }
        else if (c == 'T') { toolOverride = (toolOverride + 1) % T_COUNT; overrideUntil = now + 10000; setState(S_WORK); Serial.printf("tool=%s\n", TOOL_NAME[toolOverride]); }
        else if (c == 'e') { emoteIdx = emoteIdx % (E_COUNT - 1) + 1; startEmote((Emote)emoteIdx); }
        else if (c == 'g') { gallery = !gallery; galleryStart = now; overrideUntil = gallery ? 0xFFFFFFFFu : 0; if (gallery) { setState(S_IDLE); emote = E_NONE; emoteEndedAt = 0; } else emote = E_NONE; Serial.printf("gallery: %d\n", (int)gallery); }
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
    if (r.attnSeq < localAckSeq) localAckSeq = 0;               // el servidor se reinició: reiniciar la cuenta
    bool attnNow = r.attn && r.attnSeq > localAckSeq;           // parpadeo suave hasta que alguien pulse
    if (attnNow && !pending) pendingSince = now;
    pending = attnNow;
    if (now >= overrideUntil) {
        toolOverride = -1;
        int rs = r.state; Tool rt = (Tool)r.tool;
        if (rs == S_DONE && !pending) rs = S_IDLE;   // ya confirmada: no repetir la cara de "terminado"
        if (night && (rs == S_IDLE || rs == S_BORED)) rs = S_SLEEP;   // de noche duerme en vez de aburrirse
        if (rs == S_SLEEP && now < wakeUntil) rs = S_IDLE;            // lo has despertado con un click
        if (rs >= 0 && rs != state) {
            // saludo: primera actividad de la mañana
            if (hourNow >= 5 && hourNow < 13 && todayYday != lastGreetDay && (rs == S_ALERT || rs == S_WORK) &&
                (state == S_SLEEP || state == S_BORED || state == S_IDLE)) {
                lastGreetDay = todayYday; startReaction(R_GREET);
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
    if (gallery && now - galleryStart > 900000) { gallery = false; overrideUntil = 0; emote = E_NONE; }   // la galería caduca a los 15 min
    computeEmote(fxRaw, now, t);   // los emotes mandan sobre behaviors y reacciones
    if (gallery && emote == E_NONE && now - emoteEndedAt > 600) startEmote((Emote)(emoteIdx = emoteIdx % (E_COUNT - 1) + 1));
    // (los reflejos del IMU ganan sobre los behaviors: se calculan después y sobrescriben campos)

    // interpolar pose
    Pose tg = targetPose();
    float* cp = (float*)&cur; const float* tp = (const float*)&tg;
    // forma (8 primeros campos): muelle con ligero rebote; color: suavizado simple
    static float pv[8] = {0};
    float sdt = fminf(dt, 0.033f);
    for (int i = 0; i < 8; i++) { pv[i] += (260.0f * (tp[i] - cp[i]) - 19.0f * pv[i]) * sdt; cp[i] += pv[i] * sdt; }
    for (size_t i = 8; i < sizeof(Pose) / sizeof(float); i++) cp[i] = approach(cp[i], tp[i], 9.0f, dt);

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

    {
    float breathe = 1.0f + 0.025f * sinf(t * 2.0f);
    float b = blinkFactor(now);
    float cy = 62 + cur.yoff + fxSm[7];
    float dx = (cur.w + cur.gap) / 2.0f;
    for (int i = 0; i < 2; i++) {
        float s = fxSm[i];
        float h = fmaxf(4.0f, cur.h * breathe * s * (1.0f - b));
        float w = cur.w * (s > 1 ? 1 + (s - 1) * 0.6f : 1.0f);
        { float full = cur.h * breathe * s; if (full > 8) w *= 1 + 0.2f * clamp01(1 - h / full); }   // aplastar y estirar al parpadear
        float cx = 64 + (i ? dx : -dx) + fxSm[6];
        drawEye(i, cx, cy + fxSm[4 + i], w, h, cur, fxSm[2 + i], fxSm[9], fxSm[8], fxRaw.dizzy, fxRaw.sparkle, fxRaw.glyph, t, bg);
        eyeCx[i] = cx; eyeTop[i] = cy + fxSm[4 + i] - h / 2; eyeW[i] = w;
    }
    drawBrows();
    drawMouth(t);
    drawEffects(t, !pending);
    drawOverlays(t, now);
    drawEmoteOverlays(t, now);
    drawWaitMsg(t);
    }
    drawBorder(now);
    drawClickFlash(now);
    if (gallery && emote != E_NONE) {   // etiqueta del emote que se está viendo
        canvas.setFont(&fonts::Font2); canvas.setTextColor(rgb(180, 200, 230)); canvas.drawString(EMOTE_NAME[emote], 4, 2);
    }
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
        Serial.printf("fps=%.1f render=%.1fms push=%.1fms heap=%u wifi=%d state=%s tool=%s mood=%d attn=%d hour=%d |a|=%.2f\n",
                      frames * 1000.0f / (now - lastReport), accRender / 1000.0f / frames, accPush / 1000.0f / frames,
                      ESP.getFreeHeap(), (int)wifiUp, STATE_NAME[state], TOOL_NAME[curTool], mood, (int)pending, hourNow, lastMag);
        accRender = accPush = frames = 0; lastReport = now;
    }
    int32_t spare = 16 - (int32_t)((micros() - r0) / 1000);   // ~60 fps
    if (spare > 0) delay(spare);
}
