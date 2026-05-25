#pragma once
// ============================================================
//  MQTTOut.h  –  Publish-only MQTT output module
//
//  Topic layout (configured in Config.h via MQTT_BASE_TOPIC):
//    <base>/status            "online" on connect, "offline" via LWT
//    <base>/<plugin slug>     retained JSON document, one per active
//                             plugin, refreshed every MQTT_PUBLISH_MS
//
//  JSON payload shape (per plugin):
//    {
//      "name":   "ENV IV Unit",
//      "slug":   "env4",
//      "addr":   "0x44",
//      "bus":    "hub 0x70 ch1",
//      "uptime": 1234,        // device uptime, seconds
//      "values": { ...plugin's toJson() output... }
//    }
//
//  Connection lifecycle:
//    - begin()  : configures broker, attempts first connect
//    - update() : non-blocking; reconnects on its own every 5 s if
//                 the connection drops.  No work done while WiFi is
//                 disconnected.  Calls PubSubClient::loop() each tick
//                 so the broker's keepalive is serviced.
// ============================================================
#include "Config.h"  // for the OUT_MQTT build switch

#if OUT_MQTT
#include <Arduino.h>
#include <WiFi.h>

// Transport client is chosen at compile time by MQTT_TLS (Config.h):
//   MQTT_TLS = true  → WiFiClientSecure  (MQTTS — TLS-encrypted)
//   MQTT_TLS = false → WiFiClient        (plain MQTT/TCP — original path)
// Config.h is included first so MQTT_TLS is defined before this #if.
#if MQTT_TLS
#include <WiFiClientSecure.h>
#else
#include <WiFiClient.h>
#endif
#include <PubSubClient.h>

class Framework;
class IDevice;

class MQTTOut {
 public:
  bool enabled = OUT_MQTT;

  void begin(Framework* fw);
  void update();

  // Runtime status snapshot for /api/mqtt and verbose logging.
  // Updated by the publish/connect machinery; read-only from outside.
  struct Stats {
    uint32_t connectAttempts = 0;
    uint32_t connectSuccesses = 0;
    uint32_t lastConnectMs = 0;
    uint32_t lastDisconnectMs = 0;
    uint32_t publishCount = 0;
    uint32_t publishFailures = 0;
    uint32_t lastPublishMs = 0;
    int lastState = 0;  // PubSubClient::state() codes
  };
  const Stats& stats() const { return _stats; }
  bool connected() { return _client.connected(); }

  // Transport in use, decided at compile time by MQTT_TLS.  Surfaced
  // by /api/mqtt so a dashboard can show "plain" vs "tls".
  static bool tls() { return MQTT_TLS; }
  static const char* transport() { return MQTT_TLS ? "tls" : "plain"; }

  // TLS sub-mode detail for /api/mqtt.  Both are false whenever
  // tls() is false (plain MQTT).
  //   tlsMutual()   — the device authenticates with an X.509 client
  //                   certificate instead of a username/password.
  //   tlsVerified() — the broker's certificate is checked against
  //                   MQTT_CA_CERT (false = setInsecure / encrypt-only).
  static bool tlsMutual() { return MQTT_TLS && MQTT_TLS_MUTUAL; }
  static bool tlsVerified() { return MQTT_TLS && !MQTT_TLS_INSECURE; }

  // Force an immediate publish of every active plugin, ignoring the
  // MQTT_PUBLISH_MS throttle.  Used by /api/mqtt/publish for manual
  // testing.  Returns false if MQTT is disabled or not connected.
  bool publishNow();

  // Decode a PubSubClient::state() value into a short human string.
  // Useful for logging and the /api/mqtt endpoint.  Caller does not
  // own the returned pointer.
  static const char* stateText(int s);

 private:
  Framework* _fw = nullptr;

  // Network client — secure or plain depending on MQTT_TLS.  Both
  // WiFiClient and WiFiClientSecure derive from Arduino's Client, so
  // PubSubClient takes either through the same reference.
#if MQTT_TLS
  WiFiClientSecure _net;
#else
  WiFiClient _net;
#endif
  PubSubClient _client{_net};

  uint32_t _lastPublish = 0;
  uint32_t _lastReconnect = 0;
  bool _everConnected = false;  // controls "[MQTT] connecting..." spam
  bool _wasConnected = false;   // edge-detect for disconnect logs
  Stats _stats;

  void _tryConnect();
  void _publishAll();
  void _publishOne(IDevice* p);
  const char* _busLabel(IDevice* p, char* buf, size_t n);

  // Last three octets of the WiFi MAC in lowercase hex (e.g. "a3b1c5").
  // Used as the device identifier suffix in HA discovery so multiple
  // devices on the same broker don't collide.  Cached on first call.
  const char* _macSuffix();
  char _macSuffixBuf[7] = {0};

  // Publish one HA discovery config message per reading of every
  // active plugin.  Called after each successful broker connect when
  // MQTT_HA_DISCOVERY is true.  Cheap and idempotent — HA caches
  // configs; re-publishing identical configs is a no-op for it.
  void _publishDiscovery();
  void _publishDiscoveryForReading(IDevice* p, const char* readingName,
                                   const char* unit);

  // Map a unit string to a Home Assistant device_class, or nullptr
  // if no good match.  Affects which icon HA shows and which value
  // conversions it applies (e.g. °F ↔ °C for "temperature").
  const char* _deviceClassForUnit(const char* unit);
};

#else                 // !OUT_MQTT
#include <Arduino.h>  // uint32_t for the Stats struct
// ── Stub — MQTT compiled out via Config.h (OUT_MQTT = false) ──
//  Mirrors the real class's full public surface — including the
//  Stats struct and the static transport helpers that /api/mqtt
//  reads — so WebAPI and Framework keep building.  PubSubClient
//  is not linked.
class Framework;
class MQTTOut {
 public:
  bool enabled = false;
  void begin(Framework*) {}
  void update() {}
  struct Stats {
    uint32_t connectAttempts = 0;
    uint32_t connectSuccesses = 0;
    uint32_t lastConnectMs = 0;
    uint32_t lastDisconnectMs = 0;
    uint32_t publishCount = 0;
    uint32_t publishFailures = 0;
    uint32_t lastPublishMs = 0;
    int lastState = 0;
  };
  const Stats& stats() const { return _stats; }
  bool connected() { return false; }
  static bool tls() { return false; }
  static const char* transport() { return "disabled"; }
  static bool tlsMutual() { return false; }
  static bool tlsVerified() { return false; }
  bool publishNow() { return false; }
  static const char* stateText(int) { return "disabled"; }

 private:
  Stats _stats;
};
#endif                // OUT_MQTT
