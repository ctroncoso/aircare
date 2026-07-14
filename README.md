# AirCare — Indoor Air Quality Monitor

**Version:** 1.1.12 | **Board:** ESP32 Dev Module | **Framework:** Arduino

AirCare is an IoT firmware for the ESP32 microcontroller that monitors indoor air quality using two main sensors:

- **Senseair Sunrise** — a high-precision CO₂ sensor (I²C)
- **Bosch BME280** — a combined temperature, humidity, and barometric pressure sensor (I²C)

The device reads sensor data on a configurable interval, publishes the measurements to an MQTT broker, controls two relay outputs (fan and UV filter) based on a time schedule, and supports Over-The-Air (OTA) firmware updates. A tri-color LED (Red / Yellow / Green) provides a visual indication of the current CO₂ level.

---

## Table of Contents

- [AirCare — Indoor Air Quality Monitor](#aircare--indoor-air-quality-monitor)
  - [Table of Contents](#table-of-contents)
  - [Hardware Overview](#hardware-overview)
  - [Core Application Flow](#core-application-flow)
  - [Modules / File Descriptions](#modules--file-descriptions)
    - [`globals.h`](#globalsh)
    - [`main.cpp`](#maincpp)
    - [`mainHelper.h`](#mainhelperh)
    - [`wifiManagerHelper.h`](#wifimanagerhelperh)
    - [`mqttHelper.h`](#mqtthelperh)
    - [`ledHelper.h`](#ledhelperh)
    - [`bmeHelper.h`](#bmehelperh)
    - [`sunrise_i2c.h` / `sunrise_i2c.cpp`](#sunrise_i2ch--sunrise_i2ccpp)
    - [`sunriseHelper.h`](#sunrisehelperh)
    - [`ntpHelper.h`](#ntphelperh)
    - [`otaHelper.h`](#otahelperh)
    - [`scheduleHelper.h`](#schedulehelperh)
  - [Dependencies](#dependencies)
  - [Configuration](#configuration)

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

1. **`setup()`** — initialises serial, LED pins, WiFi, MQTT, NTP, the CO₂ sensor, and the BME280. It also checks for an OTA update on boot. If any critical sensor fails, the ESP32 restarts.
2. **`loop()`** — runs continuously:
   - Polls the button for user interaction.
   - Calls **`measurementTick()`** every 1 minute to read sensors, control relays, and publish data.
   - Calls **`updateTick()`** every 10 minutes to check for OTA updates.
   - Keeps the MQTT connection alive.

---

## Modules / File Descriptions

### `globals.h`
**Global definitions, pin assignments, and shared state.**

- **Macros/Constants** — `SEALEVELPRESSURE_HPA` (1013.25 hPa), `PROGRAM_VERSION`, `CO2_LOW` / `CO2_HIGH` thresholds (700 / 800 ppm), `PORTAL_NAME` (“AIRCARE”), `PORTAL_TIMEOUT`, `measurementDelay` (1 min), `updateDelay` (10 min).
- **Pin assignments** — Red LED (25), Yellow LED (26), Green LED (27), Relay 1 (32), Relay 2 (33).
- **`CO2_Condition` enum** — `Green` / `Yellow` / `Red` / `Unknown`, used to pick the active LED.
- **`pub_event` enum** — `ERROR` = -1, `INFO` = 0, used when publishing MQTT events.
- **Shared variables** — `doc` (JSON document), `serializedString`, `co2_State`, `WiFiClient`, `WiFiManager`.

---

### `main.cpp`
**Entry point: `setup()` and `loop()`.**

| Function               | Description |
|------------------------|-------------|
| `setup()`              | Initialises serial, LEDs, WiFi, MQTT, NTP, CO₂ sensor, BME280, and relay pins. Sends boot events over MQTT. Restarts the MCU if a sensor fails. |
| `loop()`               | Main loop: polls the button, runs the measurement cycle (`measurementTick`) and update cycle (`updateTick`), and keeps the MQTT client alive. |
| `startWifiPortal()`    | Triggered by a double‑click on the BOOT button. Opens the WiFiManager configuration portal. |
| `handleMultiClick()`   | Triggered by a multi‑click (≥3 clicks). Starts an OTA check on 3 clicks. |

---

### `mainHelper.h`
**Measurement and update scheduling.**

| Function                | Description |
|-------------------------|-------------|
| `readValues()`          | Reads temperature, pressure, altitude, and humidity from the BME280, and the four CO₂ readings (filtered/unfiltered, compensated/uncompensated) from the Sunrise sensor. |
| `getCO2_State()`        | Returns a `CO2_Condition` based on the CO₂ ppm value: Green (< 700), Yellow (700–800), Red (≥ 800). |
| `printValues()`         | Builds a JSON document with all sensor values and the current state, then serialises and prints it. |
| `state2string()`        | Converts the current CO₂ condition to a human‑readable string. |
| `measurementTick()`     | Called every 1 min. Reads sensors, prints values (including `sched_mode`, `sched_ovr`, `sched_exc` and the "next transition" line), publishes to MQTT at `/cleanair/sensor`, and updates the LEDs. The relay itself is driven by the event-driven `sched::tick()` (see `scheduleHelper.h`), not here. |
| `updateTick()`          | Called every 10 min. Triggers `ota::checkUpdate()`. |
| `testRelay()`           | Utility that activates both relays for 10 seconds, then turns them off. |

---

### `wifiManagerHelper.h`
**WiFi connection and captive portal.**

| Function            | Description |
|---------------------|-------------|
| `initWifiM()`       | Attempts to connect to a saved WiFi network using `WiFiManager.autoConnect()`. Retries up to 5 times with a 60‑second delay between attempts. If it fails, the MCU restarts. |
| `configModeCallback()` | Callback invoked when the WiFiManager enters AP mode — blinks the green LED 4 times to signal that the configuration portal is active. |

---

### `mqttHelper.h`
**MQTT client management.**

| Function              | Description |
|-----------------------|-------------|
| `initMQTT()`          | Configures the `PubSubClient` (server: `52.23.110.164`, port 1883, buffer size 256, keep‑alive 15 s) and attempts an initial connection. |
| `mqttreconnect()`     | Tries to reconnect to the MQTT broker up to 12 times (5 s apart). Restarts the MCU if all attempts fail. |
| `mqttPublish()`       | Publishes a string payload to a given MQTT topic. Calls `mqttreconnect()` if the client is disconnected. |
| `publishEvent()`      | Constructs a JSON event (`event`, `param`, `mac`, `fw`) and publishes it to `cleanair/events`. |
| `callback()`          | Handles incoming MQTT commands (see below) and prints the topic/payload to Serial. |

**Subscribed topics on connect:**
- `AirCare/inCommands/broadcast`
- `AirCare/inCommands/<MAC address>`

**Remote commands** (JSON payloads):
- `{"cmd":"RELAY","value":"ON"|"OFF"|"AUTO"}` — sticky manual override. `ON`/`OFF` force the relay regardless of schedule; `AUTO` clears the override and returns control to the schedule. Persisted to NVS.
- `{"cmd":"EXCEPTION","from":"YYYY-MM-DD","to":"YYYY-MM-DD","state":"on"|"off"}` — add a date-range exception (holiday / vacation / onsite testing). Persisted to NVS; auto-expires and round-robins when the 4-slot list is full.
- `{"cmd":"EXCEPTION_CLEAR"}` — remove all exceptions.

---

### `ledHelper.h`
**Visual indicators.**

| Function                   | Description |
|----------------------------|-------------|
| `initLEDS()`               | Sets the three LED pins as outputs and clears them. |
| `clearLeds()`              | Turns off all three LEDs. |
| `setLedOnCO2Condition()`   | Lights up the LED corresponding to the current `CO2_Condition` (Green, Yellow, Red, or unknown). |
| `blinkLed()`               | Blinks a given LED a specified number of times. Optionally restores the CO₂ condition LED after blinking. |
| `flipLed()`                | Toggles the state of a given LED pin (used during OTA progress). |
| `POSTBlinks()`             | Power‑On Self Test — blinks red, yellow, and green LEDs in sequence (currently commented out in `initLEDS()`). |

---

### `bmeHelper.h`
**BME280 temperature, humidity and pressure sensor.**

| Function    | Description |
|-------------|-------------|
| `initBME()` | Initialises the BME280 at I²C address `0x76` and sets sampling to 16× oversampling for temperature, humidity, and pressure (normal mode, filter off). Returns `false` if the sensor is not found. |

**Global variables exposed:** `temp`, `pressure`, `altitude`, `humidity`.

---

### `sunrise_i2c.h` / `sunrise_i2c.cpp`
**Low-level I²C driver for the Senseair Sunrise CO₂ sensor.**

This class provides the complete register map for the Sunrise sensor. Key public methods:

| Method                        | Description |
|-------------------------------|-------------|
| `initSunrise()`               | Initialises I²C (SDA=21, SCL=22, 100 kHz) and checks for sensor presence at address `0x68`. Retries up to 10 times. |
| `readCO2(CO2Type)`            | Reads a 16‑bit CO₂ value from one of four registers: filtered/compensated, unfiltered/compensated, filtered/uncompensated, unfiltered/uncompensated. |
| `readErrorStatus()`           | Reads the 16‑bit error status register. |
| `readChipTemp()`              | Reads the chip internal temperature. |
| `readMeasurementCount()`      | Returns the number of measurements performed. |
| `readMeasurementCycleTime()`  | Returns the current measurement cycle time. |
| `setMeasurementMode(mode)`    | Sets continuous (`0x00`) or single (`0x01`) measurement mode. |
| `setMeasurementPeriod(ms)`    | Sets the measurement period in seconds. |
| `setNbrSamples(n)`            | Sets the number of samples per measurement. |
| `resetSensor()`               | Resets the sensor by writing `0xFF` to the reset register. |
| `startSingleMeasurement()`    | Starts a single measurement (for use in single-shot mode). |
| `getSingleReading(co2_type, readyPin)` | Performs a full power‑down / single‑measurement / power‑up cycle, saving and restoring the sensor state. |
| `incrementABCTime()`          | Increments the ABC (Automatic Baseline Correction) time counter stored in RTC memory. |
| `readABCTime()` / `writeABCTime()` | Read/write the ABC elapsed time register. |
| `setABCPeriod()`              | Sets the ABC period. |
| `setABCTarget()`              | Sets the ABC target concentration. |
| `setIIRFilter()`              | Configures the internal IIR filter parameter. |
| `readCalibrationStatus()` / `clearCalibrationStatus()` / `setCalibrationCommand()` | Calibration management. |
| `readPowerDownData()` / `writePowerDownData()` | Saves/restores 24 bytes of sensor state to RTC memory for low‑power single‑shot operation. |
| `setMeterControl()` / `readMeterControl()` | Read/write the meter control register. |
| `setI2cAddress()`             | Changes the sensor’s I²C address (requires a subsequent reset). |

**Internal I²C helpers (file‑static):**
- `wakeUp()` — sends repeated start conditions to wake the sensor from sleep (up to 5 attempts).
- `read8bitSigned()` — reads a single signed byte from a register.
- `write8bit()` — writes a single byte to a register.
- `read16bitSigned()` — reads a 16‑bit signed integer (big‑endian) from a register.
- `write16bit()` — writes a 16‑bit value (two bytes) to a register.

---

### `sunriseHelper.h`
**High‑level Sunrise sensor initialisation.**

| Function          | Description |
|-------------------|-------------|
| `initCo2Sensor()` | Calls `initSunrise()`, configures the measurement period (from `measurementDelay`), sets 16 samples, enables continuous mode, resets the sensor, and waits for the first measurement to complete (up to 20 attempts). Returns `false` if the initialisation or first measurement times out. |

**Global variables exposed:** `co2_fc`, `co2_uc`, `co2_fu`, `co2_uu` (the four CO₂ readings).

---

### `ntpHelper.h`
**Network Time Protocol synchronisation.**

| Function              | Description |
|-----------------------|-------------|
| `initNTP()`           | Configures SNTP with servers `ntp.shoa.cl` and `time.nist.gov`, applies the Chile local‑time timezone rule (see below), sets the time‑sync notification callback, and enables DHCP server mode. Publishes an MQTT event and blinks the yellow LED 3 times. |
| `getTime()`           | Returns the current Unix epoch timestamp (seconds). |
| `getTM()`             | Returns the **local Chile time** (America/Santiago, DST‑aware) as a `struct tm`. |
| `timeavailable()`     | Callback invoked when the time is synchronised — prints the new local time. |
| `printLocalTime()`    | Prints the current **local** date/time to Serial in human‑readable format. |

**Timezone:** The device runs on **Chile local time with automatic daylight saving** (CLT = UTC‑4 in winter, CLST = UTC‑3 in summer; DST starts 1st Sunday of September, ends 1st Sunday of April). newlib has no IANA tz database, so this is encoded as a POSIX TZ rule applied in `initNTP()` via `setenv("TZ", "<-04>4<-03>,M9.1.6/24,M4.1.6/24", 1); tzset();`. All scheduling and time displays use this local time.

---

### `otaHelper.h`
**Over‑The‑Air firmware update via `ESP32OTAPull`.**

| Function          | Description |
|-------------------|-------------|
| `checkUpdate()`   | Checks for OTA updates by fetching an update manifest from `https://raw.githubusercontent.com/ctroncoso/aircare/main/bins/update.json`. If an update is available, blinks all three LEDs and downloads/installs the new firmware. Returns `false` after the check. |
| `callback()`      | Progress callback — prints the download percentage and toggles the yellow LED during the update. |
| `errtext(code)`   | Returns a human‑readable error string for an OTA result code. |

---

### `scheduleHelper.h`
**Event-driven relay scheduler (replaces the old per-minute HHMM polling).**

The relay's desired state is *evaluated* (never replayed), so NTP jumps or missed ticks always land in the correct state. `loop()` calls `sched::tick()` every iteration; it fires a transition only when its pre-computed edge is due (edge-triggered, no relay chatter).

**Precedence (highest → lowest):** sticky MQTT `RELAY` override → date-range `Exceptions` → `Mode` (`auto`/`on`/`off`) + weekly windows.

| Function                | Description |
|-------------------------|-------------|
| `initSchedule()`        | Called from `setup()` (after WiFi/NTP are up). Loads persisted values from NVS, then attempts to fetch and apply the remote schedule. Prints the active schedule. |
| `fetchSchedule()`       | Loads values from NVS first, then (if WiFi is connected) performs an HTTPS GET of the schedule manifest, matches the entry whose `Device` equals this MCU's MAC address, applies the `Mode`/windows/`Exceptions`, and persists them to NVS. On any failure it logs a `[SCHED]` message and keeps the NVS/default values. Also called every 10 min from `updateTick()`. |
| `tick()`                | Called every `loop()`. Consumes the handshake flags (NTP re-sync, MQTT override, MQTT exception, local midnight rollover), fires the next scheduled transition when due, and re-arms the timeline. |
| `desiredState()`        | Computes the relay state from the precedence chain above. |
| `computeNextTransition()` | Scans up to 7 days for the next ON/OFF window edge and stores it as `nextTransitionTime`. |
| `printNextTransition()` | Prints the "next transition in …" line after `Current State:` in the reading cycle. |
| `loadFromNVS()` / `saveToNVS()` | Persist `Mode`, override, `daysMask`, windows, and exceptions to the `aircare` NVS namespace. |
| `pruneExpiredExceptions()` / `evictOldestException()` | Self-cleaning: drop exceptions whose range has ended; round-robin evict the oldest if the list is full. |

**Schedule configuration file** — `bins/schedule.json` (served from the GitHub repo at `https://raw.githubusercontent.com/ctroncoso/aircare/main/bins/schedule.json`):

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

- `Device` is the ESP32 MAC address (same format as in `update.json`).
- `Mode` — `"auto"` (follow weekly windows), `"on"` (constant ON), or `"off"` (constant OFF).
- `Auto.days` — 7-char Mon→Sun bitmask (`1` = schedule active that weekday). `"1111100"` = Mon–Fri.
- `Auto.windows` — list of `["HH:MM","HH:MM"]` ON intervals for active days. **Arbitrary number** of windows/day; the lunch pause is simply the gap between two windows.
- `Exceptions` *(optional)* — date-range overrides (holiday / vacation / onsite testing). `from`/`to` are inclusive `YYYY-MM-DD`; `state` is `"on"` or `"off"`. Up to 4 are kept; expired ones auto-drop at midnight and the oldest is evicted round-robin if still full.
- **Legacy shim:** entries using the old `FilterOn`/`FilterOff`/`LunchStart`/`LunchEnd` `HHMM` fields are still accepted and converted to `auto` with two windows `[On..LunchStart]` + `[LunchEnd..Off]`.
- The device always boots with the last successfully persisted values from NVS (or the compiled-in defaults on first boot), so the relay schedule keeps working even when the server is unreachable. Remote changes are applied within 10 minutes and persisted.

---

## Dependencies

The project is built with **PlatformIO** (`platformio.ini`):

| Library                              | Version     | Purpose                     |
|--------------------------------------|-------------|-----------------------------|
| `espressif32`                        | 6.7.0       | ESP32 Arduino core          |
| `Adafruit BME280 Library`            | ^2.2.2      | BME280 sensor driver        |
| `PubSubClient`                       | ^2.8        | MQTT client                 |
| `ArduinoJson`                        | ^7.0.4      | JSON serialisation          |
| `ESP32-OTA-Pull`                     | latest      | OTA update mechanism        |
| `TaskScheduler`                      | ^3.8.5      | Task scheduling (available) |
| `WiFiManager`                        | ^2.0.17     | WiFi connection & portal    |
| `OneButton`                          | ^2.5.0      | Button input handling       |

---

## Configuration

Key parameters that can be adjusted in `globals.h`:

| Parameter            | Default     | Description                             |
|----------------------|-------------|-----------------------------------------|
| `CO2_LOW`            | 700 ppm     | Threshold below which CO₂ is “Green”    |
| `CO2_HIGH`           | 800 ppm     | Threshold above which CO₂ is “Red”      |
| `measurementDelay`   | 60 000 ms   | Interval between measurement cycles     |
| `updateDelay`        | 600 000 ms  | Interval between OTA update checks      |
| `PORTAL_NAME`        | `"AIRCARE"` | SSID when in WiFi configuration mode    |
| `PORTAL_TIMEOUT`     | 180 s       | Timeout for the configuration portal    |

The MQTT broker address is currently hard‑coded to `52.23.110.164` in `mqttHelper.h`.