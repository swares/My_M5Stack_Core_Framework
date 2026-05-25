#pragma once
// ============================================================
//  UartDevice_GPS.h  –  M5Stack GPS Unit (NMEA)   [UART]
//
//  A GNSS receiver that streams NMEA sentences over UART — the
//  AT6558-based GPS Unit and the NEO-M9N "GPS v2" module both work.
//
//  ⚠ REQUIRES the "TinyGPSPlus" library (by Mikal Hart) — install
//  via the Arduino Library Manager.  Its #include is commented out
//  in the .ino by default; uncomment it with the registration line.
//
//      fw.addPlugin(new UartDevice_GPS(Serial2, 9600, 16, 17));
//
//  Readings:  fix (1 = position valid), lat, lng, alt_m, sats,
//             speed_kmph.
//  Note: lat/lng need more precision than a float SensorVal holds,
//  so getReadings() rounds them — /api/gps (toJson) carries the
//  full-precision double values.
// ============================================================
#include "../src/IUartDevice.h"
#include <TinyGPS++.h>

class UartDevice_GPS : public IUartDevice {
 public:
  UartDevice_GPS(HardwareSerial& port, uint32_t baud = 9600, int8_t rxPin = -1,
                 int8_t txPin = -1)
      : IUartDevice(port, baud, rxPin, txPin) {}

  const char* name() const override { return "GPS Unit (NMEA)"; }
  const char* slug() const override { return "gps"; }

  // NMEA streams continuously — drain the UART every loop so the
  // RX buffer never overflows and drops sentences.
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    while (_port->available())
      _gps.encode(static_cast<char>(_port->read()));
  }

  // Cache TinyGPS++ values into plain members: TinyGPS++'s
  // accessors are non-const, but toJson()/getReadings() are const.
  void update() override {
    _fix = _gps.location.isValid();
    _lat = _gps.location.lat();
    _lng = _gps.location.lng();
    _alt = _gps.altitude.meters();
    _sats = _gps.satellites.value();
    _speed = _gps.speed.kmph();
  }

  void toJson(JsonObject& o) const override {
    o["fix"] = _fix ? 1 : 0;
    o["lat"] = _lat;
    o["lng"] = _lng;
    o["alt_m"] = _alt;
    o["sats"] = _sats;
    o["speed_kmph"] = _speed;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"fix", static_cast<float>(_fix ? 1 : 0), ""};
    b[1] = {"lat", static_cast<float>(_lat), "deg"};
    b[2] = {"lng", static_cast<float>(_lng), "deg"};
    b[3] = {"alt_m", static_cast<float>(_alt), "m"};
    b[4] = {"sats", static_cast<float>(_sats), ""};
    b[5] = {"speed_kmph", static_cast<float>(_speed), "km/h"};
    n = 6;
  }

 private:
  TinyGPSPlus _gps;
  bool _fix = false;
  double _lat = 0, _lng = 0;
  double _alt = 0, _speed = 0;
  uint32_t _sats = 0;
};
