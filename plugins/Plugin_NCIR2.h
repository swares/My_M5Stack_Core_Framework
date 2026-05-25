#pragma once
// ============================================================
//  Plugin_NCIR2.h  –  M5Stack NCIR2 Unit (MLX90614)
//  Non-contact IR thermometer.  Address 0x5A
// ============================================================
#include "../src/IDevice.h"

class Plugin_NCIR2 : public IDevice {
 public:
  const char* name() const override { return "NCIR2 Unit (MLX90614)"; }
  const char* slug() const override { return "ncir2"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x5A;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Sanity: read the ambient-temperature register (RAM 0x06)
    // and verify the result is a plausible temperature in Kelvin
    // (this is much more reliable than poking at random EEPROM
    // IDs, especially since MLX90614 EEPROM access needs the
    // address OR'd with 0x20 — a common source of bugs).
    uint8_t d[3] = {0};
    if (!regRead(0x06, d, 3))
      return false;
    uint16_t raw = static_cast<uint16_t>(d[0] | (d[1] << 8));
    // Datasheet range: -40 °C to +125 °C  →  233 K .. 398 K
    //   raw counts are K × 50.  Accept anything 200-450 K to
    //   leave generous margin for sensor warm-up / OOR.
    if (raw < 10000 || raw > 22500)
      return false;
    return true;
  }

  void update() override {
    _ambient = _readRaw(0x06) * 0.02f - 273.15f;
    _object = _readRaw(0x07) * 0.02f - 273.15f;
  }

  // The MLX90614 is SMBus-only and the datasheet flags bare
  // quick-commands (START + addr + STOP, no data) as undefined
  // behaviour, so the default isAlive() in IDevice would be
  // unsafe for this chip.  Override with the same ambient-temp
  // read begin() uses.  No longer called automatically by the
  // framework (periodic rescan was removed), but kept available
  // for manual diagnostics — if a future feature wants to verify
  // a bound chip is still alive, calling isAlive() on NCIR2 will
  // do the right thing instead of bricking the SMBus state.
  bool isAlive() override {
    uint8_t d[3] = {0};
    if (!regRead(0x06, d, 3))
      return false;
    uint16_t raw = static_cast<uint16_t>(d[0] | (d[1] << 8));
    return raw >= 10000 && raw <= 22500;
  }

  void toJson(JsonObject& o) const override {
    o["ambient"] = _ambient;
    o["ambient_unit"] = "°C";
    o["object"] = _object;
    o["object_unit"] = "°C";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"ambient", _ambient, "°C"};
    b[1] = {"object", _object, "°C"};
    n = 2;
  }

 private:
  float _ambient = 0, _object = 0;

  uint16_t _readRaw(uint8_t reg) {
    uint8_t d[3] = {0};
    regRead(reg, d, 3);
    return static_cast<uint16_t>(d[0] | (d[1] << 8));
  }
};
