#pragma once
// ============================================================
//  Plugin_FAN.h  –  M5Stack Module Fan v1.1  (SKU M013-V11)
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  An STM32F030-based PWM fan controller: set the fan's speed and
//  read its actual RPM back over I2C.  CONTROLLABLE — driven via
//  the Web API.
//
//  ⚠ Needs the "M5Module-Fan" library (M5Stack) — install it via
//  the Arduino Library Manager, then uncomment BOTH the #include
//  of this header and the registration line in the .ino.  This is
//  the same pattern the UART devices use (GPS → TinyGPSPlus,
//  Module LLM → M5Module-LLM): the official library owns the
//  verified I2C register map, so this plugin drives the module
//  THROUGH the library rather than hard-coding registers.
//
//      fw.addPlugin(new Plugin_FAN());
//
//  ── I2C address ──────────────────────────────────────────────
//  0x18 (MODULE_FAN_BASE_ADDR) — per the product spec and the
//  M5Module-Fan library.  Re-addressable 0x08-0x77; if your unit
//  was changed, edit i2cAddresses() to match.  0x18 does not clash
//  with any other plugin in this framework.
//
//  ── Bus note ─────────────────────────────────────────────────
//  M5ModuleFan::begin() (re)opens the I2C bus.  begin() below hands
//  it the framework's already-resolved internal-bus pins and speed
//  (from BoardInfo / Config.h), so the re-init reproduces the exact
//  bus the framework set up — the other internal chips (IMU / PMIC
//  / RTC) keep working unchanged.
//
//  ── Controls (Web API) ───────────────────────────────────────
//    GET /api/fan/set?power=0|1      stop / run the fan
//    GET /api/fan/set?speed=0..100   PWM duty cycle, percent
//  Every parameter is validated; anything out of range is rejected
//  and the hardware is left untouched.
//
//  Readings:  rpm, duty (%), running (1 = enabled).
// ============================================================
#include "../src/IDevice.h"
#include "../src/Config.h"
#include "../src/BoardInfo.h"
#include <m5_module_fan.hpp>

class Plugin_FAN : public IDevice {
 public:
  const char* name() const override { return "Fan Module"; }
  const char* slug() const override { return "fan"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x18;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool controllable() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Hand the library the framework's own internal-bus pins +
    // speed so its begin() re-init matches the existing bus.
    const BoardInfo& bi = BoardInfo::detect();
    if (!_fan.begin(wire, a, bi.i2cIntSda, bi.i2cIntScl, I2C_INT_FREQ))
      return false;
    // Bring the module up in a known state: a defined PWM frequency,
    // the default duty loaded, fan stopped until commanded.
    _fan.setPWMFrequency(PWM_1KHZ);
    _fan.setPWMDutyCycle(_duty);
    _fan.setStatus(MODULE_FAN_DISABLE);
    _running = false;
    return true;
  }

  void update() override {
    _rpm = _fan.getRPM();
    _duty = _fan.getPWMDutyCycle();
    _running = (_fan.getStatus() == MODULE_FAN_ENABLE);
  }

  void toJson(JsonObject& o) const override {
    o["rpm"] = _rpm;
    o["duty"] = _duty;
    o["running"] = _running ? 1 : 0;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"rpm", static_cast<float>(_rpm), "rpm"};
    b[1] = {"duty", static_cast<float>(_duty), "%"};
    b[2] = {"running", static_cast<float>(_running ? 1 : 0), ""};
    n = 3;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    if (param == "power") {
      int on = _parseBool(value);
      if (on < 0)
        return false;  // bad value
      _fan.setStatus(on ? MODULE_FAN_ENABLE : MODULE_FAN_DISABLE);
      _running = (on == 1);
      return true;
    }
    if (param == "speed") {
      int v = _parsePct(value);
      if (v < 0)
        return false;  // not 0-100
      _duty = static_cast<uint8_t>(v);
      _fan.setPWMDutyCycle(_duty);
      return true;
    }
    return false;  // unknown param
  }

  // ── Control schema ────────────────────────────────────────
  void controlSchema(JsonArray& out) const override {
    JsonObject pw = out.add<JsonObject>();
    pw["id"] = "power";
    pw["label"] = "Fan";
    pw["type"] = "toggle";
    pw["value"] = _running ? 1 : 0;

    JsonObject sp = out.add<JsonObject>();
    sp["id"] = "speed";
    sp["label"] = "Speed";
    sp["type"] = "slider";
    sp["min"] = 0;
    sp["max"] = 100;
    sp["step"] = 5;
    sp["unit"] = "%";
    sp["value"] = _duty;
  }

 private:
  M5ModuleFan _fan;
  uint16_t _rpm = 0;
  uint8_t _duty = 50;  // default duty until first speed command
  bool _running = false;

  // "1/on/true" -> 1, "0/off/false" -> 0, anything else -> -1.
  static int _parseBool(const String& v) {
    String t = v;
    t.toLowerCase();
    if (t == "1" || t == "on" || t == "true")
      return 1;
    if (t == "0" || t == "off" || t == "false")
      return 0;
    return -1;
  }
  // A decimal string in 0..100 -> that value, anything else -> -1.
  static int _parsePct(const String& v) {
    if (v.length() == 0)
      return -1;
    for (uint16_t i = 0; i < v.length(); i++)
      if (!isDigit(v.charAt(i)))
        return -1;
    int32_t n = v.toInt();
    return (n >= 0 && n <= 100) ? static_cast<int>(n) : -1;
  }
};
