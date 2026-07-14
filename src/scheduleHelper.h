#pragma once

#include "globals.h"
#include "ntpHelper.h"
#include "core/nvsStore.h"
#include "core/httpFetch.h"
#include "core/events.h"
#include <WiFi.h>
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
    // Dates stored as a packed YYYYMMDD integer (DST-safe: no mktime/86400 math).
    struct Exception
    {
        uint32_t fromDay; // inclusive start day (YYYYMMDD)
        uint32_t toDay;   // inclusive end day (YYYYMMDD)
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
        nvs::putInt(nvsNamespace, nvsKeyMode, (int)mode);
        nvs::putInt(nvsNamespace, nvsKeyOverride, (int)override);
        nvs::putInt(nvsNamespace, nvsKeyDays, (int)daysMask);
        nvs::putInt(nvsNamespace, nvsKeyWinCount, winCount);
        // Pack windows into a single int array: [start0,end0,start1,end1,...]
        int buf[16] = {0};
        for (int i = 0; i < winCount && i < 8; i++)
        {
            buf[i * 2]     = windows[i].startMin;
            buf[i * 2 + 1] = windows[i].endMin;
        }
        nvs::putBytes(nvsNamespace, nvsKeyWins, buf, sizeof(buf));

        // Pack exceptions: [from0,to0,on0, from1,to1,on1, ...] (uint32,uint32,uint8).
        uint8_t ebuf[MAX_EXCEPTIONS * 9] = {0};
        for (int i = 0; i < excCount && i < MAX_EXCEPTIONS; i++)
        {
            uint32_t *p = (uint32_t *)&ebuf[i * 9];
            p[0] = exceptions[i].fromDay;
            p[1] = exceptions[i].toDay;
            ebuf[i * 9 + 8] = exceptions[i].on ? 1 : 0;
        }
        nvs::putBytes(nvsNamespace, nvsKeyExc, ebuf, sizeof(ebuf));
        nvs::putInt(nvsNamespace, nvsKeyExcCount, excCount);

        Serial.printf("[SCHED] Saved NVS: mode=%s ovr=%s days=%d wins=%d exc=%d\n",
                      modeToString(mode), overrideToString(override), daysMask, winCount, excCount);
    }

    void loadFromNVS()
    {
        mode     = (Mode)nvs::getInt(nvsNamespace, nvsKeyMode, (int)Mode::AUTO);
        override = (Override)nvs::getInt(nvsNamespace, nvsKeyOverride, (int)Override::NONE);
        daysMask = (uint8_t)nvs::getInt(nvsNamespace, nvsKeyDays, 0b1111100);
        winCount = nvs::getInt(nvsNamespace, nvsKeyWinCount, 0);
        int buf[16] = {0};
        nvs::getBytes(nvsNamespace, nvsKeyWins, buf, sizeof(buf));
        for (int i = 0; i < winCount && i < 8; i++)
        {
            windows[i].startMin = buf[i * 2];
            windows[i].endMin   = buf[i * 2 + 1];
        }

        excCount = nvs::getInt(nvsNamespace, nvsKeyExcCount, 0);
        if (excCount > MAX_EXCEPTIONS) excCount = MAX_EXCEPTIONS;
        uint8_t ebuf[MAX_EXCEPTIONS * 9] = {0};
        nvs::getBytes(nvsNamespace, nvsKeyExc, ebuf, sizeof(ebuf));
        for (int i = 0; i < excCount; i++)
        {
            uint32_t *p = (uint32_t *)&ebuf[i * 9];
            exceptions[i].fromDay = p[0];
            exceptions[i].toDay   = p[1];
            exceptions[i].on      = ebuf[i * 9 + 8] != 0;
        }

        Serial.printf("[SCHED] Loaded NVS: mode=%s ovr=%s days=%d wins=%d exc=%d\n",
                      modeToString(mode), overrideToString(override), daysMask, winCount, excCount);
    }

    // ---------- date / exception helpers ----------
    /// @brief True once NTP has actually synced (authoritative SNTP status),
    /// so we never compute the timeline against pre-sync / epoch-0 time.
    bool timeValid()
    {
        return ntp::timeSynced();
    }

    /// @brief Local days-since-epoch for the current local time.
    uint32_t localDayNow()
    {
        time_t now = ntp::getTime();
        struct tm dt;
        localtime_r(&now, &dt);
        return (uint32_t)(dt.tm_year + 1900) * 10000
             + (uint32_t)(dt.tm_mon + 1) * 100
             + (uint32_t)dt.tm_mday;
    }

    /// @brief Parse "YYYY-MM-DD" into a local days-since-epoch value.
    bool parseDate(const char *s, uint32_t &outDay)
    {
        if (!s || strlen(s) < 10) return false;
        int y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
        int mo = (s[5]-'0')*10 + (s[6]-'0');
        int d  = (s[8]-'0')*10 + (s[9]-'0');
        if (mo < 1 || mo > 12 || d < 1 || d > 31) return false;
        // Pack as YYYYMMDD so comparisons are chronological and DST-safe
        // (no mktime/86400 math, which breaks on DST 23h/25h days).
        outDay = (uint32_t)y * 10000 + (uint32_t)mo * 100 + (uint32_t)d;
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
            uint32_t dayNum = (uint32_t)(ds.tm_year + 1900) * 10000
                            + (uint32_t)(ds.tm_mon + 1) * 100
                            + (uint32_t)ds.tm_mday;
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
    /// No-op until NTP time is valid, so we never compute/apply against epoch 0.
    void rearm()
    {
        if (!timeValid()) return;
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

    /// @brief Event-bus handler. Replaces the old volatile handshake globals
    /// (schedNeedsRearm / pendingRelayCmd / pendingException*) that were
    /// polled in tick(). Subscribed in initSchedule().
    void onEvent(Evt evt, void *ctx)
    {
        switch (evt)
        {
        case Evt::NtpSynced:
            // Local time (re)synced — recompute state + re-arm timeline.
            rearm();
            break;

        case Evt::RelayOverride:
        {
            bool on = ctx ? *static_cast<bool *>(ctx) : true;
            override = on ? Override::ON : Override::OFF;
            Serial.printf("[SCHED] Override applied -> %s\n", overrideToString(override));
            rearm();
            saveToNVS();
            break;
        }

        case Evt::RelayAuto:
            override = Override::NONE;
            Serial.printf("[SCHED] Override applied -> %s\n", overrideToString(override));
            rearm();
            saveToNVS();
            break;

        case Evt::ExceptionSet:
        {
            ExceptionReq *r = static_cast<ExceptionReq *>(ctx);
            if (r && r->fromDay != 0 && r->toDay != 0)
            {
                // Free expired slots first, then round-robin if still full.
                pruneExpiredExceptions();
                if (excCount >= MAX_EXCEPTIONS)
                    evictOldestException();
                exceptions[excCount].fromDay = r->fromDay;
                exceptions[excCount].toDay   = r->toDay;
                exceptions[excCount].on      = r->on;
                excCount++;
                Serial.printf("[SCHED] Exception added (event): %s\n",
                              r->on ? "ON" : "OFF");
            }
            rearm();
            saveToNVS();
            break;
        }

        case Evt::ExceptionClear:
            excCount = 0;
            Serial.println("[SCHED] All exceptions cleared (event).");
            rearm();
            saveToNVS();
            break;

        case Evt::BrokerChanged:
            // Not relevant to the scheduler; ignore.
            break;
        }
    }

    /// @brief Called every loop(). Fires the next transition if it's due and
    /// handles the local midnight rollover. All cross-module triggers (NTP
    /// re-sync, MQTT override/exception) now arrive via the event bus.
    void tick()
    {
        // First valid time after boot: the initial rearm() was skipped (no time
        // yet), so arm the timeline as soon as NTP becomes valid.
        if (nextTransitionTime == 0 && timeValid())
        {
            rearm();
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

        JsonDocument doc;
        // Schedule JSON uses a "Schedules" array (no "Default" fallback).
        // The helper walks that array and returns the entry matching our MAC.
        http::FetchResult res = http::fetchJsonByMac(scheduleURL, WiFi.macAddress().c_str(),
                                                     doc, "Schedules", nullptr);
        if (res.matched && !res.entry.isNull())
        {
            applyEntry(res.entry);
        }
        else if (res.ok)
        {
            Serial.println("[SCHED] No entry for this MAC — using NVS/default values.");
        }
        else
        {
            Serial.println("[SCHED] Fetch failed — using NVS/default values.");
        }

        saveToNVS();
        rearm();
    }

    void initSchedule()
    {
        Serial.println("--- Initializing schedule (event-driven)");
        events::subscribe(onEvent); // react to NTP / MQTT events via the bus
        fetchSchedule();
        Serial.printf("[SCHED] Active: mode=%s ovr=%s days=%d wins=%d exc=%d\n",
                      modeToString(mode), overrideToString(override), daysMask, winCount, excCount);
        lastEvalDay = localDayNow();
    }
} // namespace sched
