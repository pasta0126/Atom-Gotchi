# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Proyecto

AtomGotchi — compañero digital tipo Tamagotchi extendido para M5Stack Atom S3R, con app Android de gestión vía BLE.

## Estructura

```
firmware/   → PlatformIO (Arduino/C++) para el Atom S3R
app/        → Flutter para Android
```

## Firmware (PlatformIO)

```bash
# Compilar
pio run -e m5stack-atoms3r

# Flash
pio run -e m5stack-atoms3r -t upload

# Monitor serie
pio device monitor --baud 115200
```

**Dependencias clave:** `M5Unified`, `NimBLE-Arduino`

**Board:** `m5stack-atoms3` (compatible con Atom S3R)

### Arquitectura firmware

| Clase | Rol |
|---|---|
| `GotchiState` | Máquina de estados: hunger/thirst/energy/steps + moods. Toda la lógica de transición vive aquí. |
| `GotchiDisplay` | Dibuja en LCD 128×128 con M5GFX. LED RGB vía `M5.dis`. |
| `GotchiSensors` | Lectura IMU (pasos + mareo), micrófono PDM (RMS), botón. |
| `GotchiBLE` | Servidor GATT NimBLE. Notifica estado, recibe comandos y batería del teléfono. |

**Prioridad de moods** (de mayor a menor): SCARED (sin BT) → ANGRY → DIZZY → LAUGHING → EXCITED → SLEEPING → TIRED → HUNGRY → THIRSTY → SAD → HAPPY

## App Flutter

```bash
cd app

# Instalar dependencias
flutter pub get

# Ejecutar en dispositivo
flutter run

# Build APK
flutter build apk --release
```

**Dependencias clave:** `flutter_blue_plus`, `battery_plus`, `permission_handler`, `provider`

### Arquitectura app

| Archivo | Rol |
|---|---|
| `services/ble_service.dart` | ChangeNotifier: escaneo, conexión BLE, recepción de estado, envío de comandos. |
| `services/battery_service.dart` | Monitoriza batería del teléfono y la envía al dispositivo cada 60s. |
| `models/gotchi_state.dart` | Parsea el payload BLE de 7 bytes → `GotchiState`. |
| `screens/home_screen.dart` | UI principal: cara, stats, botones de acción. |
| `widgets/gotchi_face.dart` | CustomPainter que replica las mismas expresiones que el LCD. |

## Protocolo BLE

| UUID | Tipo | Descripción |
|---|---|---|
| `4fafc201-...9100` | Service | Servicio principal |
| `...2601` | READ+NOTIFY | Estado: `[mood, hunger, thirst, energy, steps_hi, steps_lo, flags]` |
| `...2602` | WRITE | Comando: `0x01`=comer, `0x02`=beber, `0x03`=mimos |
| `...2603` | WRITE | Batería teléfono: `[level, flags]` (bit0=charging) |

Device name: `AtomGotchi`

## Hardware Atom S3R relevante

- **Display:** GC9107, 128×128, accedido via `M5.Display` (M5GFX)
- **IMU:** BMI270 6-axis, accedido via `M5.Imu.getAccel()` / `getGyro()`
- **Micrófono:** PDM (DATA=GPIO6, CLK=GPIO5), accedido via `M5.Mic.record()`
- **LED RGB:** 1× WS2812B, accedido via `M5.Leds.setPixelColor(0, color)` + `M5.Leds.show()`
- **Botón:** `M5.BtnA.wasPressed()`
