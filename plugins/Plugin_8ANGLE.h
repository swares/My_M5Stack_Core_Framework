#pragma once
// ============================================================
//  Plugin_8ANGLE.h  –  M5Stack 8Angle Module
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  I2C address 0x43.  STM32-based; eight potentiometer dials,
//  a toggle switch, and eight user RGB LEDs.
//
//  INPUT — the eight dials (dial0-dial7, each 0-255) and the
//  toggle switch are reported as readings.
//
//  CONTROLLABLE — the eight LEDs are driven via the Web API:
//    GET /api/8angle/set?led0=FF0000     LED 0 → red
//    GET /api/8angle/set?led3=00FF40     LED 3 → green-ish
//  ledN: N is 0-7, value is exactly six hex digits (RRGGBB).
//  Anything else is rejected.
//
//  Register map (per M5Stack's 8Angle convention):
//    0x10 + ch   8-bit dial value   (ch 0-7, one byte each)
//    0x20        toggle switch      (one byte, 0/1)
//    0x30 + 4·n  RGB LED n          (bytes: R, G, B, brightness)
// ============================================================
#include "../src/IDevice.h"

class Plugin_8ANGLE : public IDevice {
 public:
  const char* name() const override { return "8Angle Module"; }
  const char* slug() const override { return "8angle"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x43;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool controllable() const override { return true; }  // the LEDs

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Presence check: the 8-bit dial block must read back.
    uint8_t tmp[8];
    return regRead(0x10, tmp, 8);
  }

  void update() override {
    regRead(0x10, _dial, 8);           // eight 8-bit dial values
    _switch = regRead8(0x20) ? 1 : 0;  // toggle switch
  }

  void toJson(JsonObject& o) const override {
    for (uint8_t i = 0; i < 8; i++)
      o[String("dial") + i] = _dial[i];
    o["switch"] = _switch;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    static const char* k[8] = {"dial0", "dial1", "dial2", "dial3",
                               "dial4", "dial5", "dial6", "dial7"};
    for (uint8_t i = 0; i < 8; i++)
      b[i] = {k[i], static_cast<float>(_dial[i]), ""};
    b[8] = {"switch", static_cast<float>(_switch), ""};
    n = 9;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    // ledN — N is 0-7, value is six hex digits RRGGBB.
    if (param.startsWith("led") && param.length() == 4) {
      char c = param.charAt(3);
      if (c < '0' || c > '7')
        return false;  // bad index
      int32_t rgb = _parseRgb(value);
      if (rgb < 0)
        return false;  // not RRGGBB
      uint8_t idx = c - '0';
      return _setLed(idx, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
    return false;  // unknown param
  }

  // ── Control schema ────────────────────────────────────────
  //  One colour picker per RGB LED.  The LEDs are write-only —
  //  no value is reported, so each widget starts at black.
  void controlSchema(JsonArray& out) const override {
    for (uint8_t i = 0; i < 8; i++) {
      JsonObject c = out.add<JsonObject>();
      c["id"] = String("led") + i;
      c["label"] = String("LED ") + i;
      c["type"] = "color";
      c["group"] = "RGB LEDs";
    }
    JsonObject off = out.add<JsonObject>();
    off["label"] = "All LEDs off";
    off["type"] = "button";
    off["group"] = "Quick actions";
    String q;
    for (uint8_t i = 0; i < 8; i++) {
      if (i)
        q += '&';
      q += "led";
      q += i;
      q += "=000000";
    }
    off["query"] = q;
  }

 private:
  uint8_t _dial[8] = {0};
  uint8_t _switch = 0;

  bool _setLed(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    bus->beginTransmission(addr);
    bus->write(0x30 + 4 * idx);
    bus->write(r);
    bus->write(g);
    bus->write(b);
    bus->write(100);  // brightness 0-100, full
    return bus->endTransmission() == 0;
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
  // Exactly six hex digits → 0x000000-0xFFFFFF, else -1.
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
