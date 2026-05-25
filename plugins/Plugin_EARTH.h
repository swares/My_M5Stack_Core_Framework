#pragma once
// ============================================================
//  Plugin_EARTH.h  –  M5Stack Earth Unit (soil moisture)
//  STM32-based I2C slave.  Default address 0x08
// ============================================================
#include "../src/IDevice.h"

class Plugin_EARTH : public IDevice {
 public:
  const char* name() const override { return "Earth Unit (Soil)"; }
  const char* slug() const override { return "earth"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x08;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    return true;
  }

  void update() override {
    uint8_t d[2] = {0};
    // Register 0x00 = 2-byte moisture (raw ADC)
    bus->beginTransmission(addr);
    bus->write(0x00);
    bus->endTransmission(false);
    if (bus->requestFrom(static_cast<int>(addr), 2) == 2) {
      d[0] = bus->read();
      d[1] = bus->read();
      _raw = static_cast<uint16_t>((d[0] << 8) | d[1]);
      _pct = constrain(_raw / 40.95f, 0.0f, 100.0f);
    }
  }

  void toJson(JsonObject& o) const override {
    o["moisture_raw"] = _raw;
    o["moisture"] = _pct;
    o["moisture_unit"] = "%";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"moisture", _pct, "%"};
    n = 1;
  }

 private:
  uint16_t _raw = 0;
  float _pct = 0;
};
