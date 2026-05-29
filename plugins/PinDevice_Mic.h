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
//  ⚠ The pin MUST be an ADC1 pin (ESP32: GPIO 32-39, ESP32-S3:
//  GPIO 1-10) — ADC2 fails while WiFi is on.  beginPins() warns on
//  a non-ADC1 pin.
//
//  Readings:  level     — instantaneous pk-pk over last 100 ms window.
//             level_pct — instantaneous level as percent of full scale.
//             peak      — peak-hold of level, decays over PEAK_DECAY_MS.
//             peak_pct  — peak-hold as percent.
//
//  Why peak-hold:  the embedded dashboard polls /api/all every 5 s.
//  A clap or word only lasts ~200 ms, so it almost always falls
//  between polls and the dashboard never sees it.  The peak field
//  holds the loudest recent level and decays linearly, so brief
//  sound events stay visible for several seconds.
//
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
    // ADC1 ranges differ by chip: GPIO32-39 on the original ESP32,
    // GPIO1-10 on the ESP32-S3.  Warn only when the pin is outside
    // both, which is a near-certain wiring mistake.
    if (!((_pin >= 32 && _pin <= 39) || (_pin >= 1 && _pin <= 10)))
      Serial.printf(
          "[Pin] WARNING: %s on GPIO%u may not be an ADC1 pin "
          "— analogRead can fail while WiFi is on\n",
          name(), _pin);
    // MAX4466 outputs centered around ~1.65 V with ±0.5 V swing on
    // loud audio — we need the full 0-3.3 V ADC range or the signal
    // clips at the rail and pk-pk reads as zero.
    analogSetPinAttenuation(_pin, ADC_11db);
    _winStart = millis();
    _peakSetAt = millis();
    return true;
  }

  void fastPoll() override {
    uint16_t s = analogRead(_pin);
    if (s < _lo)
      _lo = s;
    if (s > _hi)
      _hi = s;
    uint32_t now = millis();
    if (now - _winStart >= WINDOW_MS) {
      _level = (_hi >= _lo) ? static_cast<uint16_t>(_hi - _lo) : 0;
      _last_lo = _lo;
      _last_hi = _hi;
      _lo = 4095;
      _hi = 0;
      _winStart = now;

      // Peak-hold with linear decay.  Lock the source value when a
      // new peak arrives, then always compute the displayed peak
      // from that fixed source — never recurse on the decayed
      // value, or the decay rate compounds and the held peak takes
      // forever to come down.
      if (_level > _peak) {
        _peak = _level;
        _peakSource = _level;
        _peakSetAt = now;
      } else {
        uint32_t age = now - _peakSetAt;
        if (age >= PEAK_DECAY_MS) {
          _peak = _level;  // fully decayed
        } else {
          // Linear decay from _peakSource toward 0 over
          // PEAK_DECAY_MS, then floored by the current
          // instantaneous level so the held value never drops
          // below what the mic is reading right now.
          float remaining =
              1.0f - (static_cast<float>(age) / PEAK_DECAY_MS);
          uint16_t decayed =
              static_cast<uint16_t>(_peakSource * remaining);
          _peak = (decayed > _level) ? decayed : _level;
        }
      }
    }
  }

  void update() override {}  // all work happens in fastPoll()

  void toJson(JsonObject& o) const override {
    o["level"] = _level;
    o["level_pct"] = _pct(_level);
    o["peak"] = _peak;
    o["peak_pct"] = _pct(_peak);
    o["lo"] = _last_lo;  // diagnostics: lowest ADC count in last window
    o["hi"] = _last_hi;  // diagnostics: highest ADC count in last window
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"level", static_cast<float>(_level), ""};
    b[1] = {"level_pct", _pct(_level), "%"};
    b[2] = {"peak", static_cast<float>(_peak), ""};
    b[3] = {"peak_pct", _pct(_peak), "%"};
    n = 4;
  }

 private:
  static constexpr uint32_t WINDOW_MS = 100;        // pk-pk window
  static constexpr uint32_t PEAK_DECAY_MS = 6000;   // peak hold/decay
  uint8_t _pin;
  uint16_t _lo = 4095, _hi = 0;        // running window extremes
  uint16_t _last_lo = 0, _last_hi = 0;  // previous completed window
  uint16_t _level = 0;                  // last window's pk-pk
  uint16_t _peak = 0;                   // decaying peak-hold (displayed)
  uint16_t _peakSource = 0;             // value at moment peak was set
  uint32_t _winStart = 0;
  uint32_t _peakSetAt = 0;
  float _pct(uint16_t v) const { return (v * 100.0f) / 4095.0f; }
};
