import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../constants/ble_constants.dart';
import '../models/gotchi_state.dart';

class BleService extends ChangeNotifier {
  BluetoothDevice? _device;
  BluetoothCharacteristic? _stateChar;
  BluetoothCharacteristic? _cmdChar;
  BluetoothCharacteristic? _phoneChar;

  GotchiState _gotchiState = const GotchiState();
  bool _connected = false;
  bool _scanning  = false;
  String _statusMsg = 'Desconectado';

  GotchiState get gotchiState  => _gotchiState;
  bool        get isConnected  => _connected;
  bool        get isScanning   => _scanning;
  String      get statusMsg    => _statusMsg;

  StreamSubscription? _scanSub;
  StreamSubscription? _stateSub;
  StreamSubscription? _connSub;

  // ── Escanear y conectar ───────────────────────────────────────────────────

  Future<void> startScan() async {
    if (_scanning) return;
    _scanning  = true;
    _statusMsg = 'Buscando AtomGotchi…';
    notifyListeners();

    await FlutterBluePlus.startScan(
      withName: BleConstants.deviceName,
      timeout: const Duration(seconds: 15),
    );

    _scanSub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        if (r.device.platformName == BleConstants.deviceName) {
          FlutterBluePlus.stopScan();
          _connectTo(r.device);
          break;
        }
      }
    });

    FlutterBluePlus.isScanning.listen((scanning) {
      if (!scanning && !_connected) {
        _scanning  = false;
        _statusMsg = 'No encontrado. Pulsa para reintentar.';
        notifyListeners();
      }
    });
  }

  Future<void> _connectTo(BluetoothDevice device) async {
    _scanning  = false;
    _statusMsg = 'Conectando…';
    notifyListeners();

    try {
      await device.connect(timeout: const Duration(seconds: 10));
      _device = device;

      _connSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          _onDisconnected();
        }
      });

      await _discoverServices();
      _connected = true;
      _statusMsg = 'Conectado ✓';
      notifyListeners();

      // Marcar BT conectado en gotchi (actualización optimista)
      _gotchiState = GotchiState(
        mood:   _gotchiState.mood == GotchiMood.scared ? GotchiMood.happy : _gotchiState.mood,
        hunger: _gotchiState.hunger,
        thirst: _gotchiState.thirst,
        energy: _gotchiState.energy,
        steps:  _gotchiState.steps,
      );
    } catch (e) {
      _statusMsg = 'Error de conexión';
      _scanning  = false;
      notifyListeners();
    }
  }

  Future<void> _discoverServices() async {
    final services = await _device!.discoverServices();
    for (final s in services) {
      if (s.uuid.toString().toLowerCase() == BleConstants.serviceUuid) {
        for (final c in s.characteristics) {
          final uuid = c.uuid.toString().toLowerCase();
          if (uuid == BleConstants.stateCharUuid) {
            _stateChar = c;
            await c.setNotifyValue(true);
            _stateSub = c.onValueReceived.listen(_onStateReceived);
          } else if (uuid == BleConstants.cmdCharUuid) {
            _cmdChar = c;
          } else if (uuid == BleConstants.phoneCharUuid) {
            _phoneChar = c;
          }
        }
      }
    }
  }

  void _onStateReceived(List<int> bytes) {
    _gotchiState = GotchiState.fromBytes(bytes);
    notifyListeners();
  }

  void _onDisconnected() {
    _connected  = false;
    _statusMsg  = 'Desconectado';
    _stateChar  = null;
    _cmdChar    = null;
    _phoneChar  = null;
    _stateSub?.cancel();
    _connSub?.cancel();
    _gotchiState = GotchiState(mood: GotchiMood.scared);
    notifyListeners();
  }

  // ── Comandos → dispositivo ────────────────────────────────────────────────

  Future<void> sendCommand(int cmd) async {
    if (_cmdChar == null) return;
    try {
      await _cmdChar!.write([cmd], withoutResponse: true);
    } catch (_) {}
  }

  Future<void> sendPhoneBattery(int level, {bool charging = false}) async {
    if (_phoneChar == null) return;
    try {
      await _phoneChar!.write([level, charging ? 0x01 : 0x00], withoutResponse: true);
    } catch (_) {}
  }

  Future<void> disconnect() async {
    await _device?.disconnect();
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    _stateSub?.cancel();
    _connSub?.cancel();
    super.dispose();
  }
}
