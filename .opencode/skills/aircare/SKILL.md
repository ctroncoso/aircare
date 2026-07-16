---
name: aircare
description: Use when working on the aircare ESP32 → MQTT → InfluxDB → Grafana air-quality monitoring system, or any task touching firmware, the backend dashboard, OTA updates, or device liveness/alerting.
---

# aircare — project context

Self-hosted air-quality monitoring system: ESP32 devices (SenseAir Sunrise CO₂ +
BME280) publish telemetry to HiveMQ Cloud MQTT; a home-server Docker stack
(InfluxDB + Grafana + Node-RED) consumes, stores, and visualizes it.

## Architecture
- **Devices (ESP32, PlatformIO `esp32dev` env):** firmware in `src/`. Build with
  `~/.platformio/penv/bin/pio run -e esp32dev`. Publishes to HiveMQ Cloud
  (`4532efaca6c5432ea63dbae9a7690ef4.s1.eu.hivemq.cloud:8883`, TLS, user
  `aircaredevice`). Topics: `cleanair/sensor` (measurements), `cleanair/status`
  (health/liveness snapshot, ~every 5 min), `cleanair/events` (real events only).
- **Broker stays HiveMQ Cloud** — devices can't reach the home server, so the
  broker is the only entry point; Node-RED is the sole client.
- **Backend (Docker, home server, reached via Tailscale `100.125.200.49`):**
  - InfluxDB: org `home`, bucket `aircare`, token placeholder `change-me-inlfuxdb-admin-token`.
    Measurements: `sensor`, `events`, `status`. Tags: `mac`, `label`, `fw` only.
  - Grafana: host port `3001`, UI `http://localhost:3001`, creds `admin`/`aircare`.
    Datasource uid `P951FEA4DE68E13C5`, dashboard uid `aircare-main`.
  - Node-RED: live flow in `nodered-data` volume (`flows.json` is gitignored);
    deployed via admin API. `ui_base` node must NOT be present (causes
    circular-dependency error).
- **Timezone:** `America/Santiago`. CO₂ bands: green <700, yellow 700–800, red ≥800.

## Schema (InfluxDB)
- `sensor`: `t`,`p`,`h`,`co2_fc/uc/fu/uu`,`rl1`,`rl2`, plus schedule fields
  (`sched_mode`,`sched_ovr`,`sched_exc`,`exc1_*`,`exc2_*`). `rl1`=fan, `rl2`=UV.
- `events`: `event` (float, value 0) + `param` (string, pipe-delimited
  `CATEGORY|SUB|detail`). Categories: `BOOT`,`RELAY`,`EXCEPTION`,`UPDT`,`MQTT`,
  `MQTT_SUB`,`SNTP`,`SETUP`,`REPORT`,`MCU`,`WIFI_RSSI`,`HEALTH`. `WIFI_RSSI`/`HEALTH`
  are noise (filtered out of the Events panel). Boot reasons are strings:
  `BOOT|POWERON|`, `BOOT|SW|`, `BOOT|PANIC|`, `BOOT|WDT|`, `BOOT|TASK_WDT|`,
  `BOOT|BROWNOUT|`, `BOOT|DEEPSLEEP|`.
- `status`: `sched_mode/ovr/exc`, `exc1_*`, `exc2_*`, `relay*`, `rssi`, `uptime_ms`,
  `heap`, `local_time`. This is the liveness signal.

## Dashboard (Grafana)
- `backend/grafana/provisioning/dashboards/aircare.json` is committed, but the
  **provisioned `grafana.db` does NOT auto-refresh** — after editing the JSON you
  MUST re-import via API:
  `POST http://localhost:3001/api/dashboards/db` with body
  `{"dashboard": <json>, "overwrite": true}` (Basic auth `admin:aircare`).
- Panels: 1 CO₂, 2 CO₂ variants, 3 Temp, 4 Humidity, 5 Pressure, 6 Relays
  (state-timeline), 7 Events (Flux, free-text `eventFilter` box), 8 Schedule state,
  9 Device liveness (flags devices silent >10 min).
- `device` dashboard variable = multi-select (checkbox) of device `label`s.
- **Gap rendering:** time-series use `aggregateWindow(..., createEmpty: false)` so
  lines/bars stay continuous; there is intentionally NO gap-breaking (attempts to
  break the line on >5 min data loss via `createEmpty:true` / `difference()` /
  `connectNullValues` produced dots or off values and were reverted — see commit
  `cbb7c32`). Outages are surfaced by the "Device liveness" panel instead.

## Loss-of-communication handling (firmware)
- **Comms-dead watchdog (1.1.18+):** `mqtt::commsIsDead()` (src/mqttHelper.cpp)
  tracks last successful publish; if none for 60 min while WiFi is still associated,
  `loop()` (src/main.cpp) emits `MCU|COMMS_DEAD` and `ESP.restart()`s. A pure
  network outage does NOT trip it.
- MQTT reconnect uses exponential backoff 1s→30s (HiveMQ rate-limit friendly).
- **Task WDT was added then REMOVED (1.1.19):** it tripped on the first `loop()`
  because `updateTick()` runs synchronous HTTPS fetches (`httpFetch.cpp`) that block
  past the 30s timeout. The comms-dead watchdog covers the recovery goal instead.
- Current firmware version: **1.1.19**. All 8 devices in `bins/update.json` point
  to `bins/firmware.v.1.1.19.bin`. Build → copy `.pio/build/esp32dev/firmware.bin`
  to `bins/firmware.v.1.1.19.bin` → bump `bins/update.json` → commit/push; devices
  OTA-pull on next `updateTick()` (every 10 min).

## Device labels (MAC → name)
- `A8:42:E3:AB:10:88`=Jardin-NT, `A8:42:E3:AB:6F:40`=Jardin-NM,
  `C8:F0:9E:9E:C6:6C`=Eden-2B, `A8:42:E3:AB:1E:04`=Eden-3A,
  `A0:A3:B3:96:EF:F0`=Eden-2A, `A8:42:E3:AB:70:BC`=Eden-K,
  `C8:F0:9E:9E:38:40`=Test rig, `C8:F0:9E:9F:BE:D4`=Eden-1A.
- `bins/config.json` holds labels + MACs; `bins/update.json` holds OTA targets.

## Key files
- `src/main.cpp` — setup/loop, comms-dead reboot, deferred first `updateTick()`.
- `src/mqttHelper.cpp/.h` — connect/publish/reconnect, `commsIsDead()`.
- `src/app/app.cpp` — `measurementTick()` (sensor publish + status heartbeat),
  `updateTick()` (OTA/schedule/config fetch, blocking HTTPS).
- `src/ntpHelper.cpp/.h` — NTP resync + drift tracking (1.1.17).
- `src/core/board.h` — `PROGRAM_VERSION`, `measurementDelay` (=1 min),
  `updateDelay` (=10 min).
- `backend/grafana/provisioning/dashboards/aircare.json` — dashboard (re-import via API).
- `bins/update.json`, `bins/config.json`, `bins/firmware.v.1.1.*.bin`.

## Repo / git
- Push via SSH (`git@github.com:ctroncoso/aircare.git`).
- `nodered/flows.json` gitignored; live flow in `nodered-data` volume.
- `src/wifiCredentials.h`, `src/secrets.h` gitignored.
- `native` PlatformIO env has a pre-existing link error (don't rely on it).

## Conventions
- ESP32 firmware in C++ (Arduino framework), no code comments added unless asked.
- Keep measurements pure (no mutable state as tags); mutable state as fields.
