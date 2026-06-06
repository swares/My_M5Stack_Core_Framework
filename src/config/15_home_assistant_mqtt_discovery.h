#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Home Assistant MQTT Discovery ─────────────────────────────
//  When MQTT_HA_DISCOVERY is true, the framework publishes one
//  retained config message per reading to
//      <MQTT_HA_PREFIX>/sensor/<unique_id>/config
//  on every successful broker connect.  HA auto-creates the
//  entities and groups them under one "device" card identified
//  by the ESP32's MAC suffix.  Entity availability is tied to
//  the same <base>/status LWT topic — when the device drops off
//  the broker, HA marks every entity unavailable automatically.
//
//  MQTT_HA_PREFIX must match HA's "discovery_prefix" (default
//  "homeassistant" — only change if you've customised the HA
//  MQTT integration).  MQTT_DEVICE_NAME shows up as the device
//  card title in HA.
#define MQTT_HA_DISCOVERY false
[[maybe_unused]] constexpr char MQTT_HA_PREFIX[] = "homeassistant";
[[maybe_unused]] constexpr char MQTT_DEVICE_NAME[] = "M5Stack I2C Framework";
[[maybe_unused]] constexpr char MQTT_DEVICE_MODEL[] =
    "M5Stack Core (I2C Framework)";

// MQTT_DEBUG = true prints one line per successful publish (topic +
// payload size) plus disconnect/reconnect edges.  Default true while
// you're getting MQTT set up; flip to false once it's working to keep
// the serial log clean.  The /api/mqtt status endpoint stays available
// regardless of this flag.
#define MQTT_DEBUG false
