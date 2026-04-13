#include <M5Unified.h>
#include "GotchiState.h"
#include "GotchiDisplay.h"
#include "GotchiBLE.h"
#include "GotchiSensors.h"
#include "GotchiWiFi.h"

GotchiState   state;
GotchiDisplay display;
GotchiBLE     ble(state);
GotchiSensors sensors(state);
GotchiWiFi    wifi(state);

unsigned long lastBLENotify = 0;
static constexpr unsigned long BLE_NOTIFY_INTERVAL_MS = 3000;

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.internal_imu   = true;
    cfg.internal_mic   = true;
    cfg.internal_spk   = false;
    M5.begin(cfg);

    display.begin();   // Avatar arranca su propia tarea FreeRTOS
    sensors.begin();
    ble.begin();
    wifi.begin();

    Serial.println("[AtomGotchi] Listo!");
}

void loop() {
    M5.update();

    sensors.update();
    state.update();
    wifi.update();

    // Leer IMU crudo para la animación del display
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);

    // Leer micrófono RMS (reutilizamos el mismo buffer de GotchiSensors
    // pero tomamos el último valor conocido — el display lo usa solo
    // para animación, no para lógica de estado)
    // Pasamos ax/ay directamente; el display escala internamente.
    GotchiStats stats = state.getStats();
    display.update(stats, ax, ay, sensors.lastMicRms());

    // Notificar por BLE si hubo cambio o por intervalo
    unsigned long now = millis();
    bool notify = state.isMoodChanged() || (now - lastBLENotify >= BLE_NOTIFY_INTERVAL_MS);
    if (notify && ble.isConnected()) {
        ble.notifyState();
        lastBLENotify = now;
    }
    state.clearMoodChanged();

    delay(20); // ~50 Hz
}
