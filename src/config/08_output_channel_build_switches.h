#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Output channel build switches ─────────────────────────────
//  Each flag compiles in (true) or completely omits (false) one
//  output channel.  Set one to false and that channel's code AND
//  its libraries are excluded from the build — OUT_WEB false drops
//  the HTTPS server libraries, OUT_MQTT false drops PubSubClient.
//  The channel's class is replaced by an inert, same-interface
//  stub, so the rest of the framework still compiles and links
//  unchanged (it just calls no-ops).
//
//  All default to true (every channel compiled in), so a stock
//  build is byte-identical to before these became compile-time.
//
//  ⚠ The USB-serial BOOT diagnostics are always available —
//  OUT_SERIAL only governs the periodic per-plugin readings dump,
//  not the boot log (the stub still brings the UART up).
#define OUT_WEB true
#define OUT_SERIAL true
#define OUT_DISPLAY true
#define OUT_MQTT true
#define OUT_SD_LOG true
// Threshold / event alarm engine (AlertManager).  Reads what plugins
// already publish, runs rules through a state machine, and (from
// milestone 2) routes events to channel sinks.
#define OUT_ALERTS true
