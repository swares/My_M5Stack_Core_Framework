#pragma once
// ============================================================
//  Plugin_COLOR.h  –  M5Stack Color Unit (TCS3472)
//  RGBC color sensor.  Address 0x29
// ============================================================
#include "../src/IDevice.h"

class Plugin_COLOR : public IDevice {
 public:
  const char* name() const override { return "Color Unit (TCS3472)"; }
  const char* slug() const override { return "color"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x29;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // ENABLE register: PON + AEN
    regWrite(0x80, 0x03);
    delay(50);
    // Integration time = 154ms, gain = 4x
    regWrite(0x81, 0xC0);
    regWrite(0x8F, 0x01);
    uint8_t id = regRead8(0x92);
    return (id == 0x44 || id == 0x4D);
  }

  void update() override {
    uint8_t d[8] = {0};
    regRead(0x94, d, 8);
    _c = static_cast<uint16_t>(d[0] | (d[1] << 8));
    _r = static_cast<uint16_t>(d[2] | (d[3] << 8));
    _g = static_cast<uint16_t>(d[4] | (d[5] << 8));
    _b = static_cast<uint16_t>(d[6] | (d[7] << 8));
    // Normalise 0-255
    if (_c > 0) {
      _rn = (_r * 255) / _c;
      _gn = (_g * 255) / _c;
      _bn = (_b * 255) / _c;
    }
  }

  void toJson(JsonObject& o) const override {
    o["clear"] = _c;
    o["red_raw"] = _r;
    o["green_raw"] = _g;
    o["blue_raw"] = _b;
    o["r"] = _rn;
    o["r_unit"] = "";
    o["g"] = _gn;
    o["g_unit"] = "";
    o["b"] = _bn;
    o["b_unit"] = "";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"R", static_cast<float>(_rn), ""};
    b[1] = {"G", static_cast<float>(_gn), ""};
    b[2] = {"B", static_cast<float>(_bn), ""};
    b[3] = {"clear", static_cast<float>(_c), ""};
    n = 4;
  }

 private:
  uint16_t _c = 0, _r = 0, _g = 0, _b = 0;
  uint8_t _rn = 0, _gn = 0, _bn = 0;
};
