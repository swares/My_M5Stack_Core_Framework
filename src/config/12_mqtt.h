#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── MQTT ──────────────────────────────────────────────────────
//  Optional publish-only MQTT output.  Each active plugin gets one
//  retained JSON message published to "<MQTT_BASE_TOPIC>/<slug>"
//  every MQTT_PUBLISH_MS milliseconds.  Device liveness is tracked
//  via LWT on "<MQTT_BASE_TOPIC>/status" ("online" on connect,
//  "offline" if the broker loses us).  No inbound subscriptions —
//  use the WebAPI's /api/rescan if you need to re-bind.
//
//  Requires the "PubSubClient" library by Nick O'Leary, installable
//  from the Arduino Library Manager.  Build will fail with an
//  #include error if OUT_MQTT is true and the library is missing.
//
//  To enable:
//    1. Install PubSubClient via Library Manager
//    2. Set MQTT_HOST to your broker's IP or hostname
//    3. (Optional) set MQTT_USER / MQTT_PASS if the broker is authed
//
//  Leave MQTT_HOST empty ("") to keep MQTT compiled in but inert —
//  the framework logs "[MQTT] disabled (MQTT_HOST empty)" at boot
//  and never attempts to connect.  Useful when you want the same
//  binary on devices that should and shouldn't talk to a broker.
//
