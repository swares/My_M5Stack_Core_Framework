#pragma once
// ============================================================
//  PinDevice_Relay.h  –  M5Stack Relay Unit (Mini, 3 A)  [non-I2C]
//
//  A single mechanical relay driven by one GPIO line.  Plug into
//  a GPIO-capable Grove port and pass the control pin:
//      fw.addPlugin(new PinDevice_Relay(26));
//  Most M5Stack relay units are active-HIGH (pin HIGH = closed);
//  pass activeHigh=false if yours is wired inverted.
//
//  CONTROLLABLE — Web API only:
//    GET /api/relay/set?state=on      close the relay
//    GET /api/relay/set?state=off     open the relay
//  Reading:  state — 1 = closed/on, 0 = open/off.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Relay : public IPinDevice {
 public:
  explicit PinDevice_Relay(uint8_t controlPin, bool activeHigh = true)
      : _pin(controlPin), _activeHigh(activeHigh) {}

  const char* name() const override { return "Relay Unit (Mini)"; }
  const char* slug() const override { return "relay"; }
  bool controllable() const override { return true; }

  bool beginPins() override {
    pinMode(_pin, OUTPUT);
    _apply(false);  // safe initial state — relay open
    return true;
  }

  void update() override {}  // output device — nothing to poll

  void toJson(JsonObject& o) const override {
    o["state"] = _on ? 1 : 0;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"state", static_cast<float>(_on ? 1 : 0), ""};
    n = 1;
  }

  bool command(const String& param, const String& value) override {
    if (param == "state") {
      int on = _parseBool(value);
      if (on < 0)
        return false;
      _apply(on == 1);
      return true;
    }
    return false;
  }

  // ── Control schema ────────────────────────────────────────
  void controlSchema(JsonArray& out) const override {
    JsonObject c = out.add<JsonObject>();
    c["id"] = "state";
    c["label"] = "Relay closed";
    c["type"] = "toggle";
    c["value"] = _on ? 1 : 0;
  }

 private:
  uint8_t _pin;
  bool _activeHigh;
  bool _on = false;

  void _apply(bool on) {
    digitalWrite(_pin, (on == _activeHigh) ? HIGH : LOW);
    _on = on;
  }
  // Thin adapter over the shared cmd:: validator (src/CmdParse.h).
  static int _parseBool(const String& v) { return cmd::parseBool(v); }
};
