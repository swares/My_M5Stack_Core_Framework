#pragma once
// ============================================================
//  PinDevice_Watering.h  –  M5Stack Watering Unit        [non-I2C]
//
//  The Watering Unit pairs a capacitive soil-moisture sensor with a
//  small water pump.  It needs an ADC pin for the moisture level and
//  a GPIO pin that switches the pump:
//      fw.addPlugin(new PinDevice_Watering(/*moisture=*/36, /*pump=*/26));
//
//  ⚠ The moisture pin MUST be an ADC1 pin (ESP32: GPIO 32-39,
//  ESP32-S3: GPIO 1-10) — ADC2 pins fail while WiFi is on.
//  beginPins() warns on a non-ADC1 pin.
//
//  CONTROLLABLE — Web API only:
//    GET /api/watering/set?pump=0|1    pump off / on
//  Readings:  moisture_raw (0-4095), moisture_pct (0-100, relative),
//             pump (1 = running).
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Watering : public IPinDevice {
 public:
  PinDevice_Watering(uint8_t moisturePin, uint8_t pumpPin)
      : _aPin(moisturePin), _pPin(pumpPin) {}

  const char* name() const override { return "Watering Unit"; }
  const char* slug() const override { return "watering"; }
  bool controllable() const override { return true; }

  bool beginPins() override {
    pinMode(_pPin, OUTPUT);
    digitalWrite(_pPin, LOW);  // pump off at boot
    // ADC1 ranges differ by chip: GPIO32-39 on the original ESP32,
    // GPIO1-10 on the ESP32-S3.  Warn only when the pin is outside
    // both, which is a near-certain wiring mistake.
    if (!((_aPin >= 32 && _aPin <= 39) || (_aPin >= 1 && _aPin <= 10)))
      Serial.printf(
          "[Pin] WARNING: %s moisture pin GPIO%u may not be an "
          "ADC1 pin — analogRead can fail with WiFi on\n",
          name(), _aPin);
    return true;
  }

  void update() override { _raw = analogRead(_aPin); }

  void toJson(JsonObject& o) const override {
    o["moisture_raw"] = _raw;
    o["moisture_pct"] = _pct();
    o["pump"] = _pump ? 1 : 0;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"moisture_raw", static_cast<float>(_raw), ""};
    b[1] = {"moisture_pct", _pct(), "%"};
    b[2] = {"pump", static_cast<float>(_pump ? 1 : 0), ""};
    n = 3;
  }

  bool command(const String& param, const String& value) override {
    if (param == "pump") {
      int on = _parseBool(value);
      if (on < 0)
        return false;
      _pump = (on == 1);
      digitalWrite(_pPin, _pump ? HIGH : LOW);
      return true;
    }
    return false;
  }

  void controlSchema(JsonArray& out) const override {
    JsonObject p = out.add<JsonObject>();
    p["id"] = "pump";
    p["label"] = "Pump";
    p["type"] = "toggle";
    p["value"] = _pump ? 1 : 0;
  }

 private:
  uint8_t _aPin, _pPin;
  uint16_t _raw = 0;
  bool _pump = false;
  float _pct() const { return (_raw * 100.0f) / 4095.0f; }

  // Thin adapter over the shared cmd:: validator (src/CmdParse.h).
  static int _parseBool(const String& v) { return cmd::parseBool(v); }
};
