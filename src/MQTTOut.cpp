// ============================================================
//  MQTTOut.cpp
// ============================================================
#include "Config.h"        // OUT_MQTT — must precede the #if
#if OUT_MQTT
#include "MQTTOut.h"
#include "Framework.h"
#include "Settings.h"      // runtime MQTT user/pass (approach B)
#include <ArduinoJson.h>
#include <cstdio>          // snprintf

// ── begin ─────────────────────────────────────────────────────
//  Configure the broker target.  Connection is attempted here but
//  failure is not fatal — update() will keep retrying every 5 s.
//  When MQTT_HOST is empty we disable ourselves so the rest of the
//  framework runs untouched.  This matches the WebAuth pattern of
//  "compiled-in but inert when not configured".
void MQTTOut::begin(Framework* fw) {
  _fw = fw;
  if (!enabled) {
    Serial.println(F("[MQTT] disabled (OUT_MQTT=false)"));
    return;
  }
  // Effective broker host/port: NVS override (Settings page / portal)
  // first, else the compiled Secrets/Config values.  Kept in _host so
  // the pointer PubSubClient::setServer() stores stays valid.
  _host = Settings::mqttHost();
  if (_host.isEmpty()) {
    Serial.println(F("[MQTT] disabled (no broker host set)"));
    enabled = false;
    return;
  }
  uint16_t mqttPort = Settings::mqttPort();

#if MQTT_TLS
  // ── MQTTS / TLS transport ───────────────────────────────────
  //  Configure the secure client BEFORE the first connect.
  //  Broker verification: pin the broker to a known CA, or skip
  //  the check entirely (still encrypted, but no identity proof).
  //  Client identity: an X.509 client certificate set here, or a
  //  username/password sent inside the tunnel by _tryConnect().
  #if MQTT_TLS_INSECURE
  _net.setInsecure();   // encrypt only — broker identity NOT verified
  Serial.println(F("[MQTT] TLS: broker certificate NOT verified (insecure)"));
  #else
  _net.setCACert(MQTT_CA_CERT);
  Serial.println(F("[MQTT] TLS: broker verified against MQTT_CA_CERT"));
  #endif
  #if MQTT_TLS_MUTUAL
  _net.setCertificate(MQTT_CLIENT_CERT);
  _net.setPrivateKey(MQTT_CLIENT_KEY);
  Serial.println(F("[MQTT] transport: MQTTS — mutual TLS, "
                   "X.509 client-cert auth"));
  #else
  Serial.println(F("[MQTT] transport: MQTTS — TLS, username/password auth"));
  #endif
#else
  Serial.println(F("[MQTT] transport: plain TCP (unencrypted)"));
#endif

  _client.setServer(_host.c_str(), mqttPort);
  _client.setKeepAlive(MQTT_KEEPALIVE);
  // Default PubSubClient buffer is 256 bytes.  Sensor publishes
  // fit in ~300 bytes, but HA discovery config messages with the
  // device block can run ~700+ bytes per entity.  1024 is safe
  // for both.  Bump higher only if you add many extra discovery
  // fields (e.g. very long entity names or attributes templates).
  _client.setBufferSize(1024);

  Serial.printf("[MQTT] target %s:%u base='%s' clientId='%s'\n",
                _host.c_str(), mqttPort, MQTT_BASE_TOPIC, MQTT_CLIENT_ID);
  _tryConnect();
}

// ── update ────────────────────────────────────────────────────
//  Non-blocking pump.  Called every loop tick from Framework.
//  Responsibilities:
//    1. Skip everything if WiFi is down (PubSubClient would block).
//    2. Edge-detect disconnect transitions so we log them once,
//       not every loop tick.
//    3. Reconnect with 5 s back-off if the broker dropped us.
//    4. Service the broker keepalive via _client.loop().
//    5. Publish all active plugins every MQTT_PUBLISH_MS.
void MQTTOut::update() {
  if (!enabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  uint32_t now = millis();
  bool connected = _client.connected();

  // Edge-detect connection-lost so we get one log line per
  // disconnect, not one per loop iteration while we're down.
  if (_wasConnected && !connected) {
    _stats.lastDisconnectMs = now;
    _stats.lastState        = _client.state();
    Serial.printf("[MQTT] connection lost (state=%d %s)\n",
                  _stats.lastState, stateText(_stats.lastState));
  }
  _wasConnected = connected;

  if (!connected) {
    if (now - _lastReconnect < 5000) return;
    _lastReconnect = now;
    _tryConnect();
    return;
  }

  _client.loop();

  if (now - _lastPublish < MQTT_PUBLISH_MS) return;
  _lastPublish = now;
  _publishAll();
}

// ── _tryConnect ───────────────────────────────────────────────
//  Single PubSubClient::connect() call with optional auth and
//  LWT.  The will message is "offline" retained on <base>/status;
//  on successful connect we immediately publish "online" retained
//  to the same topic, so subscribers always see the latest state.
void MQTTOut::_tryConnect() {
  _stats.connectAttempts++;
  Serial.printf("[MQTT] connecting to %s:%u ...", _host.c_str(), Settings::mqttPort());

  String willTopic = String(MQTT_BASE_TOPIC) + "/status";
  // Effective broker credentials: NVS override (Settings tab / portal)
  // first, else the compiled Secrets.h values.
  String mqttUser = Settings::mqttUser();
  String mqttPass = Settings::mqttPass();
  bool ok;
  if (mqttUser.length() > 0) {
    ok = _client.connect(MQTT_CLIENT_ID,
                         mqttUser.c_str(), mqttPass.c_str(),
                         willTopic.c_str(), 0, true, "offline");
  } else {
    ok = _client.connect(MQTT_CLIENT_ID,
                         nullptr, nullptr,
                         willTopic.c_str(), 0, true, "offline");
  }

  _stats.lastState = _client.state();

  if (ok) {
    Serial.println(F(" ok"));
    _client.publish(willTopic.c_str(), "online", true);
    _everConnected           = true;
    _wasConnected            = true;
    _stats.connectSuccesses++;
    _stats.lastConnectMs     = millis();
    // Force a fresh publish on the first tick after reconnect so
    // subscribers don't have to wait MQTT_PUBLISH_MS for the first
    // batch of data.
    _lastPublish = 0;
    // HA discovery works over plain MQTT and over MQTTS with
    // username/password auth (HiveMQ Cloud, Mosquitto+TLS, the HA
    // broker, etc).  A mutual-TLS broker like AWS IoT Core rejects
    // the "homeassistant/#" topic tree and would drop the
    // connection, so skip discovery only in that mode.
    if (MQTT_HA_DISCOVERY) {
#if MQTT_TLS && MQTT_TLS_MUTUAL
      Serial.println(F("[MQTT] HA discovery skipped — mutual-TLS "
                       "broker rejects homeassistant/#"));
#else
      _publishDiscovery();
#endif
    }
  } else {
    // PubSubClient state codes — see stateText() for human names.
    Serial.printf(" failed (state=%d %s)\n",
                  _stats.lastState, stateText(_stats.lastState));
#if MQTT_TLS
    // In MQTTS mode a -2 ("connect-failed") usually means the TLS
    // handshake itself failed — a broker cert that doesn't chain to
    // MQTT_CA_CERT, an expired cert, a clock that's not NTP-synced,
    // or (mutual TLS) a client cert the broker rejected.
    // WiFiClientSecure keeps the underlying mbedTLS error; surface it.
    char errBuf[128];
    int  errNo = _net.lastError(errBuf, sizeof(errBuf));
    if (errNo != 0)
      Serial.printf("[MQTT] TLS error %d: %s\n", errNo, errBuf);
#endif
  }
}

// ── stateText ─────────────────────────────────────────────────
//  PubSubClient::state() codes from PubSubClient.h, translated to
//  short human strings.  Used in log lines and the /api/mqtt
//  status endpoint so callers don't have to memorise -4..5.
const char* MQTTOut::stateText(int s) {
  switch (s) {
    case -4: return "connection-timeout";
    case -3: return "connection-lost";
    // can't reach broker (wrong IP/port/firewall); in TLS mode
    // also a failed TLS handshake
    case -2: return "connect-failed";
    case -1: return "disconnected";
    case  0: return "ok";
    case  1: return "bad-protocol";
    case  2: return "bad-client-id";
    case  3: return "unavailable";
    case  4: return "bad-credentials";      // wrong MQTT_USER / MQTT_PASS
    case  5: return "unauthorized";
    default: return "unknown";
  }
}

// ── _publishAll ───────────────────────────────────────────────
void MQTTOut::_publishAll() {
  for (auto* p : _fw->plugins()) {
    if (!p->active) continue;
    _publishOne(p);
  }
}

// ── _publishOne ───────────────────────────────────────────────
//  Build "<base>/<slug>" topic and serialize a JSON envelope
//  populated from the plugin's getReadings().  We deliberately
//  use getReadings() rather than toJson() so the keys in the
//  payload match the HA discovery value_template's keys exactly
//  — toJson() sometimes uses longer names ("humidity" vs "humid",
//  "pressure" vs "press") which would break the template.
void MQTTOut::_publishOne(IDevice* p) {
  JsonDocument doc;
  doc["name"]   = p->name();
  doc["slug"]   = p->slug();
  char addrBuf[6];
  snprintf(addrBuf, sizeof(addrBuf), "0x%02X", p->addr);
  doc["addr"]   = addrBuf;
  char busBuf[32];
  doc["bus"]    = _busLabel(p, busBuf, sizeof(busBuf));
  doc["uptime"] = static_cast<uint32_t>(millis() / 1000);

  JsonObject values = doc["values"].to<JsonObject>();
  SensorVal vals[16];
  uint8_t n = 0;
  p->getReadings(vals, n);
  for (uint8_t i = 0; i < n; i++) {
    values[vals[i].key] = vals[i].value;
  }

  String topic = String(MQTT_BASE_TOPIC) + "/" + p->slug();

  // PubSubClient wants a C string + length.  Use measureJson to
  // size the buffer exactly — avoids a heap String for big docs.
  size_t len = measureJson(doc);
  if (len + 1 > 512) {
    Serial.printf("[MQTT] payload too big for buffer (%u > 512): %s\n",
                  (unsigned)len, p->slug());
    return;
  }
  char payload[512];
  serializeJson(doc, payload, sizeof(payload));

  if (!_client.publish(topic.c_str(), payload, MQTT_RETAIN)) {
    _stats.publishFailures++;
    _stats.lastState = _client.state();
    Serial.printf("[MQTT] publish failed for %s (state=%d %s)\n",
                  topic.c_str(), _stats.lastState,
                  stateText(_stats.lastState));
    return;
  }

  _stats.publishCount++;
  _stats.lastPublishMs = millis();
  if (MQTT_DEBUG) {
    Serial.printf("[MQTT] -> %s (%u bytes)\n", topic.c_str(), (unsigned)len);
  }
}

// ── publishNow ────────────────────────────────────────────────
//  Immediate publish of all active plugins, bypassing the
//  MQTT_PUBLISH_MS throttle.  Used by /api/mqtt/publish for
//  manual testing without waiting for the next cycle.
bool MQTTOut::publishNow() {
  if (!enabled) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!_client.connected()) return false;
  _lastPublish = millis();
  _publishAll();
  return true;
}

// ── _busLabel ─────────────────────────────────────────────────
//  Same labelling convention as SerialOut/WebAPI:
//    "internal"          — root bus (CoreS3/Core2 separate buses)
//    "external"          — root bus on Port-A (separate bus boards)
//    "shared"            — shared bus on Core1
//    "hub 0xNN chK"      — behind a PCA9548A on the external bus
//
//  Uses the framework's board info to pick the right root label.
const char* MQTTOut::_busLabel(IDevice* p, char* buf, size_t n) {
  if (p->muxAddr != 0) {
    snprintf(buf, n, "hub 0x%02X ch%u", p->muxAddr, p->muxChannel);
    return buf;
  }
  const BoardInfo& bi = _fw->board();
  if (bi.sharedBus()) return "shared";
  if (p->bus == bi.intBus) return "internal";
  if (p->bus == bi.extBus) return "external";
  return "?";
}

// ── _macSuffix ────────────────────────────────────────────────
//  Cache the last 3 MAC octets as 6 lowercase hex chars on first
//  use.  This makes every device on the broker unique without
//  forcing the user to set MQTT_CLIENT_ID per-board.
const char* MQTTOut::_macSuffix() {
  if (_macSuffixBuf[0] == 0) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(_macSuffixBuf, sizeof(_macSuffixBuf),
             "%02x%02x%02x", mac[3], mac[4], mac[5]);
  }
  return _macSuffixBuf;
}

// ── _deviceClassForUnit ───────────────────────────────────────
//  Map a unit string to a Home Assistant device_class.  Returns
//  nullptr when no good match exists, in which case the entity
//  is still created but without an icon/conversion preset.
//
//  "%" is intentionally NOT mapped — humidity, battery %, and
//  SpO2 all share that unit and HA's "humidity" class would be
//  wrong for the other two.  Set device_class manually in HA if
//  needed (Settings → Devices → Entity → Customize).
const char* MQTTOut::_deviceClassForUnit(const char* unit) {
  if (!unit || !*unit) return nullptr;
  if (strcmp(unit, "°C")  == 0) return "temperature";
  if (strcmp(unit, "°F")  == 0) return "temperature";
  if (strcmp(unit, "hPa") == 0) return "atmospheric_pressure";
  if (strcmp(unit, "Pa")  == 0) return "atmospheric_pressure";
  if (strcmp(unit, "mbar")== 0) return "atmospheric_pressure";
  if (strcmp(unit, "lx")  == 0) return "illuminance";
  if (strcmp(unit, "ppm") == 0) return "volatile_organic_compounds_parts";
  if (strcmp(unit, "ppb") == 0) return "volatile_organic_compounds_parts";
  if (strcmp(unit, "V")   == 0) return "voltage";
  if (strcmp(unit, "mV")  == 0) return "voltage";
  if (strcmp(unit, "mA")  == 0) return "current";
  if (strcmp(unit, "A")   == 0) return "current";
  if (strcmp(unit, "W")   == 0) return "power";
  if (strcmp(unit, "g")   == 0) return "weight";
  if (strcmp(unit, "kg")  == 0) return "weight";
  if (strcmp(unit, "cm")  == 0) return "distance";
  if (strcmp(unit, "m")   == 0) return "distance";
  return nullptr;
}

// ── _publishDiscovery ─────────────────────────────────────────
//  Walk every active plugin's getReadings() output and publish one
//  HA discovery config per reading.  Cheap: HA caches configs, so
//  re-publishing identical messages on every reconnect is a no-op
//  for HA but means the framework recovers automatically when HA's
//  MQTT integration is restarted (which clears its cache).
void MQTTOut::_publishDiscovery() {
  Serial.printf("[MQTT] publishing HA discovery (prefix=%s)\n", MQTT_HA_PREFIX);
  uint16_t count = 0;
  for (auto* p : _fw->plugins()) {
    if (!p->active) continue;
    SensorVal vals[16];
    uint8_t n = 0;
    p->getReadings(vals, n);
    for (uint8_t i = 0; i < n; i++) {
      _publishDiscoveryForReading(p, vals[i].key, vals[i].unit);
      count++;
    }
  }
  Serial.printf("[MQTT] discovery: %u entit%s published\n",
                count, count == 1 ? "y" : "ies");
}

// ── _publishDiscoveryForReading ───────────────────────────────
//  Build and publish a single HA discovery config message.  The
//  unique_id schema is "m5stack_<mac6>_<slug>_<reading>" — stable
//  across reboots and unique per device, so HA never duplicates.
//
//  Key fields:
//    state_topic       = <base>/<slug>            (the JSON payload)
//    value_template    = {{ value_json.values.<reading> }}
//    availability      = <base>/status   ("online" / "offline")
//    device.identifiers = [m5stack_<mac6>]  (groups all entities)
//    expire_after      = 3 × MQTT_PUBLISH_MS / 1000  (in seconds)
//
//  state_class is always "measurement" — every reading we publish
//  is a live point sample, not a cumulative counter.
void MQTTOut::_publishDiscoveryForReading(IDevice* p,
                                          const char* readingName,
                                          const char* unit) {
  const char* mac = _macSuffix();

  // Build identifiers.
  char uniqueId[64];
  snprintf(uniqueId, sizeof(uniqueId), "m5stack_%s_%s_%s",
           mac, p->slug(), readingName);

  char deviceId[32];
  snprintf(deviceId, sizeof(deviceId), "m5stack_%s", mac);

  char stateTopic[64];
  snprintf(stateTopic, sizeof(stateTopic), "%s/%s", MQTT_BASE_TOPIC, p->slug());

  char availTopic[64];
  snprintf(availTopic, sizeof(availTopic), "%s/status", MQTT_BASE_TOPIC);

  char configTopic[128];
  snprintf(configTopic, sizeof(configTopic), "%s/sensor/%s/config",
           MQTT_HA_PREFIX, uniqueId);

  char valTpl[64];
  snprintf(valTpl, sizeof(valTpl), "{{ value_json.values.%s }}", readingName);

  char entityName[64];
  snprintf(entityName, sizeof(entityName), "%s %s", p->name(), readingName);

  // Build config document.
  JsonDocument doc;
  doc["name"]               = entityName;
  doc["unique_id"]          = uniqueId;
  doc["state_topic"]        = stateTopic;
  doc["value_template"]     = valTpl;
  doc["availability_topic"] = availTopic;
  doc["payload_available"]    = "online";
  doc["payload_not_available"]= "offline";
  doc["state_class"]        = "measurement";
  doc["expire_after"]       = (MQTT_PUBLISH_MS * 3) / 1000;
  if (unit && *unit) doc["unit_of_measurement"] = unit;
  const char* dc = _deviceClassForUnit(unit);
  if (dc) doc["device_class"] = dc;

  JsonObject dev = doc["device"].to<JsonObject>();
  JsonArray ids = dev["identifiers"].to<JsonArray>();
  ids.add(deviceId);
  dev["name"]         = MQTT_DEVICE_NAME;
  dev["model"]        = MQTT_DEVICE_MODEL;
  dev["manufacturer"] = "M5Stack";

  size_t len = measureJson(doc);
  if (len + 1 > 1024) {
    Serial.printf("[MQTT] discovery payload too big (%u > 1024) for %s\n",
                  (unsigned)len, uniqueId);
    return;
  }
  char payload[1024];
  serializeJson(doc, payload, sizeof(payload));

  if (!_client.publish(configTopic, payload, true)) {
    Serial.printf("[MQTT] discovery publish failed: %s (state=%d)\n",
                  configTopic, _client.state());
  }
}
#endif  // OUT_MQTT
