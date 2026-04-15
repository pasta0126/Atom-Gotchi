#include "GotchiWiFi.h"

GotchiWiFi::GotchiWiFi(GotchiState& state)
    : _state(state), _lastPoll(0), _connectStart(0), _connecting(false) {}

// ── Public ────────────────────────────────────────────────────────────────────

void GotchiWiFi::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    _startConnect();
}

void GotchiWiFi::update() {
    // Si estamos en proceso de conexión, comprobar si ya conectó o timeout
    if (_connecting) {
        if (WiFi.status() == WL_CONNECTED) {
            _connecting = false;
            Serial.printf("[WiFi] Conectado — IP: %s\n", WiFi.localIP().toString().c_str());
        } else if (millis() - _connectStart > CONNECT_TIMEOUT) {
            Serial.println("[WiFi] Timeout — reintentando...");
            _connecting = false;
            WiFi.disconnect(true);
        }
        return;
    }

    // Si no hay conexión, reintentar
    if (WiFi.status() != WL_CONNECTED) {
        _startConnect();
        return;
    }

    // Polling periódico
    unsigned long now = millis();
    if (now - _lastPoll < POLL_MS) return;
    _lastPoll = now;

    _pollCommand();
    _pushState();
}

// ── Private ───────────────────────────────────────────────────────────────────

void GotchiWiFi::_startConnect() {
    Serial.printf("[WiFi] Conectando a %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    _connectStart = millis();
    _connecting   = true;
}

void GotchiWiFi::_pollCommand() {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(API_URL) + "/api/command/next";
    if (!http.begin(client, url)) return;
    http.setTimeout(HTTP_TIMEOUT_MS);

    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        String body = http.getString();
        body.toLowerCase();
        if      (body.indexOf("pet")     >= 0) _state.pet();
        else if (body.indexOf("shake")   >= 0) _state.onShake(false);
        else if (body.indexOf("startle") >= 0) _state.onFall();
        else if (body.indexOf("noise")   >= 0) _state.onNoise(600.0f);
    } else if (code < 0) {
        Serial.printf("[WiFi] pollCommand error: %s\n", http.errorToString(code).c_str());
    }
    http.end();
}

void GotchiWiFi::_pushState() {
    GotchiStats s = _state.getStats();

    // Construir JSON manualmente — sin dependencias extra
    String body  = "{\"mood\":"  + String((uint8_t)s.mood)
                 + ",\"steps\":" + String(s.steps)
                 + "}";

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(API_URL) + "/api/state";
    if (!http.begin(client, url)) return;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(body);
    if (code < 0) {
        Serial.printf("[WiFi] pushState error: %s\n", http.errorToString(code).c_str());
    }
    http.end();
}
