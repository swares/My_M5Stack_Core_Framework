#pragma once
// ============================================================
//  PinDevice_Angle.h  –  M5Stack Angle Unit (rotary pot) [non-I2C]
//
//  The classic Angle Unit (U016) is a rotary potentiometer wired
//  to a single ADC line — NOT the I2C variant (see Plugin_ANGLE.h
//  for that one at 0x36).  Turning the knob sweeps the wiper across
//  the full 0-3.3 V range, which this plugin reads and maps to a
//  0-100 % position and an angle in degrees.
//
//      fw.addPlugin(new PinDevice_Angle(8));       // CoreS3 Port-B White
//      fw.addPlugin(new PinDevice_Angle(36));      // Core / Core2 Port-B White
//
//  ⚠ The pin MUST be an ADC1 pin (ESP32: GPIO 32-39, ESP32-S3:
//    GPIO 1-10) — ADC2 pins fail while WiFi is on.  beginPins()
//    warns on an obviously-wrong pin.
//
//  The pot's mechanical travel is ~280-300°, but the value reported
//  is normalized: 0 % at one stop, 100 % at the other.  SWEEP_DEG
//  sets the full-scale angle — adjust it if you want true mechanical
//  degrees rather than the default 300°.
//
//  Readings:  raw   — 12-bit ADC (0-4095), averaged over SAMPLES.
//             pct   — 0-100 % of travel.
//             angle — pct mapped onto 0..SWEEP_DEG degrees.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Angle : public IPinDevice {
 public:
  explicit PinDevice_Angle(uint8_t adcPin) : _pin(adcPin) {}

  // Full-scale mechanical sweep of the pot, in degrees.  The M5Stack
  // Angle Unit turns roughly 300°; change this if yours differs or
  // if you'd rather report a 0-360 scale.
  static constexpr float SWEEP_DEG = 300.0f;

  const char* name() const override { return "Angle Unit (Rotary)"; }
  const char* slug() const override { return "angle"; }

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
    // Average a handful of reads to settle ADC + wiper noise so the
    // angle doesn't jitter when the knob is held still.
    uint32_t acc = 0;
    for (uint8_t i = 0; i < SAMPLES; i++)
      acc += analogRead(_pin);
    _raw = static_cast<uint16_t>(acc / SAMPLES);
    _pct = (_raw * 100.0f) / 4095.0f;
    _deg = (_raw * SWEEP_DEG) / 4095.0f;
  }

  void toJson(JsonObject& o) const override {
    o["raw"] = _raw;
    o["pct"] = _pct;
    o["angle"] = _deg;
    o["angle_unit"] = "°";
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"angle", _deg, "°"};
    b[1] = {"pct", _pct, "%"};
    n = 2;
  }

 private:
  static constexpr uint8_t SAMPLES = 16;
  uint8_t _pin;
  uint16_t _raw = 0;
  float _pct = 0;
  float _deg = 0;
};
