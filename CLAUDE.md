# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Project

**AtomGotchi** — Tamagotchi-style digital companion for M5Stack Atom S3R with an Android BLE management app.

```
firmware/   → PlatformIO (Arduino/C++) for the Atom S3R
app/        → Flutter for Android
```

---

## Firmware (PlatformIO)

```bash
cd firmware
pio run -e m5stack-atoms3r                    # build
pio run -e m5stack-atoms3r -t upload          # flash (hold button while connecting)
pio device monitor --baud 115200              # serial monitor
```

**Board:** `m5stack-atoms3` (compatible with Atom S3R)

**External deps:** `M5Unified@^0.2.2`, `NimBLE-Arduino@^1.4.1`

**Local lib:** `firmware/lib/M5Stack-Avatar/` — patched copy, do NOT replace with upstream.

### Class responsibilities

| Class | Role |
|---|---|
| `GotchiState` | Mood/stat machine: hunger, thirst, energy, steps. All transition logic. |
| `GotchiDisplay` | M5Stack-Avatar face, LED, neutral-palette + mood-flash system. |
| `GotchiSensors` | IMU (steps, fall, shake, orientation, dizzy), mic RMS, button. |
| `GotchiBLE` | NimBLE GATT server: state notify, command/battery/context write. |

### Mood priority (high → low)

SCARED → STARTLED → ANGRY → ANNOYED → DIZZY → LAUGHING → EXCITED → SLEEPING → TIRED → HUNGRY → THIRSTY → SAD → HAPPY

### Display design

- Background always **black**, face **white** (manga style)
- On mood change: 0.5 s colored bottom border flash (5 px strip via `COLOR_BORDER`)
- Manga emotion icons (heart, anger mark, chill bars, bubbles, sweat) in `COLOR_SECONDARY` (mood accent color) — always visible
- Avatar `start(16)` for 16-bit color; `drawLoop` stack 8192 bytes; `addTask` called after `start()`

### Stat decay

- Hunger/thirst decay primarily by **steps** (−1 per 100/80 steps), minimal time decay
- Energy decays by time; recovers while sleeping (+0.015/s, ×1.5 at night)
- Night = hour 22–7 (sent from phone via BLE context characteristic)

### Sensor thresholds

| Sensor | Threshold | Event |
|---|---|---|
| Accelerometer magnitude | < 0.35 g then > 2.5 g within 500 ms | Fall → STARTLED |
| Gyro magnitude | 60–260 °/s for > 1.2 s | DIZZY |
| Gyro magnitude | > 260 °/s for > 500 ms | Shake strong → STARTLED |
| Axis Z | > 0.82 g face-down for > 30 s | ANNOYED (grumpy) |
| Axis Z | > 0.82 g face-up, no movement, > 5 min | Induced sleep |
| Mic RMS | > 550 for > 3 s | ANNOYED |
| No movement | variance < 0.13 g for > 5 min | Sleepy |

---

## App (Flutter)

```bash
cd app
flutter pub get
flutter run
flutter build apk --release
```

**Key deps:** `flutter_blue_plus`, `battery_plus`, `permission_handler`, `provider`

### File responsibilities

| File | Role |
|---|---|
| `services/ble_service.dart` | ChangeNotifier: scan, connect, state RX, command TX. |
| `services/battery_service.dart` | Monitors phone battery + sends battery + context every 60 s. |
| `models/gotchi_state.dart` | Parses 7-byte BLE payload → `GotchiState`. 13 moods. |
| `screens/home_screen.dart` | Main UI: face, stat bars (emoji only, no labels), action buttons. |
| `widgets/gotchi_face.dart` | CustomPainter replicating device expressions for all 13 moods. |
| `widgets/stat_bar.dart` | Emoji icon + LinearProgressIndicator. No text label. |

---

## BLE Protocol

Device name: `AtomGotchi`  
Service: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

| UUID suffix | Direction | Format |
|---|---|---|
| `...2601` | READ + NOTIFY | `[mood, hunger, thirst, energy, steps_hi, steps_lo, flags]` |
| `...2602` | Phone → Device | `0x01` feed · `0x02` drink · `0x03` pet |
| `...2603` | Phone → Device | `[battery_level, flags]` (bit0 = charging) |
| `...2604` | Phone → Device | `[hour, temp_int8]` |

---

## Hardware (Atom S3R)

| Peripheral | API |
|---|---|
| Display GC9107 128×128 | `M5.Display` (M5GFX) |
| IMU BMI270 | `M5.Imu.getAccel()` / `getGyro()` |
| Mic PDM | `M5.Mic.record()` |
| LED WS2812B | `M5.Led.setColor(0, r, g, b)` |
| Button | `M5.BtnA.wasPressed()` |

---

## Avatar Library Patches (firmware/lib/M5Stack-Avatar/)

| File | Patch |
|---|---|
| `Face.cpp` | Init `b`, `h`, `battery` in 13-arg constructor; draw `COLOR_BORDER` bottom strip |
| `Avatar.cpp` | `drawLoop` stack 2048 → 8192 |
| `Effect.h` | Positions scaled 320×240 → 128×128; icon color = `COLOR_SECONDARY` |
| `ColorPalette.h` | Added `#define COLOR_BORDER "border"` |
| `ColorPalette.cpp` | Added `COLOR_BORDER` to default constructor; fixed `set()` UB with unknown keys |
