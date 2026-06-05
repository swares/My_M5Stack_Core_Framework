#pragma once
// ============================================================
//  NetDevice_ClaudeAPI_History.h  –  Claude Messages API with
//  multi-turn conversation memory   [NET]
//
//  Drop-in replacement for NetDevice_ClaudeAPI.h that fixes the
//  "Claude asks a clarifying question and the framework forgets it"
//  problem.  The original built a fresh one-message body every turn,
//  so every `ask` was a brand-new conversation with no memory — a
//  clarifying question was a dead end.  This version keeps a rolling
//  message history and resends it, so a follow-up actually continues
//  the same conversation.
//
//  To swap in: register this instead of NetDevice_ClaudeAPI:
//      fw.addPlugin(new NetDevice_ClaudeAPI_History());
//  Same slug ("claude"), same Web API, same dashboard panel.
//
//  ── What changed vs. the original ───────────────────────────
//    1. _hist holds the whole conversation (user/assistant/user…).
//       _start() appends the new user turn and sends the FULL array,
//       so Claude sees the clarifying question it asked AND the
//       user's reply to it.
//    2. On finish, the assistant's reply is appended to _hist, so the
//       next turn carries it forward.
//    3. stop_reason is captured from the streamed message_delta and
//       published, so the UI can tell end_turn from max_tokens
//       (truncated).  A trailing-"?" heuristic sets `awaiting` purely
//       as a UI hint — functionally, every turn is continuable now.
//    4. "clear" wipes the history (starts a NEW conversation).  This
//       is the ONLY way to drop context, so wire a Clear/New button.
//    5. History is bounded two ways (CLAUDE_HISTORY_MAX_MSGS and
//       CLAUDE_HISTORY_MAX_CHARS) and trimmed from the front IN PAIRS,
//       so the array always stays user-first and alternating — both
//       required by the Messages API — and never blows the heap or
//       the token budget on a desk gadget.
//
//  Still MODEL, not AGENT: text in, text out.  No filesystem/tools.
//  Codebase work still has to go to Claude Code on the Pi via
//  NetDevice_Router.  See NetDevice_ClaudeAPI.h for the full spiel.
// ============================================================
#include "../src/IPinDevice.h"
#include "../src/Config.h"     // CLAUDE_* settings
#include "../src/Settings.h"   // runtime Claude API key (approach B)
#include "../src/HttpSse.h"    // header-skip + chunked de-framing for the SSE body
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <vector>

// ── History tunables (override in Config.h if you want) ───────
//  Total messages kept (user + assistant combined).  Must be EVEN so
//  trimming-in-pairs keeps the array user-first + alternating.
#ifndef CLAUDE_HISTORY_MAX_MSGS
#define CLAUDE_HISTORY_MAX_MSGS 8
#endif
//  Total characters across all kept messages.  A rough proxy for the
//  token budget you're willing to resend each turn; also caps heap.
#ifndef CLAUDE_HISTORY_MAX_CHARS
#define CLAUDE_HISTORY_MAX_CHARS 4000
#endif

class NetDevice_ClaudeAPI_History : public IPinDevice {
 public:
  NetDevice_ClaudeAPI_History() {}

  // ── Tunables (defined in Config.h — edit them there) ──────
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
    _client.setInsecure();
    _client.setTimeout(10000);
    {
      String k = apiKey();
      if (k.length() == 0 || k.startsWith("sk-ant-REPLACE"))
        Serial.println(F("[Claude] ⚠ Claude API key not set — queries will 401"));
    }
    Serial.printf("[Claude] ready — POST https://%s%s (%s), history up to %d msgs\n",
                  HOST, PATH, MODEL, CLAUDE_HISTORY_MAX_MSGS);
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
    o["busy"]        = _busy ? 1 : 0;
    o["done"]        = _done ? 1 : 0;
    o["timed_out"]   = _timedOut ? 1 : 0;
    o["model"]       = MODEL;
    o["prompt"]      = _prompt;     // last user turn (for display)
    o["answer"]      = _answer;     // current assistant reply (streaming)
    o["turns"]       = static_cast<uint16_t>(_hist.size());
    o["stop_reason"] = _stopReason; // end_turn | max_tokens | …
    o["awaiting"]    = _awaiting ? 1 : 0;  // UI hint: reply looked like a question
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"busy", static_cast<float>(_busy ? 1 : 0), ""};
    b[1] = {"queries", static_cast<float>(_queries), ""};
    b[2] = {"turns", static_cast<float>(_hist.size()), ""};
    n = 3;
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
    if (param == "clear") {       // start a NEW conversation
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
    a["placeholder"] = "Continues the conversation — reply to follow-ups inline";
    JsonObject c = out.add<JsonObject>();
    c["id"] = "clear";
    c["label"] = "New chat";
    c["type"] = "button";
    c["query"] = "clear=1";
  }

 private:
  struct Turn { String role; String text; };

  WiFiClientSecure  _client;
  HttpSseReader     _http;        // skips headers + de-frames chunked encoding
  std::vector<Turn> _hist;        // the whole conversation so far
  String _prompt, _answer, _stopReason;
  bool   _busy = false, _done = false, _timedOut = false;
  bool   _awaiting = false;
  uint32_t _lastRxMs = 0, _queries = 0;

  // Drop oldest turns until within both budgets.  Remove in PAIRS so
  // the surviving array stays user-first and strictly alternating —
  // the Messages API rejects anything else.
  void _trim() {
    auto chars = [&]() {
      size_t t = 0;
      for (auto& m : _hist) t += m.text.length();
      return t;
    };
    while ((int)_hist.size() > CLAUDE_HISTORY_MAX_MSGS ||
           ((int)chars() > CLAUDE_HISTORY_MAX_CHARS && _hist.size() > 1)) {
      _hist.erase(_hist.begin());                 // drop oldest user turn
      if (!_hist.empty() && _hist.front().role == "assistant")
        _hist.erase(_hist.begin());               // and its assistant reply
    }
  }

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

    // Append the new user turn, then enforce the budget.
    _hist.push_back({"user", prompt});
    _trim();

    _prompt = prompt;
    _answer = "";
    _stopReason = "";
    _done = _timedOut = _awaiting = false;
    _http.begin();

    // Build the Messages API body from the FULL history.
    JsonDocument doc;
    doc["model"] = MODEL;
    doc["max_tokens"] = MAX_TOKENS;
    doc["stream"] = true;
    if (strlen(SYSTEM_PROMPT))
      doc["system"] = SYSTEM_PROMPT;
    JsonArray msgs = doc["messages"].to<JsonArray>();
    for (auto& m : _hist) {
      JsonObject o = msgs.add<JsonObject>();
      o["role"] = m.role;
      o["content"] = m.text;
    }
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
    Serial.printf("[Claude] turn %u sent (%s, %u msgs)\n",
                  _queries, MODEL, (unsigned)_hist.size());
    return true;
  }

  // One SSE line.  Skip HTTP headers until the blank line, then parse:
  //   content_block_delta → append text
  //   message_delta       → capture stop_reason
  //   message_stop        → done
  //   error               → surface the message
  void _consumeLine(String line) {
    line.trim();
    if (!line.startsWith("data:"))
      return;
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
    } else if (strcmp(type, "message_delta") == 0) {
      const char* sr = d["delta"]["stop_reason"] | "";
      if (sr && *sr)
        _stopReason = sr;        // end_turn, max_tokens, stop_sequence…
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

    // Commit the assistant reply to history so the next turn carries
    // it.  Skip empties / error sentinels so we never poison context
    // with a bracketed status string.
    if (!timedOut && _answer.length() && !_answer.startsWith("[")) {
      _hist.push_back({"assistant", _answer});
      _trim();
      // UI hint only: a reply ending in "?" is probably a clarifying
      // question.  The conversation continues either way.
      String a = _answer;
      a.trim();
      _awaiting = a.endsWith("?");
    } else if (!_hist.empty() && _hist.back().role == "user") {
      // Turn failed before any usable reply — drop the dangling user
      // turn so history stays alternating and the retry is clean.
      _hist.pop_back();
    }
  }

  void _reset() {
    _client.stop();
    _hist.clear();
    _prompt = _answer = _stopReason = "";
    _busy = _done = _timedOut = _awaiting = false;
    _http.begin();
    Serial.println(F("[Claude] history cleared — new conversation"));
  }
};
