#pragma once
// ============================================================
//  Plugin_PMIC.h  –  Built-in Power Management IC
//
//  Board-aware:
//    • CoreS3 : AXP2101 @ 0x34
//    • Core2  : AXP192  @ 0x34
//
//  Both PMICs are driven through M5Unified's M5.Power API, which
//  abstracts the underlying chip differences (battery voltage, %,
//  charge state, etc.) so the same readings work on either board.
//
//  Internal bus only.  The slug stays "pmic" for stable URL paths.
// ============================================================
#include <cstdio>
#include "../src/IDevice.h"
#include "../src/BoardInfo.h"
#include <M5Unified.h>

class Plugin_PMIC : public IDevice {
 public:
  const char* name() const override { return _displayName; }
  const char* slug() const override { return "pmic"; }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Builtin; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    // AXP2101 and AXP192 both live at 0x34.
    buf[0] = 0x34;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    const auto& bi = BoardInfo::detect();
    snprintf(_displayName, sizeof(_displayName), "PMIC (%s)", bi.pmicName);
    // Power API already started by M5.begin().  On Core1 the
    // battery chip (IP5306) isn't a meaningful I2C peripheral, so
    // BoardInfo sets hasI2cPmic=false and we refuse to bind here.
    // The scanner will then skip this plugin and Core1's address
    // 0x34 (which doesn't ACK anyway) won't show up as PMIC.
    if (!bi.hasI2cPmic)
      return false;
    return true;
  }

  void update() override {
    _vbat = M5.Power.getBatteryVoltage() / 1000.0f;  // mV → V
    _pct = M5.Power.getBatteryLevel();
    _isChg = M5.Power.isCharging();
  }

  void toJson(JsonObject& o) const override {
    o["vbat"] = _vbat;
    o["vbat_unit"] = "V";
    o["battery"] = _pct;
    o["battery_unit"] = "%";
    o["charging"] = _isChg;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"vbat", _vbat, "V"};
    b[1] = {"bat%", static_cast<float>(_pct), "%"};
    b[2] = {"charge", static_cast<float>(_isChg), ""};
    n = 3;
  }

 private:
  float _vbat = 0;
  int _pct = 0;
  bool _isChg = false;
  char _displayName[24] = "PMIC";
};
