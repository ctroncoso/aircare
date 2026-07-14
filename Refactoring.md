# Refactoring: from flat helpers to a layered module structure

This document explains the refactor performed on the `sensors` firmware (ESP32 /
PlatformIO). It is written so a student can follow *why* each change was made,
not just *what* changed.

The work lives on the branch `refactor/subsystem-restructure` and is split into
6 small, independently-buildable commits. Each section below maps to one or two
of those commits.

---

## 0. The starting point (what was wrong)

Before the refactor, `src/` looked like this:

```
src/
  main.cpp            // ~330 lines: setup + loop + button handlers
  globals.h           // a "god header": pins, constants, AND a pile of mutable state
  bmeHelper.h         // header-only: definitions AND state
  sunriseHelper.h
  ntpHelper.h
  mqttHelper.h
  otaHelper.h
  wifiManagerHelper.h
  scheduleHelper.h    // ~600 lines, header-only, owned the relay pins directly
  configHelper.h
  sunrise_i2c.h/.cpp
```

Problems a student should recognize:

1. **Header-only "helpers" with definitions inside.** Every function body lived
   in a `.h` file. That means the code is *compiled into every translation unit
   (`.cpp`) that includes the header*. With only one `.cpp` (main.cpp) that
   happened to work, but it is fragile: the moment a second `.cpp` includes the
   header you get **multiple-definition linker errors**, and the compiler has to
   re-parse the same code everywhere (slow builds).
2. **A shared mutable state blob (`globals.h`).** Pins, timing constants, the
   relay state (`rl1State`/`rl2State`), the telemetry JSON buffer (`doc`/
   `serializedString`), the CO₂ state, the WiFi client, the WiFiManager, and
   several `volatile` "handshake" flags all lived here. Any file could read or
   write any of it. That is the classic "spaghetti globals" anti-pattern.
3. **Volatile handshake flags for cross-module signaling.** The scheduler set
   `schedNeedsRearm`, MQTT set `pendingRelayCmd`, the config fetcher set
   `mqttNeedsReconnect`, and `main.cpp`'s `loop()` polled them every tick. This
   couples modules through shared globals and is easy to get wrong (missed
   resets, race conditions on the `volatile` reads).
4. **One module did too much.** `scheduleHelper.h` both *decided* the relay
   state (the scheduling logic) *and* *drove the physical pins* (the relay
   hardware). Mixing "policy" and "actuator" makes the logic hard to test and
   reuse.

The goal was **not** to change behavior. The firmware does exactly the same
thing after the refactor — it just lives in a structure that is easier to read,
build, and extend.

---

## 1. Create `core/` — the shared foundation

The first move was to pull out the things *every* module needs, so they stop
being copy-pasted or crammed into `globals.h`.

### 1a. `core/board.{h,cpp}` — pins, constants, enums

Everything hardware-specific and shared moved here:

- Pin numbers (`rlPin1`, `rlPin2`, `ledPinR/Y/G`, `SDA_PIN`, `SCL_PIN`).
- Timing constants (`measurementDelay`, `updateDelay`, `ONE_MIN`, `PORTAL_TIMEOUT`, `PORTAL_NAME`).
- The `CO2_Condition` enum (Green/Yellow/Red/Unknown) and the `pub_event` enum
  used for MQTT event publishing.
- `PROGRAM_VERSION` (read from `platformio.ini` via the `build_flags` `-D`).

**Why a `.h`/`.cpp` pair and not just a header?** `board.cpp` holds the
*definitions* of any non-inline values (e.g. `const char* PROGRAM_VERSION =
"...";`). The header only *declares* them with `extern`. That way the value is
defined in exactly one place (one translation unit) and linked everywhere else.
This is the single most important rule of C++ modularity: **declare in headers,
define in one `.cpp`.**

### 1b. `core/events.{h,cpp}` — the event bus (see §2)

### 1c. `core/nvsStore.{h,cpp}` — shared NVS helpers

Both the scheduler and the broker config needed to persist data to the ESP32's
Non-Volatile Storage. Previously each had its own `nvs::` code inline. Now there
is one small module with `getInt/getString/putInt/putString/getBytes/putBytes`.
Any module that needs persistence calls these instead of re-implementing NVS
access.

> **Student note — what is NVS?** The ESP32 has a small flash region that
> survives reboots (like a tiny hard drive for settings). "NVS" = Non-Volatile
> Storage. We use it so a device remembers its schedule / broker even if the
> network is down at boot.

### 1d. `core/httpFetch.{h,cpp}` — shared remote-config fetch

Both `config.json` (broker) and `schedule.json` are fetched from GitHub and
matched by the device's MAC address. That "download JSON, find the entry whose
`MAC` equals my MAC (or fall back to `Default`)" logic was duplicated. It is now
one function, `http::fetchJsonByMac(url, mac, doc [, arrayKey])`, returning a
`FetchResult { ok, matched, entry }`. Both config modules call it.

---

## 2. The event bus — replacing the volatile handshake flags

This is the most conceptually important change, so read it slowly.

### The old way (polling shared globals)

Modules set a flag; `loop()` checks it later:

```cpp
// scheduleHelper.h  (old)
volatile bool schedNeedsRearm = false;   // set by NTP when time syncs
// mqttHelper.h      (old)
volatile int  pendingRelayCmd = 0;        // 0=none,1=ON,2=OFF,3=AUTO
// configHelper.h    (old)
volatile bool mqttNeedsReconnect = false; // set when broker host/port changes
```

```cpp
// main.cpp loop()  (old)
if (schedNeedsRearm) { schedNeedsRearm = false; sched::rearm(); }
if (pendingRelayCmd) { ... sched::setOverride(...); pendingRelayCmd = 0; }
else if (mqttNeedsReconnect) { mqttNeedsReconnect = false; mqtt::client.disconnect(); }
```

Problems:
- Every module must know about every other module's flag (tight coupling).
- `loop()` is the only place that "dispatches" — it has to remember to check
  each flag, and a forgotten check = a stuck feature.
- `volatile` is a weak primitive; it prevents some compiler optimizations but
  does **not** give you a real event system.

### The new way (publish / subscribe)

`core/events.h` defines a small set of *events* (an `enum class Evt`) and a
simple dispatcher:

```cpp
enum class Evt { NtpSynced, RelayOverride, RelayAuto, ExceptionSet,
                 ExceptionClear, BrokerChanged };

using Handler = void (*)(Evt, void*);
void subscribe(Handler h);   // register a listener
void emit(Evt e, void* ctx); // notify all listeners
```

A module that *cares* about something **subscribes** a handler function. A
module that *causes* something **emits** an event. Nobody shares a flag.

Example — the scheduler subscribes once at init:

```cpp
void onEvent(Evt evt, void* ctx) {
  switch (evt) {
    case Evt::NtpSynced:    rearm(); break;          // time just synced → recompute
    case Evt::RelayOverride: {                        // MQTT said ON/OFF
      bool on = ctx ? *static_cast<bool*>(ctx) : true;
      override = on ? Override::ON : Override::OFF;
      rearm(); saveToNVS(); break;
    }
    case Evt::RelayAuto:    override = Override::NONE; rearm(); saveToNVS(); break;
    case Evt::ExceptionSet: /* ...add exception from ctx... */ break;
    case Evt::ExceptionClear: excCount = 0; rearm(); saveToNVS(); break;
    case Evt::BrokerChanged: break;                  // scheduler ignores this
  }
}
```

And the *producers* just emit:

```cpp
// ntpHelper.cpp — called by the SNTP library when time is (re)synced:
void timeavailable(struct timeval* t) {
  Serial.println("Got time adjustment from NTP!");
  events::emit(Evt::NtpSynced);          // <-- no global flag, just a signal
}

// mqttHelper.cpp — inside the MQTT command callback:
events::emit(Evt::RelayOverride, &on);   // &on is the "context" payload

// configHelper.cpp — when the resolved broker host/port changed:
events::emit(Evt::BrokerChanged);
```

`main.cpp` now has a tiny handler just for the broker swap, and `loop()` no
longer polls anything:

```cpp
void brokerChangedHandler(Evt evt, void* ctx) {
  if (evt == Evt::BrokerChanged) mqtt::client.disconnect(); // next reconnect binds new host
}
```

> **Student note — why is this better?** Think of it like a school
> announcement system. The old code was everyone writing notes on a shared
> whiteboard and the principal checking the board every morning. The new code is
> a PA system: the cook rings a bell (emits `NtpSynced`), and whoever cares
> (the scheduler) hears it. The cook doesn't need to know who is listening, and
> the listener doesn't need to poll a board. Adding a new listener = one
> `subscribe()` call, no changes to `loop()`.

> **Student note — the `void* ctx` payload.** An event often needs to carry
> data (e.g. "override to ON" vs "override to OFF"). We pass a generic
> `void*` pointer and the handler casts it back to the real type
> (`static_cast<bool*>`). It is the C/C++ way to send "any" data through one
> function signature. The sender and receiver must agree on the type — that is
> the contract.

---

## 3. Split the scheduler: `config/schedule` + `actuators/relay`

`scheduleHelper.h` was the biggest, messiest file. It was split into two
responsibilities:

- **`config/schedule.{h,cpp}`** — the *policy*: given the time, the weekly
  windows, the mode (auto/on/off), and any date exceptions, decide whether the
  relay *should* be ON or OFF. It owns the scheduling state (`mode`, `override`,
  `windows[]`, `exceptions[]`, `nextTransitionTime`, …).
- **`actuators/relay.{h,cpp}`** — the *hardware*: actually toggle the GPIO pins.
  It owns `rl1State`/`rl2State` (now just a single `g_state`) and exposes
  `relay::set(bool on)` / `relay::state()` / `relay::init()`.

The scheduler no longer touches pins. It calls `relay::set(want)`:

```cpp
// schedule.cpp
void applyRelay(bool on) { relay::set(on); }   // delegate to the actuator
```

Telemetry (in `app/`) reads state without knowing about pins:

```cpp
doc["rl1"] = int(relay::state());
```

> **Student note — "separation of concerns".** The *decision* ("it is 9am on a
> weekday, the window says ON") is completely separate from the *action* ("drive
> pin 26 LOW"). If you later swap the relay for a solid-state driver or a smart
> plug, you only touch `actuators/relay.cpp`. The scheduling logic never
> changes. This is the same idea as keeping `core/board.h` (what the pins *are*)
> separate from the code that *uses* them.

The header `config/schedule.h` now only **declares** the types and functions and
`extern`-declares the state; `config/schedule.cpp` **defines** everything. This
is the pattern applied to every module (see §4).

---

## 4. Convert every helper into a `.h` / `.cpp` pair

Each remaining header-only helper (`ledHelper`, `bmeHelper`, `sunriseHelper`,
`wifiManagerHelper`, `ntpHelper`, `otaHelper`, `mqttHelper`, `configHelper`) was
split:

- **`.h`**: `#pragma once`, the `#include`s it needs, and *declarations*
  (`bool initBME();`, `extern float temp;`, etc.). No function bodies.
- **`.cpp`**: `#include "module.h"` and the *definitions* (the bodies).

Two subtleties a student should internalize:

### 4a. State that must be shared across files → `extern` in the header

`mqttHelper` had `PubSubClient client(espClient);` as a definition in the
header. That would be defined in every TU that includes it → linker error. Fix:

```cpp
// mqttHelper.h
extern PubSubClient client;          // "someone, somewhere, defines this"
// mqttHelper.cpp
PubSubClient client(espClient);      // the one true definition
```

Same treatment for `cfg::brokerHost[64]`, `cfg::brokerPort`, the scheduler
state, etc.

### 4b. State that lives in a header but is included by many files → `inline`

Some module state (e.g. `bme::temp`, `sunriseH::co2_fc`, and the slimmed
`globals.h` items) is declared in a header that several `.cpp` files include. In
C++17, marking a variable `inline` tells the linker "these are all the same
variable; merge them into one." Without `inline` you get *multiple definition*
errors at link time.

```cpp
// bmeHelper.h
inline float temp = 0;     // safe to include from many .cpp files
inline Adafruit_BME280 bme;
```

> **Student note — the One Definition Rule (ODR).** A non-`inline` variable or
> function may be *defined* in exactly one translation unit. Headers are textually
> pasted into every `.cpp`, so a plain definition in a header becomes N
> definitions → linker error. `extern` (declare-only) or `inline` (merge) are the
> two standard escapes. Functions defined *inside* a class/namespace in a header
> are implicitly `inline`, which is why small helper functions there were OK —
> but we moved the bodies to `.cpp` anyway for cleaner separation and faster
> builds.

---

## 5. Move the application logic into `app/`

The measurement cycle, update cycle, telemetry printing, and the `doc`/
`serializedString` buffer used to be in `mainHelper.h` (header-only) and the
timers (`previousTimer_1/2`) were in `globals.h`. They moved to:

- **`app/app.{h,cpp}`** — owns `readValues()`, `printValues()`,
  `getCO2_State()`, `state2string()`, `measurementTick()`, `updateTick()`, and
  the telemetry buffer + the two periodic timers (now `static` to the `.cpp`,
  since only `app` uses them).

`main.cpp` now just *orchestrates*: include `app/app.h`, call
`measurementTick()` / `updateTick()` / `sched::tick()` from `loop()`. The
orchestration file shrank from ~330 lines to ~190.

> **Student note — `static` at file scope.** Declaring `static unsigned long
> previousTimer_1` inside `app.cpp` means "this name is visible only in this
> file." That is exactly what we want for a timer that nothing else should touch
> — it prevents accidental cross-file coupling and name clashes.

---

## 6. Delete dead code and slim `globals.h`

- Removed `testRelay()` (never called), `alarm_level` (never used), and the
  `setupNTP()` declaration (it was declared but never defined — only `initNTP()`
  exists).
- `globals.h` was reduced from a large shared blob to **four** items that are
  genuinely cross-cutting and have no single natural owner:
  - `co2_State` — set by `app` (measurement), read by `leds` (to restore the LED
    after a blink).
  - `espClient` — the `WiFiClient` that `PubSubClient` binds to (owned by MQTT).
  - `wm` — the `WiFiManager` (owned by wifi, but `main.cpp` calls
    `wm.startConfigPortal` on button press).
  - `previousTimer_mqtt` — the MQTT reconnect gate, used only in `main.cpp`'s
    `loop()`.

  These are marked `inline` (see §4b) so the header can be included by multiple
  `.cpp` files safely.

> **Note on the original plan:** the plan said "remove `globals.h`". In practice
> a *small* shared-state header is the honest end state — a few pieces of state
> are truly shared and forcing them into a single owner would create awkward
> dependencies (e.g. `leds` would have to depend on `app` just to restore the
> CO₂ LED). The god-header is gone; what remains is intentional and documented.

---

## 7. How to build / verify

```bash
# from the project root
~/.platformio/penv/bin/pio run
```

The refactor was done in 6 commits, each building green before the next began:

1. `core/board` + `core/events`; shared `nvsStore`/`httpFetch`; event bus replaces handshakes.
2. Scheduler split into `config/schedule` + `actuators/relay`; `scheduleHelper.h` deleted.
3. All remaining helpers split into `.h`/`.cpp` pairs.
4. Application ticks + telemetry moved into `app/`.
5. Dead code removed; `globals.h` slimmed.

No runtime behavior changed — only structure, ownership, and the
module-to-module signaling mechanism (flags → event bus).

---

## Quick reference: old → new file map

| Old | New |
|-----|-----|
| `globals.h` (god header) | `core/board.h`, `core/events.h`, `globals.h` (slim), module state |
| `scheduleHelper.h` | `config/schedule.{h,cpp}` + `actuators/relay.{h,cpp}` |
| `bmeHelper.h` (header-only) | `bmeHelper.{h,cpp}` |
| `sunriseHelper.h` (header-only) | `sunriseHelper.{h,cpp}` |
| `ntpHelper.h` (header-only) | `ntpHelper.{h,cpp}` |
| `mqttHelper.h` (header-only) | `mqttHelper.{h,cpp}` |
| `otaHelper.h` (header-only) | `otaHelper.{h,cpp}` |
| `wifiManagerHelper.h` (header-only) | `wifiManagerHelper.{h,cpp}` |
| `configHelper.h` (header-only) | `configHelper.{h,cpp}` |
| `mainHelper.h` (header-only) | `app/app.{h,cpp}` |
| (duplicated NVS/HTTP code) | `core/nvsStore.{h,cpp}`, `core/httpFetch.{h,cpp}` |