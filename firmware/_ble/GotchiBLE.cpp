#include "GotchiBLE.h"
#include <Arduino.h>

GotchiBLE::GotchiBLE(GotchiState& state)
    : _state(state), _connected(false),
      _pServer(nullptr), _pStateChar(nullptr),
      _pCmdChar(nullptr), _pPhoneChar(nullptr), _pContextChar(nullptr)
{}

void GotchiBLE::begin() {
    NimBLEDevice::init(GOTCHI_DEVICE_NAME);
    NimBLEDevice::setMTU(64);

    _pServer = NimBLEDevice::createServer();
    _pServer->setCallbacks(this);

    NimBLEService* pService = _pServer->createService(GOTCHI_SERVICE_UUID);

    // Característica de estado (lectura + notificación → app)
    _pStateChar = pService->createCharacteristic(
        GOTCHI_STATE_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    // Característica de comandos (escritura desde app)
    _pCmdChar = pService->createCharacteristic(
        GOTCHI_CMD_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _pCmdChar->setCallbacks(this);

    // Característica de estado del teléfono (escritura desde app)
    _pPhoneChar = pService->createCharacteristic(
        GOTCHI_PHONE_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _pPhoneChar->setCallbacks(this);

    // Característica de contexto: [hora(0-23), temp_c(signed), flags]
    _pContextChar = pService->createCharacteristic(
        GOTCHI_CONTEXT_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _pContextChar->setCallbacks(this);

    pService->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(GOTCHI_SERVICE_UUID);
    pAdv->setScanResponse(true);
    pAdv->start();

    Serial.println("[BLE] Advertising as '" GOTCHI_DEVICE_NAME "'");
}

void GotchiBLE::notifyState() {
    if (!_connected || !_pStateChar) return;

    GotchiStats s = _state.getStats();
    uint8_t payload[7];
    payload[0] = (uint8_t)s.mood;
    payload[1] = s.hunger;
    payload[2] = s.thirst;
    payload[3] = s.energy;
    payload[4] = (s.steps >> 8) & 0xFF;
    payload[5] = s.steps & 0xFF;
    payload[6] = s.phoneBatteryWarn ? 0x01 : 0x00;

    _pStateChar->setValue(payload, sizeof(payload));
    _pStateChar->notify();
}

// ─── Callbacks servidor ───────────────────────────────────────────────────────

void GotchiBLE::onConnect(NimBLEServer* /*pServer*/) {
    _connected = true;
    _state.onBTConnect();
    Serial.println("[BLE] Cliente conectado");
}

void GotchiBLE::onDisconnect(NimBLEServer* /*pServer*/) {
    _connected = false;
    _state.onBTDisconnect();
    Serial.println("[BLE] Cliente desconectado — reiniciando advertising");
    NimBLEDevice::getAdvertising()->start();
}

// ─── Callbacks escritura ──────────────────────────────────────────────────────

void GotchiBLE::onWrite(NimBLECharacteristic* pChar) {
    std::string uuid = pChar->getUUID().toString();

    if (uuid == GOTCHI_CMD_CHAR_UUID) {
        // Comando de la app
        std::string val = pChar->getValue();
        if (val.empty()) return;
        uint8_t cmd = (uint8_t)val[0];
        switch (cmd) {
            case CMD_FEED:  _state.feed();  break;
            case CMD_DRINK: _state.drink(); break;
            case CMD_PET:   _state.pet();   break;
            default: break;
        }

    } else if (uuid == GOTCHI_PHONE_CHAR_UUID) {
        // Estado batería del teléfono: [0]=nivel, [1]=flags
        std::string val = pChar->getValue();
        if (val.size() < 1) return;
        uint8_t level    = (uint8_t)val[0];
        bool    charging = (val.size() >= 2) && (val[1] & 0x01);
        _state.onPhoneBattery(level, charging);

    } else if (uuid == GOTCHI_CONTEXT_CHAR_UUID) {
        // Contexto del teléfono: [0]=hora(0-23), [1]=temp_c(signed), [2]=flags(reservado)
        std::string val = pChar->getValue();
        if (val.size() < 2) return;
        uint8_t hour  = (uint8_t)val[0] % 24;
        int8_t  tempC = (int8_t)val[1];
        _state.onContext(hour, tempC);
    }
}
