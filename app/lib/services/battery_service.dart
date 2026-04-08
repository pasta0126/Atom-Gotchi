import 'dart:async';
import 'package:battery_plus/battery_plus.dart';
import 'ble_service.dart';

/// Monitoriza la batería del teléfono y envía batería + contexto (hora, temperatura)
/// al Gotchi cada 60 segundos o cuando cambia el estado de carga.
class BatteryService {
  final BleService _bleService;
  final Battery    _battery = Battery();

  Timer?              _timer;
  StreamSubscription? _stateSub;
  int  _lastLevel    = -1;
  bool _lastCharging = false;

  BatteryService(this._bleService);

  void start() {
    _sendAll();
    _timer = Timer.periodic(const Duration(seconds: 60), (_) => _sendAll());

    _stateSub = _battery.onBatteryStateChanged.listen((state) {
      final charging = state == BatteryState.charging || state == BatteryState.full;
      if (charging != _lastCharging) {
        _lastCharging = charging;
        _sendAll();
      }
    });
  }

  Future<void> _sendAll() async {
    await _sendBatteryInfo();
    await _sendContext();
  }

  Future<void> _sendBatteryInfo() async {
    try {
      final level    = await _battery.batteryLevel;
      final state    = await _battery.batteryState;
      final charging = state == BatteryState.charging || state == BatteryState.full;

      if (level != _lastLevel || charging != _lastCharging) {
        _lastLevel    = level;
        _lastCharging = charging;
        await _bleService.sendPhoneBattery(level, charging: charging);
      }
    } catch (_) {}
  }

  Future<void> _sendContext() async {
    try {
      final now  = DateTime.now();
      // Temperatura: usamos un valor fijo de 20°C (sin sensor de temperatura real en el teléfono).
      // Se puede mejorar en el futuro con un plugin de clima.
      const tempC = 20;
      await _bleService.sendContext(hour: now.hour, tempC: tempC);
    } catch (_) {}
  }

  void stop() {
    _timer?.cancel();
    _stateSub?.cancel();
  }
}
