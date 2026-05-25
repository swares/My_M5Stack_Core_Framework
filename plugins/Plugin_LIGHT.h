#pragma once
// ============================================================
//  Plugin_LIGHT.h  –  M5Stack Light Unit (BH1750FVI)
//  Addresses: 0x23 (ADDR low) or 0x5C (ADDR high)
// ============================================================
#include "../src/IDevice.h"

class Plugin_LIGHT : public IDevice {
 public:
  const char* name() const override { return "Light Unit (BH1750)"; }
  const char* slug() const override { return "light"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x23;
    buf[1] = 0x5C;
    n = 2;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Power on
    bus->beginTransmission(addr);
    bus->write(0x01);
    bus->endTransmission();
    delay(10);
    // Continuous high-res mode
    bus->beginTransmission(addr);
    bus->write(0x10);
    bus->endTransmission();
    delay(180);
    return true;
  }

  void update() override {
    if (bus->requestFrom(static_cast<int>(addr), 2) != 2)
      return;
    uint8_t hi = bus->read(), lo = bus->read();
    _lux = ((hi << 8) | lo) / 1.2f;
  }

  void toJson(JsonObject& o) const override {
    o["lux"] = _lux;
    o["lux_unit"] = "lx";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"lux", _lux, "lx"};
    n = 1;
  }

 private:
  float _lux = 0;
};
