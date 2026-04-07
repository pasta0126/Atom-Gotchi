#pragma once
#include <NimBLEDevice.h>
#include "GotchiState.h"

// ─── UUIDs ────────────────────────────────────────────────────────────────────
#define GOTCHI_SERVICE_UUID     "4fafc201-1fb5-459e-8fcc-c5c9c3319100"
#define GOTCHI_STATE_CHAR_UUID  "beb5483e-36e1-4688-b7f5-ea07361b2601"
#define GOTCHI_CMD_CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b2602"
#define GOTCHI_PHONE_CHAR_UUID  "beb5483e-36e1-4688-b7f5-ea07361b2603"
#define GOTCHI_DEVICE_NAME      "AtomGotchi"

// ─── Comandos app → dispositivo ──────────────────────────────────────────────
#define CMD_FEED   0x01
#define CMD_DRINK  0x02
#define CMD_PET    0x03
#define CMD_PLAY   0x04

// ─── Formato estado (6 bytes) ─────────────────────────────────────────────────
// [0] mood, [1] hunger, [2] thirst, [3] energy, [4-5] steps (big-endian)
// + [6] flags (bit0 = phoneBatteryWarn)

class GotchiBLE : public NimBLEServerCallbacks,
                  public NimBLECharacteristicCallbacks {
public:
    explicit GotchiBLE(GotchiState& state);
    void begin();
    void notifyState();  // Llamar cuando el estado cambie

    bool isConnected() const { return _connected; }

private:
    // Callbacks servidor
    void onConnect(NimBLEServer* pServer) override;
    void onDisconnect(NimBLEServer* pServer) override;

    // Callbacks característica (escrituras desde app)
    void onWrite(NimBLECharacteristic* pChar) override;

    GotchiState& _state;
    bool         _connected;

    NimBLEServer*         _pServer;
    NimBLECharacteristic* _pStateChar;
    NimBLECharacteristic* _pCmdChar;
    NimBLECharacteristic* _pPhoneChar;
};
