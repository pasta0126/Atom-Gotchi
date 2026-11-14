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

- **OTA (no cable):** `cd firmware && OTA_PASS=$(grep OTA_PASS src/credentials.h | sed 's/.*"\(.*\)".*/\1/') pio run -e atom-gotchi-ota -t upload` (the Atom is `atom-gotchi.local`, same LAN as the dev Mac). The first flash after changing `board_build.partitions` must be by USB. `OTA_PASS` lives in the gitignored `credentials.h`.
- If no `/dev/cu.usbmodem*` appears, hold the side button while plugging in USB (download mode).
- `pio device monitor` needs an interactive TTY. To read serial from a script use pyserial (the PlatformIO venv has it).
- The firmware prints `fps / render / push / heap / wifi / state` every 2 s on serial.
- Serial debug commands are listed under Firmware architecture; tapping the screen when nothing is pending gives a "pet" reaction, and acknowledges a pending task-done alert.
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

### Hardware facts (verified on the board)

AtomS3R has **no speaker, buzzer, microphone or RGB LED** (`M5.Speaker/Mic/Led` all disabled); it has a BMI270 IMU, one button (the screen) and the 128×128 GC9107 display. All feedback is visual: soft pulses and background colours, never strobing.

### Server (`server/`)

`ClaudeActivity` (singleton, lock-guarded) keeps per-session state keyed by `session_id` and reduces it to one state with priority `wait > work > error > done > alert > idle`. Event mapping lives in `Apply`; transient states expire by age (`alert` 2.5 s, `done` 6 s, `error` 4 s), `work` is stale after 5 min without events, and with no activity the state decays to `bored` (10 min) then `sleep` (30 min). `wait` (permission/notification) persists until the next event.

It also keeps "memory" for the personality: tasks finished today, consecutive-error streak and a `mood` in −2..2, plus a `done` counter that increments on every `Stop` together with the project name (last path segment of the hook's `cwd`). Tool names are bucketed into `read|search|edit|bash|web|agent|other` (`ToolCategory`).

`GET /api/claude` returns `key=value` lines (`state, tool, project, mood, done, doneproject, tasks, sessions`) plus up to 6 `s=<id>|<name>|<state>|<doneCount>|<tool>` lines (stable order by session birth, duplicate project names get ` #n`) so the firmware needs no JSON library; `/api/claude/json` is the same for debugging. Each session carries its own done counter. Sessions with no events for 2 h are forgotten and an abandoned `wait` expires after 20 min. `POST /api/hook` takes the raw Claude Code hook JSON. `PostToolUseFailure` is mapped to `error` but is not registered in the user's hooks (unverified that Claude Code emits it).

### Firmware (`firmware/src/main.cpp`)

Single file, Arduino framework on M5Unified/M5GFX (no LVGL, no Avatar library).

- **Rendering**: everything is drawn into a full 128×128 RGB565 `M5Canvas` sprite and pushed once per frame (render ≈1 ms, push ≈6.7 ms, ~56–60 fps).
- **Poses**: each state is a row in `POSES` (eye size/gap, lid cover, tilt, bottom lid, pupil, y-offset, RGB); `targetPose()` adjusts it per tool and per mood. All values are exponentially interpolated. Keep `tilt` small for `work`: a strong inward tilt reads as angry. The background colour also interpolates per state (`bgTarget`).
- **Per-frame `Fx`**: layered effects (per-eye scale/lid/offset, gaze override, blush, yawn, sparkle, dizzy) are computed by `computeBehavior` (curious-personality micro-animations in idle/bored: look, tilt, wink, inspect, butterfly, yawn, laugh, shy, eye-roll, nod…) and `computeReaction` (IMU/button: dizzy, pet, startle), smoothed, then applied in `drawEye`. Reactions override behaviors.
- **Tool-aware work**: gaze pattern (`updateGaze`) and bottom effect (`drawEffects`) depend on `curTool`.
- **Task-done alerts (per session)**: `Track` keeps `seen`/`acked` done counters per session id (baseline at first sight, resync if the server restarts). Sessions with `done > acked` are pending: `✓ name` banner (`+N` if several), softly breathing border + background. A click acknowledges all pending at once and also cancels the `done` face.
- **Button** (`handleButton`; no triple click, no dead zone): **short click = move/dismiss**, **long press = open options / accept the selection**. Every click leaves feedback (a faint green flash over the screen, plus a blink on the face). A long press draws a circle growing from the centre (from 150 ms, full at 750 ms, radius 91 covers the 128×128 corners); the action fires only when the circle covers the whole screen, releasing earlier makes it retract and nothing happens; on completion it fades out revealing the new screen (blue = open list, green = accept). Short click: next row in the list / acknowledge all pending / boop. Long press (only when ≥2 sessions, or inside the list): open the session list / select the row (`auto` = aggregate, otherwise *follow* that session; others needing permission still interrupt). List closes after 10 s idle; a followed session that disappears reverts to auto with a toast.
- **Wait screen**: just `waiting` with three animated dots under the eyes (no message text).
- **Time of day**: `netTask` sets up NTP with the Spain timezone (`CET-1CEST…`); the loop reads the local hour. 23:00–07:00 is "night" (quiet hours): idle/bored becomes `sleep` and the display is dimmed (45, or 15 while asleep). The first activity each morning (05:00–13:00, once per day) triggers a greeting reaction (`R_GREET`: sun + sparkle eyes + "buenos dias" toast). The hour is printed in the 2-s serial report.
- **Button debug**: `pio run -e atom-gotchi-btn-ota -t upload` (or send `x` over serial) shows a screen with live press time, raw GPIO41 vs M5 state, click/long counters and history, using the same `handleButton` code. Serial logs `button: pressed/click/LONG/released` with durations.
- **IMU** (`imuUpdate`): ≥4 spikes in 1 s → dizzy; one isolated spike (not near a button press) → pet; free-fall → startle. Thresholds are untuned guesses.
- **Networking/OTA**: `netTask` (FreeRTOS, core 0) polls every second over a reused HTTPS connection (`setInsecure()`); the render loop on core 1 reads the copy under a spinlock. `ArduinoOTA` is served from the same task (progress bar overlay while updating). After 5 consecutive failures the face goes to `bored`.
- Serial debug: `0`–`7` force a state, `T` cycles tool, `b` triggers the next micro-animation, `k`/`h`/`s` reactions, `a` cancels the override.
- Adding a state: extend `State`, `STATE_NAME` (must match the server's word), `POSES`, `bgTarget`, `updateGaze`, `drawEffects`, and the server's `Apply`/priority.
