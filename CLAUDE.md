# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AtomGotchi is a Tamagotchi-style device running on the M5Stack Atom S3R (ESP32-S3). The device connects to a local WiFi network and communicates with a .NET 8 web server. A browser-based dashboard displays the Gotchi's state and sends commands.

```
[AtomGotchi] — WiFi —▶ POST /api/state     ─┐
                        GET  /api/command/next ◀─┤  .NET 8 API + static web UI
[Browser]    ◀──────── GET  /api/state/current ─┤  (Docker on Mac Mini M1)
             ────────▶ POST /api/command        ─┘
```

---

## Firmware Commands

```bash
cd firmware

# Build
pio run -e m5stack-atoms3r

# Flash (mantener botón pulsado al conectar USB para entrar en download mode)
pio run -e m5stack-atoms3r -t upload

# Monitor serie
pio device monitor --baud 115200
```

### First-time setup

Copy `firmware/src/credentials.h.example` → `firmware/src/credentials.h` and fill in:
- `WIFI_SSID` / `WIFI_PASS` — red local
- `API_URL` — IP del Mac Mini en la red, ej. `http://192.168.1.X:5000` (sin slash final)

`credentials.h` está en `.gitignore` y nunca se commitea.

> **BLE desactivado por ahora.** Los archivos `GotchiBLE.h/cpp` están guardados en `firmware/_ble/` para uso futuro. NimBLE-Arduino no está en `lib_deps`.

---

## Server Commands

```bash
cd server

# Desarrollo local (requiere .NET 8 SDK)
dotnet run

# Docker en Mac Mini M1
docker compose up --build -d

# Ver logs
docker compose logs -f
```

La web queda en `http://MAC_MINI_IP:5000`.

---

## Architecture

### Main loop (`firmware/src/main.cpp`)

Corre a ~50 Hz. Cuatro módulos comparten una única instancia de `GotchiState`:

```
GotchiSensors → dispara eventos en GotchiState (onStep, onFall, onShake…)
GotchiState   → actualiza stats y recalcula mood cada tick
GotchiWiFi    → POST /api/state + GET /api/command/next cada 3 s (no bloqueante)
GotchiDisplay → lee GotchiStats + IMU/mic crudo, anima el Avatar
```

### Module responsibilities

| Módulo | Rol |
|---|---|
| `GotchiState` | Única fuente de verdad. Stats (hunger/thirst/energy 0–100, steps), flags temporales con timestamps de expiración, `_recalcMood()` elige el mood de mayor prioridad activo. |
| `GotchiSensors` | Lee IMU a 20 Hz y micrófono a ~3 Hz. Convierte datos crudos en eventos semánticos que llama sobre `GotchiState`. |
| `GotchiWiFi` | Máquina de estados no bloqueante. Hace `POST /api/state` y `GET /api/command/next`. Parsea la respuesta del comando buscando "feed"/"drink"/"pet" en el body. |
| `GotchiDisplay` | Envuelve M5Stack-Avatar. La tarea FreeRTOS del Avatar corre en core 0; el loop principal en core 1. Comparten datos vía campos `volatile` escalares (atómicos en ESP32-S3). |

### Mood priority (`GotchiState::_recalcMood`)

```
STARTLED(11) > ANGRY(8) > ANNOYED(12) > DIZZY(4) >
LAUGHING(7) > EXCITED(5) > SLEEPING(10) > TIRED(1) >
HUNGRY(2) > THIRSTY(3) > SAD(9) > HAPPY(0)
```

Los moods con timestamp (`_dizzyUntil`, `_laughingUntil`…) se limpian cuando `millis()` supera su deadline en `update()`.

### WiFi protocol

El firmware manda cada 3 s:

| Dirección | Endpoint | Formato |
|---|---|---|
| Atom → servidor | `POST /api/state` | `{"mood":N,"hunger":N,"thirst":N,"energy":N,"steps":N,"flags":N}` |
| Atom → servidor | `GET /api/command/next` | respuesta: `"feed"` / `"drink"` / `"pet"` / `""` |
| Web → servidor | `GET /api/state/current` | devuelve el mismo JSON + `updatedAt` |
| Web → servidor | `POST /api/command` | `{"command":"feed"}` |

### Server (`server/`)

Un solo proceso .NET 8 Minimal API sirve tanto la API como la web estática desde `wwwroot/`.

- **`GotchiStore`** — singleton en memoria con lock. Guarda el último estado del Atom y un único comando pendiente. El comando se borra al ser leído por el Atom.
- **`Program.cs`** — define los 4 endpoints con Minimal API.
- **`wwwroot/index.html`** — dashboard SPA vanilla JS, hace poll a `/api/state/current` cada 3 s.

### Avatar library (parches — no reemplazar)

`firmware/lib/M5Stack-Avatar/` es una copia local parcheada. Cambios clave vs upstream:
- `Face.cpp`: punteros `Balloon`/`Effect`/`BatteryIcon` inicializados; franja `COLOR_BORDER`
- `Avatar.cpp`: stack de `drawLoop` aumentado 2048 → 8192 bytes
- `Effect.h`: posiciones de iconos escaladas de 320×240 → 128×128
- `ColorPalette.h/cpp`: clave `COLOR_BORDER` añadida; `set()` con bounds safety

**No reemplazar con la versión upstream** — crashea o renderiza mal en la pantalla 128×128.
