#pragma once

#include "globals.h"
#include "ntpHelper.h"
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

/**
 * @brief Event-driven relay scheduler.
 *
 * Replaces the old "poll every minute and recompute HHMM comparisons" logic.
 * Each device has a Mode that drives the desired relay state:
 *   - "auto" : follow a weekly window schedule (per-day bitmask + list of ON
 *              windows per active day). Arbitrary number of windows/day.
 *   - "on"   : constant ON.
 *   - "off"  : constant OFF.
 *
 * A Manual Override (injected via MQTT) can force ON/OFF and sticks until an
 * "auto" command clears it. Precedence:
 *   override != NONE ? override : (mode == auto ? evaluateWindows(now) : mode)
 *
 * The engine computes the next transition edge (window start -> ON, window end
 * -> OFF) up to 7 days ahead. loop() calls tick(); when time passes that edge,
 * the relay is switched (edge-triggered, only on change) and the timeline is
 * re-armed. State is *evaluated*, never replayed, so NTP jumps / missed ticks
 * always land in the correct state.
 *
 * Config is fetched from the online JSON (matched by MAC) and persisted to NVS
 * so reboots / network loss restore the last-known config + override.
 *
 * All branches print [SCHED] debug information to the Serial monitor.
 */

namespace sched
{
    // ---------- constants ----------
    const char *scheduleURL = "https://raw.githubusercontent.com/ctroncoso/aircare/main/bins/schedule.json";

    // ---------- types ----------
    enum class Mode
    {
        AUTO,
        ON,
        OFF
    };

    enum class Override
    {
        NONE,
        ON,
        OFF
    };

    struct Window
    {
        int startMin; // minutes from midnight (e.g. 8*60+30)
        int endMin;
    };

    // Date-range exception (holiday / vacation / onsite testing).
    // Dates stored as local days-since-epoch (DST-safe via mktime of midnight).
    struct Exception
    {
        uint32_t fromDay; // inclusive start day
        uint32_t toDay;   // inclusive end day
        bool     on;      // desired relay state during the range
    };

    const int MAX_EXCEPTIONS = 4;

    // ---------- NVS ----------
    const char *nvsNamespace = "aircare";
    const char *nvsKeyMode      = "sched_mode";    // int cast of Mode
    const char *nvsKeyOverride  = "sched_ovr";     // int cast of Override
    const char *nvsKeyDays      = "sched_days";    // 7-char bitmask as byte
    const char *nvsKeyWinCount  = "sched_winc";    // number of windows
    const char *nvsKeyWins      = "sched_wins";    // packed 2 ints per window
    const char *nvsKeyExcCount  = "sched_excc";    // number of exceptions
    const char *nvsKeyExc       = "sched_exc";     // packed exceptions blob

    // ---------- config / runtime state ----------
    Mode      mode      = Mode::AUTO;
    Override  override  = Override::NONE;
    uint8_t   daysMask  = 0b1111100; // Mon..Sun (bit0=Mon). default Mon-Fri
    Window    windows[8];            // up to 8 windows/day (plenty)
    int       winCount  = 0;

    time_t    nextTransitionTime = 0; // epoch sec of next edge
    bool      nextTransitionState = false; // desired state AFTER the edge

    Exception exceptions[MAX_EXCEPTIONS];
    int       excCount = 0;

    uint32_t  lastEvalDay = 0; // local day of last re-evaluation (for midnight rollover)

    Preferences prefs;

    // ---------- helpers ----------
    int hhmmToMin(int hhmm)
    {
        int h = hhmm / 100;
        int m = hhmm % 100;
        return h * 60 + m;
    }

    bool parseHHMM(const char *s, int &outMin)
    {
        // "HH:MM" -> minutes. Returns false on malformed input.
        if (!s || strlen(s) < 5) return false;
        int h = (s[0] - '0') * 10 + (s[1] - '0');
        int m = (s[3] - '0') * 10 + (s[4] - '0');
        if (h < 0 || h > 23 || m < 0 || m > 59) return false;
        outMin = h * 60 + m;
        return true;
    }

    Mode modeFromString(const char *s)
    {
        if (s && strcmp(s, "on") == 0)  return Mode::ON;
        if (s && strcmp(s, "off") == 0) return Mode::OFF;
        return Mode::AUTO;
    }

    const char *modeToString(Mode m)
    {
        switch (m)
        {
        case Mode::ON:  return "on";
        case Mode::OFF: return "off";
        default:        return "auto";
        }
    }

    const char *overrideToString(Override o)
    {
        switch (o)
        {
        case Override::ON:  return "on";
        case Override::OFF: return "off";
        default:            return "none";
        }
    }

    // ---------- persistence ----------
    void saveToNVS()
    {
        if (prefs.begin(nvsNamespace, false))
        {
            prefs.putInt(nvsKeyMode, (int)mode);
            prefs.putInt(nvsKeyOverride, (int)override);
            prefs.putInt(nvsKeyDays, (int)daysMask);
            prefs.putInt(nvsKeyWinCount, winCount);
            // Pack windows into a single int array: [start0,end0,start1,end1,...]
            int buf[16] = {0};
            for (int i = 0; i < winCount && i < 8; i++)
            {
                buf[i * 2]     = windows[i].startMin;
                buf[i * 2 + 1] = windows[i].endMin;
            }
            prefs.putBytes(nvsKeyWins, buf, sizeof(buf));

            // Pack exceptions: [from0,to0,on0, from1,to1,on1, ...] (uint32,uint32,uint8).
            uint8_t ebuf[MAX_EXCEPTIONS * 9] = {0};
            for (int i = 0; i < excCount && i < MAX_EXCEPTIONS; i++)
            {
                uint32_t *p = (uint32_t *)&ebuf[i * 9];
                p[0] = exceptions[i].fromDay;
                p[1] = exceptions[i].toDay;
                ebuf[i * 9 + 8] = exceptions[i].on ? 1 : 0;
            }
            prefs.putBytes(nvsKeyExc, ebuf, sizeof(ebuf));
            prefs.putInt(nvsKeyExcCount, excCount);

            prefs.end();
            Serial.printf("[SCHED] Saved NVS: mode=%s ovr=%s days=%d wins=%d exc=%d\n",
                          modeToString(mode), overrideToString(override), daysMask, winCount, excCount);
        }
        else
        {
            Serial.println("[SCHED] Failed to open NVS (write).");
        }
    }

    void loadFromNVS()
    {
        if (prefs.begin(nvsNamespace, true))
        {
            if (prefs.isKey(nvsKeyMode))
            {
                mode     = (Mode)prefs.getInt(nvsKeyMode, (int)Mode::AUTO);
                override = (Override)prefs.getInt(nvsKeyOverride, (int)Override::NONE);
                daysMask = (uint8_t)prefs.getInt(nvsKeyDays, 0b1111100);
                winCount = prefs.getInt(nvsKeyWinCount, 0);
                int buf[16] = {0};
                prefs.getBytes(nvsKeyWins, buf, sizeof(buf));
                for (int i = 0; i < winCount && i < 8; i++)
                {
                    windows[i].startMin = buf[i * 2];
                    windows[i].endMin   = buf[i * 2 + 1];
                }

                excCount = prefs.getInt(nvsKeyExcCount, 0);
                if (excCount > MAX_EXCEPTIONS) excCount = MAX_EXCEPTIONS;
                uint8_t ebuf[MAX_EXCEPTIONS * 9] = {0};
                prefs.getBytes(nvsKeyExc, ebuf, sizeof(ebuf));
                for (int i = 0; i < excCount; i++)
                {
                    uint32_t *p = (uint32_t *)&ebuf[i * 9];
                    exceptions[i].fromDay = p[0];
                    exceptions[i].toDay   = p[1];
                    exceptions[i].on      = ebuf[i * 9 + 8] != 0;
                }

                prefs.end();
                Serial.printf("[SCHED] Loaded NVS: mode=%s ovr=%s days=%d wins=%d exc=%d\n",
                              modeToString(mode), overrideToString(override), daysMask, winCount, excCount);
            }
            else
            {
                Serial.println("[SCHED] No NVS schedule found, keeping compiled defaults.");
                prefs.end();
            }
        }
        else
        {
            Serial.println("[SCHED] Failed to open NVS (read).");
        }
    }

    // ---------- date / exception helpers ----------
    /// @brief Local days-since-epoch for the current local time.
    uint32_t localDayNow()
    {
        time_t now = ntp::getTime();
        struct tm dt;
        localtime_r(&now, &dt);
        dt.tm_hour = 0; dt.tm_min = 0; dt.tm_sec = 0;
        return (uint32_t)mktime(&dt) / 86400L;
    }

    /// @brief Parse "YYYY-MM-DD" into a local days-since-epoch value.
    bool parseDate(const char *s, uint32_t &outDay)
    {
        if (!s || strlen(s) < 10) return false;
        int y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
        int mo = (s[5]-'0')*10 + (s[6]-'0');
        int d  = (s[8]-'0')*10 + (s[9]-'0');
        if (mo < 1 || mo > 12 || d < 1 || d > 31) return false;
        struct tm t = {0};
        t.tm_year = y - 1900;
        t.tm_mon  = mo - 1;
        t.tm_mday = d;
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        time_t epoch = mktime(&t); // local TZ (Chile, DST-aware)
        if (epoch < 0) return false;
        outDay = (uint32_t)epoch / 86400L;
        return true;
    }

    /// @brief If an exception is active for local day `day`, set outState and return true.
    bool exceptionActive(uint32_t day, bool &outState)
    {
        for (int i = 0; i < excCount; i++)
        {
            if (day >= exceptions[i].fromDay && day <= exceptions[i].toDay)
            {
                outState = exceptions[i].on;
                return true;
            }
        }
        return false;
    }

    // ---------- evaluation ----------
    /// @brief Compute the desired relay state at time `now` for an AUTO device.
    bool evaluateWindows(const struct tm &dt)
    {
        if (winCount == 0) return false;
        // dt.tm_wday: 0=Sun..6=Sat. daysMask bit0=Mon..bit6=Sun.
        int maskIndex = (dt.tm_wday + 6) % 7; // Sun(0)->6, Mon(1)->0 ...
        if (!(daysMask & (1 << maskIndex))) return false;

        int nowMin = dt.tm_hour * 60 + dt.tm_min;
        for (int i = 0; i < winCount; i++)
        {
            if (nowMin >= windows[i].startMin && nowMin < windows[i].endMin)
                return true;
        }
        return false;
    }

    /// @brief Desired relay state right now, applying full precedence:
    ///   1. sticky MQTT manual override (highest)
    ///   2. date-range exception (holiday / vacation / onsite)
    ///   3. Mode (auto/on/off) + weekly windows
    bool desiredState()
    {
        if (override == Override::ON)  return true;
        if (override == Override::OFF) return false;

        bool exState;
        if (exceptionActive(localDayNow(), exState))
            return exState;

        if (mode == Mode::ON)  return true;
        if (mode == Mode::OFF) return false;
        // AUTO
        struct tm dt = ntp::getTM();
        return evaluateWindows(dt);
    }

    /// @brief Scan forward up to 7 days for the next window edge; returns epoch
    /// of the edge and the state that becomes true AFTER crossing it.
    /// An active exception (or manual override) is treated as a constant state
    /// (no scheduled edges) until the day rolls over / config changes.
    void computeNextTransition()
    {
        time_t now = ntp::getTime();

        bool exState;
        if (override != Override::NONE || exceptionActive(localDayNow(), exState) ||
            mode == Mode::ON || mode == Mode::OFF || winCount == 0)
        {
            nextTransitionTime = 0; // constant until day rollover / config change
            return;
        }

        struct tm dt;
        localtime_r(&now, &dt);
        time_t best = 0;
        bool   bestState = false;

        for (int dayOffset = 0; dayOffset < 7; dayOffset++)
        {
            // Skip days covered by an exception.
            time_t dayStart = now + (time_t)dayOffset * 86400L;
            struct tm ds;
            localtime_r(&dayStart, &ds);
            ds.tm_hour = 0; ds.tm_min = 0; ds.tm_sec = 0;
            uint32_t dayNum = (uint32_t)mktime(&ds) / 86400L;
            bool dummy;
            if (exceptionActive(dayNum, dummy)) continue;

            ds.tm_hour = 0; ds.tm_min = 0; ds.tm_sec = 0;
            time_t dayEpoch = mktime(&ds);

            int maskIndex = (ds.tm_wday + 6) % 7;
            if (!(daysMask & (1 << maskIndex))) continue;

            for (int i = 0; i < winCount; i++)
            {
                time_t startE = dayEpoch + windows[i].startMin * 60L;
                if (startE > now && (best == 0 || startE < best))
                { best = startE; bestState = true; }
                time_t endE = dayEpoch + windows[i].endMin * 60L;
                if (endE > now && (best == 0 || endE < best))
                { best = endE; bestState = false; }
            }
        }

        nextTransitionTime = best;
        nextTransitionState = bestState;
        if (best)
        {
            struct tm te; localtime_r(&best, &te);
            Serial.printf("[SCHED] Next transition in %ld s -> %s\n",
                          (long)(best - now), bestState ? "ON" : "OFF");
        }
        else
        {
            Serial.println("[SCHED] No upcoming transition found (7d scan).");
        }
    }

    /// @brief Drive the physical relay only on state change (edge-triggered).
    void applyRelay(bool on)
    {
        if (on && !(rl1State && rl2State))
        {
            digitalWrite(rlPin1, LOW);
            digitalWrite(rlPin2, LOW);
            rl1State = 1; rl2State = 1;
            Serial.println("[SCHED] RELAY -> ON");
        }
        else if (!on && (rl1State || rl2State))
        {
            digitalWrite(rlPin1, HIGH);
            digitalWrite(rlPin2, HIGH);
            rl1State = 0; rl2State = 0;
            Serial.println("[SCHED] RELAY -> OFF");
        }
    }

    /// @brief Recompute current desired state + re-arm the transition timeline.
    void rearm()
    {
        lastEvalDay = localDayNow();
        bool want = desiredState();
        applyRelay(want);
        computeNextTransition();
    }

    /// @brief Serial line printed after "Current State:" in the reading cycle.
    void printNextTransition()
    {
        if (override != Override::NONE)
        {
            Serial.printf("SCHED Next transition: none (override %s)\n",
                          overrideToString(override));
            return;
        }
        bool exState;
        if (exceptionActive(localDayNow(), exState))
        {
            Serial.printf("SCHED Next transition: none (exception %s)\n",
                          exState ? "ON" : "OFF");
            return;
        }
        if (mode == Mode::ON || mode == Mode::OFF || winCount == 0)
        {
            Serial.printf("SCHED Next transition: none (mode %s)\n", modeToString(mode));
            return;
        }
        if (nextTransitionTime != 0)
        {
            time_t now = ntp::getTime();
            struct tm te; localtime_r(&nextTransitionTime, &te);
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &te);
            Serial.printf("SCHED Next transition in %ld s -> %s (%s)\n",
                          (long)(nextTransitionTime - now),
                          nextTransitionState ? "ON" : "OFF", buf);
        }
        else
        {
            Serial.println("SCHED Next transition: none");
        }
    }

    /// @brief Drop exceptions whose range has fully ended (toDay < today).
    /// Called on day rollover and before adding a new one, so expired slots
    /// free themselves and the fixed-size list stays self-cleaning.
    void pruneExpiredExceptions()
    {
        uint32_t today = localDayNow();
        int w = 0;
        for (int i = 0; i < excCount; i++)
        {
            if (exceptions[i].toDay >= today)
            {
                if (w != i) exceptions[w] = exceptions[i];
                w++;
            }
        }
        if (w != excCount)
        {
            excCount = w;
            Serial.printf("[SCHED] Pruned expired exceptions, %d remain\n", excCount);
        }
    }

    /// @brief Evict the oldest exception (lowest fromDay) to free a slot.
    void evictOldestException()
    {
        if (excCount == 0) return;
        int oldest = 0;
        for (int i = 1; i < excCount; i++)
            if (exceptions[i].fromDay < exceptions[oldest].fromDay) oldest = i;
        for (int i = oldest; i < excCount - 1; i++)
            exceptions[i] = exceptions[i + 1];
        excCount--;
        Serial.println("[SCHED] Evicted oldest exception (round-robin).");
    }

    /// @brief Called every loop(). Fires the next transition if it's due, and
    /// consumes the handshake flags set by other modules (NTP re-sync, MQTT
    /// override, MQTT exception, midnight day rollover). Keeps sched:: free of
    /// cross-header coupling.
    void tick()
    {
        // NTP (re)synced — recompute state + timeline.
        if (schedNeedsRearm)
        {
            schedNeedsRearm = false;
            rearm();
        }

        // MQTT override request.
        if (pendingRelayCmd != 0)
        {
            int cmd = pendingRelayCmd;
            pendingRelayCmd = 0;
            if (cmd == 1)      override = Override::ON;
            else if (cmd == 2) override = Override::OFF;
            else if (cmd == 3) override = Override::NONE;
            Serial.printf("[SCHED] Override applied -> %s\n", overrideToString(override));
            rearm();
            saveToNVS();
        }

        // MQTT exception injection / clear.
        if (pendingException)
        {
            pendingException = false;
            if (pendingExceptionClear)
            {
                pendingExceptionClear = false;
                excCount = 0;
                Serial.println("[SCHED] All exceptions cleared (MQTT).");
            }
            else if (pendingExcFrom != 0 && pendingExcTo != 0)
            {
                // Free expired slots first, then round-robin if still full.
                pruneExpiredExceptions();
                if (excCount >= MAX_EXCEPTIONS)
                    evictOldestException();
                exceptions[excCount].fromDay = pendingExcFrom;
                exceptions[excCount].toDay   = pendingExcTo;
                exceptions[excCount].on      = pendingExcOn;
                excCount++;
                Serial.printf("[SCHED] Exception added (MQTT): %s\n",
                              pendingExcOn ? "ON" : "OFF");
            }
            rearm();
            saveToNVS();
        }

        // Local midnight rollover — prune expired exceptions, re-evaluate.
        uint32_t today = localDayNow();
        if (today != lastEvalDay)
        {
            Serial.println("[SCHED] Day rollover detected.");
            pruneExpiredExceptions();
            rearm();
        }

        // Scheduled edge transition.
        if (nextTransitionTime != 0)
        {
            time_t now = ntp::getTime();
            if (now >= nextTransitionTime)
            {
                applyRelay(nextTransitionState);
                computeNextTransition();
            }
        }
    }

    /// @brief Direct (in-process) override injection, e.g. from a future local API.
    void setOverride(Override o)
    {
        override = o;
        Serial.printf("[SCHED] Override set -> %s\n", overrideToString(o));
        rearm();
        saveToNVS();
    }

    /// @brief Add a date-range exception (inclusive days). No-op if list full.
    void addException(uint32_t fromDay, uint32_t toDay, bool on)
    {
        if (excCount >= MAX_EXCEPTIONS) return;
        exceptions[excCount].fromDay = fromDay;
        exceptions[excCount].toDay   = toDay;
        exceptions[excCount].on      = on;
        excCount++;
        rearm();
        saveToNVS();
    }

    /// @brief Remove all exceptions.
    void clearExceptions()
    {
        excCount = 0;
        rearm();
        saveToNVS();
    }

    // ---------- remote config parsing ----------
    void applyEntry(JsonObject entry)
    {
        Mode m = modeFromString(entry["Mode"] | "auto");
        mode = m;

        if (m == Mode::AUTO)
        {
            // New schema: days bitmask + windows list.
            const char *days = entry["Auto"]["days"] | "1111100";
            daysMask = 0;
            for (int i = 0; i < 7 && days[i]; i++)
                if (days[i] == '1') daysMask |= (1 << i);

            winCount = 0;
            JsonArray wins = entry["Auto"]["windows"];
            if (wins.size() > 0)
            {
                for (JsonArray w : wins)
                {
                    if (winCount >= 8) break;
                    int s, e;
                    if (parseHHMM(w[0] | "", s) && parseHHMM(w[1] | "", e))
                    {
                        windows[winCount].startMin = s;
                        windows[winCount].endMin   = e;
                        winCount++;
                    }
                }
            }
            else
            {
                // Legacy 4-field fallback.
                int on  = entry["FilterOn"]   | 830;
                int off = entry["FilterOff"]  | 1830;
                int ls  = entry["LunchStart"] | 1230;
                int le  = entry["LunchEnd"]   | 1430;
                windows[0] = {hhmmToMin(on), hhmmToMin(ls)};
                windows[1] = {hhmmToMin(le), hhmmToMin(off)};
                winCount = 2;
            }
        }
        else
        {
            winCount = 0; // constant mode — no windows needed
        }

        // Date-range exceptions (holiday / vacation / onsite testing).
        excCount = 0;
        JsonArray excs = entry["Exceptions"];
        for (JsonObject x : excs)
        {
            if (excCount >= MAX_EXCEPTIONS) break;
            const char *from = x["from"] | "";
            const char *to   = x["to"]   | "";
            const char *st   = x["state"] | "off";
            uint32_t fd, td;
            if (parseDate(from, fd) && parseDate(to, td))
            {
                exceptions[excCount].fromDay = fd;
                exceptions[excCount].toDay   = td;
                exceptions[excCount].on      = (strcmp(st, "on") == 0);
                excCount++;
            }
        }
    }

    void fetchSchedule()
    {
        loadFromNVS();

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[SCHED] WiFi not connected — using NVS/default values.");
            rearm();
            return;
        }

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient https;
        https.setConnectTimeout(5000);
        https.setTimeout(5000);
        if (!https.begin(client, scheduleURL))
        {
            Serial.println("[SCHED] HTTPS begin failed — using NVS/default values.");
            rearm();
            return;
        }

        int httpCode = https.GET();
        if (httpCode != HTTP_CODE_OK)
        {
            Serial.printf("[SCHED] HTTP GET failed (code %d) — using NVS/default values.\n", httpCode);
            https.end();
            rearm();
            return;
        }

        String payload = https.getString();
        https.end();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err)
        {
            Serial.printf("[SCHED] JSON parse error: %s — using NVS/default values.\n", err.c_str());
            rearm();
            return;
        }

        const char *myMac = WiFi.macAddress().c_str();
        bool matched = false;
        JsonArray schedules = doc["Schedules"];
        for (JsonObject entry : schedules)
        {
            const char *device = entry["Device"] | "";
            if (strcmp(device, myMac) == 0)
            {
                applyEntry(entry);
                matched = true;
                break;
            }
        }

        if (!matched)
            Serial.println("[SCHED] No entry for this MAC — using NVS/default values.");

        saveToNVS();
        rearm();
    }

    void initSchedule()
    {
        Serial.println("--- Initializing schedule (event-driven)");
        fetchSchedule();
        Serial.printf("[SCHED] Active: mode=%s ovr=%s days=%d wins=%d exc=%d\n",
                      modeToString(mode), overrideToString(override), daysMask, winCount, excCount);
        lastEvalDay = localDayNow();
    }
} // namespace sched
