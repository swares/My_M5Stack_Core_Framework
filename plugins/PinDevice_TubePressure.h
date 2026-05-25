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
//  ⚠ The pin MUST be an ADC1 pin (GPIO 32-39) — ADC2 pins fail
//  while WiFi is on.  beginPins() warns on a non-ADC1 pin.
//
//  Readings:  pressure   — kPa (range -100..200).
//             millivolts — raw sensor output in mV (diagnostics).
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_TubePressure : public IPinDevice {
 public:
  explicit PinDevice_TubePressure(uint8_t adcPin = 36) : _pin(adcPin) {}

  // Conversion constants for the MCP-H10-B200KPPN (from M5Stack
  // docs):  P = K * Vout + B.
  static constexpr float K = 100.0f;   // kPa per volt
  static constexpr float B = -110.0f;  // kPa offset

  const char* name() const override { return "Tube Pressure Unit"; }
  const char* slug() const override { return "tubepress"; }

  bool beginPins() override {
    if (_pin < 32 || _pin > 39)
      Serial.printf(
          "[Pin] WARNING: %s on GPIO%u is not an ADC1 pin "
          "(32-39) — analogRead fails while WiFi is on\n",
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
    _pressure = K * (_mv / 1000.0f) + B;
  }

  void toJson(JsonObject& o) const override {
    o["pressure"] = _pressure;
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
};
