#pragma once
// ============================================================
//  PinDevice_MQ.h  –  Generic MQ-series gas sensor   [non-I2C]
//
//  One driver for the constant-heater MQ gas sensors (MQ-2, MQ-3,
//  MQ-4, MQ-5, MQ-6, MQ-8, MQ-135).  These are purely analog: a
//  heated tin-dioxide element whose resistance Rs falls as the
//  target gas concentration rises.  Rs is read as the voltage
//  across a series load resistor RL on one ADC pin.
//
//  ⚠ NOT for MQ-7 / MQ-9.  Those need a CYCLED heater (≈5 V for
//  60 s, then ≈1.4 V for 90 s, sampling in the low phase) driven
//  from a second controllable pin — a different device.  This
//  driver assumes a steady heater voltage.
//
//  ── Wiring ───────────────────────────────────────────────────
//      fw.addPlugin(new PinDevice_MQ(36, PinDevice_MQ::MQ2));
//  • adcPin MUST be an ADC1 pin (ESP32: GPIO 32-39, ESP32-S3:
//    GPIO 1-10) — ADC2 fails with
//    WiFi on.  beginPins() warns otherwise.
//  • A bare MQ breakout runs its element at 5 V and AOUT can swing
//    to ~5 V — over the ESP32 ADC's 3.3 V limit.  Wire AOUT through
//    a resistor divider and pass its ratio (Vsensor / Vpin).  The
//    default 2.0 matches a 1:1 divider (two equal resistors); pass
//    1.0 only if the output is already guaranteed ≤3.3 V.
//
//  ── Calibration ──────────────────────────────────────────────
//  ppm output is intentionally NOT computed yet — it needs a
//  clean-air baseline (Ro) and per-gas curve constants.  For now
//  the device reports the sensor's output voltage and its Rs
//  estimate, which already show the qualitative trend.  RL is
//  often a trimmer on the module, so set rlOhms to your board's
//  value for Rs to be meaningful.
//
//  ── Warmup ───────────────────────────────────────────────────
//  MQ elements need a preheat each power-up before the reading
//  settles.  beginPins() starts a timer; `warming` stays 1 (with
//  `warmup_s` counting down) until warmupSec elapses — default
//  180 s.  Readings publish throughout; they are just flagged as
//  not-yet-stable.  A brand-new sensor additionally needs a long
//  initial burn-in (commonly 24-48 h) that firmware cannot enforce.
//
//  Readings:  sensor_v (V, reconstructed sensor output),
//             rs (kΩ, sensor resistance estimate),
//             warming (1 = still preheating).
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_MQ : public IPinDevice {
 public:
  // Constant-heater MQ models handled by this driver.
  enum MQModel : uint8_t {
    MQ2 = 0,  // LPG, smoke, propane, methane, hydrogen
    MQ3,      // alcohol / ethanol vapour
    MQ4,      // methane / CNG
    MQ5,      // LPG, natural gas
    MQ6,      // LPG, butane
    MQ8,      // hydrogen
    MQ135     // air quality — NH3, NOx, CO2, benzene, smoke
  };

  // adcPin       — ADC1 pin (ESP32: 32-39, ESP32-S3: 1-10) on the module's AOUT
  // model        — which MQ sensor
  // warmupSec    — preheat seconds before readings are trusted
  // vcVolts      — heater/loop supply voltage (5 V typical)
  // rlOhms       — series load resistor (match your module)
  // dividerRatio — Vsensor / Vpin of the AOUT input divider (>=1)
  PinDevice_MQ(uint8_t adcPin, MQModel model, uint32_t warmupSec = 180,
               float vcVolts = 5.0f, uint32_t rlOhms = 10000,
               float dividerRatio = 2.0f)
      : _pin(adcPin),
        _model(model),
        _warmupMs(static_cast<uint32_t>(warmupSec) * 1000UL),
        _vc(vcVolts),
        _rl(rlOhms),
        _div(dividerRatio) {}

  const char* name() const override { return _modelName(_model); }
  const char* slug() const override { return _modelSlug(_model); }

  bool beginPins() override {
    // ADC1 ranges differ by chip: GPIO32-39 on the original ESP32,
    // GPIO1-10 on the ESP32-S3.  Warn only when the pin is outside
    // both, which is a near-certain wiring mistake.
    if (!((_pin >= 32 && _pin <= 39) || (_pin >= 1 && _pin <= 10)))
      Serial.printf(
          "[Pin] WARNING: %s on GPIO%u may not be an ADC1 pin "
          "— analogRead can fail while WiFi is on\n",
          name(), _pin);
    analogSetPinAttenuation(_pin, ADC_11db);  // full 0-3.3 V range
    _startMs = millis();                      // warmup clock
    return true;
  }

  void update() override {
    // Average a handful of millivolt reads to settle ADC noise.
    uint32_t acc = 0;
    for (uint8_t i = 0; i < SAMPLES; i++)
      acc += analogReadMilliVolts(_pin);
    _pinMv = static_cast<uint16_t>(acc / SAMPLES);
    // Reconstruct the sensor's own output voltage ahead of the divider.
    _sensorV = (_pinMv / 1000.0f) * _div;
    // Rs = RL * (Vc - Vout) / Vout — guard the divide and the
    // physically-impossible Vout >= Vc (bad divider ratio / wiring).
    if (_sensorV > 0.05f && _sensorV < _vc)
      _rsK = (_rl / 1000.0f) * (_vc - _sensorV) / _sensorV;
    else
      _rsK = 0.0f;
  }

  void toJson(JsonObject& o) const override {
    o["model"] = name();
    o["pin"] = _pin;
    o["pin_mv"] = _pinMv;
    o["sensor_v"] = _sensorV;
    o["rs_kohm"] = _rsK;
    o["warming"] = _warming() ? 1 : 0;
    o["warmup_s"] = _warmupRemainingS();
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"sensor_v", _sensorV, "V"};
    b[1] = {"rs", _rsK, "kΩ"};
    b[2] = {"warming", static_cast<float>(_warming() ? 1 : 0), ""};
    n = 3;
  }

 private:
  static constexpr uint8_t SAMPLES = 16;
  uint8_t _pin;
  MQModel _model;
  uint32_t _warmupMs;
  float _vc;
  uint32_t _rl;
  float _div;
  uint32_t _startMs = 0;
  uint16_t _pinMv = 0;
  float _sensorV = 0;
  float _rsK = 0;

  bool _warming() const { return (millis() - _startMs) < _warmupMs; }
  uint16_t _warmupRemainingS() const {
    uint32_t e = millis() - _startMs;
    return (e >= _warmupMs) ? 0
                            : static_cast<uint16_t>((_warmupMs - e) / 1000);
  }

  static const char* _modelName(MQModel m) {
    switch (m) {
      case MQ2:
        return "MQ-2 Gas Sensor (LPG/smoke)";
      case MQ3:
        return "MQ-3 Gas Sensor (alcohol)";
      case MQ4:
        return "MQ-4 Gas Sensor (methane)";
      case MQ5:
        return "MQ-5 Gas Sensor (LPG/natural gas)";
      case MQ6:
        return "MQ-6 Gas Sensor (LPG/butane)";
      case MQ8:
        return "MQ-8 Gas Sensor (hydrogen)";
      case MQ135:
        return "MQ-135 Gas Sensor (air quality)";
    }
    return "MQ Gas Sensor";
  }
  static const char* _modelSlug(MQModel m) {
    switch (m) {
      case MQ2:
        return "mq2";
      case MQ3:
        return "mq3";
      case MQ4:
        return "mq4";
      case MQ5:
        return "mq5";
      case MQ6:
        return "mq6";
      case MQ8:
        return "mq8";
      case MQ135:
        return "mq135";
    }
    return "mq";
  }
};
