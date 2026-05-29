#pragma once
// ============================================================
//  PinDevice_Grove2Grove.h  –  M5Stack Unit Grove2Grove   [non-I2C]
//
//  A 1-in-1-out switched Grove expansion unit.  It gates 5 V / 1 A
//  through to a downstream Grove port and measures the current
//  drawn by whatever is plugged into that port:
//
//      PWR_EN  (digital out) — HIGH powers the downstream port
//      Current (analog  in)  — 0..1000 mA.  The sense output sits
//                              at a non-zero baseline voltage at
//                              zero current; current rises ~1 mV
//                              per mA above that baseline.
//
//  Port-B wiring (M5Stack Core):
//      PWR_EN        = Yellow = GPIO26
//      current sense = White  = GPIO36
//      fw.addPlugin(new PinDevice_Grove2Grove());        // 26, 36
//      fw.addPlugin(new PinDevice_Grove2Grove(26, 36));  // explicit
//
//  ⚠ The sense pin MUST be an ADC1 pin (ESP32: GPIO 32-39, ESP32-S3:
//  GPIO 1-10) — ADC2 pins fail while WiFi is on.  beginPins() warns
//  on a non-ADC1 pin.
//
//  CONTROLLABLE — drive it via the Web API only:
//    GET /api/grove2grove/set?power=1     power the downstream port
//    GET /api/grove2grove/set?power=0     cut downstream power
//
//  ⚠ SAFETY: beginPins() forces PWR_EN OFF, then samples the
//  zero-current baseline voltage (valid precisely because no
//  current flows while the port is unpowered).  The user enables
//  power explicitly from the dashboard.
//
//  Readings:  current — mA drawn by the downstream port.
//             power   — 1 = downstream port powered, 0 = off.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Grove2Grove : public IPinDevice {
 public:
  explicit PinDevice_Grove2Grove(uint8_t pwrEnPin = 26, uint8_t sensePin = 36)
      : _enPin(pwrEnPin), _aPin(sensePin) {}

  const char* name() const override { return "Grove2Grove Unit"; }
  const char* slug() const override { return "grove2grove"; }
  bool controllable() const override { return true; }

  bool beginPins() override {
    // ADC1 ranges differ by chip: GPIO32-39 on the original ESP32,
    // GPIO1-10 on the ESP32-S3.  Warn only when the pin is outside
    // both, which is a near-certain wiring mistake.
    if (!((_aPin >= 32 && _aPin <= 39) || (_aPin >= 1 && _aPin <= 10)))
      Serial.printf(
          "[Pin] WARNING: %s sense pin GPIO%u may not be an ADC1 "
          "pin — analogRead can fail while WiFi is on\n",
          name(), _aPin);
    pinMode(_enPin, OUTPUT);
    digitalWrite(_enPin, LOW);  // SAFETY — start powered off
    _power = 0;
    analogSetPinAttenuation(_aPin, ADC_11db);  // full 0-3.3V range
    delay(5);                                  // let the rail settle at 0
    _vrefMv = _readMv();                       // zero-current baseline
    return true;
  }

  void update() override {
    float ma = static_cast<float>(_readMv()) -
               static_cast<float>(_vrefMv);  // ~1 mV per mA
    if (ma < 0)
      ma = 0;
    if (ma > 1000)
      ma = 1000;
    _current = ma;
  }

  void toJson(JsonObject& o) const override {
    o["current"] = _current;
    o["power"] = _power;
    o["sense_mv"] = _lastMv;
    o["vref_mv"] = _vrefMv;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"current", _current, "mA"};
    b[1] = {"power", static_cast<float>(_power), ""};
    n = 2;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    if (param == "power") {
      int on = _parseBool(value);
      if (on < 0)
        return false;  // reject bad input
      digitalWrite(_enPin, on ? HIGH : LOW);
      _power = static_cast<uint8_t>(on);
      return true;
    }
    return false;  // unknown param
  }

  // ── Control schema ────────────────────────────────────────
  void controlSchema(JsonArray& out) const override {
    JsonObject c = out.add<JsonObject>();
    c["id"] = "power";
    c["label"] = "Downstream power";
    c["type"] = "toggle";
    c["value"] = _power ? 1 : 0;
  }

 private:
  static constexpr uint8_t SAMPLES = 16;

  uint8_t _enPin, _aPin;
  uint8_t _power = 0;
  uint16_t _vrefMv = 0;  // zero-current baseline (captured at boot)
  uint16_t _lastMv = 0;  // last averaged sense reading
  float _current = 0;    // mA

  // Average SAMPLES millivolt reads of the sense pin; caches the
  // result in _lastMv and returns it.
  uint16_t _readMv() {
    uint32_t acc = 0;
    for (uint8_t i = 0; i < SAMPLES; i++)
      acc += analogReadMilliVolts(_aPin);
    _lastMv = static_cast<uint16_t>(acc / SAMPLES);
    return _lastMv;
  }

  // "1/on/true" -> 1, "0/off/false" -> 0, anything else -> -1.
  static int _parseBool(const String& v) {
    String t = v;
    t.toLowerCase();
    if (t == "1" || t == "on" || t == "true")
      return 1;
    if (t == "0" || t == "off" || t == "false")
      return 0;
    return -1;
  }
};
