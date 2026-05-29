#pragma once
// ============================================================
//  PinDevice_Earth.h  –  M5Stack Earth Unit (soil)   [non-I2C]
//
//  The Earth unit has TWO outputs: an analog soil-moisture level
//  and a digital "dry" comparator output (its threshold set by the
//  on-unit potentiometer).  Plug into a Grove port that carries
//  both an ADC pin and a GPIO pin, and pass both:
//      fw.addPlugin(new PinDevice_Earth(/*analog=*/36, /*digital=*/26));
//
//  ⚠ The analog pin MUST be an ADC1 pin (ESP32: GPIO 32-39,
//  ESP32-S3: GPIO 1-10) — see PinDevice_Light for why.  beginPins()
//  warns on a non-ADC1 pin.
//
//  Readings:  moisture_raw (0-4095), moisture_pct (0-100, relative),
//             dry (1 = past the unit's wet/dry threshold).
//  The slug is "soil" so it never clashes with the I2C Plugin_EARTH.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Earth : public IPinDevice {
 public:
  PinDevice_Earth(uint8_t analogPin, uint8_t digitalPin)
      : _aPin(analogPin), _dPin(digitalPin) {}

  const char* name() const override { return "Earth Unit (soil)"; }
  const char* slug() const override { return "soil"; }

  bool beginPins() override {
    pinMode(_dPin, INPUT);
    // ADC1 ranges differ by chip: GPIO32-39 on the original ESP32,
    // GPIO1-10 on the ESP32-S3.  Warn only when the pin is outside
    // both, which is a near-certain wiring mistake.
    if (!((_aPin >= 32 && _aPin <= 39) || (_aPin >= 1 && _aPin <= 10)))
      Serial.printf(
          "[Pin] WARNING: %s analog pin GPIO%u may not be an "
          "ADC1 pin — analogRead can fail with WiFi on\n",
          name(), _aPin);
    return true;
  }

  void update() override {
    _raw = analogRead(_aPin);
    _dry = (digitalRead(_dPin) == HIGH) ? 1 : 0;
  }

  void toJson(JsonObject& o) const override {
    o["moisture_raw"] = _raw;
    o["moisture_pct"] = _pct();
    o["dry"] = _dry;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"moisture_raw", static_cast<float>(_raw), ""};
    b[1] = {"moisture_pct", _pct(), "%"};
    b[2] = {"dry", static_cast<float>(_dry), ""};
    n = 3;
  }

 private:
  uint8_t _aPin, _dPin;
  uint16_t _raw = 0;
  uint8_t _dry = 0;
  float _pct() const { return (_raw * 100.0f) / 4095.0f; }
};
