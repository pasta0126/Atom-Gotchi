# AtomGotchi

A Tamagotchi-style digital companion running on the **M5Stack Atom S3R**, managed from Android via Bluetooth Low Energy.

The device shows an animated face that reacts to its internal state, the environment (sound, movement, orientation) and commands sent from the phone. It gets hungry, thirsty, tired, scared, dizzy, and happy — and lets you know.

---

## Hardware

| Component | Spec |
|---|---|
| Board | [M5Stack Atom S3R](https://docs.m5stack.com/en/core/AtomS3R) |
| MCU | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| Display | GC9107, 128×128 px, TFT color |
| IMU | BMI270 6-axis (accel + gyro) |
| Microphone | PDM (GPIO 5/6) |
| LED | 1× WS2812B RGB |
| Button | 1× side button |
| Connectivity | BLE 5.0 (NimBLE) |

---

## Features

### Firmware
- **Animated face** via [M5Stack-Avatar](https://github.com/meganetaaan/m5stack-avatar) (local patched copy)  
  Eyes and mouth react to mood, gaze follows tilt, autonomous saccades, auto-blink
- **13 moods** with a priority system — the highest-priority active condition wins
- **Stat system**: hunger, thirst, energy (0–100), step counter
  - Stats decay primarily from steps walked, secondarily from time
  - Energy recovers while sleeping (faster at night)
- **Sensor-driven events**
  - Step detection from accelerometer
  - Fall detection (free-fall + impact)
  - Shake detection (light → dizzy, strong → startled)
  - Orientation: face-down 30 s → grumpy; face-up 5 min → induced sleep
  - Sustained loud noise (> 3 s) → annoyed
  - Inactivity 5 min → sleepy
  - Sound spikes → eyes dart toward source
  - IMU tilt → eye direction shift
- **Button interactions**: 1-5 presses = laughing; mash > 5 = annoyed; very rapid = angry
- **Night mode**: if the phone sends the current hour (22h–7h) and energy is low → sleep
- **BLE server** (GATT / NimBLE): notifies state, receives feed/drink/pet commands and phone context
- **Display style**: black background, white face (manga aesthetic), colored mood icons (♥ 💢 ~~~ Zzz 💧), 0.5 s colored border flash on mood change

### Android App
- Scans and connects to the device automatically
- Shows a live rendering of the Gotchi face matching the device expression
- Stat bars (hunger / thirst / energy) with emoji icons
- Action buttons: Feed 🍔 · Drink 💧 · Pet 🤗
- Sends phone battery level + current hour and temperature every 60 s

---

## Mood System

Priority (highest → lowest):

| Priority | Mood | Trigger |
|---|---|---|
| 1 | **SCARED** | BLE disconnected |
| 2 | **STARTLED** | Fall or strong shake |
| 3 | **ANGRY** | Button mashed rapidly |
| 4 | **ANNOYED** | Sustained noise or too many pets |
| 5 | **DIZZY** | Moderate sustained rotation |
| 6 | **LAUGHING** | 1–5 button presses |
| 7 | **EXCITED** | Energy or hunger just restored |
| 8 | **SLEEPING** | Low energy at night / induced |
| 9 | **TIRED** | Energy < 30 % |
| 10 | **HUNGRY** | Hunger < 25 % |
| 11 | **THIRSTY** | Thirst < 25 % |
| 12 | **SAD** | Multiple stats low |
| 13 | **HAPPY** | Default |

---

## BLE Protocol

Device name: **`AtomGotchi`**  
Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b` (base `...9100`)

| Characteristic | UUID suffix | Mode | Format |
|---|---|---|---|
| State | `...2601` | READ + NOTIFY | `[mood, hunger, thirst, energy, steps_hi, steps_lo, flags]` (7 bytes) |
| Command | `...2602` | WRITE | `0x01` feed · `0x02` drink · `0x03` pet |
| Phone battery | `...2603` | WRITE | `[level_0-100, flags]` (bit 0 = charging) |
| Context | `...2604` | WRITE | `[hour_0-23, temp_celsius_int8]` |

---

## Project Structure

```
AtomGotchi/
├── firmware/               # PlatformIO project (C++ / Arduino)
│   ├── src/
│   │   ├── main.cpp
│   │   ├── GotchiState.h/cpp      # Mood & stat logic
│   │   ├── GotchiDisplay.h/cpp    # Avatar rendering, LED
│   │   ├── GotchiSensors.h/cpp    # IMU, mic, button
│   │   └── GotchiBLE.h/cpp        # GATT server
│   ├── lib/
│   │   └── M5Stack-Avatar/        # Patched local copy of the avatar library
│   └── platformio.ini
└── app/                    # Flutter Android app
    └── lib/
        ├── models/gotchi_state.dart
        ├── services/ble_service.dart
        ├── services/battery_service.dart
        ├── screens/home_screen.dart
        └── widgets/
            ├── gotchi_face.dart
            └── stat_bar.dart
```

---

## Building & Flashing the Firmware

### Prerequisites
- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- USB cable to Atom S3R

```bash
cd firmware

# Build
pio run -e m5stack-atoms3r

# Flash  (put Atom in download mode: hold button while connecting USB)
pio run -e m5stack-atoms3r -t upload

# Serial monitor
pio device monitor --baud 115200
```

> The Avatar library lives in `firmware/lib/M5Stack-Avatar/` and contains custom patches
> for the 128×128 display. Do **not** replace it with the upstream version.

---

## Building the Android App

### Prerequisites
- [Flutter SDK](https://flutter.dev/docs/get-started/install) ≥ 3.x
- Android device with Bluetooth LE (API 21+)
- USB debugging enabled on the device

```bash
cd app

# Install dependencies
flutter pub get

# Run on connected device
flutter run

# Build release APK
flutter build apk --release
# Output: app/build/outputs/flutter-apk/app-release.apk
```

### Required Android permissions
Declared in `AndroidManifest.xml` — granted at runtime by the app:
- `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT` (Android 12+)
- `ACCESS_FINE_LOCATION` (required for BLE scan on older Android)

---

## Avatar Library Patches

The `firmware/lib/M5Stack-Avatar/` copy contains the following changes vs upstream:

| File | Change |
|---|---|
| `Face.cpp` | Fixed uninitialized `Balloon`, `Effect`, `BatteryIcon` pointers; added `COLOR_BORDER` bottom strip |
| `Avatar.cpp` | Increased `drawLoop` stack from 2048 → 8192 bytes |
| `Effect.h` | Scaled icon positions from 320×240 → 128×128; icons use `COLOR_SECONDARY` (mood accent color) |
| `ColorPalette.h/cpp` | Added `COLOR_BORDER` key; fixed `set()` to handle new keys without UB |

---

## License

[MIT](LICENSE)
