#pragma once
// ============================================================
//  PinDevice_ECG.h  –  M5Stack ECG Module (AD8232)      [non-I2C]
//
//  The AD8232 is an analog ECG front-end.  It produces:
//    • an analog OUTPUT proportional to the heart's electrical
//      signal — read on an ADC pin;
//    • two lead-off-detection digital outputs, LO+ and LO-, each
//      driven HIGH when its electrode loses skin contact.
//
//  A single analogRead() of an ECG waveform is meaningless, so —
//  exactly like PinDevice_Mic — this device samples fast via the
//  framework's fastPoll() hook and reports the peak-to-peak swing
//  over a ~1 s window (a crude "signal present / beat amplitude"
//  indicator) together with the lead-off status.  Deriving an
//  actual BPM would need a beat detector (see Plugin_HEART) and is
//  intentionally out of scope for this device.
//
//      // adcPin, then optional LO+ / LO- pins  (-1 = not wired)
//      fw.addPlugin(new PinDevice_ECG(36, 26, 25));
//
//  ⚠ adcPin MUST be an ADC1 pin (ESP32: GPIO 32-39, ESP32-S3:
//  GPIO 1-10) — ADC2 fails while
//  WiFi is on.  The exact M-Bus GPIOs the ECG Module routes its
//  OUTPUT / LO+ / LO- signals to depend on the module and how it
//  is stacked, so pass the pins you actually wired.  Pass -1 for a
//  lead-off pin you have not connected.
//
//  Readings:  signal (peak-to-peak ADC counts over the window),
//             leads_off (1 = at least one electrode disconnected).
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_ECG : public IPinDevice {
 public:
  explicit PinDevice_ECG(uint8_t adcPin, int8_t loPlusPin = -1,
                         int8_t loMinusPin = -1)
      : _pin(adcPin), _loP(loPlusPin), _loM(loMinusPin) {}

  const char* name() const override { return "ECG Module (AD8232)"; }
  const char* slug() const override { return "ecg"; }

  // Sample fast so the peak-to-peak window catches the real swing
  // of the ECG waveform.
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
    if (_loP >= 0)
      pinMode(static_cast<uint8_t>(_loP), INPUT);
    if (_loM >= 0)
      pinMode(static_cast<uint8_t>(_loM), INPUT);
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
      _signal = (_hi >= _lo) ? static_cast<uint16_t>(_hi - _lo) : 0;
      _lo = 4095;
      _hi = 0;
      _winStart = millis();
    }
  }

  // Lead-off is a slow-changing digital state — reading it at the
  // normal poll rate is plenty.
  void update() override {
    bool off = false;
    if (_loP >= 0 && digitalRead(static_cast<uint8_t>(_loP)))
      off = true;
    if (_loM >= 0 && digitalRead(static_cast<uint8_t>(_loM)))
      off = true;
    _leadsOff = off;
  }

  void toJson(JsonObject& o) const override {
    o["signal"] = _signal;
    o["leads_off"] = _leadsOff ? 1 : 0;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"signal", static_cast<float>(_signal), ""};
    b[1] = {"leads_off", static_cast<float>(_leadsOff ? 1 : 0), ""};
    n = 2;
  }

 private:
  static constexpr uint32_t WINDOW_MS = 1000;  // ~one heartbeat of swing
  uint8_t _pin;                                // ECG analog-output pin
  int8_t _loP, _loM;                           // LO+ / LO- pins (-1 off)
  uint16_t _lo = 4095, _hi = 0;                // running window extremes
  uint16_t _signal = 0;                        // last window's pk-pk
  bool _leadsOff = false;
  uint32_t _winStart = 0;
};
