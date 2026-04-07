import 'dart:async';
import 'package:battery_plus/battery_plus.dart';
import 'ble_service.dart';

/// Monitoriza la batería del teléfono y la envía al Gotchi cada 60 segundos
/// o cuando cambia el estado de carga.
class BatteryService {
  final BleService _bleService;
  final Battery    _battery = Battery();

  Timer?              _timer;
  StreamSubscription? _stateSub;
  int  _lastLevel  = -1;
  bool _lastCharging = false;

  BatteryService(this._bleService);

  void start() {
    _sendBatteryInfo();
    _timer = Timer.periodic(const Duration(seconds: 60), (_) => _sendBatteryInfo());

    _stateSub = _battery.onBatteryStateChanged.listen((state) {
      final charging = state == BatteryState.charging || state == BatteryState.full;
      if (charging != _lastCharging) {
        _lastCharging = charging;
        _sendBatteryInfo();
      }
    });
  }

  Future<void> _sendBatteryInfo() async {
    try {
      final level    = await _battery.batteryLevel;
      final state    = await _battery.batteryState;
      final charging = state == BatteryState.charging || state == BatteryState.full;

      if (level != _lastLevel || charging != _lastCharging) {
        _lastLevel   = level;
        _lastCharging = charging;
        await _bleService.sendPhoneBattery(level, charging: charging);
      }
    } catch (_) {}
  }

  void stop() {
    _timer?.cancel();
    _stateSub?.cancel();
  }
}
