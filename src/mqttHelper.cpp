// mqttHelper.cpp — MQTT broker connect/publish/subscribe implementation.
#include "mqttHelper.h"
#include "secrets.h"  // MQTT_USER / MQTT_PASS (gitignored, private)
#include "core/nvsStore.h"  // nvs::putBytes/getBytes — persist buffered telemetry across reboot
#include "otaHelper.h"  // ota::checkUpdate() — used by the UPDATE command
#include "ntpHelper.h"  // ntp::getTime() / ntp::getTM() — local time for the report
#include "actuators/relay.h"  // relay::state() — relay status for the report
#include <cctype>      // toupper() — normalize command casing
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace mqtt
{
    PubSubClient client(espClient); // defined here (single TU) — owned ONLY by mqttTask

    // --- MQTT moved off loopTask (fixes loopTask WDT wedges) -----------------
    // PubSubClient::readByte() busy-waits on available() and client.connect()'s
    // TLS handshake can block forever (WiFiClientSecure::setTimeout is a no-op
    // for the TLS recv/handshake in this core). Any of those on loopTask wedged
    // loop() and tripped the 120s task WDT. So ALL client network I/O now runs
    // in mqttTask (pinned core 0); loopTask only enqueues publishes to a
    // thread-safe queue. A dedicated mqttWatchdogTask deletes+recreates mqttTask
    // if it ever wedges, so a bad TLS handshake recovers WITHOUT rebooting and
    // WITHOUT freezing the sensors/I2C/OTA on loopTask.
    #define MQTT_TASK_WEDGE_MS 30000   // mqttTask must cycle within this or it's killed
    static QueueHandle_t g_pubQ = nullptr;
    static TaskHandle_t g_mqttTask = nullptr;
    static volatile uint32_t g_mqttAliveMs = 0;   // set each mqttTask iteration
    static volatile bool    g_mqttForceDisc = false;
    static bool             g_mqttTaskRunning = false;

    // Print the exact parameters used for the broker connection so configuration
    // mismatches (host/port/user) are visible in the serial log.
    void logConnectionParams(const char *clientId)
    {
        Serial.printf("[MQTT] host=%s port=%d user=%s clientId=%s tls=insecure\n",
                      cfg::brokerHost, cfg::brokerPort, MQTT_USER, clientId);
    }

    // Prepare the secure client for a fresh connection. After a disconnect the
    // WiFiClientSecure socket/TLS state can be left stale, which causes the next
    // connect() to time out (rc=-1) instead of starting a new handshake. Always
    // stop the client and re-apply setInsecure() before reconnecting.
    // SNI (required by HiveMQ Cloud) is set automatically by WiFiClientSecure
    // because we pass the broker HOSTNAME (never a raw IP) to setServer() below.
    void prepareSecureClient()
    {
        espClient.stop();
        espClient.setInsecure();
        // Hard recv timeout so client.loop()'s blocking recv() always returns
        // within ~2s. Without this, a stalled/starved TLS socket (e.g. while an
        // OTA download hammers the WiFi stack on the other core) parks loopTask
        // in recv() forever and trips the 120s task WDT. Re-applied after every
        // stop() since stop() clears socket options.
        espClient.setTimeout(2000);
    }

    // Exponential backoff state for the periodic reconnect in loop() and at
    // boot. HiveMQ Cloud rate-limits connection attempts; hammering it right
    // after a reset (or after an abrupt disconnect) makes the broker stop
    // answering the TLS handshake (rc=-1/-4). We therefore start at 1s and
    // back off exponentially (1s, 2s, 4s, ... up to 30s cap) with jitter, so
    // retries spread out instead of hammering. SNI (also required by HiveMQ
    // Cloud) is handled automatically because we always pass the broker
    // HOSTNAME (never a raw IP) to setServer().
    static uint8_t g_backoffStep = 0;
    static const unsigned long BACKOFF_BASE_MS = 1000;    // 1 s — start small, grow exponentially
    static const unsigned long BACKOFF_MAX_MS  = 30000;   // 30 s cap
    static unsigned long g_lastReconnectAttempt = 0;      // last reconnect try (ms)
    static unsigned long g_reconnectDelay = BACKOFF_BASE_MS; // current backoff (grows on failure)

    // MQTT keepalive period (seconds). 30s gives the broker a 45s grace window
    // before it may drop an idle socket — comfortably covering the blocking
    // sensor init in setup() (~10s) and the periodic HTTP fetches in
    // updateTick() (~5-15s), during which client.loop() (which sends the
    // PINGREQ keepalive) is not called. A shorter value risks the broker
    // closing the connection before loop() resumes pumping keepalives.
    static const uint16_t MQTT_KEEPALIVE_S = 30;

    // ------------------------------------------------------------------------
    // "Communication dead" watchdog — last-resort autonomous recovery.
    //
    // mqttLoop() already reconnects with exponential backoff, so a brief link
    // drop is handled. But sometimes the TLS socket gets wedged (HiveMQ
    // silently dropping the session yields rc=-1/-4 that backoff alone can't
    // clear) and the device sits "connected enough" to think it's fine yet
    // never delivers a packet. To catch that, we record the last time ANY
    // publish actually succeeded. If that goes stale for too long *while WiFi
    // is still up* (a network outage would not be fixed by rebooting), we force
    // ESP.restart() so the TLS/socket state is rebuilt from scratch.
    static unsigned long g_lastPublishOkMs = 0;   // 0 == "never succeeded"
    static const unsigned long COMMS_DEAD_MS = 60UL * 60UL * 1000UL; // 60 min
    static bool g_commsDeadRebooted = false;       // edge latch for the event

    // Called from mqttPublish() only on a genuine successful client.publish().
    inline void markPublishOk() { g_lastPublishOkMs = millis(); }

    // --- stale-socket guard (fixes loopTask WDT during/after OTA) -----------
    // PubSubClient::readByte() busy-waits on _client->available() and
    // WiFiClientSecure::setTimeout() does NOT bound the TLS recv, so calling
    // client.loop() on a socket that is "connected" but has no inbound data
    // (e.g. a TLS session gone stale after a 90s OTA download) blocks loopTask
    // in readPacket()/readByte() forever -> 120s task WDT reset. To avoid that
    // we only ever call client.loop() when real inbound data is pending
    // (espClient.available() > 0, a non-blocking check), and we force-tear the
    // socket once it has been silent longer than the keepalive window so the
    // next call takes the (bounded) reconnect branch instead of the blocking
    // read. Keepalive PINGREQ is sent opportunistically whenever we do loop();
    // if the socket goes silent we tear+reconnect rather than risk a blocking
    // read.
    static unsigned long g_lastMqttTrafficMs = 0; // last time client.loop() returned with a live link
    static const unsigned long MQTT_STALE_MS = 45000; // > 30s keepalive; tear a silent socket

    // Only safe to call client.loop() when inbound data is ready (non-blocking
    // read) or the link is down (reconnect branch is bounded). Returns true when
    // we should still attempt client.loop() this cycle.
    static bool mqttCanLoop()
    {
        if (!client.connected())
            return true; // reconnect branch — bounded connect(), no blocking read
        if (millis() - g_lastMqttTrafficMs >= MQTT_STALE_MS)
        {
            // Socket "connected" but silent past keepalive -> it is stale/wedged.
            // Tear it down so the next call reconnects cleanly instead of
            // blocking in readPacket().
            Serial.println("[MQTT] socket silent >keepalive — stopping stale link");
            espClient.stop();
            return true; // now !connected -> reconnect branch
        }
        // Connected and recently live: only loop() if there is actual inbound
        // data to process. This guarantees readByte() never spins on an empty
        // socket. (Keepalive PINGREQ is covered when data arrives or on the
        // next stale-teardown.)
        return espClient.available() > 0;
    }

    // Mark the link as having had traffic this cycle (so the stale timer resets).
    static void mqttMarkTraffic()
    {
        if (client.connected())
            g_lastMqttTrafficMs = millis();
    }

    // True when no publish has succeeded for COMMS_DEAD_MS despite WiFi being
    // associated. Returns false if WiFi itself is down (let the network recover
    // rather than rebooting into the same outage).
    bool commsIsDead()
    {
        if (WiFi.status() != WL_CONNECTED) return false; // network-side, not our fault
        if (g_lastPublishOkMs == 0) return false;        // nothing to compare yet
        return (millis() - g_lastPublishOkMs) >= COMMS_DEAD_MS;
    }

    // ------------------------------------------------------------------------
    // Telemetry buffer for dropped-connection resilience.
    //
    // When the broker link is down, measurementTick() keeps producing one
    // sample per minute. Instead of dropping them (the old behaviour), we
    // enqueue them in a bounded RAM ring and mirror the most recent N to NVS
    // so a *reboot* during the outage doesn't lose them either. On the next
    // successful (re)connect the queue is drained back to "cleanair/sensor".
    //
    // NVS constraints drive the design:
    //   - the nvs partition is only 20 KB and is shared with WiFiManager,
    //     config and schedule, so we cap persisted samples at Q_PERSIST_MAX;
    //   - a single NVS blob is capped at ~1984 bytes, so we write the queue
    //     as fixed-size chunks of Q_CHUNK samples (Q_CHUNK*Q_SLOT < 1984).
    // ------------------------------------------------------------------------
    static const size_t  Q_SLOT        = 512 + 1; // matches app.cpp serializedString[512+1] so buffered samples are never truncated
    static const int     Q_CAP         = 64;    // RAM ring capacity (~1 h @ 1/min, ~33 KB)
    static const int     Q_PERSIST_MAX = 30;    // max samples mirrored to NVS (~30 min)
    static const int     Q_CHUNK       = 3;     // samples per NVS blob (3*513 = 1539 < 1984 cap)

    static char   g_queue[Q_CAP][Q_SLOT];
    static int    g_qHead  = 0;   // index of oldest sample
    static int    g_qCount = 0;   // number of buffered samples
    static bool   g_wasConnected = false; // edge detection for (re)connect -> flush

    static void qPush(const char *s)
    {
        if (g_qCount >= Q_CAP)        // full: drop oldest, keep freshest
        {
            g_qHead = (g_qHead + 1) % Q_CAP;
            g_qCount--;
        }
        int tail = (g_qHead + g_qCount) % Q_CAP;
        strncpy(g_queue[tail], s, Q_SLOT - 1);
        g_queue[tail][Q_SLOT - 1] = '\0';
        g_qCount++;
    }

    static bool qPop(char *out)
    {
        if (g_qCount == 0) return false;
        strncpy(out, g_queue[g_qHead], Q_SLOT - 1);
        out[Q_SLOT - 1] = '\0';
        g_qHead = (g_qHead + 1) % Q_CAP;
        g_qCount--;
        return true;
    }

    // Write the most recent Q_PERSIST_MAX samples to NVS as chunked blobs.
    static void persistQueue()
    {
        nvs::withWrite("mqttq", [](Preferences &p)
        {
            int n = g_qCount;
            if (n > Q_PERSIST_MAX) n = Q_PERSIST_MAX;
            int start = (g_qHead + g_qCount - n) % Q_CAP;
            p.putInt("cnt", n);
            int chunk = 0, written = 0;
            while (written < n)
            {
                int inChunk = min(Q_CHUNK, n - written);
                char blob[Q_CHUNK * Q_SLOT];
                for (int i = 0; i < inChunk; i++)
                {
                    int idx = (start + written + i) % Q_CAP;
                    memcpy(blob + i * Q_SLOT, g_queue[idx], Q_SLOT);
                }
                p.putBytes(("c" + String(chunk)).c_str(), blob, (size_t)inChunk * Q_SLOT);
                written += inChunk;
                chunk++;
            }
            p.putInt("chunks", chunk);
        });
    }

    // Load any persisted queue from NVS back into the RAM ring (called at boot).
    static void loadPersisted()
    {
        nvs::withRead("mqttq", [](Preferences &p)
        {
            int n = p.getInt("cnt", 0);
            int chunks = p.getInt("chunks", 0);
            if (n <= 0 || chunks <= 0) return;
            int read = 0;
            for (int c = 0; c < chunks && read < n; c++)
            {
                int inChunk = min(Q_CHUNK, n - read);
                char blob[Q_CHUNK * Q_SLOT];
                size_t got = p.getBytes(("c" + String(c)).c_str(), blob, (size_t)inChunk * Q_SLOT);
                if (got == 0) break;
                for (int i = 0; i < inChunk; i++) qPush(blob + i * Q_SLOT);
                read += inChunk;
            }
            if (g_qCount > 0)
                Serial.printf("MQTT: recovered %d samples from NVS\n", g_qCount);
        });
    }

    // Remove the persisted queue from NVS (called once fully flushed/replaced).
    static void clearPersisted()
    {
        nvs::withWrite("mqttq", [](Preferences &p)
        {
            int chunks = p.getInt("chunks", 0);
            for (int c = 0; c < chunks; c++) p.remove(("c" + String(c)).c_str());
            p.remove("cnt");
            p.remove("chunks");
        });
    }

    // Drain the RAM queue to the broker. Stops and re-buffers if the link drops
    // mid-flush. Emits a RECOVERED event so the gap is visible server-side.
    static void mqttFlushQueue()
    {
        if (g_qCount == 0) return;
        char buf[Q_SLOT];
        int flushed = 0;
        while (qPop(buf))
        {
            if (!client.connected())   // link dropped again — push this one back
            {
                qPush(buf);
                break;
            }
            client.publish("cleanair/sensor", buf);
            client.loop();             // pump keepalive between sends
            delay(10);                 // tiny yield so a large flush won't starve the watchdog
            flushed++;
        }
        if (flushed > 0)
        {
            Serial.printf("MQTT: flushed %d buffered sample(s)\n", flushed);
            publishEvent(INFO, "MQTT|RECOVERED|" + String(flushed));
        }
        if (g_qCount == 0) clearPersisted();
        else persistQueue();           // still have unflushed (link dropped) — keep persisted
    }

    unsigned long mqttBackoffInterval()
    {
        // 1s, 2s, 4s, 8s, 16s, 30s(cap) ... with +/-25% jitter. Matches the
        // HiveMQ guidance: start small and back off exponentially so we neither
        // hammer the broker (which makes it stop answering the TLS handshake,
        // rc=-1/-4) nor wait needlessly long when the link is only briefly down.
        unsigned long raw = BACKOFF_BASE_MS * (1UL << min(g_backoffStep, (uint8_t)5));
        if (raw > BACKOFF_MAX_MS) raw = BACKOFF_MAX_MS;
        long jitter = (long)(raw * 0.25 * ((float)random(0, 100) / 100.0 - 0.5) * 2);
        unsigned long interval = raw + jitter;
        return interval;
    }

    void mqttResetBackoff()
    {
        g_backoffStep = 0;
    }

    bool initMQTT()
    {
        // The broker (HiveMQ Cloud) requires TLS on port 8883. Configure the
        // secure client before binding PubSubClient. setInsecure() disables
        // certificate verification — consistent with the HTTPS config fetch in
        // httpFetch.cpp. For production, replace with setCACert() + the
        // broker's root CA.
        prepareSecureClient();

        client.setCallback(callback);
        // Must be large enough for the /cleanair/sensor telemetry packet
        // (topic + up to 512-byte serializedString + MQTT header). 256 was too
        // small, causing publish() to silently drop measurements.
        client.setBufferSize(512);
        client.setKeepAlive(MQTT_KEEPALIVE_S);
        client.setServer(cfg::brokerHost, cfg::brokerPort);

        // Retry at boot with exponential backoff so the startup events aren't
        // lost. HiveMQ Cloud throttles rapid reconnects, so we space attempts
        // out instead of hammering. Bounded so we don't block forever — if it
        // still fails we continue without MQTT.
        const uint8_t BOOT_MAX_ATTEMPTS = 5;
        for (uint8_t attempt = 1; attempt <= BOOT_MAX_ATTEMPTS && !client.connected(); attempt++)
        {
            String clientId = "aircare-";
            clientId += WiFi.macAddress();
            Serial.printf("Attempting MQTT connection (boot %d/%d)...", attempt, BOOT_MAX_ATTEMPTS);
            logConnectionParams(clientId.c_str());
            prepareSecureClient();
            client.setServer(cfg::brokerHost, cfg::brokerPort);
            if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS))
            {
                Serial.println("connected");
                g_lastMqttTrafficMs = millis(); // reset stale timer on boot connect
                break;
            }
            Serial.printf("failed, rc=%d\n", client.state());
            if (attempt < BOOT_MAX_ATTEMPTS)
            {
                unsigned long wait = mqttBackoffInterval();
                g_backoffStep++;   // grow the backoff for the next boot attempt
                Serial.printf("MQTT: boot retry in %lu ms\n", wait);
                delay(wait);
            }
        }
        mqttResetBackoff();
        loadPersisted();   // pull any queue saved before a reboot during outage
        g_wasConnected = client.connected();
        return client.connected();
    }

    // startMqttTask() / isConnected() are defined later, after the MQTT task
    // machinery they reference (PubItem / mqttTask / mqttWatchdogTask).

    // Subscribe to the inbound command topics and confirm on the events channel
    // so it is verifiable (from the server side) that the device is actually
    // listening for remote commands. Called on every (re)connect.
    void subscribeCommands()
    {
        String broadcast = "AirCare/inCommands/broadcast";
        String own       = String("AirCare/inCommands/") + WiFi.macAddress();
        bool ok = client.subscribe(broadcast.c_str()) &&
                  client.subscribe(own.c_str());
        if (ok)
        {
            Serial.printf("MQTT: subscribed to %s and %s\n", broadcast.c_str(), own.c_str());
            publishEvent(INFO, "MQTT_SUB|OK|Subscribed to command channels");
        }
        else
        {
            Serial.printf("MQTT: subscribe failed (rc=%d)\n", client.state());
            publishEvent(ERROR, "MQTT_SUB|FAIL|Command channel subscribe failed");
        }
    }

    bool mqttTryReconnect()
    {
        String clientId = "aircare-" + WiFi.macAddress();
        logConnectionParams(clientId.c_str());
        prepareSecureClient();
        client.setServer(cfg::brokerHost, cfg::brokerPort);
        if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS))
        {
            Serial.println("MQTT: reconnected.");
            subscribeCommands();
        }
        else
        {
            Serial.printf("MQTT: reconnect attempt failed, rc=%d\n", client.state());
        }
        return client.connected();
    }

    // --- async MQTT task machinery (inserted after deps are declared) --------
    // Publish queue item: topic + payload.
    struct PubItem
    {
        char topic[64];
        char payload[512 + 1];
    };

    // Enqueue a publish (thread-safe, non-blocking). Used by loopTask and any
    // other task; the mqttTask drains it. Drops if full.
    static bool pubEnqueue(const char *topic, const char *payload)
    {
        if (g_pubQ == nullptr) return false;
        PubItem item;
        strncpy(item.topic, topic, sizeof(item.topic) - 1);
        item.topic[sizeof(item.topic) - 1] = '\0';
        strncpy(item.payload, payload, sizeof(item.payload) - 1);
        item.payload[sizeof(item.payload) - 1] = '\0';
        return xQueueSend(g_pubQ, &item, 0) == pdTRUE;
    }

    // Drain the publish queue: publish each item; if a sensor-topic publish
    // fails (link down), buffer it to NVS so a reboot/outage doesn't lose it.
    static void drainPubQueue()
    {
        PubItem item;
        while (xQueueReceive(g_pubQ, &item, 0) == pdTRUE)
        {
            bool sensor = (strcmp(item.topic, "cleanair/sensor") == 0);
            if (client.connected())
            {
                if (client.publish(item.topic, item.payload))
                {
                    markPublishOk();
                    g_lastMqttTrafficMs = millis();
                }
                else if (sensor)
                {
                    qPush(item.payload);
                    persistQueue();
                }
            }
            else if (sensor)
            {
                qPush(item.payload);
                persistQueue();
            }
        }
    }

    // Flush any NVS-backed buffer on (re)connect (defined earlier, near the
    // queue helpers; drains buffered telemetry and emits a RECOVERED event).

    // The MQTT task: owns client/espClient exclusively. Reconnects (bounded by
    // the watchdog) and pumps keepalive + inbound + drains the publish queue.
    static void mqttTask(void *pv)
    {
        (void)pv;
        for (;;)
        {
            g_mqttAliveMs = millis(); // watchdog liveness heartbeat

            if (g_mqttForceDisc)
            {
                espClient.stop();
                client.disconnect();
                g_mqttForceDisc = false;
            }

            if (!client.connected())
            {
                unsigned long now = millis();
                if (now - g_lastReconnectAttempt >= g_reconnectDelay)
                {
                    g_lastReconnectAttempt = now;
                    mqttTryReconnect();
                    if (client.connected())
                    {
                        g_lastMqttTrafficMs = millis();
                        g_lastReconnectAttempt = 0;
                        g_reconnectDelay = BACKOFF_BASE_MS;
                        mqttResetBackoff();
                        mqttFlushQueue();
                    }
                    else
                    {
                        g_backoffStep++;
                        g_reconnectDelay = mqttBackoffInterval();
                    }
                }
            }
            else
            {
                // Only loop() when inbound data is pending (readByte can't spin).
                if (espClient.available() > 0)
                {
                    client.loop();
                    g_lastMqttTrafficMs = millis();
                }
                drainPubQueue();
                g_lastReconnectAttempt = 0;
                g_reconnectDelay = BACKOFF_BASE_MS;
                mqttResetBackoff();
            }

            vTaskDelay(pdMS_TO_TICKS(50)); // 20 Hz poll; yields to other tasks
        }
    }

    // Watchdog for mqttTask: if it hasn't cycled within MQTT_TASK_WEDGE_MS (a
    // wedged TLS handshake/recv), kill and recreate it so the socket/TLS state
    // is rebuilt. loopTask is unaffected and keeps running.
    static void mqttWatchdogTask(void *pv)
    {
        (void)pv;
        for (;;)
        {
            if (g_mqttTaskRunning && (millis() - g_mqttAliveMs >= MQTT_TASK_WEDGE_MS))
            {
                Serial.printf("[MQTT] task wedged >%ds — killing & restarting\n",
                              MQTT_TASK_WEDGE_MS / 1000);
                if (g_mqttTask != nullptr)
                {
                    vTaskDelete(g_mqttTask);
                    g_mqttTask = nullptr;
                }
                g_mqttTaskRunning = false;
            }
            if (!g_mqttTaskRunning && g_pubQ != nullptr)
            {
                g_mqttAliveMs = millis();
                BaseType_t ok = xTaskCreatePinnedToCore(mqttTask, "mqtt", 8192, NULL, 3, &g_mqttTask, 0);
                if (ok == pdPASS)
                {
                    g_mqttTaskRunning = true;
                    Serial.println("[MQTT] task (re)started");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Start the async MQTT task + its watchdog. Call ONCE from setup(), AFTER
    // initMQTT() has connected the first time (so the task inherits a live
    // socket). From this point loopTask must NOT touch client/espClient — it
    // publishes only via mqttPublish() (queue). The watchdog recreates the task
    // if a TLS handshake/recv ever wedges it, so loopTask (sensors/I2C/OTA) is
    // never blocked by the broker.
    void startMqttTask()
    {
        static bool started = false;
        if (started) return;
        started = true;
        g_pubQ = xQueueCreate(32, sizeof(PubItem));
        g_mqttAliveMs = millis();
        xTaskCreatePinnedToCore(mqttTask, "mqtt", 8192, NULL, 3, &g_mqttTask, 0);
        g_mqttTaskRunning = true;
        xTaskCreatePinnedToCore(mqttWatchdogTask, "mqtt_wdog", 2048, NULL, 1, NULL, 0);
        Serial.println("[MQTT] async task + watchdog started (loopTask off the socket).");
    }

    // True when the broker link is currently up (read by loopTask/diagnostics).
    bool isConnected()
    {
        return client.connected();
    }

    // mqttLoop() is now a no-op for the running system: the mqttTask (started by
    // startMqttTask()) owns all client I/O. Kept as an API shim so callers don't
    // change. It does nothing once the task is running.
    void mqttLoop()
    {
        // Intentionally empty: MQTT I/O runs in mqttTask. (See startMqttTask.)
    }

    // Pump the MQTT client synchronously — ONLY valid during setup(), before
    // startMqttTask() launches the async mqttTask (used in the bounded sensor-
    // init waits). After the task starts, publishing goes through the queue.
    void mqttPump()
    {
        if (!g_mqttTaskRunning)
        {
            if (mqttCanLoop())
            {
                client.loop();
                mqttMarkTraffic();
            }
        }
    }

    // Tear down the MQTT socket — signals the mqttTask to disconnect (thread-
    // safe; the task owns the socket). Used after an OTA window so the stale
    // TLS session is rebuilt by mqttTask rather than risking a blocking read.
    void forceDisconnect()
    {
        g_mqttForceDisc = true;
    }

    // Publishes to the broker. Enqueues to the thread-safe publish queue (the
    // mqttTask drains it). This lets loopTask publish without ever touching the
    // non-thread-safe client. If the link is down when the task drains it, sensor
    // telemetry is buffered to RAM+NVS and replayed on reconnect; other topics
    // (events/warnings) are best-effort and dropped while disconnected.
    void mqttPublish(const char *mq_path, const char *content)
    {
        if (!pubEnqueue(mq_path, content))
        {
            // Queue full: buffer sensor telemetry to NVS so it isn't lost; drop
            // everything else (events are "now"-oriented).
            if (strcmp(mq_path, "cleanair/sensor") == 0)
            {
                qPush(content);
                persistQueue();
                Serial.printf("MQTT: queue full — buffered sample (%d pending)\n", g_qCount);
            }
        }
    }

    void publishEvent(pub_event event, String param)
    {
        // Local buffer: events no longer share the telemetry buffer
        // (app.cpp's serializedString), removing cross-module coupling.
        static char eventBuf[512];
        JsonDocument doc;
        doc["event"] = event;
        doc["param"] = param;
        doc["mac"] = WiFi.macAddress();
        doc["label"] = cfg::label();
        doc["fw"] = PROGRAM_VERSION;
        serializeJson(doc, eventBuf);
        mqtt::mqttPublish("cleanair/events", eventBuf);
    }

    // Publish a Warning to the dedicated aircare/event topic. Uses a string
    // severity ("Warning") — distinct from cleanair/events (numeric event code)
    // — so operators can route/alert on hardware faults separately.
    void publishWarning(String param)
    {
        static char warnBuf[512];
        JsonDocument doc;
        doc["event"] = "Warning";
        doc["param"] = param;
        doc["mac"] = WiFi.macAddress();
        doc["label"] = cfg::label();
        doc["fw"] = PROGRAM_VERSION;
        serializeJson(doc, warnBuf);
        mqtt::mqttPublish("aircare/event", warnBuf);
    }

    // Publish a status snapshot (config/state + health) to cleanair/status.
    // Covers both the periodic health heartbeat and the on-demand REPORT
    // command. Carries MAC/label/fw, the health snapshot (local time, RSSI,
    // uptime, free heap) and the live schedule/exception/relay state. Kept off
    // the sensor measurement so telemetry stays pure measurements.
    void publishStatus(const char *param)
    {
        static char statusBuf[512];
        JsonDocument doc;
        doc["event"] = INFO;
        doc["param"] = param;
        doc["mac"] = WiFi.macAddress();
        doc["label"] = cfg::label();
        doc["fw"] = PROGRAM_VERSION;

        // Local time as YYYY-MM-DD HH:MM:SS (Chile TZ via ntp).
        struct tm dt = ntp::getTM();
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &dt);
        doc["local_time"] = timeStr;

        doc["rssi"] = WiFi.RSSI();                 // dBm (negative; higher = better)
        doc["uptime_ms"] = millis();
        doc["heap"] = ESP.getFreeHeap();           // free heap in bytes (health)
        doc["relay"] = relay::state(1) ? "ON" : "OFF";   // fan channel (relay 1)
        doc["relay2"] = relay::state(2) ? "ON" : "OFF";  // UV channel (relay 2)
        doc["relay_both"] = relay::bothOn() ? "ON" : "OFF";

        // Schedule mode/override plus the next (up to 2) upcoming exceptions.
        doc["sched_mode"] = sched::modeToString(sched::mode);
        doc["sched_ovr"]  = sched::overrideToString(sched::override);
        doc["sched_exc"]  = sched::excCount;
        sched::ExceptionView excView[sched::MAX_EXC_PUBLISH];
        int excViewCount = 0;
        sched::getExceptionList(excView, &excViewCount);
        if (excViewCount >= 1)
        {
            doc["exc1_from"]  = excView[0].fromDay;
            doc["exc1_to"]    = excView[0].toDay;
            doc["exc1_state"] = excView[0].on ? "ON" : "OFF";
        }
        if (excViewCount >= 2)
        {
            doc["exc2_from"]  = excView[1].fromDay;
            doc["exc2_to"]    = excView[1].toDay;
            doc["exc2_state"] = excView[1].on ? "ON" : "OFF";
        }

        serializeJson(doc, statusBuf);
        mqtt::mqttPublish("cleanair/status", statusBuf);
        Serial.printf("MQTT: status -> %s\n", statusBuf);
    }

    // Case-insensitive command compare so "report", "REPORT", "Report" all
    // match. MQTT payloads are often typed in lowercase by clients/tools.
    bool cmdEquals(const char *a, const char *b)
    {
        if (a == nullptr || b == nullptr) return false;
        while (*a && *b)
        {
            if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
                return false;
            a++;
            b++;
        }
        return *a == *b; // both hit NUL at the same time
    }

    void callback(char *topic, byte *payload, unsigned int length)
    {
        Serial.print("Message arrived [");
        Serial.print(topic);
        Serial.print("] ");

        char strpayload[length + 1];
        memcpy(strpayload, payload, length);
        strpayload[length] = 0;
        Serial.println(strpayload);

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, strpayload);
        if (err)
        {
            Serial.printf("MQTT: callback JSON parse error: %s\n", err.c_str());
            return;
        }

        const char *cmd = doc["cmd"] | "";
        if (cmdEquals(cmd, "RELAY"))
        {
            const char *value = doc["value"] | "";
            if (cmdEquals(value, "ON"))
            {
                bool on = true;
                events::emit(Evt::RelayOverride, &on);
                publishEvent(INFO, "RELAY|OVERRIDE_ON|Manual override ON via MQTT");
            }
            else if (cmdEquals(value, "OFF"))
            {
                bool on = false;
                events::emit(Evt::RelayOverride, &on);
                publishEvent(INFO, "RELAY|OVERRIDE_OFF|Manual override OFF via MQTT");
            }
            else if (cmdEquals(value, "AUTO"))
            {
                events::emit(Evt::RelayAuto);
                publishEvent(INFO, "RELAY|AUTO|Override cleared, back to schedule");
            }
            else
            {
                Serial.printf("MQTT: unknown RELAY value '%s'\n", value);
            }
        }
        else if (cmdEquals(cmd, "EXCEPTION"))
        {
            const char *from = doc["from"] | "";
            const char *to = doc["to"] | "";
            const char *st = doc["state"] | "off";
            uint32_t fd = 0, td = 0;
            if (sched::parseDate(from, fd) && sched::parseDate(to, td))
            {
                ExceptionReq req{fd, td, (cmdEquals(st, "on"))};
                events::emit(Evt::ExceptionSet, &req);
                publishEvent(INFO, String("EXCEPTION|SET|") + st + " " + from + ".." + to);
            }
            else
            {
                Serial.printf("MQTT: bad EXCEPTION dates '%s'..'%s'\n", from, to);
            }
        }
        else if (cmdEquals(cmd, "EXCEPTION_CLEAR"))
        {
            events::emit(Evt::ExceptionClear);
            publishEvent(INFO, "EXCEPTION|CLEAR|All exceptions cleared via MQTT");
        }
        else if (cmdEquals(cmd, "REBOOT"))
        {
            // Remote reboot so a flashed-but-not-applied image (or any stuck
            // state) can be recovered without physical access. Ack first, then
            // restart. mqttPump() ensures the PUBLISH leaves before reset.
            publishEvent(INFO, "MCU|RESTART|Restart requested via MQTT");
            mqttPump();
            leds::blinkLed(ledPinG, 3, false);
            delay(1000);
            ESP.restart();
        }
        else if (cmdEquals(cmd, "UPDATE") || cmdEquals(cmd, "UPDATE_NOW"))
        {
            // Force an immediate OTA check/install. checkUpdate() reboots on
            // success, so a pending remote update can be applied on demand.
            publishEvent(INFO, "UPDT|FORCED|Update check requested via MQTT");
            mqttPump();
            ota::checkUpdate();
        }
        else if (cmdEquals(cmd, "REPORT"))
        {
            // Status snapshot (on-demand). Works on both the broadcast and the
            // per-device MAC topic, so a broadcast "report" makes every unit
            // answer with its own status on cleanair/status.
            publishStatus("REPORT|STATUS|Device status report");
        }
        else
        {
            Serial.printf("MQTT: unknown cmd '%s'\n", cmd);
        }
    }
}