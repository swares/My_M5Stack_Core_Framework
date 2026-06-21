// ============================================================
//  AlertManager.cpp  –  Threshold / event alarm engine (milestone 1)
//  See AlertManager.h for the design overview.
// ============================================================
#include "AlertManager.h"

#if OUT_ALERTS
#include <math.h>
#include <string.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "Framework.h"   // full definition: _fw->plugins(), IDevice
#include "IDevice.h"
#include "Settings.h"    // NVS rule blob (hybrid with the Config seed)

void AlertManager::begin(Framework* fw) {
  _fw = fw;
  if (!enabled) return;
  // Hybrid load (same model as Wi-Fi/MQTT): an NVS rule blob overrides
  // the compiled Config.h seed; a parse failure or no blob → seed.
  String nvs = Settings::alertRules();
  if (nvs.length() && _deserializeRules(nvs)) {
    Serial.printf("[Alerts] engine up — %u rule(s) from NVS\n",
                  static_cast<unsigned>(_rules.size()));
  } else {
    _loadSeedRules();
    Serial.printf("[Alerts] engine up — %u seed rule(s)\n",
                  static_cast<unsigned>(_rules.size()));
  }
  _rebuildRt();
}

// ── Seed rules ───────────────────────────────────────────────
//  Compiled defaults for the two named "critical" hazards.  Runtime
//  (NVS) rules + a dashboard editor arrive in a later milestone; this
//  hard-codes the same two rules the feature ships with.
void AlertManager::_loadSeedRules() {
  // Radiation — geiger usv_per_h >= ALERT_RAD_USV (sustained, latched).
  Rule rad{};
  rad.id = 1;
  strncpy(rad.slug, "geiger", sizeof(rad.slug) - 1);
  rad.slug[sizeof(rad.slug) - 1] = '\0';
  strncpy(rad.key,  "usv_per_h", sizeof(rad.key) - 1);
  rad.key[sizeof(rad.key) - 1] = '\0';
  rad.kind = K_THRESHOLD;
  rad.op = OP_GE;
  rad.threshold = ALERT_RAD_USV;
  rad.severity = SEV_CRITICAL;
  rad.latch = true;
  rad.hysteresisPct = 15;
  rad.cooldownSec = ALERT_RAD_COOLDOWN;
  rad.debounce = 2;
  rad.channels = CH_BUZZER | CH_LCD | CH_MQTT | CH_SD | CH_DASH;
  _rules.push_back(rad);

  // Lightning — AS3935 strikes increments, gated distance_km <= N (edge).
  Rule lt{};
  lt.id = 2;
  strncpy(lt.slug, "lightning", sizeof(lt.slug) - 1);
  lt.slug[sizeof(lt.slug) - 1] = '\0';
  strncpy(lt.key,  "strikes", sizeof(lt.key) - 1);
  lt.key[sizeof(lt.key) - 1] = '\0';
  lt.kind = K_EVENT;
  lt.hasGate = true;
  strncpy(lt.gateKey, "distance_km", sizeof(lt.gateKey) - 1);
  lt.gateKey[sizeof(lt.gateKey) - 1] = '\0';
  lt.gateOp = OP_LE;
  lt.gateVal = ALERT_LIGHTNING_KM;
  lt.severity = SEV_CRITICAL;
  lt.latch = false;
  lt.cooldownSec = ALERT_LIGHTNING_COOLDOWN;
  lt.channels = CH_BUZZER | CH_LCD | CH_MQTT | CH_SD | CH_DASH;
  _rules.push_back(lt);
}

void AlertManager::update() {
  if (!enabled || !_fw) return;
  _pumpHttp();   // drain one queued email/webhook POST per loop (serialised)
  uint32_t now = millis();
  // Evaluate at the poll cadence, so "debounce N" counts N samples
  // (not N loop iterations) and matches the sensor refresh rate.
  if (now - _lastEval < POLL_MS) return;
  _lastEval = now;
  for (size_t i = 0; i < _rules.size(); i++)
    if (_rules[i].enabled) _evalRule(_rules[i], _rt[i], now);
}

// Pull one reading value by (slug, key) from the live plugins.
//  false = plugin absent, or present but doesn't expose that key — in
//  either case the rule simply doesn't fire (no alarming on no data).
bool AlertManager::_readKey(const char* slug, const char* key, float& out) const {
  for (auto* p : _fw->plugins()) {
    if (!p->active) continue;
    if (strcmp(p->slug(), slug) != 0) continue;
    SensorVal v[16];
    uint8_t n = 0;
    p->getReadings(v, n);
    for (uint8_t i = 0; i < n; i++)
      if (strcmp(v[i].key, key) == 0) { out = v[i].value; return true; }
    return false;  // matched the plugin, but key not in getReadings()
  }
  return false;    // no such plugin bound
}

bool AlertManager::_cmp(float v, Op op, float t) {
  switch (op) {
    case OP_GE: return v >= t;
    case OP_LE: return v <= t;
    case OP_GT: return v > t;
    case OP_LT: return v < t;
  }
  return false;
}

// Hysteresis clear test for a sustained THRESHOLD rule: the reading
// must back off past the band before the alarm resets, so it doesn't
// chatter around the threshold.
bool AlertManager::_cleared(const Rule& r, float v) const {
  float band = fabsf(r.threshold) * (r.hysteresisPct / 100.0f);
  if (r.op == OP_GE || r.op == OP_GT) return v < (r.threshold - band);
  return v > (r.threshold + band);
}

bool AlertManager::_cooldownOk(const RT& st, const Rule& r, uint32_t now) const {
  return st.lastNotify == 0 ||
         (now - st.lastNotify) >= static_cast<uint32_t>(r.cooldownSec) * 1000UL;
}

void AlertManager::_evalRule(Rule& r, RT& st, uint32_t now) {
  float val;
  if (!_readKey(r.slug, r.key, val))
    return;  // no data for this rule this round

  bool gateOk = true;
  if (r.hasGate) {
    float gv;
    gateOk = _readKey(r.slug, r.gateKey, gv) && _cmp(gv, r.gateOp, r.gateVal);
  }

  bool cond;
  if (r.kind == K_THRESHOLD)
    cond = _cmp(val, r.op, r.threshold) && gateOk;
  else  // EVENT: an upward edge on a counter (strikes++)
    cond = (!isnan(st.lastVal) && val > st.lastVal) && gateOk;

  switch (st.state) {
    case ST_IDLE:
      if (cond) {
        if (r.kind == K_EVENT) {
          if (_cooldownOk(st, r, now)) {
            _emit(r, EV_RAISED, val);
            st.lastNotify = now;
            if (r.latch) st.state = ST_LATCHED;  // else one-shot, stay IDLE
          }
        } else {
          if (++st.deb >= (r.debounce ? r.debounce : 1)) {
            _emit(r, EV_RAISED, val);
            st.lastNotify = now;
            st.state = ST_ACTIVE;
            st.deb = 0;
          }
        }
      } else {
        st.deb = 0;
      }
      break;

    case ST_ACTIVE:  // a sustained THRESHOLD alarm in progress
      if (_cleared(r, val)) {
        _emit(r, EV_CLEARED, val);
        st.state = r.latch ? ST_LATCHED : ST_IDLE;
        st.deb = 0;
      } else if (_cooldownOk(st, r, now)) {
        _emit(r, EV_RENOTIFY, val);  // still over the line — remind
        st.lastNotify = now;
      }
      break;

    case ST_LATCHED:  // condition gone (or one-shot) — hold until ack()
      if (r.kind == K_THRESHOLD && _cmp(val, r.op, r.threshold) && gateOk &&
          _cooldownOk(st, r, now)) {
        _emit(r, EV_RAISED, val);  // re-tripped while latched
        st.lastNotify = now;
      }
      break;
  }

  st.lastVal = val;
}

void AlertManager::ack(int ruleId) {
  if (!enabled) return;
  for (size_t i = 0; i < _rules.size(); i++) {
    if (_rt[i].state == ST_LATCHED && (ruleId < 0 || _rules[i].id == ruleId)) {
      _rt[i].state = ST_IDLE;
      _rt[i].deb = 0;
      _emit(_rules[i], EV_ACK, _rt[i].lastVal);
    }
  }
}

void AlertManager::_emit(const Rule& r, EventType type, float v) {
  Event& e = _ring[_ringHead];
  e.ruleId = r.id;
  e.type = type;
  e.severity = r.severity;
  e.value = v;
  e.tsMs = millis();
  strncpy(e.slug, r.slug, sizeof(e.slug) - 1); e.slug[sizeof(e.slug) - 1] = 0;
  strncpy(e.key, r.key, sizeof(e.key) - 1);    e.key[sizeof(e.key) - 1] = 0;
  _ringHead = (_ringHead + 1) % RING;
  if (_ringCount < RING) _ringCount++;

  static const char* TY[] = {"RAISED", "RENOTIFY", "CLEARED", "ACK"};
  static const char* SV[] = {"INFO", "WARN", "CRITICAL"};
  Serial.printf("[Alerts] %-8s %-8s %s.%s = %.3f  (channels 0x%03X)\n",
                TY[type], SV[r.severity], r.slug, r.key, v, r.channels);
  _route(r, type, v);
}

// ── _route — fan one event out to the channel sinks ──────────
//  Milestone 2 wires the four "cheap" sinks (Buzzer / LCD / MQTT / SD).
//  LoRa / Email / Webhook / SMS land in their own milestones — their
//  bits are already set on the rule, so adding them here is a drop-in.
void AlertManager::_route(const Rule& r, EventType type, float v) {
  const bool raised  = (type == EV_RAISED || type == EV_RENOTIFY);
  const bool cleared = (type == EV_CLEARED || type == EV_ACK);

  static const char* TYT[] = {"raised", "renotify", "cleared", "ack"};
  static const char* SVT[] = {"info", "warn", "critical"};
  char text[72];
  snprintf(text, sizeof(text), "%s  %s %s=%.2f",
           SVT[r.severity], r.slug, r.key, v);

  // Buzzer — built-in M5 speaker, on a raised WARN/CRITICAL only.
  if ((r.channels & CH_BUZZER) && ALERT_BUZZER && raised &&
      r.severity >= SEV_WARN) {
    M5.Speaker.tone(ALERT_BUZZER_HZ, ALERT_BUZZER_MS);
  }

  // LCD banner.
  if (r.channels & CH_LCD) {
    if (raised)       _fw->display.setAlert(text, r.severity);
    else if (cleared) _fw->display.clearAlert();
  }

  // MQTT — one JSON event to <base>/alert.
  if (r.channels & CH_MQTT) {
    char json[176];
    snprintf(json, sizeof(json),
             "{\"rule\":%u,\"type\":\"%s\",\"sev\":\"%s\",\"slug\":\"%s\","
             "\"key\":\"%s\",\"value\":%.3f,\"ts\":%lu}",
             r.id, TYT[type], SVT[r.severity], r.slug, r.key, v,
             static_cast<unsigned long>(millis()));
    _fw->mqtt.publishAlert(json);
  }

  // SD — one CSV row to /alerts.csv:
  //   datetime,uptime_ms,type,sev,slug,key,value
  // datetime is ISO-8601 wall-clock (NTP- or RTC-sourced) or blank when
  // the clock isn't set — matching the sensor log's leading columns.
  if (r.channels & CH_SD) {
    char iso[24];
    _fw->nowIso8601(iso, sizeof(iso));   // writes "" when no wall-clock
    char row[140];
    snprintf(row, sizeof(row), "%s,%lu,%s,%s,%s,%s,%.3f",
             iso, static_cast<unsigned long>(millis()), TYT[type],
             SVT[r.severity], r.slug, r.key, v);
    _fw->sdlog.logAlert(row);
  }
  // LoRa P2P — transmit on state edges only (raise/clear), not every
  // renotify, to spare airtime.  Reuses the LoRa plugin's existing
  // command("send", …) — found by slug, the same decoupling the router
  // uses.  Absent device → channel silently disabled.
  if ((r.channels & CH_LORA) && (type == EV_RAISED || type == EV_CLEARED)) {
    for (auto* p : _fw->plugins()) {
      if (p->active && strcmp(p->slug(), "lora") == 0) {
        p->command("send", String("ALERT ") + text);
        break;
      }
    }
  }

  // Webhook / Email — outbound HTTPS, on state edges only (handshakes
  // are heavy), enqueued for serialised sending in update().
  if (type == EV_RAISED || type == EV_CLEARED) {
    if ((r.channels & CH_WEBHOOK) && ALERT_WEBHOOK_URL[0]) {
      char json[176];
      snprintf(json, sizeof(json),
               "{\"rule\":%u,\"type\":\"%s\",\"sev\":\"%s\",\"slug\":\"%s\","
               "\"key\":\"%s\",\"value\":%.3f}",
               r.id, TYT[type], SVT[r.severity], r.slug, r.key, v);
      _enqueueHttp(ALERT_WEBHOOK_URL, ALERT_WEBHOOK_AUTH, json);
    }
    if ((r.channels & CH_EMAIL) && ALERT_EMAIL_URL[0]) {
      String eb = String("{\"to\":\"") + ALERT_EMAIL_TO +
                  "\",\"subject\":\"Alert: " + r.slug + "." + r.key +
                  "\",\"text\":\"" + text + "\"}";
      _enqueueHttp(ALERT_EMAIL_URL, ALERT_EMAIL_AUTH, eb);
    }
  }

  // SMS — via the cellular modem, edges only (per-message cost + the
  // single Port-C UART).  Reuses the modem plugin's command("sms",
  // "<number>,<text>"); the modem enforces its own daily cap.  Absent
  // modem → channel no-ops.
  if ((r.channels & CH_SMS) && ALERT_SMS_TO[0] &&
      (type == EV_RAISED || type == EV_CLEARED)) {
    for (auto* p : _fw->plugins()) {
      if (p->active && strcmp(p->slug(), "modem") == 0) {
        p->command("sms", String(ALERT_SMS_TO) + "," + text);
        break;
      }
    }
  }

  // CH_DASH — served by toJson()/the event ring (milestone 3).
}

// ── Outbound HTTPS queue (email / webhook) ───────────────────
void AlertManager::_enqueueHttp(const char* url, const char* auth,
                                const String& body) {
  if (_httpQ.size() >= HTTPQ_MAX) {
    Serial.println(F("[Alerts] http queue full — dropping a notification"));
    return;
  }
  _httpQ.push_back({String(url), String(auth ? auth : ""), body});
}

//  Drain ONE job per call so two TLS handshakes never overlap (and
//  never contend for heap with more than one in flight at a time).
void AlertManager::_pumpHttp() {
  if (_httpQ.empty() || WiFi.status() != WL_CONNECTED) return;
  // Hold the job in the queue (don't dequeue) while the heap is too low
  // for another TLS session — it retries on a later update() once memory
  // frees up, rather than being dropped by a doomed handshake.
  if (MIN_TLS_HEAP && ESP.getFreeHeap() < MIN_TLS_HEAP) return;
  HttpJob job = _httpQ.front();
  _httpQ.erase(_httpQ.begin());
  _httpPost(job.url, job.auth, job.body);
}

//  One-shot HTTPS POST.  Blocking but bounded (8 s) and serialised via
//  the queue; alerts are infrequent so the brief loop stall is fine.
bool AlertManager::_httpPost(const String& url, const String& auth,
                             const String& body) {
  if (!url.startsWith("https://")) {
    Serial.println(F("[Alerts] http sink needs an https:// URL"));
    return false;
  }
  String u = url.substring(8);
  int slash = u.indexOf('/');
  String hostport = (slash < 0) ? u : u.substring(0, slash);
  String path = (slash < 0) ? "/" : u.substring(slash);
  int colon = hostport.indexOf(':');
  String host = (colon < 0) ? hostport : hostport.substring(0, colon);
  uint16_t port = (colon < 0) ? 443 : hostport.substring(colon + 1).toInt();

  WiFiClientSecure c;
  c.setInsecure();        // encrypt-only (no CA pinning on a desk gadget)
  c.setTimeout(8000);
  if (!c.connect(host.c_str(), port)) {
    Serial.printf("[Alerts] http POST connect failed: %s:%u\n", host.c_str(), port);
    return false;
  }
  c.printf("POST %s HTTP/1.1\r\n", path.c_str());
  c.printf("Host: %s\r\n", host.c_str());
  if (auth.length()) c.printf("Authorization: %s\r\n", auth.c_str());
  c.print(F("Content-Type: application/json\r\n"));
  c.print(F("Connection: close\r\n"));
  c.printf("Content-Length: %u\r\n\r\n", body.length());
  c.print(body);

  String status;
  uint32_t t0 = millis();
  while (millis() - t0 < 8000 && c.connected() && !status.length()) {
    if (c.available()) status = c.readStringUntil('\n');
    else delay(5);
  }
  c.stop();
  bool ok = status.indexOf(" 2") > 0;  // HTTP/1.1 2xx
  Serial.printf("[Alerts] http POST %s -> %s\n", host.c_str(),
                status.length() ? status.c_str() : "(no reply)");
  return ok;
}

// ── injectEvent — inbound webhook path ───────────────────────
//  Pushes an externally-sourced alert directly into the ring and routes
//  it to the configured channel sinks, bypassing rule-engine evaluation.
//  The synthetic Rule (id = 0) carries the caller-supplied slug/key/sev/
//  channels so _emit() and _route() work without modification.
bool AlertManager::injectEvent(const char* slug, const char* key,
                                float value, Severity sev, uint16_t channels) {
  if (!enabled || !slug || !slug[0] || !key || !key[0]) return false;
  Rule r{};
  r.id       = 0;    // 0 = externally injected — not a rule-engine entry
  r.severity = sev;
  r.channels = channels;
  strncpy(r.slug, slug, sizeof(r.slug) - 1); r.slug[sizeof(r.slug) - 1] = '\0';
  strncpy(r.key,  key,  sizeof(r.key)  - 1); r.key[sizeof(r.key)  - 1] = '\0';
  _emit(r, EV_RAISED, value);
  return true;
}

void AlertManager::toJson(JsonObject& o) const {
  o["enabled"] = enabled ? 1 : 0;
  o["rules"] = static_cast<uint8_t>(_rules.size());
  uint8_t active = 0;
  for (auto& s : _rt) if (s.state != ST_IDLE) active++;
  o["active"] = active;
  JsonArray ev = o["events"].to<JsonArray>();
  static const char* TY[] = {"raised", "renotify", "cleared", "ack"};
  static const char* SV[] = {"info", "warn", "critical"};
  for (uint8_t i = 0; i < _ringCount; i++) {
    uint8_t idx = (uint8_t)((_ringHead + RING - _ringCount + i) % RING);
    const Event& e = _ring[idx];
    JsonObject j = ev.add<JsonObject>();
    j["rule"] = e.ruleId;
    j["type"] = TY[e.type];
    j["sev"] = SV[e.severity];
    j["slug"] = e.slug;
    j["key"] = e.key;
    j["value"] = e.value;
    j["age_ms"] = millis() - e.tsMs;
  }
}

// ── Rule (de)serialisation ───────────────────────────────────
//  Compact JSON, short keys to bound the NVS blob.
void AlertManager::_ruleToJson(const Rule& r, JsonObject o) {
  o["id"]    = r.id;
  o["en"]    = r.enabled ? 1 : 0;
  o["slug"]  = r.slug;
  o["key"]   = r.key;
  o["kind"]  = static_cast<uint8_t>(r.kind);
  o["op"]    = static_cast<uint8_t>(r.op);
  o["thr"]   = r.threshold;
  o["gate"]  = r.hasGate ? 1 : 0;
  o["gk"]    = r.gateKey;
  o["gop"]   = static_cast<uint8_t>(r.gateOp);
  o["gv"]    = r.gateVal;
  o["sev"]   = static_cast<uint8_t>(r.severity);
  o["ch"]    = r.channels;
  o["deb"]   = r.debounce;
  o["latch"] = r.latch ? 1 : 0;
  o["hyst"]  = r.hysteresisPct;
  o["cd"]    = r.cooldownSec;
}

bool AlertManager::_ruleFromJson(JsonObjectConst o, Rule& r) {
  r = Rule{};
  // Clamp every enum field coming from JSON to its valid range.  These
  // values index fixed-size string tables (SV[]/TYT[]/SVT[]) in _emit /
  // _route / toJson, and Op feeds a switch — an out-of-range value from a
  // crafted POST /api/alerts/rules/save or a corrupt NVS blob would
  // otherwise cause an out-of-bounds read.  An invalid value falls back
  // to the field's safe default rather than being trusted.
  auto clampEnum = [](uint8_t v, uint8_t hi, uint8_t dflt) -> uint8_t {
    return v <= hi ? v : dflt;
  };
  r.id            = o["id"]   | 0;
  r.enabled       = (o["en"]  | 1) != 0;
  strncpy(r.slug, o["slug"] | "", sizeof(r.slug) - 1);
  r.slug[sizeof(r.slug) - 1] = '\0';
  strncpy(r.key,  o["key"]  | "", sizeof(r.key) - 1);
  r.key[sizeof(r.key) - 1] = '\0';
  r.kind          = static_cast<Kind>(
      clampEnum(o["kind"] | 0, K_EVENT, K_THRESHOLD));
  r.op            = static_cast<Op>(clampEnum(o["op"] | 0, OP_LT, OP_GE));
  r.threshold     = o["thr"]  | 0.0f;
  r.hasGate       = (o["gate"] | 0) != 0;
  strncpy(r.gateKey, o["gk"] | "", sizeof(r.gateKey) - 1);
  r.gateKey[sizeof(r.gateKey) - 1] = '\0';
  r.gateOp        = static_cast<Op>(clampEnum(o["gop"] | 1, OP_LT, OP_LE));
  r.gateVal       = o["gv"]   | 0.0f;
  r.severity      = static_cast<Severity>(
      clampEnum(o["sev"] | 1, SEV_CRITICAL, SEV_WARN));
  r.channels      = o["ch"]   | 0;
  r.debounce      = o["deb"]  | 2;
  r.latch         = (o["latch"] | 0) != 0;
  r.hysteresisPct = o["hyst"] | 10.0f;
  r.cooldownSec   = o["cd"]   | 60;
  return r.slug[0] != '\0' && r.key[0] != '\0';  // minimal validity
}

String AlertManager::_serializeRules() const {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto& r : _rules) _ruleToJson(r, arr.add<JsonObject>());
  String out;
  serializeJson(doc, out);
  return out;
}

bool AlertManager::_deserializeRules(const String& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;   // parse error
  if (!doc.is<JsonArray>()) return false;
  _rules.clear();
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    Rule r;
    if (_ruleFromJson(o, r)) _rules.push_back(r);
  }
  return true;  // valid array (possibly empty = deliberately no rules)
}

void AlertManager::_saveRules()  { Settings::setAlertRules(_serializeRules()); }
void AlertManager::_rebuildRt()  { _rt.assign(_rules.size(), RT{}); }

int AlertManager::_ruleIndex(uint8_t id) const {
  for (size_t i = 0; i < _rules.size(); i++)
    if (_rules[i].id == id) return static_cast<int>(i);
  return -1;
}

uint8_t AlertManager::_nextId() const {
  uint8_t mx = 0;
  for (auto& r : _rules) if (r.id > mx) mx = r.id;
  return mx + 1;
}

void AlertManager::rulesToJson(JsonArray& arr) const {
  for (auto& r : _rules) _ruleToJson(r, arr.add<JsonObject>());
}

bool AlertManager::upsertRule(JsonObjectConst o) {
  if (!enabled) return false;
  Rule r;
  if (!_ruleFromJson(o, r)) return false;
  if (r.id == 0) r.id = _nextId();         // new rule
  int idx = _ruleIndex(r.id);
  if (idx >= 0) _rules[idx] = r;
  else          _rules.push_back(r);
  _rebuildRt();
  _saveRules();
  return true;
}

bool AlertManager::deleteRule(uint8_t id) {
  if (!enabled) return false;
  int idx = _ruleIndex(id);
  if (idx < 0) return false;
  _rules.erase(_rules.begin() + idx);
  _rebuildRt();
  _saveRules();
  return true;
}

void AlertManager::resetRules() {
  if (!enabled) return;
  _rules.clear();
  _loadSeedRules();
  _rebuildRt();
  Settings::setAlertRules("");  // drop the NVS override → seed on next boot too
}
#endif  // OUT_ALERTS
