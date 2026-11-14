# AtomGotchi

A desk companion that shows what Claude Code is doing. It runs on an **M5Stack Atom S3R** (ESP32-S3, 128×128 display) and shows an expressive animated face that reacts to your Claude Code sessions: idle, thinking, waiting for permission, done, error, bored and asleep.

Claude Code hooks → small .NET 8 API → Atom polls over WiFi. See `AGENTS.md` for architecture, build/flash/deploy commands and hook setup.
