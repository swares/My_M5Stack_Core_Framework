#pragma once
// ============================================================
//  Plugin_TVOC.h  –  M5Stack TVOC/eCO2 Unit (SGP30)
//  Address 0x58
// ============================================================
#include "../src/IDevice.h"

class Plugin_TVOC : public IDevice {
 public:
  const char* name() const override { return "TVOC Unit (SGP30)"; }
  const char* slug() const override { return "tvoc"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x58;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Init air quality measurement
    _cmd(0x2003);
    delay(20);
    return true;
  }

  void update() override {
    _cmd(0x2008);  // measure_air_quality
    delay(12);
    if (bus->requestFrom(static_cast<int>(addr), 6) != 6)
      return;
    uint8_t d[6];
    for (auto& x : d)
      x = bus->read();
    _eco2 = static_cast<uint16_t>((d[0] << 8) | d[1]);
    _tvoc = static_cast<uint16_t>((d[3] << 8) | d[4]);
  }

  void toJson(JsonObject& o) const override {
    o["eCO2"] = _eco2;
    o["eCO2_unit"] = "ppm";
    o["TVOC"] = _tvoc;
    o["TVOC_unit"] = "ppb";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"eCO2", static_cast<float>(_eco2), "ppm"};
    b[1] = {"TVOC", static_cast<float>(_tvoc), "ppb"};
    n = 2;
  }

 private:
  uint16_t _eco2 = 400, _tvoc = 0;

  void _cmd(uint16_t c) {
    bus->beginTransmission(addr);
    bus->write(c >> 8);
    bus->write(c & 0xFF);
    bus->endTransmission();
  }
};
