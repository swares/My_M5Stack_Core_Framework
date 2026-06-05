#pragma once
// ============================================================
//  AlertManager.h  –  Threshold / event alarm engine  (milestone 1)
//
//  One more toggleable output module, the same shape as MQTTOut /
//  SDLogger / DisplayManager: a member of Framework that runs in the
//  loop, reads what plugins already publish (getReadings()), evaluates
//  a set of rules through a per-rule state machine, and records the
//  resulting Alert events.
//
//  This is the framework's one genuinely new primitive — an EDGE /
//  EVENT state machine.  Everything else is current-state polling;
//  this adds "a thing just crossed a line / just happened".
//
//  ── Milestone 1 (this file): the engine, headless ─────────────
//    Rules → debounce → state machine (OK → tripped → active →
//    cleared, or latched-until-ack) → events pushed to an in-RAM ring
//    and the serial log.  No channel sinks yet — routing to SMS /
//    LoRa / Email / Webhook / Buzzer / LCD / MQTT / Dashboard / SD is
//    milestone 2.  The Rule already carries its target channel
//    bitmask so that wiring is a drop-in later.
//
//  ── Rule kinds ───────────────────────────────────────────────
//    THRESHOLD — sustained compare, e.g. geiger usv_per_h >= 5.0.
//                Clears through a hysteresis band; debounced.
//    EVENT     — an edge on an increasing counter, e.g. AS3935
//                strikes increments, gated by distance_km <= 10.
//
//  Split .h/.cpp (forward-declares Framework) to break the
//  Framework <-> module include cycle — exactly like MQTTOut/SDLogger.
// ============================================================
#include "Config.h"  // OUT_ALERTS build switch + seed defaults

#if OUT_ALERTS
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

class Framework;
class IDevice;

class AlertManager {
 public:
  bool enabled = OUT_ALERTS;

  // ── Public types ─────────────────────────────────────────
  enum Severity : uint8_t { SEV_INFO = 0, SEV_WARN = 1, SEV_CRITICAL = 2 };
  enum Op : uint8_t { OP_GE = 0, OP_LE, OP_GT, OP_LT };
  enum Kind : uint8_t { K_THRESHOLD = 0, K_EVENT };
  // Channel bitmask — the sinks are wired in milestone 2; defined now
  // so a rule (and the routing matrix) already carries its target set.
  enum Channel : uint16_t {
    CH_NONE = 0, CH_BUZZER = 1 << 0, CH_LCD = 1 << 1, CH_MQTT = 1 << 2,
    CH_SD = 1 << 3, CH_DASH = 1 << 4, CH_LORA = 1 << 5, CH_EMAIL = 1 << 6,
    CH_WEBHOOK = 1 << 7, CH_SMS = 1 << 8
  };
  enum EventType : uint8_t { EV_RAISED = 0, EV_RENOTIFY, EV_CLEARED, EV_ACK };

  struct Rule {
    uint8_t  id = 0;
    bool     enabled = true;
    char     slug[12] = {0};   // plugin slug the reading comes from
    char     key[16]  = {0};   // reading key (a getReadings() name)
    Kind     kind = K_THRESHOLD;
    Op       op = OP_GE;
    float    threshold = 0;
    // Optional single gate — one extra condition that must also hold.
    bool     hasGate = false;
    char     gateKey[16] = {0};
    Op       gateOp = OP_LE;
    float    gateVal = 0;
    Severity severity = SEV_WARN;
    uint16_t channels = CH_NONE;      // target sinks (used in milestone 2)
    uint8_t  debounce = 2;            // consecutive qualifying samples
    bool     latch = false;          // latch-until-ack vs auto-reset
    float    hysteresisPct = 10;     // auto-reset band, % of threshold
    uint16_t cooldownSec = 60;       // min seconds between notifications
  };

  struct Event {
    uint8_t   ruleId;
    EventType type;
    Severity  severity;
    char      slug[12];
    char      key[16];
    float     value;
    uint32_t  tsMs;
  };

  // ── Lifecycle (called by Framework) ──────────────────────
  void begin(Framework* fw);
  void update();

  // Release latched rule(s).  ruleId < 0 acks every latched rule.
  // (Driven from the dashboard / REST in a later milestone.)
  void ack(int ruleId = -1);

  // Snapshot for /api/alerts + the dashboard banner (milestone 3).
  void toJson(JsonObject& o) const;

  uint8_t ruleCount() const { return static_cast<uint8_t>(_rules.size()); }

  // ── Rule CRUD (milestone 3b — editor / persistence) ──────
  //  rulesToJson serialises the full rule set for the editor;
  //  upsert/delete mutate it and persist to NVS; reset drops the NVS
  //  override back to the Config.h seed.
  void rulesToJson(JsonArray& arr) const;
  bool upsertRule(JsonObjectConst r);   // add (id 0) or replace by id
  bool deleteRule(uint8_t id);
  void resetRules();

 private:
  enum State : uint8_t { ST_IDLE = 0, ST_ACTIVE, ST_LATCHED };
  struct RT {
    State    state = ST_IDLE;
    uint8_t  deb = 0;
    float    lastVal = NAN;     // previous sample (EVENT edge detection)
    uint32_t lastNotify = 0;
  };

  Framework* _fw = nullptr;
  std::vector<Rule> _rules;
  std::vector<RT>   _rt;

  static constexpr uint8_t RING = 16;
  Event   _ring[RING];
  uint8_t _ringHead = 0, _ringCount = 0;
  uint32_t _lastEval = 0;

  void _loadSeedRules();
  void _evalRule(Rule& r, RT& st, uint32_t now);
  bool _readKey(const char* slug, const char* key, float& out) const;
  static bool _cmp(float v, Op op, float t);
  bool _cleared(const Rule& r, float v) const;
  bool _cooldownOk(const RT& st, const Rule& r, uint32_t now) const;
  void _emit(const Rule& r, EventType type, float v);
  void _route(const Rule& r, EventType type, float v);  // fan-out to sinks

  // ── 3b persistence helpers ────────────────────────────────
  String  _serializeRules() const;
  bool    _deserializeRules(const String& json);
  void    _saveRules();
  void    _rebuildRt();
  int     _ruleIndex(uint8_t id) const;
  uint8_t _nextId() const;
  static void _ruleToJson(const Rule& r, JsonObject o);
  static bool _ruleFromJson(JsonObjectConst o, Rule& r);

  // ── Outbound HTTPS queue (milestone 5 — email / webhook) ──
  //  Sinks enqueue a POST; update() drains ONE per call so TLS
  //  handshakes never overlap (serialised, per the design note).
  struct HttpJob { String url, auth, body; };
  std::vector<HttpJob> _httpQ;
  static constexpr uint8_t HTTPQ_MAX = 8;
  void _enqueueHttp(const char* url, const char* auth, const String& body);
  void _pumpHttp();
  bool _httpPost(const String& url, const String& auth, const String& body);
};

#else  // !OUT_ALERTS
// ── Stub — alarm engine compiled out via Config.h ────────────
//  Mirrors the surface Framework + WebAPI call (begin/update/ack/
//  toJson/enabled).
#include <ArduinoJson.h>
class Framework;
class AlertManager {
 public:
  bool enabled = false;
  void begin(Framework*) {}
  void update() {}
  void ack(int = -1) {}
  void toJson(JsonObject&) {}
  void rulesToJson(JsonArray&) const {}
  bool upsertRule(JsonObjectConst) { return false; }
  bool deleteRule(uint8_t) { return false; }
  void resetRules() {}
};
#endif  // OUT_ALERTS
