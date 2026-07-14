// events.h — lightweight synchronous event bus.
// Replaces the cross-module `volatile` handshake globals (schedNeedsRearm,
// pendingRelayCmd, mqttNeedsReconnect, ...) that were previously written by
// one module and polled by another, removing hidden coupling and the need
// for modules to include each other just to share a helper.
#pragma once

#include <cstdint>

// Event identifiers emitted by subsystems.
enum class Evt
{
    NtpSynced,     // NTP (re)synced — re-evaluate relay timeline
    RelayOverride, // MQTT requested ON / OFF (ctx: bool* on)
    RelayAuto,     // MQTT requested return to AUTO schedule
    ExceptionSet,  // MQTT injected a date-range exception (ctx: ExceptionReq*)
    ExceptionClear,// MQTT cleared all exceptions
    BrokerChanged, // dynamic broker host/port changed (ctx: none)
};

// Context payload for Evt::ExceptionSet.
struct ExceptionReq
{
    uint32_t fromDay;
    uint32_t toDay;
    bool on;
};

// Handler signature: receives the event and an optional caller context.
using EventHandler = void (*)(Evt evt, void *ctx);

namespace events
{
    // Maximum number of subscribers (fixed-size to avoid heap use on ESP32).
    const int MAX_HANDLERS = 8;

    // Subscribe a handler. Returns true on success, false if the table is full.
    bool subscribe(EventHandler h);

    // Emit an event to every subscribed handler (synchronous).
    void emit(Evt evt, void *ctx = nullptr);

    // Test helper: number of subscribed handlers.
    int handlerCount();
}