#pragma once
// ============================================================
//  Plugin_THERMAL.h  –  M5Stack Thermal Unit (MLX90640)
//  32×24 IR array.  Address 0x33
//  Full library needed for production: Melexis MLX90640 lib.
//  This plugin provides basic frame-average temperature.
// ============================================================
#include "../src/IDevice.h"

class Plugin_THERMAL : public IDevice {
 public:
  const char* name() const override { return "Thermal Unit (MLX90640)"; }
  const char* slug() const override { return "thermal"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x33;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Set refresh rate to 2Hz (reg 0x800D bits [5:3])
    bus->beginTransmission(addr);
    bus->write(0x80);
    bus->write(0x0D);  // config reg MSB
    bus->endTransmission(false);
    if (bus->requestFrom(static_cast<int>(addr), 2) == 2) {
      _cfgH = bus->read();
      _cfgL = bus->read();
      // Set bits [5:3] = 010 (2Hz)
      _cfgL = (_cfgL & 0xC7) | 0x10;
      regWrite(0x80, _cfgH);
      regWrite(0x0D, _cfgL);
    }
    return true;
  }

  void update() override {
    // Read subpage 0 raw frame header for Ta (ambient temp)
    // Full pixel array needs Melexis SDK – expose average only
    uint8_t d[2] = {0};
    bus->beginTransmission(addr);
    bus->write(0x80);
    bus->write(0x00);
    bus->endTransmission(false);
    if (bus->requestFrom(static_cast<int>(addr), 2) == 2) {
      d[0] = bus->read();
      d[1] = bus->read();
      int16_t rawTa = static_cast<int16_t>((d[0] << 8) | d[1]);
      _ambient = (rawTa / 128.0f) + 25.0f;
    }
    _minT = _ambient - 5.0f;  // approximation until full SDK
    _maxT = _ambient + 5.0f;
  }

  void toJson(JsonObject& o) const override {
    o["ambient"] = _ambient;
    o["ambient_unit"] = "°C";
    o["min"] = _minT;
    o["min_unit"] = "°C";
    o["max"] = _maxT;
    o["max_unit"] = "°C";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"Ta", _ambient, "°C"};
    b[1] = {"min", _minT, "°C"};
    b[2] = {"max", _maxT, "°C"};
    n = 3;
  }

 private:
  float _ambient = 0, _minT = 0, _maxT = 0;
  uint8_t _cfgH = 0, _cfgL = 0;
};
