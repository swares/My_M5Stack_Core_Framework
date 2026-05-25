#pragma once
// ============================================================
//  IPinDevice.h  –  Base class for non-I2C "pin" devices
//
//  The framework's IDevice model is built around I2C: a plugin is
//  discovered by probing an I2C address.  M5Stack also makes many
//  units that are NOT I2C — GPIO, PWM, ADC, 1-Wire — such as the
//  PIR, Relay, Buzzer, Servo, Light (CdS) and Earth (soil) units.
//
//  IPinDevice lets those live in the same framework.  It derives
//  IDevice, so a pin device flows through the framework and every
//  output channel (Web API, MQTT, SD log, serial, display) with no
//  special-casing — but it stubs out the I2C contract:
//
//    • i2cAddresses() returns count 0, so the boot I2C scan never
//      tries to bind a pin device (there is no address to match).
//    • The framework instead calls beginPins() once at startup and,
//      if it succeeds, marks the device active.
//
//  KEY DIFFERENCE FROM AN I2C PLUGIN: there is NO auto-detection.
//  A bare GPIO/ADC pin cannot be probed for "is a PIR connected?".
//  A pin device is active because YOU registered it in the .ino
//  with the pin(s) it is wired to — the framework trusts that.
//
//  Pin numbers are passed to each concrete device's constructor:
//      fw.addPlugin(new PinDevice_PIR(/*signalPin=*/36));
//
//  ⚠ Pin choice matters:
//    • ADC: while WiFi is connected (this framework keeps it up)
//      only ADC1 pins — GPIO 32-39 — work for analogRead(); ADC2
//      pins silently fail.  The ADC devices warn on a non-ADC1 pin.
//    • Avoid GPIO 21/22 (Port-A I2C) and the LCD / SD SPI pins.
// ============================================================
#include "IDevice.h"

class IPinDevice : public IDevice {
 public:
  // Marks this as a non-I2C device — the framework routes startup
  // through beginPins() instead of the I2C scan/bind path.
  bool isPinDevice() const override { return true; }

  // ── I2C contract — stubbed out (a pin device has no I2C bus) ──
  // No address → the boot scan's address loop never matches us.
  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    (void)buf;
    n = 0;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  // The framework never calls the I2C begin() for a pin device
  // (n == 0 above), but IDevice declares it pure-virtual.
  bool begin(TwoWire* wire, uint8_t a) override {
    (void)wire;
    (void)a;
    return false;
  }
  // IDevice's default isAlive() pokes the I2C bus — which is null
  // for a pin device.  Override so it never dereferences it.
  bool isAlive() override { return active; }

  // ── Pin-device lifecycle ──────────────────────────────────
  //  Called once by Framework::begin() after the I2C scan.
  //  Configure pinMode() / ledcAttach() / etc. here.  Return true
  //  on success; the framework then marks the device active.
  virtual bool beginPins() = 0;
};
