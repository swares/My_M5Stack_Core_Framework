#pragma once
// ============================================================
//  Plugin_RTC.h  –  Built-in RTC on the host M5Stack board.
//
//  Both M5Stack CoreS3 and M5Stack Core2 use the same BM8563
//  real-time clock at I2C address 0x51, driven via M5Unified's
//  M5.Rtc API.  No board-specific code needed.
//
//  Internal bus only.
// ============================================================
#include <cstdio>
#include "../src/IDevice.h"
#include <M5Unified.h>

class Plugin_RTC : public IDevice {
 public:
  const char* name() const override { return "BM8563 (RTC)"; }
  const char* slug() const override { return "rtc"; }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Builtin; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x51;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    return true;  // started by M5.begin()
  }

  void update() override {
    auto dt = M5.Rtc.getDateTime();
    _y = dt.date.year;
    _mo = dt.date.month;
    _d = dt.date.date;
    _h = dt.time.hours;
    _mi = dt.time.minutes;
    _s = dt.time.seconds;
    snprintf(_ts, sizeof(_ts), "%04d-%02d-%02d %02d:%02d:%02d", _y, _mo, _d, _h,
             _mi, _s);
  }

  void toJson(JsonObject& o) const override {
    o["datetime"] = _ts;
    o["year"] = _y;
    o["month"] = _mo;
    o["day"] = _d;
    o["hour"] = _h;
    o["minute"] = _mi;
    o["second"] = _s;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"hour", static_cast<float>(_h), "h"};
    b[1] = {"min", static_cast<float>(_mi), "m"};
    b[2] = {"sec", static_cast<float>(_s), "s"};
    n = 3;
  }

 private:
  int _y = 0, _mo = 0, _d = 0, _h = 0, _mi = 0, _s = 0;
  char _ts[24] = "";
};
