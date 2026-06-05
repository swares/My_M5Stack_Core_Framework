#pragma once
// ============================================================
//  NetDevice_ClaudeAPI.h  –  Direct Anthropic Messages API   [NET]
//
//  Lets the CoreS3 ask the Claude *model* directly over the
//  internet — no Orange Pi, no orchestrator in between.  It is a
//  WiFiClientSecure HTTPS client that POSTs to the Anthropic
//  Messages API and streams the reply back into the framework's
//  dashboard / REST / MQTT plumbing, exactly like the Module LLM.
//
//      fw.addPlugin(new NetDevice_ClaudeAPI());
//
//  ── READ THIS FIRST: model, not agent ───────────────────────
//  This calls Claude the *API* — text in, text out.  Great for
//  "explain this trace", "draft this regex", "summarise this".
//  It is NOT Claude *Code*: there is no filesystem, shell, git
//  repo, or tool loop here, because the ESP32 cannot host one.
//  Anything that must READ OR EDIT A CODEBASE still has to go to
//  Claude Code on the Pi / a dev box (see NetDevice_Router +
//  orchestrator.py).  Use this for smart *answers*, not for work
//  that produces a diff.
//
//  ── Trade-offs you are accepting by enabling it ─────────────
//    • The API key lives in firmware on a desk gadget whose flash
//      can be read.  Treat it as low-trust; scope/rotate the key.
//      Prefer routing through the Pi if the key matters.
//    • It bypasses the local-first tier — every call is cloud.
//      Reserve it for the "smart text" middle of a 3-way router
//      (local model -> Claude API -> Claude Code), not the default.
//
//  ── How it works in this framework ──────────────────────────
//  CONTROLLABLE — driven via the Web API only:
//      GET /api/claude/set?ask=<prompt>    start a query
//      GET /api/claude/set?clear=1         clear last prompt/answer
//  Runs ASYNCHRONOUSLY so the loop never stalls:
//    • command("ask") opens the TLS socket, sends the request, and
//      returns at once, setting _busy.
//    • fastPoll() drains the streamed SSE response a line at a time,
//      folding content deltas into _answer.
//    • update() enforces an inactivity timeout (REPLY_IDLE_MS).
//    • toJson() publishes answer + busy/done so the dashboard polls
//      /api/claude and shows the reply as it streams in.
//
//  Subclasses IPinDevice: it is a pure network client, so it has no
//  I2C address (the boot scan skips it) and is activated once via
//  beginPins() — the same path IUartDevice and NetDevice_Router use.
//  No edits to Framework.
// ============================================================
#include "../src/IPinDevice.h"
#include "../src/Config.h"     // CLAUDE_* settings
#include "../src/Settings.h"   // runtime Claude API key (approach B)
#include "../src/HttpSse.h"    // header-skip + chunked de-framing for the SSE body
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

class NetDevice_ClaudeAPI : public IPinDevice {
 public:
  NetDevice_ClaudeAPI() {}

  // ── Tunables (defined in Config.h — edit them there) ──────
  //  ⚠ The API key is a runtime value now (Settings tab / setup
  //  portal → NVS, falling back to CLAUDE_API_KEY in Secrets.h).
  static String apiKey() { return Settings::claudeKey(); }
  static constexpr const char* MODEL   = CLAUDE_MODEL;
  static constexpr const char* SYSTEM_PROMPT = CLAUDE_SYSTEM_PROMPT;
  static constexpr int      MAX_TOKENS    = CLAUDE_MAX_TOKENS;
  static constexpr uint32_t REPLY_IDLE_MS = CLAUDE_REPLY_IDLE_MS;
  static constexpr uint16_t ANSWER_MAX    = CLAUDE_ANSWER_MAX;

  static constexpr const char* HOST = "api.anthropic.com";
  static constexpr uint16_t    PORT = 443;
  static constexpr const char* PATH = "/v1/messages";
  static constexpr const char* API_VERSION = CLAUDE_API_VERSION;

  // ── Identity ──────────────────────────────────────────────
  const char* name() const override { return "Claude API"; }
  const char* slug() const override { return "claude"; }
  bool controllable() const override { return true; }

  // ── Activation (IPinDevice hook — no I2C, no pins) ────────
  bool beginPins() override {
    // Anthropic presents a public CA chain; for a desk gadget on a
    // trusted LAN, setInsecure() (encrypt, skip cert validation) is
    // the pragmatic choice.  To pin properly, paste the CA root and
    // call _client.setCACert(...) here instead.
    _client.setInsecure();
    _client.setTimeout(10000);
    {
      String k = apiKey();
      if (k.length() == 0 || k.startsWith("sk-ant-REPLACE"))
        Serial.println(F("[Claude] ⚠ Claude API key not set — queries will 401"));
    }
    Serial.printf("[Claude] ready — POST https://%s%s (%s)\n", HOST, PATH, MODEL);
    return true;
  }

  // ── Async servicing ───────────────────────────────────────
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    if (!_busy)
      return;
    // The reader skips the HTTP headers and de-frames chunked transfer
    // encoding, so each line handed back is a clean SSE body line — a
    // chunk boundary can no longer split a data: event mid-JSON.
    _http.feed(_client);
    String line;
    while (_http.nextLine(line))
      _consumeLine(line);
    if (_http.complete() ||
        (!_client.connected() && !_client.available()))
      _finish(false);
  }

  void update() override {
    if (_busy && (millis() - _lastRxMs) > REPLY_IDLE_MS) {
      Serial.println(F("[Claude] reply timed out"));
      _finish(true);
    }
  }

  // ── Data access ───────────────────────────────────────────
  void toJson(JsonObject& o) const override {
    o["busy"]      = _busy ? 1 : 0;
    o["done"]      = _done ? 1 : 0;
    o["timed_out"] = _timedOut ? 1 : 0;
    o["model"]     = MODEL;
    o["prompt"]    = _prompt;
    o["answer"]    = _answer;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"busy", static_cast<float>(_busy ? 1 : 0), ""};
    b[1] = {"queries", static_cast<float>(_queries), ""};
    n = 2;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    if (param == "ask") {
      if (_busy) {
        Serial.println(F("[Claude] ask rejected — reply still in flight"));
        return false;
      }
      if (value.length() == 0)
        return false;
      return _start(value);
    }
    if (param == "clear") {
      _reset();
      return true;
    }
    return false;
  }

  void controlSchema(JsonArray& out) const override {
    JsonObject a = out.add<JsonObject>();
    a["id"] = "ask";
    a["label"] = "Ask Claude";
    a["type"] = "text";
    a["placeholder"] = "Ask the Claude model directly (text answer, no tools)";
    JsonObject c = out.add<JsonObject>();
    c["id"] = "clear";
    c["label"] = "Clear";
    c["type"] = "button";
    c["query"] = "clear=1";
  }

 private:
  WiFiClientSecure _client;
  HttpSseReader _http;   // skips headers + de-frames chunked encoding
  String _prompt, _answer;
  bool   _busy = false, _done = false, _timedOut = false;
  uint32_t _lastRxMs = 0, _queries = 0;

  bool _start(const String& prompt) {
    if (WiFi.status() != WL_CONNECTED) {
      _answer = "[offline]";
      _finish(false);
      return false;
    }
    if (MIN_TLS_HEAP && ESP.getFreeHeap() < MIN_TLS_HEAP) {
      _answer = "[low memory — try again]";
      _finish(false);
      return false;
    }
    if (!_client.connect(HOST, PORT)) {
      _answer = "[cannot reach api.anthropic.com]";
      _finish(false);
      return false;
    }
    _prompt = prompt;
    _answer = "";
    _done = _timedOut = false;
    _http.begin();

    // Build the Messages API body with streaming enabled.
    JsonDocument doc;
    doc["model"] = MODEL;
    doc["max_tokens"] = MAX_TOKENS;
    doc["stream"] = true;
    if (strlen(SYSTEM_PROMPT))
      doc["system"] = SYSTEM_PROMPT;
    JsonArray msgs = doc["messages"].to<JsonArray>();
    JsonObject m = msgs.add<JsonObject>();
    m["role"] = "user";
    m["content"] = prompt;
    String body;
    serializeJson(doc, body);

    _client.printf("POST %s HTTP/1.1\r\n", PATH);
    _client.printf("Host: %s\r\n", HOST);
    _client.printf("x-api-key: %s\r\n", apiKey().c_str());
    _client.printf("anthropic-version: %s\r\n", API_VERSION);
    _client.print(F("Content-Type: application/json\r\n"));
    _client.print(F("Accept: text/event-stream\r\n"));
    _client.print(F("Connection: close\r\n"));
    _client.printf("Content-Length: %u\r\n\r\n", body.length());
    _client.print(body);

    _busy = true;
    _lastRxMs = millis();
    _queries++;
    Serial.printf("[Claude] query sent (%s)\n", MODEL);
    return true;
  }

  // One decoded SSE body line (headers + chunk framing already removed
  // by HttpSseReader).  Parse
  //   data: {"type":"content_block_delta","delta":{"text":"..."}}
  // and stop on message_stop.  Errors arrive as a JSON {"type":"error"}.
  void _consumeLine(String line) {
    line.trim();
    if (!line.startsWith("data:"))
      return;                       // skip "event:" lines and keep-alives
    String payload = line.substring(5);
    payload.trim();
    if (payload.length() == 0)
      return;

    JsonDocument d;
    if (deserializeJson(d, payload))
      return;
    _lastRxMs = millis();
    const char* type = d["type"] | "";

    if (strcmp(type, "content_block_delta") == 0) {
      const char* t = d["delta"]["text"] | "";
      if (t && _answer.length() < ANSWER_MAX)
        _answer += t;
    } else if (strcmp(type, "message_stop") == 0) {
      _finish(false);
    } else if (strcmp(type, "error") == 0) {
      const char* msg = d["error"]["message"] | "unknown error";
      _answer = String("[api error] ") + msg;
      _finish(false);
    }
  }

  void _finish(bool timedOut) {
    _busy = false;
    _done = true;
    _timedOut = timedOut;
    _client.stop();
  }

  void _reset() {
    _client.stop();
    _prompt = _answer = "";
    _busy = _done = _timedOut = false;
    _http.begin();
  }
};
