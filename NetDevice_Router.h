#pragma once
// ============================================================
//  NetDevice_Router.h  –  Local→Cloud escalation router   [NET]
//
//  The "router + voice" tier (see the escalation-ladder doc, §00–05)
//  embodied as a single framework plugin.  Where UartDevice_ModuleLLM
//  talks DOWN a UART to the on-board AX630C, this talks OUT over
//  HTTPS to your Orange Pi orchestrator — and decides, per turn,
//  which one should answer.
//
//      auto* llm = new UartDevice_ModuleLLM(Serial2, 115200);
//      fw.addPlugin(llm);                          // the local model
//      fw.addPlugin(new NetDevice_Router(llm));    // the router
//
//  ── Why it needs NO framework changes ───────────────────────
//  It is NOT an I2C device and NOT a UART device — it is a pure
//  network client.  IPinDevice already gives exactly the contract
//  we want: i2cAddresses() returns 0 (the boot scan skips us), and
//  the framework activates us once via beginPins().  So we simply
//  subclass IPinDevice — no new base class, no edits to Framework.
//  (IUartDevice extends IPinDevice for the same reason.)
//
//  ── How it works ────────────────────────────────────────────
//  CONTROLLABLE — driven via the Web API only:
//      GET /api/route/set?ask=<prompt>    submit one turn
//      GET /api/route/set?clear=1         reset
//  controlSchema() makes the dashboard render a chat box + Clear
//  button automatically, exactly like the LLM panel.
//
//  A turn runs ASYNCHRONOUSLY so the loop never stalls:
//    1. command("ask") classifies on-device (_classify) and returns
//       at once, setting _busy.
//    2a. TRIVIAL → delegate to the on-board Module LLM via its public
//        command("ask"), then MIRROR its streamed answer by reading
//        its public toJson() each fastPoll().  Fully decoupled — no
//        edits to UartDevice_ModuleLLM.  route_taken = "local".
//    2b. SMART TEXT (optional 3rd route, ROUTER_DIRECT_API) → delegate
//        the same decoupled way to a NetDevice_ClaudeAPI plugin: a
//        non-coding turn too rich for the 0.5B but needing no repo
//        goes straight to the Anthropic API, skipping the Pi.
//        route_taken = "direct_api".  ⚠ puts an API key in flash.
//    2c. HARD → open a WiFiClientSecure to the Pi, POST the §03 brief,
//        and read the streamed SSE reply incrementally in fastPoll().
//        route_taken = "escalated".  The CoreS3 never holds the
//        Anthropic key — the Pi orchestrator owns Claude Code.
//    3. toJson() publishes prompt/answer/route_taken/busy/done so the
//       dashboard streams the reply in, and route_taken flows to MQTT
//       and the SD log for free (local-vs-escalated accounting).
//
//  The Pi side is expected to answer text/event-stream with lines
//  of  data: {"delta":"<text>"}  ...  data: {"finish":true}  or a
//  final  data: [DONE].  Adjust _consumeLine() to match your
//  orchestrator's exact response shape.
// ============================================================
#include "../src/IPinDevice.h"
#include "../src/Config.h"     // ROUTER_* settings
#include <WiFiClientSecure.h>   // pulls in WiFi.h
#include <ArduinoJson.h>

class NetDevice_Router : public IPinDevice {
 public:
  // localLlm:  the on-board Module LLM plugin for trivial turns.
  // directApi: an optional NetDevice_ClaudeAPI plugin for the 3rd
  //   "smart text" route (see ROUTER_DIRECT_API in Config.h).  Pass
  //   nullptr (the default) for a classic two-way router.
  // Pass localLlm = nullptr to make the router escalation-only.
  explicit NetDevice_Router(IDevice* localLlm = nullptr,
                            IDevice* directApi = nullptr)
      : _localLlm(localLlm), _directApi(directApi) {}

  // ── Tunables (defined in Config.h — edit them there) ──────
  //  The Pi orchestrator that fronts the RKLLama router + Claude Code.
  static constexpr const char* PI_HOST = ROUTER_PI_HOST;
  static constexpr uint16_t    PI_PORT = ROUTER_PI_PORT;
  static constexpr const char* PI_PATH = ROUTER_PI_PATH;
  static constexpr const char* BEARER  = ROUTER_BEARER;     // "" = no auth header
  // LAN box with a self-signed cert → ROUTER_TLS_INSECURE true
  // (encrypt only).  For a real cert, set it false in Config.h and
  // pin the CA in beginPins() — the same pattern MQTTOut uses.
  static constexpr bool        TLS_INSECURE = ROUTER_TLS_INSECURE;
  // Abandon a reply only after this long with NO token at all — an
  // INACTIVITY timeout, not a total cap (a long answer that keeps
  // streaming stays healthy).  Mirrors UartDevice_ModuleLLM.
  static constexpr uint32_t    REPLY_IDLE_MS = ROUTER_REPLY_IDLE_MS;
  static constexpr uint16_t    ANSWER_MAX    = ROUTER_ANSWER_MAX;
  // Outbound-connect retry (rides out transient TLS-alloc failures
  // during a dashboard HTTPS burst).
  static constexpr uint8_t     CONNECT_TRIES      = ROUTER_CONNECT_TRIES;
  static constexpr uint16_t    CONNECT_BACKOFF_MS = ROUTER_CONNECT_BACKOFF_MS;

  // ── Identity ──────────────────────────────────────────────
  const char* name() const override { return "Router"; }
  const char* slug() const override { return "route"; }
  bool controllable() const override { return true; }

  // ── Activation (IPinDevice hook — no I2C, no pins) ────────
  bool beginPins() override {
    if (TLS_INSECURE)
      _client.setInsecure();
    // else _client.setCACert(PI_CA_CERT);
    _client.setTimeout(8000);  // ms — connect/read budget
    if (WiFi.status() != WL_CONNECTED)
      Serial.println(F("[Router] WiFi down at boot — will retry per request"));
    Serial.printf("[Router] ready — escalates to https://%s:%u%s\n",
                  PI_HOST, PI_PORT, PI_PATH);
    return true;  // virtual device: always active once registered
  }

  // ── Async servicing ───────────────────────────────────────
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    if (!_busy)
      return;
#if ROUTER_LLM_TIEBREAK
    if (_route == CLASSIFYING) { _pumpClassify(); return; }
#endif
    if (_route == LOCAL)
      _pumpLocal();
    else if (_route == DIRECT_API)
      _pumpDirect();
    else if (_route == ESCALATED)
      _pumpEscalated();
  }

  void update() override {
    if (_busy && (millis() - _lastRxMs) > REPLY_IDLE_MS) {
      Serial.printf("[Router] reply timed out — silent for %u ms\n",
                    static_cast<uint32_t>(REPLY_IDLE_MS));
      _finish(/*timedOut=*/true);
    }
  }

  // ── Data access ───────────────────────────────────────────
  void toJson(JsonObject& o) const override {
    o["busy"]        = _busy ? 1 : 0;
    o["done"]        = _done ? 1 : 0;
    o["timed_out"]   = _timedOut ? 1 : 0;
    o["route_taken"] = _route == LOCAL ? "local"
                       : _route == DIRECT_API ? "direct_api"
                       : _route == ESCALATED ? "escalated"
                       : _route == CLASSIFYING ? "classifying" : "";
    o["prompt"]      = _prompt;
    o["answer"]      = _answer;
    o["escalations"] = _escalations;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"busy", static_cast<float>(_busy ? 1 : 0), ""};
    b[1] = {"escalations", static_cast<float>(_escalations), ""};
    n = 2;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    if (param == "ask") {
      if (_busy) {
        Serial.println(F("[Router] ask rejected — a reply is still in flight"));
        return false;
      }
      if (value.length() == 0) {
        Serial.println(F("[Router] ask rejected — empty prompt"));
        return false;
      }
      _prompt = value;
      _answer = "";
      _done = false;
      _timedOut = false;
      Route r = _classify(value);
#if ROUTER_LLM_TIEBREAK
      // The prefilter only escalates on an explicit signal, so its
      // "LOCAL" verdict really means "nothing matched" — the ambiguous
      // middle.  When a local model is present and there's somewhere to
      // escalate to, let the model adjudicate yes/no before defaulting
      // to it.  Decisive ESCALATED / DIRECT_API verdicts skip this.
      if (r == LOCAL && _localLlm && _hasEscalationTarget())
        return _startClassify(value);
#endif
      switch (r) {
        case ESCALATED:  return _startEscalated(value);
        case DIRECT_API: return _startDirect(value);
        default:         return _startLocal(value);
      }
    }
    if (param == "clear") {
      _reset();
      return true;
    }
    return false;  // unknown param
  }

  void controlSchema(JsonArray& out) const override {
    JsonObject a = out.add<JsonObject>();
    a["id"] = "ask";
    a["label"] = "Ask";
    a["type"] = "text";
    a["placeholder"] = "Type a request — trivial stays local, hard escalates";
    JsonObject c = out.add<JsonObject>();
    c["id"] = "clear";
    c["label"] = "Clear";
    c["type"] = "button";
    c["query"] = "clear=1";
  }

 private:
  enum Route : uint8_t { NONE, LOCAL, DIRECT_API, ESCALATED, CLASSIFYING };

  IDevice*          _localLlm;
  IDevice*          _directApi;
  WiFiClientSecure  _client;
  String            _prompt, _answer, _rxbuf;
  Route             _route = NONE;
  bool              _busy = false, _done = false, _timedOut = false;
  bool              _headersDone = false;
  uint32_t          _lastRxMs = 0;
  uint32_t          _escalations = 0;

  // ── Classify into one of three routes ─────────────────────
  //  Order matters: a coding task always wins (it needs the agent),
  //  then "smart text" (cloud model, no repo) if the 3rd route is
  //  enabled and wired, else everything falls to the local model.
  Route _classify(const String& p) const {
    const bool hard = _needsEscalation(p);
    // Coding / agent task: prefer the Pi orchestrator, which owns Claude
    // Code (real filesystem + tools).  Only escalate if a Pi is actually
    // configured — with ROUTER_PI_HOST empty we never open an outbound
    // TLS socket, which would otherwise block this single-threaded
    // server while connect() to an unreachable host times out.
    if (hard && strlen(PI_HOST) > 0)
      return ESCALATED;
#if ROUTER_DIRECT_API
    // No Pi (or a non-coding "smart text" turn): hand off to the Claude
    // API directly when a NetDevice_ClaudeAPI is wired.  A hard task
    // with no Pi still goes to the Claude *model* (text answer, no repo)
    // rather than dead-ending at the tiny local LLM.
    if (_directApi && (hard || _isSmartText(p)))
      return DIRECT_API;
#endif
    return LOCAL;
  }

  // "Smart text": non-coding, but richer than the 0.5B handles well —
  //  matches a ROUTER_DIRECT_KEYWORDS token, or simply runs long.
  bool _isSmartText(const String& p) const {
    String s = p;
    s.toLowerCase();
    String list = ROUTER_DIRECT_KEYWORDS;
    int start = 0;
    while (start < (int)list.length()) {
      int comma = list.indexOf(',', start);
      if (comma < 0)
        comma = list.length();
      String kw = list.substring(start, comma);
      kw.trim();
      if (kw.length() && s.indexOf(kw) >= 0)
        return true;
      start = comma + 1;
    }
    // word-count fallback: long prompts outgrow the tiny local model
    unsigned words = p.length() ? 1 : 0;
    for (unsigned i = 1; i < p.length(); i++)
      if (p[i] == ' ' && p[i - 1] != ' ')
        words++;
    return words >= ROUTER_DIRECT_MIN_WORDS;
  }

  // ── Classify (the §02 heuristic prefilter, on-device, ~0 ms) ──
  //  Conservative: bias toward escalating, since escalating an easy
  //  turn is cheaper than under-serving a hard one.  Extend the
  //  keyword/extension lists to taste; if you want a model-judged
  //  decision for the ambiguous middle, route undecided prompts to
  //  the local LLM with a one-token "escalate? yes/no" system prompt.
  bool _needsEscalation(const String& p) const {
    String s = p;
    s.toLowerCase();
    // Walk the comma-separated ROUTER_ESCALATE_KEYWORDS list from
    // Config.h, testing each token against the prompt.
    String list = ROUTER_ESCALATE_KEYWORDS;
    int start = 0;
    while (start < (int)list.length()) {
      int comma = list.indexOf(',', start);
      if (comma < 0)
        comma = list.length();
      String kw = list.substring(start, comma);
      kw.trim();
      if (kw.length() && s.indexOf(kw) >= 0)
        return true;
      start = comma + 1;
    }
    if (p.indexOf('/') >= 0)  // looks like a path
      return true;
    static const char* ext[] = {".py", ".ts", ".tsx", ".js", ".jsx", ".go",
                                ".rs", ".java", ".c", ".cpp", ".h", ".sql",
                                ".sh", ".json", ".yaml"};
    for (auto e : ext)
      if (s.indexOf(e) >= 0)
        return true;
    return false;
  }

  // True when escalation has somewhere to go (a Pi, or the direct API).
  bool _hasEscalationTarget() const {
    if (strlen(PI_HOST) > 0)
      return true;
#if ROUTER_DIRECT_API
    if (_directApi)
      return true;
#endif
    return false;
  }

#if ROUTER_LLM_TIEBREAK
  // ── LLM tiebreaker: does this ambiguous turn need escalation? ──
  //  Reached only for turns the keyword/extension prefilter did NOT
  //  flag (see command("ask")), and only when a local model exists and
  //  there's a target to escalate to.  Runs ONE short yes/no classify
  //  inference on the local model asynchronously (route = CLASSIFYING);
  //  _pumpClassify() reads the verdict and dispatches the real turn.

  // Kick off the classification turn.  The user's prompt is already in
  // _prompt; we ask the model a wrapped yes/no question and mirror its
  // reply in _pumpClassify().
  bool _startClassify(const String& prompt) {
    String q = String(ROUTER_TIEBREAK_PROMPT) + "\n\nRequest: " + prompt;
    if (!_localLlm->command("ask", q)) {
      // Local model busy/unavailable — skip the tiebreaker and answer
      // locally on a best-effort basis (its own fallback handles the
      // unavailable case).
      return _startLocal(prompt);
    }
    _route = CLASSIFYING;
    _busy = true;
    _lastRxMs = millis();
    Serial.println(F("[Router] tiebreaker — asking local model to classify"));
    return true;
  }

  // Watch the classification turn.  While the local model is still
  // thinking, keep the router's idle timer fresh.  Once it finishes,
  // read the one-word verdict, clear the model's scratch, and dispatch
  // the REAL turn: "yes" → escalate (Pi, else direct API), else local.
  void _pumpClassify() {
    JsonDocument d;
    JsonObject o = d.to<JsonObject>();
    _localLlm->toJson(o);
    const bool done = (o["done"] | 0) || (o["timed_out"] | 0);
    const bool busy = (o["busy"] | 0);
    if (!done && busy) {
      _lastRxMs = millis();   // classification still streaming — stay alive
      return;
    }
    String raw = o["answer"] | "";
#if ROUTER_TIEBREAK_TRACE
    // Dump the model's exact reply so the classification prompt can be
    // tuned — invaluable while picking ROUTER_TIEBREAK_PROMPT wording.
    Serial.printf("[Router] tiebreaker raw reply: \"%s\"\n", raw.c_str());
#endif
    String verdict = raw;
    verdict.toLowerCase();
    const bool escalate = verdict.indexOf("yes") >= 0 ||
                          verdict.indexOf("escalate") >= 0 ||
                          verdict.indexOf("code") >= 0;
    _localLlm->command("clear", "");   // wipe scratch before the real turn
    Serial.printf("[Router] tiebreaker verdict: %s\n",
                  escalate ? "ESCALATE" : "local");
    if (escalate) {
      if (strlen(PI_HOST) > 0) { _startEscalated(_prompt); return; }
#if ROUTER_DIRECT_API
      if (_directApi) { _startDirect(_prompt); return; }
#endif
    }
    _startLocal(_prompt);
  }
#endif  // ROUTER_LLM_TIEBREAK

  // ── Local path: delegate to the Module LLM, mirror its answer ──
  bool _startLocal(const String& prompt) {
    if (!_localLlm) {
#if ROUTER_FALLBACK_ESCALATE
      Serial.println(F("[Router] no local LLM — falling back to escalate"));
      return _startEscalated(prompt);
#else
      _answer = "[no local LLM configured]";
      _finish(false);
      return false;
#endif
    }
    if (!_localLlm->command("ask", prompt)) {
#if ROUTER_FALLBACK_ESCALATE
      Serial.println(F("[Router] local LLM unavailable — falling back to escalate"));
      return _startEscalated(prompt);
#else
      _answer = "[local LLM busy or unavailable]";
      _finish(false);
      return false;
#endif
    }
    _route = LOCAL;
    _busy = true;
    _lastRxMs = millis();
    Serial.println(F("[Router] handled locally → Module LLM"));
    return true;
  }

  // Read the local LLM's PUBLIC state via toJson() — zero coupling
  // to its internals.  The Module LLM's own fastPoll() is what
  // actually drives its UART; we only observe.
  void _pumpLocal() {
    JsonDocument d;
    JsonObject o = d.to<JsonObject>();
    _localLlm->toJson(o);
    String a = o["answer"] | "";
    if (a.length() && a != _answer) {
      _answer = a;
      _lastRxMs = millis();
    }
    bool done = (o["done"] | 0) || (o["timed_out"] | 0);
    bool busy = (o["busy"] | 0);
    if (done || !busy)
      _finish((o["timed_out"] | 0) != 0);
  }

  // ── Direct-API path: delegate to NetDevice_ClaudeAPI, mirror it ──
  //  Same decoupled pattern as the local path — the router drives the
  //  Claude API plugin through its PUBLIC command()/toJson() only.
  bool _startDirect(const String& prompt) {
    if (!_directApi) {                  // 3rd route off or unwired
      return _startLocal(prompt);       // safe fallback
    }
    if (!_directApi->command("ask", prompt)) {
      _answer = "[direct API busy or unavailable]";
      _finish(false);
      return false;
    }
    _route = DIRECT_API;
    _busy = true;
    _lastRxMs = millis();
    _escalations++;                     // a cloud call, counted too
    Serial.println(F("[Router] smart text → direct Claude API"));
    return true;
  }

  void _pumpDirect() {
    JsonDocument d;
    JsonObject o = d.to<JsonObject>();
    _directApi->toJson(o);
    String a = o["answer"] | "";
    if (a.length() && a != _answer) {
      _answer = a;
      _lastRxMs = millis();
    }
    bool done = (o["done"] | 0) || (o["timed_out"] | 0);
    bool busy = (o["busy"] | 0);
    if (done || !busy)
      _finish((o["timed_out"] | 0) != 0);
  }

  // ── Escalated path: stream the reply from the Pi orchestrator ──
  bool _startEscalated(const String& prompt) {
    if (WiFi.status() != WL_CONNECTED) {
      _answer = "[offline — cannot escalate]";
      _finish(false);
      return false;
    }
    // Opening the TLS socket can fail transiently if the dashboard's
    // HTTPS server is mid-handshake and heap is briefly fragmented.
    // Retry a few times with a doubling backoff before giving up.
    bool connected = false;
    uint16_t backoff = CONNECT_BACKOFF_MS;
    for (uint8_t attempt = 1; attempt <= CONNECT_TRIES; attempt++) {
      _client.stop();                   // release any half-open socket
      if (_client.connect(PI_HOST, PI_PORT)) {
        connected = true;
        break;
      }
      Serial.printf("[Router] connect attempt %u/%u failed\n",
                    attempt, static_cast<unsigned>(CONNECT_TRIES));
      if (attempt < CONNECT_TRIES) {
        delay(backoff);
        uint32_t nb = (uint32_t)backoff * 2u;     // double, capped at 2 s
        backoff = nb > 2000u ? 2000 : (uint16_t)nb;
      }
    }
    if (!connected) {
      _answer = "[cannot reach Pi orchestrator]";
      _finish(false);
      return false;
    }
    // Build the §03 brief.  conversation_summary would come from a
    // running chat history if you keep one; goal alone works to start.
    JsonDocument b;
    b["goal"] = prompt;
    b["why"] = "classified on-device as a coding / agent task";
    b["source"] = "m5stack-cores3";
    b["max_tokens"] = 2048;
    String body;
    serializeJson(b, body);

    _client.printf("POST %s HTTP/1.1\r\n", PI_PATH);
    _client.printf("Host: %s\r\n", PI_HOST);
    if (strlen(BEARER))
      _client.printf("Authorization: Bearer %s\r\n", BEARER);
    _client.print(F("Content-Type: application/json\r\n"));
    _client.print(F("Accept: text/event-stream\r\n"));
    _client.print(F("Connection: close\r\n"));
    _client.printf("Content-Length: %u\r\n\r\n", body.length());
    _client.print(body);

    _route = ESCALATED;
    _busy = true;
    _headersDone = false;
    _rxbuf = "";
    _lastRxMs = millis();
    _escalations++;
    Serial.printf("[Router] escalated to Pi → %s%s\n", PI_HOST, PI_PATH);
    return true;
  }

  void _pumpEscalated() {
    while (_client.available()) {
      char c = _client.read();
      if (c == '\r')
        continue;
      if (c == '\n') {
        _consumeLine();
        _rxbuf = "";
      } else if (_rxbuf.length() < 2048) {
        _rxbuf += c;
      }
    }
    // Server closed the connection with nothing left to read → the
    // stream is over even if no explicit [DONE] arrived.
    if (!_client.connected() && !_client.available())
      _finish(false);
  }

  // One line of the SSE response.  Skip HTTP headers until the blank
  // line, then parse  data: {...}  events.
  void _consumeLine() {
    String line = _rxbuf;
    line.trim();
    if (!_headersDone) {
      if (line.length() == 0)
        _headersDone = true;
      return;
    }
    if (!line.startsWith("data:"))
      return;
    String payload = line.substring(5);
    payload.trim();
    if (payload == "[DONE]") {
      _finish(false);
      return;
    }
    JsonDocument d;
    if (deserializeJson(d, payload))
      return;  // not JSON — ignore keep-alives / comments
    _lastRxMs = millis();
    const char* delta = d["delta"] | (d["content"] | static_cast<const char*>(nullptr));
    if (delta && _answer.length() < ANSWER_MAX)
      _answer += delta;
    if (d["finish"].as<bool>())
      _finish(false);
  }

  // ── Shared completion / reset ─────────────────────────────
  void _finish(bool timedOut) {
    _busy = false;
    _done = true;
    _timedOut = timedOut;
    if (_route == ESCALATED)
      _client.stop();
  }

  void _reset() {
    _client.stop();
    // Propagate the reset downstream so the dashboard "Clear" button is a
    // true reset of the whole chain — wipes the local model's scratch and
    // the Claude API plugin's conversation history, not just the router.
    if (_localLlm)  _localLlm->command("clear", "");
    if (_directApi) _directApi->command("clear", "");
    _prompt = "";
    _answer = "";
    _rxbuf = "";
    _route = NONE;
    _busy = false;
    _done = false;
    _timedOut = false;
    _headersDone = false;
  }
};
