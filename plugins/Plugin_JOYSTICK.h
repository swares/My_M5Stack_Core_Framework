#pragma once
// ============================================================
//  Plugin_JOYSTICK.h  –  M5Stack Joystick Unit
//  STM32 I2C slave.  Address 0x52
// ============================================================
#include "../src/IDevice.h"

class Plugin_JOYSTICK : public IDevice {
 public:
  const char* name() const override { return "Joystick Unit"; }
  const char* slug() const override { return "joystick"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x52;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    return true;
  }

  void update() override {
    bus->beginTransmission(addr);
    bus->write(0x02);
    bus->endTransmission(false);
    if (bus->requestFrom(static_cast<int>(addr), 3) != 3)
      return;
    _x = bus->read();  // 0-254
    _y = bus->read();
    _btn = bus->read();  // 0=pressed
  }

  void toJson(JsonObject& o) const override {
    o["x"] = _x;
    o["x_unit"] = "";
    o["y"] = _y;
    o["y_unit"] = "";
    o["btn"] = !_btn;  // invert: 1 = pressed
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"x", static_cast<float>(_x), ""};
    b[1] = {"y", static_cast<float>(_y), ""};
    b[2] = {"btn", static_cast<float>(!_btn), ""};
    n = 3;
  }

 private:
  uint8_t _x = 127, _y = 127, _btn = 1;
};
