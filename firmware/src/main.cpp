#include <M5Unified.h>
#include "GotchiState.h"
#include "GotchiDisplay.h"
#include "GotchiSensors.h"
#include "GotchiWiFi.h"

GotchiState   state;
GotchiDisplay display;
GotchiSensors sensors(state);
GotchiWiFi    wifi(state);

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.internal_imu   = true;
    cfg.internal_mic   = true;
    cfg.internal_spk   = false;
    M5.begin(cfg);

    display.begin();   // Avatar arranca su propia tarea FreeRTOS
    sensors.begin();
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

    GotchiStats stats = state.getStats();
    display.update(stats, ax, ay, sensors.lastMicRms());

    state.clearMoodChanged();

    delay(20); // ~50 Hz
}
