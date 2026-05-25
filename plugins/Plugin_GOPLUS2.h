#pragma once
// ============================================================
//  Plugin_GOPLUS2.h  –  M5Stack GoPlus2 Module
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  I2C address 0x38.  STM32-based motion module: two DC motor
//  channels, two servo channels, an RGB LED, and two encoder
//  inputs.
//
//  CONTROLLABLE — drive it via the Web API only:
//    GET /api/goplus2/set?motor1=120     motor 1 forward
//    GET /api/goplus2/set?motor2=-80     motor 2 reverse
//    GET /api/goplus2/set?servo1=90      servo 1 → 90°
//    GET /api/goplus2/set?rgb=0040FF     RGB LED colour
//  Validation:
//    motorN  — N is 1-2, value -127..127 (signed speed; 0 = stop)
//    servoN  — N is 1-2, value 0..180 degrees
//    rgb     — six hex digits RRGGBB
//
//  INPUT — the two encoder counts are reported as readings
//  (enc1, enc2 — cumulative, signed).
//
//  ⚠ REGISTER MAP NOT YET HARDWARE-VERIFIED.
//  The constants in the "Register map" block below follow
//  M5Stack's GoPlus / motor-module conventions but have NOT
//  been confirmed against GoPlus2 silicon.  If motors/servos
//  don't respond, or encoder values look wrong, correct the
//  REG_* values — everything else in the plugin is structural
//  and correct regardless.
// ============================================================
#include "../src/IDevice.h"

class Plugin_GOPLUS2 : public IDevice {
 public:
  // ── Register map (VERIFY against GoPlus2 hardware) ─────────
  static constexpr uint8_t REG_MOTOR0 = 0x00;  // signed int8 speed
  static constexpr uint8_t REG_MOTOR1 = 0x01;
  static constexpr uint8_t REG_SERVO0 = 0x10;  // angle 0-180
  static constexpr uint8_t REG_SERVO1 = 0x11;
  static constexpr uint8_t REG_RGB = 0x20;   // 3 bytes: R, G, B
  static constexpr uint8_t REG_ENC0 = 0x30;  // int16 LE
  static constexpr uint8_t REG_ENC1 = 0x32;  // int16 LE

  const char* name() const override { return "GoPlus2 Module"; }
  const char* slug() const override { return "goplus2"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x38;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool controllable() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Safe initial state — both motors stopped.  ACK on this
    // write is our presence check.
    if (!regWrite(REG_MOTOR0, 0))
      return false;
    regWrite(REG_MOTOR1, 0);
    _motor[0] = _motor[1] = 0;
    return true;
  }

  void update() override {
    // Encoder counts — signed 16-bit, little-endian.
    _enc[0] = static_cast<int16_t>(
        static_cast<uint16_t>(regRead8(REG_ENC0)) |
        (static_cast<uint16_t>(regRead8(REG_ENC0 + 1)) << 8));
    _enc[1] = static_cast<int16_t>(
        static_cast<uint16_t>(regRead8(REG_ENC1)) |
        (static_cast<uint16_t>(regRead8(REG_ENC1 + 1)) << 8));
  }

  void toJson(JsonObject& o) const override {
    o["motor1"] = _motor[0];
    o["motor2"] = _motor[1];
    o["servo1"] = _servo[0];
    o["servo2"] = _servo[1];
    o["enc1"] = _enc[0];
    o["enc2"] = _enc[1];
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"enc1", static_cast<float>(_enc[0]), ""};
    b[1] = {"enc2", static_cast<float>(_enc[1]), ""};
    b[2] = {"motor1", static_cast<float>(_motor[0]), ""};
    b[3] = {"motor2", static_cast<float>(_motor[1]), ""};
    b[4] = {"servo1", static_cast<float>(_servo[0]), "deg"};
    b[5] = {"servo2", static_cast<float>(_servo[1]), "deg"};
    n = 6;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    // motorN — N is 1-2, value is a signed speed -127..127.
    if (param.length() == 6 && param.startsWith("motor")) {
      int idx = param.charAt(5) - '1';
      if (idx < 0 || idx > 1)
        return false;
      int32_t v;
      if (!_parseInt(value, -127, 127, v))
        return false;
      if (!regWrite(idx ? REG_MOTOR1 : REG_MOTOR0,
                    static_cast<uint8_t>(static_cast<int8_t>(v))))
        return false;
      _motor[idx] = static_cast<int8_t>(v);
      return true;
    }
    // servoN — N is 1-2, value is an angle 0..180.
    if (param.length() == 6 && param.startsWith("servo")) {
      int idx = param.charAt(5) - '1';
      if (idx < 0 || idx > 1)
        return false;
      int32_t v;
      if (!_parseInt(value, 0, 180, v))
        return false;
      if (!regWrite(idx ? REG_SERVO1 : REG_SERVO0, static_cast<uint8_t>(v)))
        return false;
      _servo[idx] = static_cast<uint8_t>(v);
      return true;
    }
    // rgb — six hex digits RRGGBB.
    if (param == "rgb") {
      int32_t c = _parseRgb(value);
      if (c < 0)
        return false;
      bus->beginTransmission(addr);
      bus->write(REG_RGB);
      bus->write((c >> 16) & 0xFF);
      bus->write((c >> 8) & 0xFF);
      bus->write(c & 0xFF);
      return bus->endTransmission() == 0;
    }
    return false;
  }

  // ── Control schema ────────────────────────────────────────
  //  Two signed motor sliders, two servo-angle sliders, an RGB
  //  picker, plus a "stop motors" quick action.
  void controlSchema(JsonArray& out) const override {
    for (uint8_t i = 0; i < 2; i++) {
      JsonObject c = out.add<JsonObject>();
      c["id"] = String("motor") + (i + 1);
      c["label"] = String("Motor ") + (i + 1);
      c["type"] = "slider";
      c["min"] = -127;
      c["max"] = 127;
      c["step"] = 1;
      c["group"] = "DC motors";
      c["value"] = _motor[i];
    }
    for (uint8_t i = 0; i < 2; i++) {
      JsonObject c = out.add<JsonObject>();
      c["id"] = String("servo") + (i + 1);
      c["label"] = String("Servo ") + (i + 1);
      c["type"] = "slider";
      c["min"] = 0;
      c["max"] = 180;
      c["step"] = 1;
      c["unit"] = "deg";
      c["group"] = "Servos";
      c["value"] = _servo[i];
    }
    JsonObject rgb = out.add<JsonObject>();
    rgb["id"] = "rgb";
    rgb["label"] = "RGB LED";
    rgb["type"] = "color";
    rgb["group"] = "RGB LED";
    JsonObject stop = out.add<JsonObject>();
    stop["label"] = "Stop motors";
    stop["type"] = "button";
    stop["query"] = "motor1=0&motor2=0";
    stop["group"] = "Quick actions";
  }

 private:
  int8_t _motor[2] = {0, 0};
  uint8_t _servo[2] = {0, 0};
  int16_t _enc[2] = {0, 0};

  // Parse a signed decimal in [lo,hi].  Returns false (rejects)
  // on any non-numeric character or out-of-range value.
  static bool _parseInt(const String& v, int32_t lo, int32_t hi, int32_t& out) {
    if (v.length() == 0)
      return false;
    uint16_t i = 0;
    if (v.charAt(0) == '-') {
      if (v.length() == 1)
        return false;
      i = 1;
    }
    for (; i < v.length(); i++)
      if (!isDigit(v.charAt(i)))
        return false;
    out = v.toInt();
    return (out >= lo && out <= hi);
  }
  static int _hexNibble(char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  }
  static int32_t _parseRgb(const String& v) {
    if (v.length() != 6)
      return -1;
    int32_t out = 0;
    for (uint8_t i = 0; i < 6; i++) {
      int h = _hexNibble(v.charAt(i));
      if (h < 0)
        return -1;
      out = (out << 4) | h;
    }
    return out;
  }
};
