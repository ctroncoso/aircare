# AirCare backend

Self-hosted backend for the AirCare ESP32 air-quality sensors. Runs on the home
server and is reached via Tailscale.

```
 ESP32 → HiveMQ Cloud (TLS 8883) → cleanair/sensor, cleanair/events
                                  ← AirCare/inCommands/<mac>, /broadcast
                                         │
                                         ▼ (MQTT client, same creds)
              ┌─────────── Home server (Docker Compose) ───────────┐
              │  Node-RED  ──▶  InfluxDB  ◀──  Grafana              │
              │  (glue+UI)      (time-series)   (dashboards)        │
              └────────────────────────────────────────────────────┘
```

## Stack
- **InfluxDB 2.x** — time-series store (bucket `aircare`).
- **Node-RED** — MQTT↔DB glue + control UI (auto-installs dashboard + InfluxDB nodes).
- **Grafana** — dashboards (auto-provisioned InfluxDB datasource).

The broker stays **HiveMQ Cloud** (devices can't reach the home server). No local
broker, no public reverse proxy — Tailscale provides secure remote access.

## Run
```bash
cp .env.example .env      # fill in MQTT creds + strong passwords/token
docker compose up -d
```
- InfluxDB: http://<host>:8086  (init from .env on first boot)
- Node-RED: http://<host>:1880
- Grafana:   http://<host>:3000

All three are also reachable over Tailscale (MagicDNS).

## Milestones
- [x] **1. Compose stack + InfluxDB init** (this folder)
- [x] **2. Node-RED → HiveMQ Cloud**: `flows.json` deployed and **connected** to `4532efaca6c5432ea63dbae9a7690ef4.s1.eu.hivemq.cloud:8883` (subscribes `cleanair/sensor`, `cleanair/events`).
- [x] **3. InfluxDB writes**: `flows.json` maps telemetry → `sensor` measurement, events → `events` measurement (token baked from `.env`). Write path verified end-to-end (synthetic point written & purged). `fn_events` also captures REPORT/health fields (`rssi`, `uptime_ms`, `local_time`, relay states). **Live device data confirmed** — real `sensor` points landing (e.g. `co2_fc` ≈ 494 ppm).
- [x] **4. Grafana dashboards**: provisioned `AirCare` dashboard (`/d/aircare/aircare`) — CO₂ (700/800 bands), CO₂ variants, temp/humidity/pressure, relay state-timeline, events table, `mac` template variable. Flux queries validated against InfluxDB. (Note: dashboard *provider* YAML is only read at Grafana startup — restart the container after adding it.)
- [x] **5. Node-RED control UI**: dashboard "Control" tab at `/ui` — target device **dropdown** (populated at startup from `bins/config.json`, mounted read-only at `/data/config.json`) + broadcast toggle, RELAY ON/OFF/AUTO, EXCEPTION (from/to/state) + EXCEPTION_CLEAR, REBOOT, UPDATE, REPORT. Publishes JSON to `AirCare/inCommands/<mac>` or `AirCare/inCommands/broadcast` (QoS 1). Command publish path verified end-to-end via MQTT round-trip. **Note:** the `config.json` mount was added later — recreate the Node-RED container (`docker compose up -d nodered`) so the file appears in `/data`.
- [x] **6. Tailscale access**: Grafana `:3001`, Node-RED `:1880` reachable over Tailscale (`100.125.200.49`)
- [x] **7. Grafana alerts**: provisioned under `grafana/provisioning/alerting/` — `CO2 High (>=800 ppm)` (per-`mac`, `for: 5m`) and `Device Offline (no telemetry 10m)` (`noDataState: Alerting`). Both evaluate healthy; contact point `aircare-default` (webhook → `http://nodered:1880/alert`) + default notification policy. **Note:** alerting provisioning is read only at Grafana startup — restart the container after editing.
