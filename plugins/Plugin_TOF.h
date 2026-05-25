#pragma once
// ============================================================
//  Plugin_TOF.h  –  M5Stack ToF Unit (VL53L0X)
//  Laser ranging 0-2000 mm.  Address 0x29
// ============================================================
#include "../src/IDevice.h"

class Plugin_TOF : public IDevice {
 public:
  const char* name() const override { return "ToF Unit (VL53L0X)"; }
  const char* slug() const override { return "tof"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x29;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Load default tuning and set single-ranging mode
    regWrite(0x88, 0x00);
    regWrite(0x80, 0x01);
    regWrite(0xFF, 0x01);
    regWrite(0x00, 0x00);
    regWrite(0x91, 0x3C);  // stop_variable
    regWrite(0x00, 0x01);
    regWrite(0xFF, 0x00);
    regWrite(0x80, 0x00);
    return regRead8(0xC0) == 0xEE;  // model ID check
  }

  void update() override {
    // Trigger single-shot ranging
    regWrite(0x00, 0x01);  // SYSRANGE_START
    uint8_t status = 0;
    uint32_t t = millis();
    do {
      status = regRead8(0x13);  // RESULT_INTERRUPT_STATUS
    } while (!(status & 0x07) && millis() - t < 200);

    uint8_t d[2] = {0};
    regRead(0x1E, d, 2);  // RESULT_RANGE_STATUS + 10
    _dist = static_cast<uint16_t>((d[0] << 8) | d[1]);
    regWrite(0x0B, 0x01);  // clear interrupt
  }

  void toJson(JsonObject& o) const override {
    o["distance"] = _dist;
    o["distance_unit"] = "mm";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"dist", static_cast<float>(_dist), "mm"};
    n = 1;
  }

 private:
  uint16_t _dist = 0;
};
