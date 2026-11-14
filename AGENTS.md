# AGENTS.md

This file provides guidance to AI coding agents (Claude Code, Codex, etc.) when working with code in this repository.

## Project Overview

AtomGotchi is a desk companion on an M5Stack Atom S3R (ESP32-S3, 128×128 display) that shows what Claude Code is doing, as an expressive animated face with personality. Claude Code hooks report events to a small .NET 8 API, which aggregates all sessions into one state; the Atom polls that state over WiFi.

```
Claude Code hooks ─▶ hooks/gotchi-hook.sh ─▶ POST /api/hook ─┐
                                                              ▼
                                      .NET 8 API (void-server, Traefik)
                                                              ▲
Atom S3R ──WiFi/HTTPS──▶ GET /api/claude (≈1 s) ──────────────┘
```

Production URL: `https://atom-api.northernarchive.com` (API only, no web UI).

## Commands

### Firmware (`firmware/`, PlatformIO, env `atom-gotchi`)

```bash
cd firmware
pio run -e atom-gotchi                                   # build
pio run -e atom-gotchi -t upload --upload-port /dev/cu.usbmodemXXXX   # flash
```

- If no `/dev/cu.usbmodem*` appears, hold the side button while plugging in USB (download mode).
- `pio device monitor` needs an interactive TTY. To read serial from a script use pyserial (the PlatformIO venv has it).
- The firmware prints `fps / render / push / heap / wifi / state` every 2 s on serial.
- Serial debug override: send `0`–`7` to force a state for 10 s, `a` to cancel; tapping the screen cycles states.
- First-time setup: copy `firmware/src/credentials.h.example` → `firmware/src/credentials.h` (gitignored) and set `WIFI_SSID`, `WIFI_PASS`, `API_URL`. The SSID is case-sensitive and the ESP32 only does 2.4 GHz.

### Server (`server/`)

```bash
cd server
dotnet run                      # local dev (port from ASPNETCORE_URLS, e.g. http://0.0.0.0:5055)
```

- **Port 5000 is taken by AirPlay Receiver on macOS** — use another port locally.
- The dev Mac only has the .NET 10 runtime; run with `DOTNET_ROLL_FORWARD=Major`.

### Deploy (void-server, NOT the Mac Mini)

The Mac Mini is for development; deployment goes to `void-server` over SSH. Convention there: `~/<project>/docker-compose.yml`, external network `proxy`, Traefik labels with `certresolver=le`.

```bash
rsync -az --delete --exclude bin --exclude obj ./server/ void-server:atom-gotchi/server/
ssh void-server 'cd ~/atom-gotchi/server && docker compose up -d --build'
```

There is no CI; deploys are manual.

### Hooks

`hooks/gotchi-hook.sh` reads the hook JSON from stdin and POSTs it in the background (1 s timeout, fails silently). Target is `GOTCHI_URL` (default: production). `hooks/settings.example.json` is the template for `~/.claude/settings.json`; the user's real hooks are already installed there and affect every Claude Code session.

## Architecture

### Server (`server/`)

`ClaudeActivity` (singleton, lock-guarded) keeps per-session state keyed by `session_id` and reduces it to one state with priority `wait > work > error > done > alert > idle`. Event mapping lives in `Apply`; transient states expire by age (`alert` 2.5 s, `done` 6 s, `error` 4 s), `work` is considered stale after 5 min without events, and with no activity the state decays to `bored` (10 min) then `sleep` (30 min). `wait` (permission/notification) persists until the next event. `Program.cs` exposes `POST /api/hook`, `GET /api/claude` (plain-text state word), `GET /api/claude/json` (debug).

### Firmware (`firmware/src/main.cpp`)

Single file, Arduino framework on M5Unified/M5GFX (no LVGL, no Avatar library).

- **Rendering**: everything is drawn into a full 128×128 RGB565 `M5Canvas` sprite and pushed once per frame (render ≈0.4 ms, push ≈6.6 ms, capped at ~60 fps).
- **Poses**: each state is a row in the `POSES` table (eye size/gap, lid cover, tilt, bottom lid, pupil, y-offset, RGB). Every frame all values are exponentially interpolated toward the target pose, so transitions are free. Layered on top: auto-blink, gaze saccades (`updateGaze`), breathing, and per-state effects (`drawEffects`: dots, `?`, sparkles, tear, zzz).
- **Networking**: `netTask` (FreeRTOS, core 0) polls `API_URL/api/claude` every second over a reused HTTPS connection (`setInsecure()`, no cert validation) and writes `remoteState`; the render loop on core 1 reads it. After 5 consecutive failures the face goes to `bored` instead of freezing.
- Adding a state: extend `State`, `STATE_NAME` (must match the server's word), `POSES`, `updateGaze`, `drawEffects`, and the server's `Apply`/priority.
