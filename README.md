# AirCare — Indoor Air Quality Monitor

**Version:** 1.1.13 | **Board:** ESP32 Dev Module | **Framework:** Arduino

AirCare is an IoT firmware for the ESP32 microcontroller that monitors indoor air quality using two main sensors:

- **Senseair Sunrise** — a high-precision CO₂ sensor (I²C)
- **Bosch BME280** — a combined temperature, humidity, and barometric pressure sensor (I²C)

The device reads sensor data on a configurable interval, publishes the measurements to an MQTT broker, controls two relay outputs (fan and UV filter) based on a time schedule, and supports Over-The-Air (OTA) firmware updates. A tri-color LED (Red / Yellow / Green) provides a visual indication of the current CO₂ level.

---

## Table of Contents

- [AirCare — Indoor Air Quality Monitor](#aircare--indoor-air-quality-monitor)
  - [Table of Contents](#table-of-contents)
  - [Architecture](#architecture)
  - [Hardware Overview](#hardware-overview)
  - [Core Application Flow](#core-application-flow)
  - [Modules / File Descriptions](#modules--file-descriptions)
    - [`core/` — shared foundation](#core--shared-foundation)
    - [`src/app/app.*`](#srcappapp)
    - [`src/main.cpp`](#srcmaincpp)
    - [`src/wifiManagerHelper.*`](#srcwifimanagerhelper)
    - [`src/mqttHelper.*`](#srcmqtthelper)
    - [`src/configHelper.*`](#srcconfighelper)
    - [`src/ledHelper.*`](#srcledhelper)
    - [`src/bmeHelper.*`](#srcbmehelper)
    - [`src/sunrise_i2c.*` / `src/sunriseHelper.*`](#srcsunrise_i2c--srcsunrisehelper)
    - [`src/ntpHelper.*`](#srcntpphelper)
    - [`src/otaHelper.*`](#srcotahelper)
    - [`src/config/schedule.*`](#srcconfigschedule)
    - [`src/actuators/relay.*`](#srcactuatorsrelay)
  - [Dependencies](#dependencies)
  - [Configuration](#configuration)

---

## Architecture

The firmware was restructured from a flat collection of header-only "helper"
files into a small layered module tree. Each module owns one concern and exposes
a `.h`/`.cpp` pair (declarations in the header, definitions in the `.cpp`).
Cross-module signalling no longer uses shared `volatile` handshake flags; it uses
a tiny synchronous **event bus** (`core/events.h`).

```
src/
  main.cpp                 # orchestration: setup()/loop(), button handlers
  globals.h                # slim shared state (co2_State, espClient, wm)
  core/
    board.{h,cpp}          # pins, build constants, enums, remote URLs
    events.{h,cpp}         # synchronous publish/subscribe event bus
    nvsStore.{h,cpp}       # Preferences (NVS) wrapper
    httpFetch.{h,cpp}      # HTTPS fetch-by-MAC JSON helper
    co2check.h             # standalone CO2 sample validity check (unit-tested)
  app/
    app.{h,cpp}            # measurement/update cycles + telemetry buffer
  config/
    schedule.{h,cpp}       # event-driven relay scheduler (policy)
    configHelper.{h,cpp}   # dynamic MQTT broker config
  actuators/
    relay.{h,cpp}          # relay GPIO driver (hardware)
  net/sensors helpers:
    wifiManagerHelper.*    # WiFi connect + captive portal
    mqttHelper.*           # MQTT client (TLS, queue, backoff)
    ntpHelper.*            # SNTP + Chile local time
    otaHelper.*            # OTA via ESP32OTAPull
    bmeHelper.*            # BME280 driver
    sunriseHelper.*        # Sunrise high-level init
    sunrise_i2c.{h,cpp}    # Sunrise low-level I2C register driver
```

**Event bus.** Modules `subscribe()` a handler and `emit()` events; nobody
shares a flag. Events: `NtpSynced`, `RelayOverride`, `RelayAuto`,
`ExceptionSet`, `ExceptionClear`, `BrokerChanged`. Example: the scheduler
subscribes to `NtpSynced` (re-arm its timeline) and to the MQTT-injected
`RelayOverride`/`ExceptionSet`; NTP and MQTT simply `emit` without knowing who
listens. See `Refactoring.md` for the full rationale.

---

## Hardware Overview

| Component            | Pin / I²C Address | Description                              |
|----------------------|-------------------|------------------------------------------|
| ESP32 Dev Module     | —                 | Main microcontroller                     |
| Senseair Sunrise     | I²C address `0x68`, SDA=21, SCL=22 | CO₂ sensor (0–10 000 ppm) |
| BME280               | I²C address `0x76` | Temperature, humidity, pressure sensor   |
| Red LED              | GPIO 25           | High CO₂ indicator                       |
| Yellow LED           | GPIO 26           | Medium CO₂ indicator                     |
| Green LED            | GPIO 27           | Low CO₂ indicator                        |
| Relay 1 (Fan)        | GPIO 32           | Activates extraction fan                 |
| Relay 2 (UV)         | GPIO 33           | Activates UV-C filter                    |
| Button               | GPIO 0 (BOOT)     | Double‑click → WiFi config portal; multi‑click → manual OTA |

---

## Core Application Flow

1. **`setup()`** — initialises serial, the status LEDs (including a POST blink
   sequence), the relay pins (forced OFF out of reset), WiFi, NTP, the relay
   schedule, an OTA check, the dynamic broker config, MQTT, and finally the two
   sensors. A critical sensor failure keeps the MQTT keepalive alive long enough
   to deliver an error event, then restarts the MCU.
2. **`loop()`** — runs continuously:
   - Polls the BOOT button (`OneButton`).
   - Calls **`measurementTick()`** every 1 minute to read sensors, validate the
     CO₂ sample, publish telemetry, and update the LED.
   - Calls **`sched::tick()`** every iteration — an edge-triggered scheduler
     that fires relay transitions when their pre-computed time arrives.
   - Calls **`updateTick()`** every 10 minutes to check OTA and refresh the
     remote schedule + broker config.
   - Pumps the MQTT client (keepalive + inbound) and reconnects with backoff.

---

## Modules / File Descriptions

### `globals.h`
**Minimal cross-cutting shared state** (the old "god header" was slimmed during
the refactor; see `Refactoring.md`).

- **Constants/enums** now live in `core/board.h`: `PROGRAM_VERSION`,
  `CO2_LOW` / `CO2_HIGH` (700 / 800 ppm), `PORTAL_NAME`, `PORTAL_TIMEOUT`,
  `measurementDelay` (1 min), `updateDelay` (10 min), `CO2_Condition`,
  `pub_event` (`ERROR = -1`, `INFO = 0`), and the remote manifest URLs.
- **Shared state:** `co2_State` (set by `app`, read by `leds` to restore the LED
  after a blink), `espClient` (`WiFiClientSecure`, bound by `mqtt`), and `wm`
  (`WiFiManager`, used by `main.cpp` to open the portal on button press).

---

### `core/` — shared foundation

#### `core/board.h`
Hardware pins, build constants, enums, and the centralised remote URLs
(`REMOTE_BASE_URL`, `scheduleURL`, `updateURL`). Replaces the old monolithic
`globals.h`.

#### `core/events.h` / `core/events.cpp`
Synchronous event bus (see [Architecture](#architecture)). Fixed-size handler
table (`MAX_HANDLERS = 8`), no heap use.

#### `core/nvsStore.h` / `core/nvsStore.cpp`
Thin `Preferences` wrapper: `getInt` / `getString` / `getBytes` /
`putInt` / `putString` / `putBytes` plus `withRead` / `withWrite` RAII
helpers. Used by the scheduler and broker config for persistence.

#### `core/httpFetch.h` / `core/httpFetch.cpp`
`http::fetchJsonByMac(url, mac, doc [, arrayKey][, defaultKey])` — performs an
HTTPS GET, finds the `Devices` entry whose `Device` equals this MCU's MAC
(or falls back to a `Default` entry), and returns a `FetchResult { ok, matched,
entry }`. Used by both `configHelper` and the schedule fetcher so the
"download + match by MAC" logic is defined once.

#### `core/co2check.h`
`co2Valid(uint16_t)` — pure, Arduino-free validity check (rejects `0` and
`>= 10000`). Extracted so the boundary logic is unit-tested in the native
(`host`) environment.

---

### `src/app/app.*`
**Measurement and update scheduling** (replaces the old `mainHelper.h`).

| Function                | Description |
|-------------------------|-------------|
| `readValues()`          | Reads temperature, pressure, altitude, humidity from the BME280, and the four CO₂ readings (`co2_fc`/`co2_uc`/`co2_fu`/`co2_uu`) from the Sunrise sensor. |
| `getCO2_State()`        | Returns a `CO2_Condition` from the CO₂ ppm: Green (< 700), Yellow (700–800), Red (≥ 800). |
| `printValues()`         | Builds the JSON telemetry document, serialises it into a bounded `512+1` buffer, and prints it (plus the next-transition line). |
| `state2string()`        | Converts the current CO₂ condition to a human‑readable string. |
| `measurementTick()`     | Every 1 min: reads sensors, **validates** the CO₂ sample (`co2Valid()` + error-status); if invalid it skips publish/LED so a stale/zero reading never shows as "Green". Otherwise publishes `/cleanair/sensor` and updates the LED. Also emits periodic HEALTH telemetry every 5 min. |
| `updateTick()`          | Every 10 min: runs `ota::checkUpdate()`, then refreshes the remote schedule and broker config (skipped while an OTA is in progress). |

---

### `src/main.cpp`
**Entry point: `setup()` and `loop()`.**

| Function               | Description |
|------------------------|-------------|
| `setup()`              | Initialises serial, LEDs (POST), relay pins, WiFi, NTP, schedule, OTA, dynamic broker config, MQTT, and the sensors. Sends boot events. Restarts the MCU if a sensor fails. |
| `loop()`               | Polls the button, runs `measurementTick()` / `sched::tick()` / `updateTick()`, and pumps MQTT. |
| `startWifiPortal()`    | Triggered by a double‑click on the BOOT button. Opens the WiFiManager portal, then reboots. |
| `handleMultiClick()`   | Triggered by a multi‑click (≥3 clicks). Starts an OTA check on 3 clicks. |
| `brokerChangedHandler()` | Event-bus handler: on `BrokerChanged` it disconnects the MQTT client so the next reconnect binds to the new endpoint. |

---

### `src/wifiManagerHelper.*`
**WiFi connection and captive portal.**

| Function            | Description |
|---------------------|-------------|
| `initWifiM()`       | Connects via `WiFiManager.autoConnect()`; retries up to 5 times with a 60‑second delay between attempts. Returns `false` if all fail (caller restarts the MCU). |
| `configModeCallback()` | Invoked when WiFiManager enters AP mode — blinks the green LED 4 times. |

---

### `src/mqttHelper.*`
**MQTT client management** (TLS, queue, backoff).

| Function              | Description |
|-----------------------|-------------|
| `initMQTT()`          | Configures `PubSubClient` against the **dynamic** broker (`cfg::brokerHost` / `cfg::brokerPort`, TLS on 8883, buffer 512, keep‑alive 30 s) and connects with exponential backoff. On failure it returns `false` and the firmware continues running without a broker. |
| `mqttTryReconnect()`  | Single reconnect attempt used by `mqttLoop()`. |
| `subscribeCommands()` | Subscribes to the command topics (see below) and confirms on the events channel. |
| `mqttLoop()`          | Called every `loop()`: pumps keepalive, reconnects with exponential backoff (1 s → 30 s cap) on link drop, and flushes the buffered queue on (re)connect. |
| `mqttPump()`          | Pumps the client **without** reconnecting — used during blocking waits (sensor init, HTTP fetches) to keep the socket alive. |
| `mqttPublish()`       | Publishes to a topic. If the link is down, **`cleanair/sensor`** samples are queued (RAM + NVS) and replayed on reconnect; all other topics are dropped while disconnected. |
| `publishEvent()`      | Builds a JSON event (`event`, `param`, `mac`, `fw`) and publishes to `cleanair/events`. |
| `publishReport()`     | Status snapshot (MAC, local time, RSSI, uptime, relay states) in response to a `REPORT` command. |
| `callback()`          | Handles inbound commands (below). |

**Telemetry buffering.** While the broker is unreachable, `cleanair/sensor`
samples are held in a bounded RAM ring and mirrored to NVS (latest ~30 min) so a
reboot during the outage doesn't lose them. They are drained on the next
successful reconnect (with a `MQTT|RECOVERED` event).

**Subscribed topics on connect:**
- `AirCare/inCommands/broadcast`
- `AirCare/inCommands/<MAC address>`

**Remote commands** (JSON payloads):
- `{"cmd":"RELAY","value":"ON"|"OFF"|"AUTO"}` — sticky manual override. `ON`/`OFF` force the relay regardless of schedule; `AUTO` clears the override. Persisted to NVS.
- `{"cmd":"EXCEPTION","from":"YYYY-MM-DD","to":"YYYY-MM-DD","state":"on"|"off"}` — add a date-range exception (holiday / vacation / onsite testing). Persisted to NVS; auto-expires and round-robins when the 4-slot list is full.
- `{"cmd":"EXCEPTION_CLEAR"}` — remove all exceptions.
- `{"cmd":"REBOOT"}` — remote restart.
- `{"cmd":"UPDATE"|"UPDATE_NOW"}` — force an immediate OTA check/install.
- `{"cmd":"REPORT"}` — reply with a status snapshot.

> **Security note:** the TLS client uses `setInsecure()` (no certificate
> verification), consistent with the HTTPS config fetch. For production, supply
> the broker's root CA via `setCACert()`.

---

### `src/configHelper.*`
**Dynamic MQTT broker configuration.** Mirrors the scheduler's
"fetch-from-remote-JSON → persist → refresh" pattern so the broker endpoint
(host + port) can change without reflashing every device.

Resolution order per device:
1. matching MAC entry in the `Devices` array of `config.json`;
2. the top-level `Default` entry;
3. last value persisted in NVS;
4. the compiled-in `FALLBACK_BROKER` / `FALLBACK_PORT` (HiveMQ Cloud).

On a change, it emits `Evt::BrokerChanged` so the main loop disconnects and
rebinds. Invalid endpoints (`0.0.0.0`, empty, bad port) are rejected to avoid
severing the remote channel.

---

### `src/ledHelper.*`
**Visual indicators.**

| Function                   | Description |
|----------------------------|-------------|
| `initLEDS()`               | Sets the three LED pins as outputs and clears them. |
| `POSTBlinks()`             | Power‑On Self Test — blinks red, yellow, green in sequence (called from `setup()`). |
| `clearLeds()`              | Turns off all three LEDs. |
| `setLedOnCO2Condition()`   | Lights the LED matching the current `CO2_Condition`. |
| `blinkLed()`               | Blinks a given LED N times; optionally restores the CO₂ condition LED. |
| `flipLed()`                | Toggles a pin (used during OTA progress). |

---

### `src/bmeHelper.*`
**BME280 temperature, humidity and pressure sensor.**

| Function    | Description |
|-------------|-------------|
| `initBME()` | Initialises the BME280 at I²C `0x76` with 16× oversampling (normal mode, filter off). Returns `false` if not found. |

**State:** `bme::temp`, `bme::pressure`, `bme::altitude`, `bme::humidity`.

---

### `src/sunrise_i2c.*` / `src/sunriseHelper.*`
**Senseair Sunrise CO₂ sensor** — `sunrise_i2c.*` is the low-level I²C register
driver (full register map: measurement mode/period/samples, ABC, IIR filter,
calibration, power-down data, single-shot reads, etc.). `sunriseHelper.*` is the
high-level init wrapper.

| Function          | Description |
|-------------------|-------------|
| `initCo2Sensor()` (`sunriseHelper`) | Calls `initSunrise()`, sets the measurement period (from `measurementDelay`), 16 samples, continuous mode, resets, and waits for the first measurement (up to 20 attempts). Returns `false` on timeout. |

**State:** `co2_fc` (filtered/compensated), `co2_uc` (unfiltered/compensated),
`co2_fu` (filtered/uncompensated), `co2_uu` (unfiltered/uncompensated). Only
`co2_fc` drives logic; the other three are telemetry-only.

---

### `src/ntpHelper.*`
**Network Time Protocol synchronisation.**

| Function              | Description |
|-----------------------|-------------|
| `initNTP()`           | Configures SNTP (`ntp.shoa.cl`, `time.nist.gov`), applies the Chile local-time TZ rule, sets the sync callback, enables DHCP SNTP. Blinks the yellow LED 3 times. |
| `getTime()`           | Current Unix epoch (seconds). |
| `getTM()`             | **Local Chile time** (America/Santiago, DST-aware) as `struct tm`. |
| `timeavailable()`     | Sync callback — prints local time and emits `Evt::NtpSynced`. |
| `printLocalTime()`    | Prints the current local date/time. |

**Timezone:** Chile local time with automatic DST (CLT = UTC‑4 winter, CLST =
UTC‑3 summer; DST starts 1st Sunday of September, ends 1st Sunday of April),
encoded as a POSIX TZ rule in `initNTP()`.

---

### `src/otaHelper.*`
**Over-The-Air firmware update via `ESP32OTAPull`.**

| Function          | Description |
|-------------------|-------------|
| `checkUpdate()`   | Fetches the update manifest from `updateURL` (centralised in `core/board.h`), downloads/installs if a newer version exists, then reboots to apply. Guards the OTA window so `updateTick()` skips the other blocking fetches that cycle. |
| `callback()`      | Progress callback — prints percentage and toggles the yellow LED. |
| `errtext(code)`   | Human‑readable OTA result string. |

> **Partition table:** the device ships with a per-device OTA partition layout
> flashed at the factory; `platformio.ini` intentionally keeps
> `board_build.partitions` commented so a default `pio run` does not overwrite
> it. Do not enable it without also flashing the matching partitions.

---

### `src/config/schedule.*`
**Event-driven relay scheduler** (policy only — drives the relay via
`actuators/relay`, never touches pins directly). Replaces the old
`scheduleHelper.h`.

The relay's desired state is *evaluated* (never replayed), so NTP jumps or
missed ticks always land in the correct state. `loop()` calls `sched::tick()`
every iteration; it fires a transition only when its pre-computed edge is due
(edge-triggered, no relay chatter).

**Precedence (highest → lowest):** sticky MQTT `RELAY` override → date-range
`Exceptions` → `Mode` (`auto`/`on`/`off`) + weekly windows.

| Function                | Description |
|-------------------------|-------------|
| `initSchedule()`        | Called from `setup()`; subscribes to the event bus, loads NVS, fetches/apply remote schedule. |
| `fetchSchedule()`       | Loads NVS, then (if WiFi up) HTTPS-fetches the manifest by MAC, applies it, persists to NVS. Also called every 10 min. |
| `tick()`                | Every `loop()`: handles day rollover (prunes expired exceptions), and fires the next transition at its edge. |
| `desiredState()`        | Computes the relay state from the precedence chain. |
| `computeNextTransition()` | Scans up to 7 days for the next ON/OFF edge. |
| `printNextTransition()` | Prints the "next transition in …" line. |
| `loadFromNVS()` / `saveToNVS()` | Persist `Mode`, override, `daysMask`, windows, exceptions. |
| `pruneExpiredExceptions()` / `evictOldestException()` | Self-cleaning: drop ended exceptions; round-robin evict oldest if full. |

**Schedule configuration file** — `bins/schedule.json` (served from the repo at
`scheduleURL`):

```json
{
  "Schedules": [
    {
      "Device": "A8:42:E3:AB:10:88",
      "Mode": "auto",
      "Auto": {
        "days": "1111100",
        "windows": [["08:30", "12:30"], ["14:30", "18:30"]]
      },
      "Exceptions": [
        { "from": "2026-07-20", "to": "2026-08-10", "state": "off" },
        { "from": "2026-09-18", "to": "2026-09-18", "state": "on" }
      ]
    }
  ]
}
```

- `Device` is the ESP32 MAC address.
- `Mode` — `"auto"` (weekly windows), `"on"` (constant ON), or `"off"` (constant OFF).
- `Auto.days` — 7-char Mon→Sun bitmask (`1` = active). `"1111100"` = Mon–Fri.
- `Auto.windows` — list of `["HH:MM","HH:MM"]` ON intervals; arbitrary count.
- `Exceptions` *(optional)* — date-range overrides; up to 4 kept, auto-expire at midnight, round-robin evict when full.
- **Legacy shim:** entries using the old `FilterOn`/`FilterOff`/`LunchStart`/`LunchEnd` HHMM fields are still accepted and converted to `auto` with two windows.
- The device always boots with the last persisted values (or defaults), so the
  schedule survives reboots / server outages; remote changes apply within 10 min.

---

### `src/actuators/relay.*`
**Relay GPIO driver** (hardware). The fan (relay 1) and UV (relay 2) are
independent channels driven active-low.

| Function            | Description |
|---------------------|-------------|
| `init()`            | Configures pins as outputs and forces both relays OFF out of reset. |
| `set(Id/int, bool)` | Edge-triggered ON/OFF for one channel. |
| `setBoth(bool)`     | Drives both channels (used by the scheduler). |
| `state(Id/int)`     | Logical state of a channel. |
| `bothOn()` / `anyOn()` | Combined state queries. |

> The relay pins are forced HIGH (de-energized) in `init()` because the ESP32
> powers up with pins LOW — without this the active-low modules would stay ON
> until the first scheduler `rearm()`.

---

## Dependencies

Built with **PlatformIO** (`platformio.ini`):

| Library                       | Version     | Purpose                     |
|-------------------------------|-------------|-----------------------------|
| `espressif32`                 | 6.7.0       | ESP32 Arduino core          |
| `Adafruit BME280 Library`     | ^2.2.2      | BME280 sensor driver        |
| `PubSubClient`                | ^2.8        | MQTT client                 |
| `ArduinoJson`                 | ^7.0.4      | JSON serialisation          |
| `ESP32-OTA-Pull`              | (git)       | OTA update mechanism        |
| `WiFiManager`                 | ^2.0.17     | WiFi connection & portal    |
| `OneButton`                   | ^2.5.0      | Button input handling       |

> `TaskScheduler` was removed from the build — scheduling is done with
> `millis()` polling in `loop()` plus the event bus, not a timer library.

---

## Configuration

Key compile-time parameters live in `core/board.h`:

| Parameter            | Default     | Description                             |
|----------------------|-------------|-----------------------------------------|
| `CO2_LOW`            | 700 ppm     | Threshold below which CO₂ is "Green"    |
| `CO2_HIGH`           | 800 ppm     | Threshold above which CO₂ is "Red"      |
| `measurementDelay`   | 60 000 ms   | Interval between measurement cycles     |
| `updateDelay`        | 600 000 ms  | Interval between OTA/update checks      |
| `PORTAL_NAME`        | `"AIRCARE"` | SSID when in WiFi configuration mode    |
| `PORTAL_TIMEOUT`     | 180 s       | Timeout for the configuration portal    |

- **MQTT broker** is *dynamic*: resolved from `bins/config.json` (matched by
  MAC, or `Default`), persisted to NVS, with a compiled-in HiveMQ Cloud
  fallback. It is no longer hard-coded.
- **Remote manifest URLs** (`scheduleURL`, `updateURL`, `configURL`) are
  centralised in `core/board.h` / `configHelper.cpp`.
- `secrets.h` (MQTT credentials) is git-ignored; copy `secrets.h.example`.

---

## TODO / Proposed Improvements

Brainstorm from a code review (2026‑07‑15), with current status.

### A. Correctness bugs — DONE
1. `co2_fu` read the wrong register — fixed (`co2_fu` now reads
   `CO2_FILTERED_UNCOMPENSATED`).
2. No CO₂ validity/error check — added `co2Valid()` + `readErrorStatus()`;
   invalid samples skip publish/LED.
3. `rl2` telemetry duplicated `rl1` — relays are now independent
   (`relay::state(1)` / `relay::state(2)`), real `rl1`/`rl2` published.

### B. Robustness / reliability
4. `serializedString[300]` could truncate — **FIXED** (512+1, bounded
   `serializeJson`, queue slots matched).
5. `mqttPublish` persists to NVS on every dropped sample — *pending*. Persist
   every N samples or only on reconnect (NVS write endurance ~10⁵).
6. Hardcoded manifest URLs — **FIXED** (centralised in `core/board.h`).
7. 4 CO₂ I²C reads per cycle, only 1 used for logic — *pending*. Consider
   lazy-reading telemetry-only values; `delay(100)` sits between BME/CO₂ reads.

### C. Structure / maintainability
8. Event bus is synchronous, no dedup/unsubscribe — *pending (optional)*. A
   re-entrant `emit` could recurse; a queued model would be safer.
9. `state2string()` re-derives state instead of using stored `co2_State` —
   *pending*. Minor.
10. Tests — **PARTIAL**. Native (`host`) tests exist for `relay` and
    `co2check`. Scheduler logic (`desiredState`, `computeNextTransition`,
    `exceptionActive`, `evaluateWindows`) is still untested.

### D. Features / nice-to-haves
11. Periodic HEALTH telemetry — **DONE** (`measurementTick()` publishes heap +
    RSSI every 5 min).
12. Remote-configurable CO₂ thresholds — *pending*. `CO2_LOW`/`CO2_HIGH` are
    compile-time; expose via MQTT/NVS like the schedule.
13. `GET_CONFIG` command — *pending*. Dump current schedule/override/exceptions
    back over MQTT for remote verification.
