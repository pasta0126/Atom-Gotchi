/// UUIDs BLE — deben coincidir con el firmware
class BleConstants {
  static const String deviceName         = 'AtomGotchi';
  static const String serviceUuid        = '4fafc201-1fb5-459e-8fcc-c5c9c3319100';
  static const String stateCharUuid      = 'beb5483e-36e1-4688-b7f5-ea07361b2601';
  static const String cmdCharUuid        = 'beb5483e-36e1-4688-b7f5-ea07361b2602';
  static const String phoneCharUuid      = 'beb5483e-36e1-4688-b7f5-ea07361b2603';
  static const String contextCharUuid   = 'beb5483e-36e1-4688-b7f5-ea07361b2604';

  // Comandos → dispositivo
  static const int cmdFeed  = 0x01;
  static const int cmdDrink = 0x02;
  static const int cmdPet   = 0x03;
  static const int cmdPlay  = 0x04;
}
