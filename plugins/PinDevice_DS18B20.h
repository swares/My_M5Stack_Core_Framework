#pragma once
// ============================================================
//  PinDevice_DS18B20.h  –  DS18B20 1-Wire temperature  [non-I2C]
//
//  A DS18B20 digital thermometer on a 1-Wire bus — one GPIO line
//  with a 4.7 kΩ pull-up to 3V3.
//
//  ⚠ REQUIRES TWO LIBRARIES (install via the Arduino Library
//  Manager):
//      • "OneWire"            by Paul Stoffregen
//      • "DallasTemperature"  by Miles Burton
//  Because of that dependency this header is NOT #included by the
//  .ino by default — uncomment its #include there together with
//  the registration line.
//
//      fw.addPlugin(new PinDevice_DS18B20(26));
//
//  The temperature conversion runs NON-BLOCKING: update() reads the
//  result requested last cycle and immediately kicks off the next,
//  so the main loop never stalls.  Resolution is 10-bit (~0.25 °C,
//  ~190 ms) which comfortably finishes within one poll cycle.
//
//  Reading:  temp (°C).  A value of -127 means no sensor detected.
// ============================================================
#include "../src/IPinDevice.h"
#include <OneWire.h>
#include <DallasTemperature.h>

class PinDevice_DS18B20 : public IPinDevice {
 public:
  explicit PinDevice_DS18B20(uint8_t pin)
      : _pin(pin), _wire(pin), _sensors(&_wire) {}

  const char* name() const override { return "DS18B20 Temp"; }
  const char* slug() const override { return "ds18b20"; }

  bool beginPins() override {
    _sensors.begin();
    _sensors.setResolution(10);            // ~0.25 °C, ~190 ms
    _sensors.setWaitForConversion(false);  // non-blocking
    _count = _sensors.getDeviceCount();
    if (_count == 0)
      Serial.printf(
          "[Pin] WARNING: %s — no 1-Wire sensor on GPIO%u "
          "(check the 4.7k pull-up resistor)\n",
          name(), _pin);
    _sensors.requestTemperatures();  // kick off the first read
    return true;
  }

  void update() override {
    // The conversion requested last cycle (>= POLL_MS ago) is done.
    _temp = _sensors.getTempCByIndex(0);
    _sensors.requestTemperatures();  // start the next one
  }

  void toJson(JsonObject& o) const override {
    o["temp"] = _temp;
    o["sensors"] = _count;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"temp", _temp, "°C"};
    n = 1;
  }

 private:
  uint8_t _pin;
  OneWire _wire;
  DallasTemperature _sensors;
  float _temp = -127.0f;
  uint8_t _count = 0;
};
