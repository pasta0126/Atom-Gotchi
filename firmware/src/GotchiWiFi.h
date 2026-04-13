#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "GotchiState.h"
#include "credentials.h"

class GotchiWiFi {
public:
    explicit GotchiWiFi(GotchiState& state);

    void begin();   // llamar en setup()
    void update();  // llamar en loop() — no bloqueante

private:
    void _startConnect();
    void _pollCommand();
    void _pushState();

    GotchiState&  _state;
    unsigned long _lastPoll;
    unsigned long _connectStart;
    bool          _connecting;

    static constexpr unsigned long POLL_MS        = 3000;
    static constexpr unsigned long CONNECT_TIMEOUT = 15000;
    static constexpr int           HTTP_TIMEOUT_MS = 5000;
};
