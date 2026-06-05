#pragma once
// ============================================================
//  PinDevice_Buzzer.h  –  M5Stack Buzzer Unit   [non-I2C]
//
//  A magnetic buzzer driven by a PWM square wave.  Uses the ESP32
//  LEDC peripheral directly (no external library).  Plug into a
//  GPIO/PWM-capable Grove port and pass the signal pin:
//      fw.addPlugin(new PinDevice_Buzzer(26));
//
//  CONTROLLABLE — Web API only:
//    GET /api/buzzer/set?freq=2000     set tone frequency (Hz)
//    GET /api/buzzer/set?state=on      sound continuously
//    GET /api/buzzer/set?state=off     silence
//    GET /api/buzzer/set?beep=150      sound for 150 ms, then stop
//  Validation:
//    freq  — 20..20000 Hz
//    state — 0/1/on/off
//    beep  — 1..10000 ms
//  Readings:  freq, playing.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Buzzer : public IPinDevice {
 public:
  explicit PinDevice_Buzzer(uint8_t signalPin) : _pin(signalPin) {}

  const char* name() const override { return "Buzzer Unit"; }
  const char* slug() const override { return "buzzer"; }
  bool controllable() const override { return true; }

  // fastPoll gives a timed beep a crisp cut-off instead of the
  // coarse POLL_MS granularity update() would impose.
  bool wantsFastPoll() const override { return true; }

  bool beginPins() override {
    // Attach the pin to LEDC.  The initial freq is a placeholder;
    // ledcWriteTone() sets the real frequency on each play.
    if (!ledcAttach(_pin, 2000, 10))
      return false;
    ledcWriteTone(_pin, 0);  // start silent
    return true;
  }

  void update() override {}

  void fastPoll() override {
    // End a timed beep once its deadline passes (millis-wrap safe).
    if (_beepUntil && static_cast<int32_t>(millis() - _beepUntil) >= 0) {
      _beepUntil = 0;
      _silence();
    }
  }

  void toJson(JsonObject& o) const override {
    o["freq"] = _freq;
    o["playing"] = _playing ? 1 : 0;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"freq", static_cast<float>(_freq), "Hz"};
    b[1] = {"playing", static_cast<float>(_playing ? 1 : 0), ""};
    n = 2;
  }

  bool command(const String& param, const String& value) override {
    if (param == "freq") {
      int32_t v;
      if (!_parseInt(value, 20, 20000, v))
        return false;
      _freq = static_cast<uint16_t>(v);
      if (_playing)
        ledcWriteTone(_pin, _freq);  // retune live
      return true;
    }
    if (param == "state") {
      int on = _parseBool(value);
      if (on < 0)
        return false;
      _beepUntil = 0;  // cancel any beep
      if (on) {
        _sound();
      } else {
        _silence();
      }
      return true;
    }
    if (param == "beep") {
      int32_t ms;
      if (!_parseInt(value, 1, 10000, ms))
        return false;
      _sound();
      _beepUntil = millis() + static_cast<uint32_t>(ms);
      if (_beepUntil == 0)
        _beepUntil = 1;  // 0 means "no beep"
      return true;
    }
    return false;
  }

  // ── Control schema ────────────────────────────────────────
  void controlSchema(JsonArray& out) const override {
    JsonObject f = out.add<JsonObject>();
    f["id"] = "freq";
    f["label"] = "Frequency";
    f["type"] = "slider";
    f["min"] = 20;
    f["max"] = 20000;
    f["step"] = 10;
    f["unit"] = "Hz";
    f["group"] = "Tone";
    f["value"] = _freq;

    JsonObject s = out.add<JsonObject>();
    s["id"] = "state";
    s["label"] = "Sound continuously";
    s["type"] = "toggle";
    s["group"] = "Tone";
    s["value"] = _playing ? 1 : 0;

    JsonObject b = out.add<JsonObject>();
    b["label"] = "Beep (150 ms)";
    b["type"] = "button";
    b["query"] = "beep=150";
    b["group"] = "Quick actions";
  }

 private:
  uint8_t _pin;
  uint16_t _freq = 2000;
  bool _playing = false;
  uint32_t _beepUntil = 0;  // millis() deadline; 0 = no timed beep

  void _sound() {
    ledcWriteTone(_pin, _freq);
    _playing = true;
  }
  void _silence() {
    ledcWriteTone(_pin, 0);
    _playing = false;
  }

  // Thin adapters over the shared cmd:: validators (src/CmdParse.h).
  static int _parseBool(const String& v) { return cmd::parseBool(v); }
  static bool _parseInt(const String& v, int32_t lo, int32_t hi, int32_t& out) {
    return cmd::parseInt(v, lo, hi, out);
  }
};
