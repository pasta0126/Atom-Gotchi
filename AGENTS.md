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
- Serial debug commands are listed under Firmware architecture; pressing the screen acknowledges attention / pets the gotchi.
- First-time setup: copy `firmware/src/credentials.h.example` → `firmware/src/credentials.h` (gitignored) and set `WIFI_SSID`, `WIFI_PASS`, `API_URL`. The SSID is case-sensitive and the ESP32 only does 2.4 GHz.

### Server (`server/`)

`ClaudeActivity` (singleton, lock-guarded) keeps per-session state keyed by `session_id` and reduces it to one state with priority `wait > work > error > done > alert > idle`. Event mapping lives in `Apply`; transient states expire by age (`alert` 2.5 s, `done` 6 s, `error` 4 s), `work` is stale after 5 min without events, and with no activity the state decays to `bored` (10 min) then `sleep` (30 min). `wait` (permission/notification) persists until the next event but expires after 20 min; sessions with no events for 2 h are forgotten. It also keeps "memory" for the personality (tasks finished today, error streak → `mood` in −2..2) and buckets tool names into `read|search|edit|bash|web|agent|other`.

**The server owns session selection and attention** so the device stays dumb:
- *Active session*: `GET /api/sessions` lists sessions (`id`, readable `name` — project folder, `#n` suffix for duplicates —, `state`, `tool`, `doneCount`, `idleSeconds`, `active`); `POST /api/active {"id": "<id | id prefix | name>"}` picks the one to follow (`"auto"`/`null` = all, aggregated); `GET /api/active`. Following a session makes its state the face's state (another session in `wait` still interrupts) and only its `Stop` raises attention. It reverts to auto when that session ends/is forgotten. `hooks/gotchi-session.sh [list | use <n|id|name> | auto]` is a CLI wrapper.
- *Attention* = a finished task nobody confirmed: `Stop` bumps `attnSeq` (only for the followed session, or any in auto); `POST /api/ack` sets `ackSeq = attnSeq`.
- `GET /api/claude` (polled by the Atom) returns `key=value` lines: `state, tool, mood, attn, attnseq, sessions` — no project names; `/api/claude/json` is the full snapshot for debugging. `POST /api/hook` takes the raw Claude Code hook JSON. `PostToolUseFailure` is mapped to `error` but is not registered in the user's hooks (unverified that Claude Code emits it).

### Firmware (`firmware/src/main.cpp`)

Single file, Arduino framework on M5Unified/M5GFX (no LVGL, no Avatar library).

- **Rendering**: everything is drawn into a full 128×128 RGB565 `M5Canvas` sprite and pushed once per frame (render ≈1 ms, push ≈6.7 ms, ~56–60 fps).
- **Poses**: each state is a row in `POSES` (eye size/gap, lid cover, tilt, bottom lid, pupil, y-offset, RGB); `targetPose()` adjusts it per tool and per mood. All values are exponentially interpolated. Keep `tilt` small for `work`: a strong inward tilt reads as angry. The background colour also interpolates per state (`bgTarget`).
- **Per-frame `Fx`**: layered effects (per-eye scale/lid/offset, gaze override, blush, yawn, sparkle, dizzy) are computed by `computeBehavior` (curious-personality micro-animations in idle/bored: look, tilt, wink, inspect, butterfly, yawn, laugh, shy, eye-roll, nod…) and `computeReaction` (IMU/button: dizzy, pet, startle), smoothed, then applied in `drawEye`. Reactions override behaviors.
- **Tool-aware work**: gaze pattern (`updateGaze`) and bottom effect (`drawEffects`) depend on `curTool`.
- **Attention**: when the server says `attn=1` (and `attnseq` is newer than `localAckSeq`), the border and background softly breathe green until any button press. No project names or labels are drawn on the device. The `waiting` screen is just that word with three animated dots.
- **Interaction** (only a generic button press and shaking): *any press* (`handleButton`, uses `wasPressed`, no short/long classification — that was unreliable) acknowledges the attention (`ackRequested` → `netTask` POSTs `/api/ack`, plus local `localAckSeq` for instant feedback), flashes the screen faintly green, blinks and gives a pet reaction (hearts); on a sleeping gotchi it also wakes it for 3 min. *Shaking* (`imuUpdate`: ≥4 acceleration spikes in 1 s) makes it dizzy (`R_DIZZY`), or annoyed (`R_ANNOYED`: angry brows/red/steam) if it was asleep or you shook it again within 30 s. Free-fall gives a startle. Thresholds are untuned.
- **Time of day**: `netTask` sets up NTP with the Spain timezone (`CET-1CEST…`); the loop reads the local hour. 23:00–07:00 is "night" (quiet hours): idle/bored becomes `sleep` and the display is dimmed (45, or 15 while asleep). The first activity each morning (05:00–13:00, once per day) triggers a greeting reaction (`R_GREET`: sun + sparkle eyes + "buenos dias" toast). The hour is printed in the 2-s serial report.
- **Networking/OTA**: `netTask` (FreeRTOS, core 0) polls every second over a reused HTTPS connection (`setInsecure()`); the render loop on core 1 reads the copy under a spinlock. `ArduinoOTA` is served from the same task (progress bar overlay while updating). After 5 consecutive failures the face goes to `bored`.
- Serial debug: `0`–`7` force a state, `T` cycles tool, `b` triggers the next micro-animation, `k`/`h`/`s` reactions (dizzy/pet/startle), `a` cancels the override. The 2-s report also prints `attn`, `hour`.
- Adding a state: extend `State`, `STATE_NAME` (must match the server's word), `POSES`, `bgTarget`, `updateGaze`, `drawEffects`, and the server's `Apply`/priority.
