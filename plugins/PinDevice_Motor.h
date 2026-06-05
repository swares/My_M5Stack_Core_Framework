#pragma once
// ============================================================
//  PinDevice_Motor.h  –  M5Stack DC Motor Unit           [non-I2C]
//
//  A small brushed DC motor driven by a PWM line — covers the
//  Vibration Motor Unit (N20) and the Mini Fan Unit (N20 motor with
//  a propeller).  The motor's drive transistor is switched by a PWM
//  square wave and the duty cycle sets the speed.  Uses the ESP32
//  LEDC peripheral directly (no external library).
//
//      fw.addPlugin(new PinDevice_Motor(26));
//
//  CONTROLLABLE — Web API only:
//    GET /api/motor/set?speed=0..100   PWM duty, percent (0 = stop)
//  Readings:  speed (0-100 %), running (1 = speed > 0).
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Motor : public IPinDevice {
 public:
  // PWM carrier for the LEDC channel: 20 kHz (inaudible), 8-bit duty.
  static constexpr uint32_t PWM_FREQ = 20000;
  static constexpr uint8_t PWM_BITS = 8;

  explicit PinDevice_Motor(uint8_t signalPin) : _pin(signalPin) {}

  const char* name() const override { return "DC Motor Unit"; }
  const char* slug() const override { return "motor"; }
  bool controllable() const override { return true; }

  bool beginPins() override {
    if (!ledcAttach(_pin, PWM_FREQ, PWM_BITS))
      return false;
    ledcWrite(_pin, 0);  // start stopped
    return true;
  }

  void update() override {}

  void toJson(JsonObject& o) const override {
    o["speed"] = _speed;
    o["running"] = (_speed > 0) ? 1 : 0;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"speed", static_cast<float>(_speed), "%"};
    b[1] = {"running", static_cast<float>(_speed > 0 ? 1 : 0), ""};
    n = 2;
  }

  bool command(const String& param, const String& value) override {
    if (param == "speed") {
      int32_t v;
      if (!_parsePct(value, v))
        return false;
      _speed = static_cast<uint8_t>(v);
      // Map 0-100 % onto the 8-bit (0-255) LEDC duty range.
      ledcWrite(_pin, static_cast<uint32_t>(_speed) * 255 / 100);
      return true;
    }
    return false;
  }

  void controlSchema(JsonArray& out) const override {
    JsonObject s = out.add<JsonObject>();
    s["id"] = "speed";
    s["label"] = "Speed";
    s["type"] = "slider";
    s["min"] = 0;
    s["max"] = 100;
    s["step"] = 5;
    s["unit"] = "%";
    s["value"] = _speed;
  }

 private:
  uint8_t _pin;
  uint8_t _speed = 0;  // 0-100 %

  // A decimal string in 0..100 -> that value; anything else fails.
  static bool _parsePct(const String& v, int32_t& out) {
    return cmd::parseInt(v, 0, 100, out);
  }
};
