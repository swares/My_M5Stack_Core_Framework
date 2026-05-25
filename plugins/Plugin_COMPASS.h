#pragma once
// ============================================================
//  Plugin_COMPASS.h  –  M5Stack Compass Unit (QMC5883L)
//  3-axis magnetometer.  Address 0x0D
// ============================================================
#include <math.h>
#include "../src/IDevice.h"

class Plugin_COMPASS : public IDevice {
 public:
  const char* name() const override { return "Compass Unit (QMC5883L)"; }
  const char* slug() const override { return "compass"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x0D;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Soft reset
    regWrite(0x0B, 0x01);
    delay(5);
    // Control reg 1: continuous mode, 200Hz, ±8G, OSR=512
    regWrite(0x09, 0x1D);
    return regRead8(0x0D) == 0xFF;  // chip ID
  }

  void update() override {
    // Wait for data ready
    uint8_t status = regRead8(0x06);
    if (!(status & 0x01))
      return;

    uint8_t d[6] = {0};
    regRead(0x00, d, 6);
    _x = static_cast<int16_t>(d[0] | (d[1] << 8));
    _y = static_cast<int16_t>(d[2] | (d[3] << 8));
    _z = static_cast<int16_t>(d[4] | (d[5] << 8));

    // Heading in degrees (0-360), magnetic north
    float heading =
        atan2f(static_cast<float>(_y), static_cast<float>(_x)) * 180.0f / M_PI;
    if (heading < 0)
      heading += 360.0f;
    _heading = heading;
  }

  void toJson(JsonObject& o) const override {
    o["mx"] = _x;
    o["mx_unit"] = "raw";
    o["my"] = _y;
    o["my_unit"] = "raw";
    o["mz"] = _z;
    o["mz_unit"] = "raw";
    o["heading"] = _heading;
    o["heading_unit"] = "°";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"heading", _heading, "°"};
    b[1] = {"mx", static_cast<float>(_x), "raw"};
    b[2] = {"my", static_cast<float>(_y), "raw"};
    b[3] = {"mz", static_cast<float>(_z), "raw"};
    n = 4;
  }

 private:
  int16_t _x = 0, _y = 0, _z = 0;
  float _heading = 0;
};
