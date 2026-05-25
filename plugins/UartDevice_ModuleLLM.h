#pragma once
// ============================================================
//  UartDevice_ModuleLLM.h  –  M5Stack Module LLM   [UART]
//
//  Offline-AI module (AX630C SoC) that runs a small language model
//  on-device.  It speaks M5Stack's StackFlow protocol — JSON over
//  UART at 115200 8N1 — handled here by the official M5Module-LLM
//  Arduino library.  The module stacks on the M-Bus and its UART
//  lands on the host board's Port-C pins.
//
//      fw.addPlugin(new UartDevice_ModuleLLM(Serial2, 115200));
//
//  ⚠ Needs the "M5Module-LLM" library (M5Stack) — install it, then
//  uncomment BOTH the #include and the registration line.  It
//  shares the single Port-C UART slot with the GPS / Barcode /
//  Modem devices — register at most one UART device.
//
//  ── How it works in this framework ──────────────────────────
//  This is a TEXT-CHAT integration; the module's ASR / TTS / KWS
//  voice units are not used.  It runs ASYNCHRONOUSLY so the rest
//  of the framework keeps ticking while the model is "thinking":
//    • beginUart() opens the port, verifies the module responds,
//      and loads the language model.  Model loading BLOCKS for a
//      few seconds — once, at boot.
//    • A prompt is submitted with command("ask", <text>), which
//      calls the non-blocking llm.inference() and returns at once.
//    • fastPoll() pumps the UART every loop iteration and folds
//      the streamed reply tokens into _answer, so sensor polling
//      and the web server are never stalled by a query.
//    • toJson() publishes "answer" plus "busy"/"done" flags, so
//      the dashboard chat panel polls /api/llm and shows the reply
//      as it streams in.
//
//  CONTROLLABLE — driven via the Web API only:
//    GET /api/llm/set?ask=<prompt>     start a query
//    GET /api/llm/set?clear=1          clear the last prompt/answer
//  A new query is rejected while one is still in flight.
// ============================================================
#include "../src/IUartDevice.h"
#include <M5ModuleLLM.h>

class UartDevice_ModuleLLM : public IUartDevice {
 public:
  UartDevice_ModuleLLM(HardwareSerial& port, uint32_t baud = 115200,
                       int8_t rxPin = -1, int8_t txPin = -1)
      : IUartDevice(port, baud, rxPin, txPin) {}

  // ── Tunables ──────────────────────────────────────────────
  //  SYSTEM_PROMPT — optional persona / instruction handed to the
  //  model at setup ("" = none).  MAX_TOKENS — longest reply the
  //  model will generate (the library's hard ceiling is 1023).
  static constexpr const char* SYSTEM_PROMPT = "";
  static constexpr int MAX_TOKENS = 512;
  // Abandon a reply only if the module falls SILENT this long.  This
  // is an INACTIVITY timeout, not a total-duration cap: the qwen2.5
  // model streams only a few tokens per second, so a long answer can
  // legitimately take minutes — but as long as tokens keep arriving
  // the reply is healthy.  A gap this long with no token at all means
  // the module has genuinely stalled or been lost.
  static constexpr uint32_t REPLY_IDLE_MS = 30000;
  // Ceiling on the in-RAM answer buffer.
  static constexpr uint16_t ANSWER_MAX = 4096;
  // Trace every StackFlow message addressed to our LLM unit to the
  // serial console.  Verbose while a reply streams (one line per
  // token) but invaluable when the module rejects a query — it
  // prints the module's exact error JSON.  Leave false in normal
  // use; flip to true to debug a misbehaving query.
  static constexpr bool TRACE_RX = false;
  // HardwareSerial RX buffer for the Port-C UART.  The Module LLM
  // streams JSON reply tokens in bursts; the ESP32's 256-byte
  // default buffer can overflow between fastPoll() calls when the
  // main loop is briefly busy (an HTTPS handshake, an SD write),
  // which splits a StackFlow message and drops a token.  4 KB
  // absorbs the bursts.  Applied by IUartDevice before begin().
  static constexpr size_t RX_BUFFER = 4096;

  const char* name() const override { return "Module LLM"; }
  const char* slug() const override { return "llm"; }
  bool controllable() const override { return true; }
  size_t rxBufferSize() const override { return RX_BUFFER; }

  bool beginUart() override {
    if (!_llm.begin(_port)) {
      Serial.println(F("[LLM] begin() failed"));
      return false;
    }
    // Bounded connection probe — never hang the boot waiting.
    bool up = false;
    for (uint8_t i = 0; i < 10 && !up; i++) {
      if (_llm.checkConnection())
        up = true;
      else
        delay(200);
    }
    if (!up) {
      Serial.println(F("[LLM] no response from Module LLM on Port-C"));
      return false;
    }
    // ── Clear stale StackFlow units on the module ───────────────
    //  The Module LLM (AX630C) runs its own OS and keeps running
    //  when the host ESP32 resets.  Every llm.setup() allocates a
    //  unit instance and loads the model into the AX630C's limited
    //  RAM; those units are NOT freed by an ESP32 reboot.  After a
    //  few host reboots the module's memory fills up and setup()
    //  starts failing — it returns the unit *type* ("llm") instead
    //  of an allocated instance id ("llm.1003"), and every later
    //  inference is rejected with error -4 "inference data push
    //  false" because no such unit exists.  sys.reset() restarts
    //  the module's StackFlow service and frees every unit, so a
    //  fresh setup() always has room.  This is exactly what the
    //  official M5Module-LLM examples do before llm.setup().
    Serial.println(F("[LLM] resetting module StackFlow service ..."));
    int rr = _llm.sys.reset();  // blocks until reset finishes
    if (rr != MODULE_LLM_OK)
      Serial.printf("[LLM] sys.reset() returned %d (continuing anyway)\n", rr);

    // Load the model.  This BLOCKS for a few seconds while the
    // AX630C loads weights — acceptable as a one-off at boot.
    Serial.println(F("[LLM] loading language model (takes a few seconds) ..."));
    m5_module_llm::ApiLlmSetupConfig_t cfg;
    cfg.max_token_len = MAX_TOKENS;
    if (SYSTEM_PROMPT[0] != '\0')
      cfg.prompt = SYSTEM_PROMPT;
    // model / response_format ("llm.utf-8.stream") / input are left
    // at the library defaults: streaming UTF-8 text in and out.
    _workId = _llm.llm.setup(cfg, "llm_setup");
    // A successful setup returns an allocated instance id such as
    // "llm.1003" — a numeric ".NNNN" suffix.  A bare "llm" means
    // setup() got no success response.  That is EITHER a hard error
    // (model missing / won't load) OR simply that the model load
    // outran the library's internal setup() timeout.  Watch the
    // module either way: dump every message it sends (so the real
    // cause is on the console, not guessed) and, if a late setup
    // success arrives, adopt its allocated work_id.
    if (!_workIdValid()) {
      Serial.printf(
          "[LLM] setup() returned work_id='%s' (not an "
          "allocated instance) — watching the module up to "
          "20 s for the cause / a late response ...\n",
          _workId.c_str());
      uint32_t t0 = millis();
      bool hardError = false;
      while (millis() - t0 < 20000 && !_workIdValid() && !hardError) {
        _llm.update();
        for (auto& m : _llm.msg.responseMsgList) {
          Serial.printf("[LLM] setup-rx[work_id=%s]: %s\n", m.work_id.c_str(),
                        m.raw_msg.c_str());
          JsonDocument d;
          if (deserializeJson(d, m.raw_msg))
            continue;
          int ec = d["error"]["code"] | -99;
          if (m.work_id.startsWith("llm.") && ec == 0) {
            _workId = m.work_id;  // late setup success
            Serial.printf("[LLM] adopted late setup work_id=%s\n",
                          _workId.c_str());
          } else if (ec != 0 && ec != -99) {
            Serial.printf(
                "[LLM] module reported setup error code=%d "
                "— stopping the watch\n",
                ec);
            hardError = true;
          }
        }
        _llm.msg.responseMsgList.clear();
        delay(50);
      }
    }
    if (!_workIdValid()) {
      Serial.printf(
          "[LLM] model setup FAILED — final work_id='%s'. "
          "The module returned no allocated LLM instance; "
          "see the [LLM] setup-rx line(s) above for the "
          "module's own error message.\n",
          _workId.c_str());
      return false;
    }
    Serial.printf("[LLM] ready — work_id=%s\n", _workId.c_str());
    _connected = true;
    return true;
  }

  // Pump the module's UART every loop so streamed reply tokens are
  // not lost to an RX-buffer overflow at the slow poll rate.
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    if (!_connected)
      return;
    _llm.update();  // read + parse UART traffic
    for (auto& m : _llm.msg.responseMsgList) {
      // Trace EVERY response, matching or not — a work_id mismatch
      // (reply addressed to something other than our unit) would
      // otherwise look like total silence.
      if (TRACE_RX)
        Serial.printf("[LLM] rx[work_id=%s]: %s\n", m.work_id.c_str(),
                      m.raw_msg.c_str());
      if (m.work_id == _workId)
        _ingest(m.raw_msg);
    }
    _llm.msg.responseMsgList.clear();
  }

  void update() override {
    // Fail a reply only if the module has gone silent — see
    // REPLY_IDLE_MS.  A long answer that is still streaming steadily
    // refreshes _lastRxMs on every token and so never trips this.
    if (_busy && (millis() - _lastRxMs) > REPLY_IDLE_MS) {
      _busy = false;
      _timedOut = true;
      Serial.printf(
          "[LLM] reply timed out — no token from module for "
          "%u ms (%u chars received so far)\n",
          static_cast<uint32_t>(REPLY_IDLE_MS), (unsigned)_answer.length());
    }
  }

  void toJson(JsonObject& o) const override {
    o["connected"] = _connected ? 1 : 0;
    o["busy"] = _busy ? 1 : 0;
    o["done"] = _done ? 1 : 0;
    o["timed_out"] = _timedOut ? 1 : 0;
    o["prompt"] = _prompt;
    o["answer"] = _answer;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"connected", static_cast<float>(_connected ? 1 : 0), ""};
    b[1] = {"busy", static_cast<float>(_busy ? 1 : 0), ""};
    n = 2;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    if (param == "ask") {
      // Reject with a reason on the serial console — a bare "false"
      // returned to the web layer is otherwise invisible.
      if (!_connected) {
        Serial.println(F("[LLM] ask rejected — module not connected"));
        return false;
      }
      if (_busy) {
        Serial.println(F("[LLM] ask rejected — a reply is still in flight"));
        return false;
      }
      if (value.length() == 0) {
        Serial.println(F("[LLM] ask rejected — empty prompt"));
        return false;
      }
      _prompt = value;
      _answer = "";
      _done = false;
      _timedOut = false;
      int r = _llm.llm.inference(_workId, value, "llm_infer");
      Serial.printf("[LLM] inference submitted (rc=%d) — prompt: %.80s\n", r,
                    value.c_str());
      if (r != MODULE_LLM_OK) {
        // The library could not even hand the request to the module.
        Serial.printf("[LLM] inference() refused by library, rc=%d\n", r);
        _answer = String("[inference not sent — library rc=") + r + "]";
        _done = true;
        return false;
      }
      _busy = true;
      _lastRxMs = millis();  // arm the inactivity timeout
      return true;
    }
    if (param == "clear") {
      _prompt = "";
      _answer = "";
      _done = false;
      _timedOut = false;
      return true;
    }
    return false;  // unknown param
  }

 private:
  M5ModuleLLM _llm;
  String _workId;
  String _prompt, _answer;
  bool _connected = false;
  bool _busy = false, _done = false, _timedOut = false;
  uint32_t _lastRxMs = 0;  // millis() of the last message from our unit

  // A usable LLM work_id is an allocated instance like "llm.1003" —
  // it carries a numeric ".NNNN" suffix.  The bare unit type "llm"
  // means setup() never received a success response.
  bool _workIdValid() const {
    return _workId.length() > 0 && _workId.indexOf('.') > 0;
  }

  // Fold one StackFlow response message (raw JSON, addressed to our
  // LLM unit) into _answer.  Streaming replies arrive as
  //   { "data": { "delta": "<text>", "finish": <bool> }, ... }
  // a non-streaming reply would carry the whole string in "data".
  void _ingest(const String& raw) {
    JsonDocument doc;
    if (deserializeJson(doc, raw))
      return;              // not valid JSON
    _lastRxMs = millis();  // any message from our unit = reply alive
    // A non-zero error code ends the exchange.  Capture WHY — the
    // code and message go to the serial console and into _answer so
    // the dashboard chat panel shows the fault instead of "[no reply]".
    if (doc["error"]["code"].is<int>() && doc["error"]["code"].as<int>() != 0) {
      int ec = doc["error"]["code"].as<int>();
      const char* em = doc["error"]["message"] | "(no message)";
      Serial.printf("[LLM] module returned error — code=%d message=%s\n", ec,
                    em);
      _answer = String("[module error ") + ec + ": " + em + "]";
      _busy = false;
      _done = true;
      return;
    }
    JsonVariant data = doc["data"];
    if (data.is<JsonObject>()) {
      const char* delta = data["delta"];
      if (delta && _answer.length() < ANSWER_MAX)
        _answer += delta;
      if (data["finish"].as<bool>()) {
        _busy = false;
        _done = true;
        Serial.printf("[LLM] reply complete — %u chars (streamed)\n",
                      (unsigned)_answer.length());
      }
    } else if (data.is<const char*>()) {
      const char* whole = data.as<const char*>();
      if (whole)
        _answer = whole;
      _busy = false;
      _done = true;
      Serial.printf("[LLM] reply complete — %u chars (non-streamed)\n",
                    (unsigned)_answer.length());
    }
  }
};
