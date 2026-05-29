#pragma once
// ============================================================
//  PinDevice_TubePressure.h  –  M5Stack Unit Tube Pressure [non-I2C]
//
//  A positive/negative gas-pressure gauge (sensor model
//  MCP-H10-B200KPPN).  It maps -100..200 kPa proportionally to an
//  analog output of 0.1..3.1 V on a single ADC line:
//
//      P[kPa] = K * Vout[V] + B        (K = 100, B = -110)
//
//  e.g. 0.1 V -> -100 kPa,  3.1 V -> +200 kPa.  Rated accuracy is
//  about ±1.5 kPa.
//
//  Port-B wiring (M5Stack Core):  analog = White = GPIO36.
//      fw.addPlugin(new PinDevice_TubePressure());      // default 36
//      fw.addPlugin(new PinDevice_TubePressure(36));    // explicit
//
//  ⚠ The pin MUST be an ADC1 pin (ESP32: GPIO 32-39, ESP32-S3:
//  GPIO 1-10) — ADC2 pins fail while WiFi is on.  beginPins() warns
//  on a non-ADC1 pin.
//
//  Readings:  pressure   — kPa (range -100..200), zero-corrected.
//             millivolts — raw sensor output in mV (diagnostics).
//
//  Auto-zero:  the first stable reading after ZERO_DELAY_MS is
//  captured as the ambient baseline and subtracted from every
//  subsequent reading.  This trims out the typical ±3 kPa unit-to-
//  unit offset drift, so an undisturbed tube reads ~0 kPa.  The
//  raw (uncorrected) value is still available as 'pressure_raw' in
//  the JSON payload for diagnostics.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_TubePressure : public IPinDevice {
 public:
  explicit PinDevice_TubePressure(uint8_t adcPin = 36) : _pin(adcPin) {}

  // Conversion constants for the MCP-H10-B200KPPN (from M5Stack
  // docs):  P = K * Vout + B.
  static constexpr float K = 100.0f;   // kPa per volt
  static constexpr float B = -110.0f;  // kPa offset

  // Wait this long after boot before capturing the zero baseline,
  // so the ADC, the sensor's 5V rail, and the Grove cable have all
  // settled.  ~5 s is plenty.
  static constexpr uint32_t ZERO_DELAY_MS = 5000;

  const char* name() const override { return "Tube Pressure Unit"; }
  const char* slug() const override { return "tubepress"; }

  bool beginPins() override {
    // ADC1 ranges differ by chip: GPIO32-39 on the original ESP32,
    // GPIO1-10 on the ESP32-S3.  Warn only when the pin is outside
    // both, which is a near-certain wiring mistake.
    if (!((_pin >= 32 && _pin <= 39) || (_pin >= 1 && _pin <= 10)))
      Serial.printf(
          "[Pin] WARNING: %s on GPIO%u may not be an ADC1 pin "
          "— analogRead can fail while WiFi is on\n",
          name(), _pin);
    analogSetPinAttenuation(_pin, ADC_11db);  // full 0-3.3V range
    return true;
  }

  void update() override {
    // Average a handful of millivolt reads to settle ADC noise.
    uint32_t acc = 0;
    for (uint8_t i = 0; i < SAMPLES; i++)
      acc += analogReadMilliVolts(_pin);
    _mv = static_cast<uint16_t>(acc / SAMPLES);
    _pressure_raw = K * (_mv / 1000.0f) + B;

    // Capture the ambient baseline once, after the sensor has had
    // time to settle.  Everything thereafter is reported relative
    // to that zero.
    if (!_zeroed && millis() > ZERO_DELAY_MS) {
      _zero_kpa = _pressure_raw;
      _zeroed = true;
      Serial.printf("[Pin] %s zeroed at %.2f kPa\n", name(), _zero_kpa);
    }
    _pressure = _pressure_raw - _zero_kpa;
  }

  void toJson(JsonObject& o) const override {
    o["pressure"] = _pressure;
    o["pressure_raw"] = _pressure_raw;
    o["zero_kpa"] = _zero_kpa;
    o["millivolts"] = _mv;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"pressure", _pressure, "kPa"};
    n = 1;
  }

 private:
  static constexpr uint8_t SAMPLES = 16;
  uint8_t _pin;
  uint16_t _mv = 0;
  float _pressure = 0;
  float _pressure_raw = 0;
  float _zero_kpa = 0;
  bool _zeroed = false;
};
