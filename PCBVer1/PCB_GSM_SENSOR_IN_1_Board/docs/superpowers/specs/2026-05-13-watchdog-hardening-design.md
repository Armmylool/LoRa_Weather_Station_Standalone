# Watchdog Timer Hardening

## Problem

The rover firmware runs in field conditions with poor GSM signal. The current ESP task WDT (30s) prevents hard hangs, but several code paths don't feed it, risking false resets. There is also no per-state timeout safety net — if a state machine state gets stuck in an unexpected condition, only the hardware WDT catches it.

## Changes

### Part 1 — Fill WDT gaps

Add `esp_task_wdt_reset()` / `feedWDT()` in four locations that currently run without feeding the watchdog:

1. **WifiApServer::handleClient()** — 60s of web serving with no WDT feed
2. **WifiApServer::serveFileDownload()** — file streaming during download
3. **setup()** — between LittleFS.begin() and serial init (flash can hang on corruption)
4. **STATE_FINISH while(1) loop** — the fallback loop if TPL5110 doesn't cut power

### Part 2 — Per-state software timeout

Add a global `stateEntryTime` that records `millis()` when each state begins. At the top of `loop()`, before the `switch`, check if the current state has exceeded its maximum duration. If so, log a warning and force-transition to the appropriate fallback state.

| State | Max duration | On timeout fallback |
|---|---|---|
| STATE_GSM_INIT | GSM_INIT_TIMEOUT_MS (120s) | STATE_NTP |
| STATE_NTP | TIME_WAIT_TIMEOUT (30s) | STATE_WIFI_AP |
| STATE_WIFI_AP | WIFI_AP_TIMEOUT (60s) | STATE_SOIL |
| STATE_SOIL | SOIL_TIMEOUT (30s) | STATE_WEATHER |
| STATE_WEATHER | WEATHER_TIMEOUT (30s) | STATE_SAVE |
| STATE_SAVE | 10s | STATE_RECONNECT |
| STATE_RECONNECT | RECONNECT_TIMEOUT (120s) | STATE_FINISH |
| STATE_PUBLISH | MQTT_PUBLISH_TIMEOUT (60s) | STATE_FINISH |

This is a safety net on top of existing per-state timeout logic — not a replacement.

### Part 3 — Increase network timeouts for field conditions

| Constant | Old | New | Reason |
|---|---|---|---|
| GSM_INIT_TIMEOUT_MS | 60s | 120s | Weak signal network registration |
| RECONNECT_TIMEOUT | 60s | 120s | Software reset + re-register |
| MQTT_PUBLISH_TIMEOUT | 30s | 60s | Slow GPRS round-trip |
| NTP_TIMEOUT_MS | 30s | 60s | NTP sync over weak GPRS |

## Files modified

- `include/utilities.h` — increase timeout constants
- `src/main.cpp` — add stateEntryTime tracking, per-state timeout check at top of loop, feedWDT in STATE_FINISH fallback
- `src/WifiApServer.cpp` — add feedWDT in handleClient() and serveFileDownload()

## Approach

Inline timeout checks in the state machine (Approach A). No new classes or files.
