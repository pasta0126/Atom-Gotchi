import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../constants/ble_constants.dart';
import '../models/gotchi_state.dart';
import '../services/ble_service.dart';
import '../services/battery_service.dart';
import '../widgets/gotchi_face.dart';
import '../widgets/stat_bar.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> with WidgetsBindingObserver {
  late BatteryService _batteryService;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    final ble = context.read<BleService>();
    _batteryService = BatteryService(ble);
    // Arrancar escaneo automático
    ble.startScan();
    _batteryService.start();
  }

  @override
  void dispose() {
    _batteryService.stop();
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Consumer<BleService>(
      builder: (context, ble, _) {
        final state = ble.gotchiState;
        return Scaffold(
          backgroundColor: _darkBg(state.mood),
          body: SafeArea(
            child: Column(
              children: [
                _buildHeader(ble, state),
                Expanded(child: _buildFaceArea(state)),
                _buildMoodLabel(state),
                _buildStats(state),
                _buildActions(ble),
                const SizedBox(height: 12),
              ],
            ),
          ),
        );
      },
    );
  }

  // ── Header ────────────────────────────────────────────────────────────────

  Widget _buildHeader(BleService ble, GotchiState state) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Row(
        children: [
          const Text('AtomGotchi',
              style: TextStyle(
                  color: Colors.white,
                  fontSize: 20,
                  fontWeight: FontWeight.bold)),
          const Spacer(),
          // Indicador batería del teléfono
          if (state.phoneBatteryWarn)
            const Padding(
              padding: EdgeInsets.only(right: 8),
              child: Icon(Icons.battery_alert, color: Colors.redAccent, size: 20),
            ),
          // Estado BLE
          GestureDetector(
            onTap: () => ble.isConnected ? null : ble.startScan(),
            child: Row(
              children: [
                Icon(
                  ble.isConnected
                      ? Icons.bluetooth_connected
                      : ble.isScanning
                          ? Icons.bluetooth_searching
                          : Icons.bluetooth_disabled,
                  color: ble.isConnected
                      ? Colors.greenAccent
                      : ble.isScanning
                          ? Colors.blueAccent
                          : Colors.grey,
                  size: 22,
                ),
                const SizedBox(width: 4),
                Text(
                  ble.statusMsg,
                  style: TextStyle(
                    color: ble.isConnected ? Colors.greenAccent : Colors.grey,
                    fontSize: 12,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // ── Cara ──────────────────────────────────────────────────────────────────

  Widget _buildFaceArea(GotchiState state) {
    return Center(
      child: AnimatedSwitcher(
        duration: const Duration(milliseconds: 400),
        child: GotchiFace(
          key: ValueKey(state.mood),
          mood: state.mood,
          size: 240,
        ),
      ),
    );
  }

  // ── Mood label ────────────────────────────────────────────────────────────

  Widget _buildMoodLabel(GotchiState state) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Text(
        '${state.mood.emoji}  ${state.mood.label}',
        style: const TextStyle(
            color: Colors.white,
            fontSize: 22,
            fontWeight: FontWeight.w600,
            letterSpacing: 0.5),
      ),
    );
  }

  // ── Stats ─────────────────────────────────────────────────────────────────

  Widget _buildStats(GotchiState state) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 24),
      child: Column(
        children: [
          StatBar(
              label: 'Hambre',
              icon: '🍔',
              value: state.hunger,
              color: Colors.orange),
          StatBar(
              label: 'Sed',
              icon: '💧',
              value: state.thirst,
              color: Colors.lightBlue),
          StatBar(
              label: 'Energía',
              icon: '⚡',
              value: state.energy,
              color: Colors.greenAccent),
          Padding(
            padding: const EdgeInsets.only(top: 6),
            child: Row(
              children: [
                const Text('👟', style: TextStyle(fontSize: 16)),
                const SizedBox(width: 6),
                Text(
                  '${state.steps} pasos',
                  style: const TextStyle(color: Colors.white54, fontSize: 13),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // ── Botones de acción ─────────────────────────────────────────────────────

  Widget _buildActions(BleService ble) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceEvenly,
        children: [
          _actionBtn('🍔', 'Comer', Colors.orange,
              () => ble.sendCommand(BleConstants.cmdFeed)),
          _actionBtn('💧', 'Beber', Colors.lightBlue,
              () => ble.sendCommand(BleConstants.cmdDrink)),
          _actionBtn('🤗', 'Mimos', Colors.pinkAccent,
              () => ble.sendCommand(BleConstants.cmdPet)),
        ],
      ),
    );
  }

  Widget _actionBtn(
      String emoji, String label, Color color, VoidCallback onTap) {
    return GestureDetector(
      onTap: onTap,
      child: Column(
        children: [
          Container(
            width: 64,
            height: 64,
            decoration: BoxDecoration(
              color: color.withOpacity(0.2),
              borderRadius: BorderRadius.circular(16),
              border: Border.all(color: color.withOpacity(0.6), width: 2),
            ),
            child: Center(
                child: Text(emoji, style: const TextStyle(fontSize: 28))),
          ),
          const SizedBox(height: 4),
          Text(label,
              style: TextStyle(color: color, fontSize: 11)),
        ],
      ),
    );
  }

  // ── Helpers ───────────────────────────────────────────────────────────────

  Color _darkBg(GotchiMood mood) => switch (mood) {
        GotchiMood.angry    => const Color(0xFF2A0000),
        GotchiMood.scared   => const Color(0xFF050510),
        GotchiMood.sad      => const Color(0xFF020820),
        GotchiMood.sleeping => const Color(0xFF020208),
        GotchiMood.excited  => const Color(0xFF1A1800),
        GotchiMood.dizzy    => const Color(0xFF0D0018),
        _                   => const Color(0xFF0A1A12),
      };
}
