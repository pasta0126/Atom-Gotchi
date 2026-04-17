# AtomGotchi

A Tamagotchi-style digital companion running on the **M5Stack Atom S3R** (ESP32-S3). The device connects to a local WiFi network, a .NET 8 server manages all vitals and decay, and a browser dashboard lets you care for it from any device on the same network.

```
[Atom S3R]  ──WiFi──▶  POST /api/state        ─┐
                        GET  /api/command/next  ◀─┤  .NET 8 API + static web UI
[Browser]   ◀────────  GET  /api/state/current ─┤  (Docker on Mac Mini M1)
            ─────────▶ POST /api/command        ─┘
```

---

## Hardware

| Component    | Spec                                                         |
| ------------ | ------------------------------------------------------------ |
| Board        | [M5Stack Atom S3R](https://docs.m5stack.com/en/core/AtomS3R) |
| MCU          | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz                     |
| Display      | GC9107, 128×128 px TFT color                                 |
| IMU          | BMI270 6-axis (accel + gyro)                                 |
| Microphone   | PDM (GPIO 5/6)                                               |
| LED          | 1× WS2812B RGB                                               |
| Button       | 1× side button                                               |
| Connectivity | WiFi (802.11 b/g/n)                                          |

---

## Stats

Three vitals live on the server (0–100). The Atom reads them back every 3 s via the WiFi sync.

| Stat            | Decays                                                         | Recovers                                  | Critical threshold                              |
| --------------- | -------------------------------------------------------------- | ----------------------------------------- | ----------------------------------------------- |
| **Hunger** 🍎    | −0.5 / min → empty in ~3.3 h                                   | +40 per Feed                              | < 25 → HUNGRY mood; < 10 for 10 min → gets sick |
| **Happiness** 😊 | −0.33 / min → empty in ~5 h; extra −0.33/min while very hungry | +varies (see commands)                    | < 25 → SAD mood                                 |
| **Energy** ⚡    | −0.25 / min awake → empty in ~6.7 h                            | +0.80 / min while sleeping → full in ~2 h | —                                               |

---

## Lifecycle

The Gotchi can get sick and die if neglected. All timers run server-side, even when the Atom is offline.

```
Normal → Pooped → [not cleaned 15 min] → Sick → [no medicine 30 min] → Dead
Normal → Very hungry (hunger < 10) → [10 min] → Sick → [no medicine 30 min] → Dead
```

| Event                | Trigger                         | Effect                                    |
| -------------------- | ------------------------------- | ----------------------------------------- |
| 💩 Poop               | Every 30–90 min (random)        | Happiness −10; badge appears on dashboard |
| 🤒 Sick (from poop)   | Poop uncleaned for 15 min       | `sick = true`; SICK mood                  |
| 🤒 Sick (from hunger) | Hunger < 10 for 10 min          | `sick = true`; SICK mood                  |
| 💀 Dead               | Sick for 30 min untreated       | DEAD mood; all buttons disabled           |
| ✨ Restart            | "Reiniciar" button on dashboard | All stats reset to 80                     |

---

## Mood System

The firmware picks the **highest-priority active condition** each tick. Timed moods (`_Until` flags) expire automatically.

| Priority | Mood           | Trigger                                          | Duration                         |
| -------- | -------------- | ------------------------------------------------ | -------------------------------- |
| 1        | **DEAD** 💀     | dead flag from server                            | permanent until restart          |
| 2        | **SICK** 🤒     | sick flag from server                            | until medicine given             |
| 3        | **STARTLED** 😱 | fall or strong shake                             | 4 s (2–3 s if shake)             |
| 4        | **ANGRY** 😠    | face-down 30 s, or button rage                   | 8 s                              |
| 5        | **ANNOYED** 😤  | sustained loud noise, or too many pets           | 6 s (3 s tail after noise stops) |
| 6        | **DIZZY** 😵    | moderate sustained rotation                      | 4 s                              |
| 7        | **LAUGHING** 😄 | 1–5 button presses or pet command                | 2–2.5 s                          |
| 8        | **EXCITED** 🌟  | loud noise spike                                 | 1–5 s (scales with volume)       |
| 9        | **SLEEPING** 💤 | sleep toggle, face-up 5 min, or inactivity 5 min | until woken / energy full        |
| 10       | **HUNGRY** 😋   | hunger < 25                                      | until fed                        |
| 11       | **SAD** 😢      | happiness < 25                                   | until happiness rises            |
| 12       | **BORED** 😑    | no interaction for 3 min                         | until any interaction            |
| 13       | **HAPPY** 😊    | default                                          | —                                |

---

## Sensor Events (Firmware)

The IMU runs at 20 Hz and the microphone at ~3 Hz. Raw sensor data is converted into semantic events that update mood state.

### IMU — accelerometer

| Event          | Condition                                                                   | Fires                                       |
| -------------- | --------------------------------------------------------------------------- | ------------------------------------------- |
| **Fall**       | Accel drops below 0.35 g (free fall), then spikes above 2.5 g within 500 ms | `onFall()` → STARTLED 4 s                   |
| **Face down**  | Z-axis > 0.82 g for **30 s**                                                | `onFaceDown30s()` → ANGRY 8 s               |
| **Face up**    | Z-axis < −0.82 g, no movement, for **5 min**                                | `onFaceUp5min()` → induced sleep 10 min     |
| **Inactivity** | No movement (accel variation < 0.13 g) for **5 min**                        | `onInactivity5min()` → induced sleep 10 min |

### IMU — gyroscope

| Event              | Condition                                | Fires                              |
| ------------------ | ---------------------------------------- | ---------------------------------- |
| **Shake (light)**  | Gyro ≥ 60 °/s sustained for 500 ms       | wakes from sleep                   |
| **Shake (strong)** | Gyro ≥ 260 °/s sustained for 500 ms      | `onShake(strong)` → STARTLED 2–3 s |
| **Dizzy**          | Gyro 150–260 °/s sustained for **1.2 s** | `onDizzy()` → DIZZY 4 s            |

### Microphone

| Event               | Condition                          | Fires                                         |
| ------------------- | ---------------------------------- | --------------------------------------------- |
| **Noise spike**     | RMS > 400 (sampled every 300 ms)   | `onNoise()` → EXCITED 1–5 s (scales with RMS) |
| **Sustained noise** | RMS > 550 for **3 s** continuously | `onSustainedNoise(true)` → ANNOYED 6 s        |
| **Noise stops**     | RMS drops below threshold          | `onSustainedNoise(false)` → ANNOYED fades 3 s |

### Button (physical, side of device)

| Interaction | Condition                  | Effect                           |
| ----------- | -------------------------- | -------------------------------- |
| **Laugh**   | 1–5 presses within 8 s     | LAUGHING 2.5 s                   |
| **Annoyed** | More than 5 presses in 8 s | ANNOYED 5 s                      |
| **Rage**    | ≥ 5 presses within 2 s     | ANGRY 8 s (overrides everything) |

---

## Web Dashboard Commands

Open `http://<server-ip>:5000` in a browser. The dashboard polls state every 3 s.

### Care buttons

| Button                   | Stat effect (immediate, server-side)              | Animation on device |
| ------------------------ | ------------------------------------------------- | ------------------- |
| 🍎 **Alimentar**          | Hunger +40                                        | "feed"              |
| 🎮 **Jugar**              | Happiness +30, Energy −10                         | LAUGHING 2 s        |
| 🤗 **Acariciar**          | Happiness +15                                     | LAUGHING 2 s        |
| 💤 **Dormir / Despertar** | Toggles sleeping (energy recovers while on)       | —                   |
| 💊 **Medicina**           | Clears sick flag, Happiness +10                   | LAUGHING 2 s        |
| 🧹 **Limpiar**            | Clears poop flag, Happiness +5, resets poop timer | LAUGHING 2 s        |

### Fun buttons

| Button        | Effect                              |
| ------------- | ----------------------------------- |
| 🌀 **Agitar**  | Triggers `onShake(false)` on device |
| 💥 **Asustar** | Triggers `onFall()` → STARTLED 4 s  |

### Dashboard indicators

- **Dot / status bar** — green pulse = Atom connected (last update < 12 s ago)
- **Last seen** — timestamp of last sync from device
- **Age** — time since last restart
- **💩 badge** — poop pending; Limpiar button glows
- **🤒 badge** — sick; Medicina button glows
- **Stat bars** — green > 50, yellow 26–50, red ≤ 25

---

## WiFi Protocol

The Atom syncs with the server every **3 s** (non-blocking state machine):

| Direction     | Endpoint                 | Payload                                                                                           |
| ------------- | ------------------------ | ------------------------------------------------------------------------------------------------- |
| Atom → server | `POST /api/state`        | `{"mood": N}`                                                                                     |
| Server → Atom | Response to POST         | `{"hunger":N,"happiness":N,"energy":N,"sick":bool,"dead":bool,"needsClean":bool,"sleeping":bool}` |
| Atom → server | `GET /api/command/next`  | server returns `"feed"` / `"pet"` / `"shake"` / `"startle"` / `""` and clears it                  |
| Web → server  | `GET /api/state/current` | full state JSON + `updatedAt` + `ageHours`                                                        |
| Web → server  | `POST /api/command`      | `{"command":"feed"}` (or play/pet/sleep/medicine/clean/shake/startle/restart)                     |

---

## Project Structure

```
AtomGotchi/
├── firmware/                       # PlatformIO project (C++ / Arduino)
│   ├── src/
│   │   ├── main.cpp                # Main loop ~50 Hz, core 1
│   │   ├── GotchiState.h/cpp       # Mood logic, sensor event handlers
│   │   ├── GotchiDisplay.h/cpp     # Avatar rendering, LED animations
│   │   ├── GotchiSensors.h/cpp     # IMU (20 Hz) + mic (~3 Hz) + button
│   │   ├── GotchiWiFi.h/cpp        # Non-blocking WiFi state machine
│   │   └── credentials.h           # WiFi SSID/pass + API_URL (gitignored)
│   ├── lib/
│   │   └── M5Stack-Avatar/         # Patched local copy (do not replace)
│   └── platformio.ini
└── server/                         # .NET 8 Minimal API
    ├── Program.cs                  # 4 endpoints
    ├── GotchiStore.cs              # Singleton: vitals, decay timer, command queue
    ├── Models.cs                   # DTOs
    ├── wwwroot/index.html          # SPA dashboard (vanilla JS + Tailwind)
    └── docker-compose.yml
```

---

## Building & Flashing the Firmware

### Prerequisites
- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)

### First-time setup

Copy `firmware/src/credentials.h.example` → `firmware/src/credentials.h` and fill in:

```cpp
#define WIFI_SSID "your-network"
#define WIFI_PASS "your-password"
#define API_URL   "http://192.168.1.X:5000"   // no trailing slash
```

```bash
cd firmware

# Build
pio run -e m5stack-atoms3r

# Flash  (hold button while plugging USB to enter download mode)
pio run -e m5stack-atoms3r -t upload

# Serial monitor
pio device monitor --baud 115200
```

> `firmware/lib/M5Stack-Avatar/` is a patched copy tuned for the 128×128 display.
> **Do not replace it with the upstream version** — it will crash or render incorrectly.

---

## Running the Server

### Local (.NET 8 SDK required)

```bash
cd server
dotnet run
# Dashboard at http://localhost:5000
```

### Docker (Mac Mini M1)

```bash
cd server
docker compose up --build -d
docker compose logs -f
# Dashboard at http://<mac-mini-ip>:5000
```

---

## Avatar Library Patches

`firmware/lib/M5Stack-Avatar/` contains the following changes vs upstream:

| File                 | Change                                                                                             |
| -------------------- | -------------------------------------------------------------------------------------------------- |
| `Face.cpp`           | Fixed uninitialized `Balloon`, `Effect`, `BatteryIcon` pointers; added `COLOR_BORDER` bottom strip |
| `Avatar.cpp`         | Increased `drawLoop` stack 2048 → 8192 bytes                                                       |
| `Effect.h`           | Scaled icon positions from 320×240 → 128×128; icons use `COLOR_SECONDARY`                          |
| `ColorPalette.h/cpp` | Added `COLOR_BORDER` key; fixed `set()` bounds safety                                              |
