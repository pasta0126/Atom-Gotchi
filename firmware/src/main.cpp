#include <M5Unified.h>
#include "GotchiState.h"
#include "GotchiDisplay.h"
#include "GotchiBLE.h"
#include "GotchiSensors.h"

GotchiState   state;
GotchiDisplay display;
GotchiBLE     ble(state);
GotchiSensors sensors(state);

unsigned long lastBLENotify = 0;
static constexpr unsigned long BLE_NOTIFY_INTERVAL_MS = 3000;

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.internal_imu   = true;
    cfg.internal_mic   = true;
    cfg.internal_spk   = false;
    M5.begin(cfg);

    display.begin();
    sensors.begin();
    ble.begin();

    Serial.println("[AtomGotchi] Listo!");
}

void loop() {
    M5.update(); // actualiza botones, IMU, etc.

    sensors.update();
    state.update();

    // Notificar por BLE si hubo cambio de estado o por intervalo
    unsigned long now = millis();
    bool notify = state.isMoodChanged() || (now - lastBLENotify >= BLE_NOTIFY_INTERVAL_MS);
    if (notify && ble.isConnected()) {
        ble.notifyState();
        lastBLENotify = now;
    }
    state.clearMoodChanged();

    // Actualizar pantalla
    display.update(state.getStats());

    delay(20); // ~50 Hz main loop
}
