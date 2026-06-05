#pragma once
// ============================================================
//  PinDevice_Servo.h  –  Hobby servo (SG90 etc.)   [non-I2C]
//
//  A standard hobby servo driven by a 50 Hz PWM signal.  Uses the
//  ESP32 LEDC peripheral directly (no external library).  Plug
//  into a GPIO/PWM-capable Grove port and pass the signal pin:
//      fw.addPlugin(new PinDevice_Servo(26));
//
//  CONTROLLABLE — Web API only:
//    GET /api/servo/set?angle=90      move to 90°
//    GET /api/servo/set?us=1500       set pulse width directly (µs)
//  Validation:
//    angle — 0..180°  (0° → 500 µs, 180° → 2500 µs)
//    us    — 500..2500 µs
//  Readings:  angle, us.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Servo : public IPinDevice {
 public:
  explicit PinDevice_Servo(uint8_t signalPin) : _pin(signalPin) {}

  const char* name() const override { return "Servo (SG90)"; }
  const char* slug() const override { return "servo"; }
  bool controllable() const override { return true; }

  bool beginPins() override {
    // 50 Hz (20 ms period), 16-bit duty resolution.
    if (!ledcAttach(_pin, 50, PWM_BITS))
      return false;
    _writeUs(1500);  // park at centre (≈90°)
    return true;
  }

  void update() override {}

  void toJson(JsonObject& o) const override {
    o["angle"] = _angle;
    o["us"] = _us;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"angle", static_cast<float>(_angle), "deg"};
    b[1] = {"us", static_cast<float>(_us), "us"};
    n = 2;
  }

  bool command(const String& param, const String& value) override {
    if (param == "angle") {
      int32_t v;
      if (!_parseInt(value, 0, 180, v))
        return false;
      _writeUs(static_cast<uint16_t>(500 + (v * 2000L) / 180));
      _angle = static_cast<uint16_t>(v);
      return true;
    }
    if (param == "us") {
      int32_t v;
      if (!_parseInt(value, 500, 2500, v))
        return false;
      _writeUs(static_cast<uint16_t>(v));
      _angle =
          static_cast<uint16_t>(((static_cast<int32_t>(_us) - 500) * 180L) /
                                2000);
      return true;
    }
    return false;
  }

  // ── Control schema ────────────────────────────────────────
  void controlSchema(JsonArray& out) const override {
    JsonObject c = out.add<JsonObject>();
    c["id"] = "angle";
    c["label"] = "Angle";
    c["type"] = "slider";
    c["min"] = 0;
    c["max"] = 180;
    c["step"] = 1;
    c["unit"] = "deg";
    c["value"] = _angle;
  }

 private:
  static constexpr uint8_t PWM_BITS = 16;  // LEDC duty resolution
  uint8_t _pin;
  uint16_t _us = 1500;   // last pulse width (µs)
  uint16_t _angle = 90;  // last angle (°)

  void _writeUs(uint16_t us) {
    // duty counts = (us / 20000 µs) · 2^PWM_BITS
    uint32_t maxCount = (1UL << PWM_BITS) - 1;
    uint32_t duty = (static_cast<uint32_t>(us) * maxCount) / 20000UL;
    ledcWrite(_pin, duty);
    _us = us;
  }
  // Thin adapter over the shared cmd:: validator (src/CmdParse.h).
  static bool _parseInt(const String& v, int32_t lo, int32_t hi, int32_t& out) {
    return cmd::parseInt(v, lo, hi, out);
  }
};
