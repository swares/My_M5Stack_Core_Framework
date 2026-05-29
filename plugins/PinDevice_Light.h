#pragma once
// ============================================================
//  PinDevice_Light.h  –  M5Stack Light Unit (CdS)   [non-I2C]
//
//  A CdS photoresistor on an analog (ADC) line.  Plug into an
//  ADC-capable Grove port and pass the analog pin:
//      fw.addPlugin(new PinDevice_Light(36));
//
//  ⚠ The pin MUST be an ADC1 pin (ESP32: GPIO 32-39, ESP32-S3:
//  GPIO 1-10).  ADC2 pins do not work with analogRead() while WiFi
//  is connected, and this framework keeps WiFi up — beginPins()
//  warns on a non-ADC1 pin.  On the classic ESP32, GPIO 36 / 39 are
//  input-only ADC1 pins and ideal here.
//
//  Readings:  light_raw (0-4095), light_pct (0-100, relative).
//  The slug is "cds" so it never clashes with an I2C light plugin.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Light : public IPinDevice {
 public:
  explicit PinDevice_Light(uint8_t adcPin) : _pin(adcPin) {}

  const char* name() const override { return "Light Unit (CdS)"; }
  const char* slug() const override { return "cds"; }

  bool beginPins() override {
    // ADC1 ranges differ by chip: GPIO32-39 on the original ESP32,
    // GPIO1-10 on the ESP32-S3.  Warn only when the pin is outside
    // both, which is a near-certain wiring mistake.
    if (!((_pin >= 32 && _pin <= 39) || (_pin >= 1 && _pin <= 10)))
      Serial.printf(
          "[Pin] WARNING: %s on GPIO%u may not be an ADC1 pin "
          "— analogRead can fail while WiFi is on\n",
          name(), _pin);
    return true;
  }

  void update() override { _raw = analogRead(_pin); }

  void toJson(JsonObject& o) const override {
    o["light_raw"] = _raw;
    o["light_pct"] = _pct();
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"light_raw", static_cast<float>(_raw), ""};
    b[1] = {"light_pct", _pct(), "%"};
    n = 2;
  }

 private:
  uint8_t _pin;
  uint16_t _raw = 0;
  float _pct() const { return (_raw * 100.0f) / 4095.0f; }
};
