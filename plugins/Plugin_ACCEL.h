#pragma once
// ============================================================
//  Plugin_ACCEL.h  –  M5Stack Accel Unit (ADXL345)
//  Addresses: 0x53 (SDO=VCC) or 0x1D (SDO=GND)
// ============================================================
#include "../src/IDevice.h"

class Plugin_ACCEL : public IDevice {
 public:
  const char* name() const override { return "Accel Unit (ADXL345)"; }
  const char* slug() const override { return "accel"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x53;
    buf[1] = 0x1D;
    n = 2;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    uint8_t id = regRead8(0x00);
    if (id != 0xE5)
      return false;
    regWrite(0x2D, 0x08);  // POWER_CTL: measure
    regWrite(0x31, 0x0B);  // DATA_FORMAT: ±16g full res
    return true;
  }

  void update() override {
    uint8_t d[6] = {0};
    regRead(0x32, d, 6);
    _x = static_cast<int16_t>(d[0] | (d[1] << 8)) * 0.0039f;
    _y = static_cast<int16_t>(d[2] | (d[3] << 8)) * 0.0039f;
    _z = static_cast<int16_t>(d[4] | (d[5] << 8)) * 0.0039f;
  }

  void toJson(JsonObject& o) const override {
    o["ax"] = _x;
    o["ax_unit"] = "g";
    o["ay"] = _y;
    o["ay_unit"] = "g";
    o["az"] = _z;
    o["az_unit"] = "g";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"ax", _x, "g"};
    b[1] = {"ay", _y, "g"};
    b[2] = {"az", _z, "g"};
    n = 3;
  }

 private:
  float _x = 0, _y = 0, _z = 0;
};
