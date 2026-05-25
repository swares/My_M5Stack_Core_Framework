#pragma once
// ============================================================
//  Plugin_ANGLE.h  –  M5Stack Angle Unit (rotary pot, 12-bit ADC)
//  STM32 I2C slave.  Address 0x36
// ============================================================
#include "../src/IDevice.h"

class Plugin_ANGLE : public IDevice {
 public:
  const char* name() const override { return "Angle Unit (Rotary)"; }
  const char* slug() const override { return "angle"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x36;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    return true;
  }

  void update() override {
    uint8_t d[2] = {0};
    bus->beginTransmission(addr);
    bus->write(0x00);
    bus->endTransmission(false);
    if (bus->requestFrom(static_cast<int>(addr), 2) == 2) {
      d[0] = bus->read();
      d[1] = bus->read();
      _raw = static_cast<uint16_t>((d[0] | (d[1] << 8)) & 0x0FFF);
      _deg = _raw * 360.0f / 4096.0f;
    }
  }

  void toJson(JsonObject& o) const override {
    o["raw"] = _raw;
    o["angle"] = _deg;
    o["angle_unit"] = "°";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"angle", _deg, "°"};
    n = 1;
  }

 private:
  uint16_t _raw = 0;
  float _deg = 0;
};
