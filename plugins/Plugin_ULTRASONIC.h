#pragma once
// ============================================================
//  Plugin_ULTRASONIC.h  –  M5Stack Ultrasonic Unit (I2C v2)
//  RCWL-9600 based.  Address 0x57
// ============================================================
#include "../src/IDevice.h"

class Plugin_ULTRASONIC : public IDevice {
 public:
  const char* name() const override { return "Ultrasonic Unit"; }
  const char* slug() const override { return "ultrasonic"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x57;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    return true;
  }

  void update() override {
    // Send trigger command 0x01
    bus->beginTransmission(addr);
    bus->write(0x01);
    bus->endTransmission();
    delay(50);  // wait for pulse

    // Read 3 bytes: distance in mm (24-bit big-endian)
    if (bus->requestFrom(static_cast<int>(addr), 3) != 3)
      return;
    uint32_t d = 0;
    d = static_cast<uint32_t>(bus->read()) << 16;
    d |= static_cast<uint32_t>(bus->read()) << 8;
    d |= static_cast<uint32_t>(bus->read());
    _mm = static_cast<float>(d);
    _cm = _mm / 10.0f;
  }

  void toJson(JsonObject& o) const override {
    o["distance_mm"] = _mm;
    o["distance_mm_unit"] = "mm";
    o["distance_cm"] = _cm;
    o["distance_cm_unit"] = "cm";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"dist_cm", _cm, "cm"};
    n = 1;
  }

 private:
  float _mm = 0, _cm = 0;
};
