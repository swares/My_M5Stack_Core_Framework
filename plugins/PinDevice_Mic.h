#pragma once
// ============================================================
//  PinDevice_Mic.h  –  M5Stack Mic Unit (analog, MAX4466)  [non-I2C]
//
//  An analog electret microphone on an ADC line.  A single
//  analogRead() of an audio signal is meaningless — the useful
//  output is a SOUND LEVEL, so this device samples fast via the
//  framework's fastPoll() hook and reports the peak-to-peak swing
//  over a short window.
//
//      fw.addPlugin(new PinDevice_Mic(36));
//
//  ⚠ The pin MUST be an ADC1 pin (GPIO 32-39) — ADC2 fails while
//  WiFi is on.  beginPins() warns on a non-ADC1 pin.
//
//  Readings:  level (peak-to-peak ADC counts), level_pct (0-100).
//  This is a RELATIVE loudness indicator, not a calibrated dB SPL.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Mic : public IPinDevice {
 public:
  explicit PinDevice_Mic(uint8_t adcPin) : _pin(adcPin) {}

  const char* name() const override { return "Mic Unit (analog)"; }
  const char* slug() const override { return "mic"; }

  // Sample fast so the peak-to-peak window catches real audio swing.
  bool wantsFastPoll() const override { return true; }

  bool beginPins() override {
    if (_pin < 32 || _pin > 39)
      Serial.printf(
          "[Pin] WARNING: %s on GPIO%u is not an ADC1 pin "
          "(32-39) — analogRead fails while WiFi is on\n",
          name(), _pin);
    _winStart = millis();
    return true;
  }

  void fastPoll() override {
    uint16_t s = analogRead(_pin);
    if (s < _lo)
      _lo = s;
    if (s > _hi)
      _hi = s;
    if (millis() - _winStart >= WINDOW_MS) {
      _level = (_hi >= _lo) ? static_cast<uint16_t>(_hi - _lo) : 0;
      _lo = 4095;
      _hi = 0;
      _winStart = millis();
    }
  }

  void update() override {}  // all work happens in fastPoll()

  void toJson(JsonObject& o) const override {
    o["level"] = _level;
    o["level_pct"] = _pct();
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"level", static_cast<float>(_level), ""};
    b[1] = {"level_pct", _pct(), "%"};
    n = 2;
  }

 private:
  static constexpr uint32_t WINDOW_MS = 100;  // peak-to-peak window
  uint8_t _pin;
  uint16_t _lo = 4095, _hi = 0;  // running window extremes
  uint16_t _level = 0;           // last completed window's pk-pk
  uint32_t _winStart = 0;
  float _pct() const { return (_level * 100.0f) / 4095.0f; }
};
