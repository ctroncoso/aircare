// schedule.h — event-driven relay scheduler (declarations).
//
// Each device has a Mode that drives the desired relay state:
//   - "auto" : follow a weekly window schedule (per-day bitmask + list of ON
//              windows per active day). Arbitrary number of windows/day.
//   - "on"   : constant ON.
//   - "off"  : constant OFF.
//
// A Manual Override (injected via MQTT, delivered through the event bus) can
// force ON/OFF and sticks until an "auto" command clears it.
//
// The engine computes the next transition edge (window start -> ON, window end
// -> OFF) up to 7 days ahead. tick() fires the next transition when time passes
// that edge; the relay is switched (edge-triggered, only on change) via the
// actuators/relay module. State is *evaluated*, never replayed.
//
// Config is fetched from the online JSON (matched by MAC) and persisted to NVS
// so reboots / network loss restore the last-known config + override.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>
#include "core/board.h"
#include "core/events.h"
#include "core/nvsStore.h"
#include "core/httpFetch.h"
#include "ntpHelper.h"
#include "actuators/relay.h"

namespace sched
{
    // ---------- types ----------
    enum class Mode { AUTO, ON, OFF };
    enum class Override { NONE, ON, OFF };

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

    // ---------- config / runtime state (defined in schedule.cpp) ----------
    extern const char *scheduleURL;
    extern Mode      mode;
    extern Override  override;
    extern uint8_t   daysMask;
    extern Window    windows[8];
    extern int       winCount;
    extern time_t    nextTransitionTime;
    extern bool      nextTransitionState;
    extern Exception exceptions[MAX_EXCEPTIONS];
    extern int       excCount;
    extern uint32_t  lastEvalDay;

    // ---------- string helpers ----------
    Mode modeFromString(const char *s);
    const char *modeToString(Mode m);
    const char *overrideToString(Override o);

    // ---------- date / exception helpers ----------
    uint32_t localDayNow();
    bool parseDate(const char *s, uint32_t &outDay);
    bool parseHHMM(const char *s, int &outMin);
    bool exceptionActive(uint32_t day, bool &outState);

    // ---------- evaluation ----------
    bool desiredState();
    void computeNextTransition();

    // ---------- relay application ----------
    void applyRelay(bool on);          // thin wrapper -> relay::set
    void rearm();

    // ---------- telemetry ----------
    void printNextTransition();

    // ---------- exceptions maintenance ----------
    void pruneExpiredExceptions();
    void evictOldestException();

    // ---------- event bus handler (subscribed in initSchedule) ----------
    void onEvent(Evt evt, void *ctx);

    // ---------- per-loop tick ----------
    void tick();

    // ---------- direct (local API) injection ----------
    void setOverride(Override o);
    void addException(uint32_t fromDay, uint32_t toDay, bool on);
    void clearExceptions();

    // ---------- remote config ----------
    void applyEntry(JsonObject entry);
    void fetchSchedule();

    // ---------- lifecycle ----------
    void initSchedule();
}