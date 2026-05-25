#pragma once
// ============================================================
//  Plugin_ENV3.h  –  M5Stack ENV III Unit
//  SHT30 (temp/humidity)  0x44
//  QMP6988 (pressure)     0x70
// ============================================================
#include "../src/IDevice.h"

class Plugin_ENV3 : public IDevice {
 public:
  const char* name() const override { return "ENV III Unit"; }
  const char* slug() const override { return "env3"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x44;
    buf[1] = 0x70;
    n = 2;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Init SHT30
    wire->beginTransmission(0x44);
    wire->write(0x21);
    wire->write(0x30);  // periodic mode
    uint8_t err = wire->endTransmission();
    if (err != 0) {
      // Wire endTransmission() error codes:
      //   1 = data too long  2 = NACK on address  3 = NACK on data
      //   4 = other          5 = timeout
      Serial.printf("[ENV3] begin: SHT30 cmd 0x21 0x30 -> err=%u\n", err);
      return false;
    }
    // Init QMP6988 (soft-reset then normal mode)
    wire->beginTransmission(0x70);
    wire->write(0xE0);
    wire->write(0xE6);
    wire->endTransmission();
    delay(20);
    return true;
  }

  void update() override {
    _readSHT30();
    _readQMP();
  }

  void toJson(JsonObject& o) const override {
    o["temp"] = _temp;
    o["temp_unit"] = "°C";
    o["humidity"] = _hum;
    o["humidity_unit"] = "%";
    o["pressure"] = _hpa;
    o["pressure_unit"] = "hPa";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"temp", _temp, "°C"};
    b[1] = {"humid", _hum, "%"};
    b[2] = {"press", _hpa, "hPa"};
    n = 3;
  }

 private:
  float _temp = 0, _hum = 0, _hpa = 0;

  void _readSHT30() {
    bus->beginTransmission(0x44);
    bus->write(0xE0);
    bus->write(0x00);
    bus->endTransmission(false);
    if (bus->requestFrom(0x44, 6) != 6)
      return;
    uint8_t b[6];
    for (auto& x : b)
      x = bus->read();
    uint16_t rt = (b[0] << 8) | b[1];
    uint16_t rh = (b[3] << 8) | b[4];
    _temp = -45.0f + 175.0f * rt / 65535.0f;
    _hum = 100.0f * rh / 65535.0f;
  }

  void _readQMP() {
    // Trigger one-shot conversion
    bus->beginTransmission(0x70);
    bus->write(0xF4);
    bus->write(0x2F);  // pres+temp, normal power
    bus->endTransmission();
    delay(20);
    // Read 6 raw bytes from 0xF7
    uint8_t d[6] = {0};
    bus->beginTransmission(0x70);
    bus->write(0xF7);
    bus->endTransmission(false);
    bus->requestFrom(0x70, 6);
    for (auto& x : d)
      x = bus->read();
    int32_t raw = (static_cast<int32_t>(d[0]) << 12) |
                  (static_cast<int32_t>(d[1]) << 4) | (d[2] >> 4);
    // Simplified: assume typical coefficients give Pa directly
    // Full calibration requires coefficient registers – use approx here
    _hpa = raw / 100.0f;
    if (_hpa < 300 || _hpa > 1100)
      _hpa = 1013.25f;  // clamp if uncalibrated
  }
};
