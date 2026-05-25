#pragma once
// ============================================================
//  Plugin_FACES2_ENCODER.h  –  M5Stack Faces II Encoder panel
//
//  STACKABLE faceplate — five rotary encoders, each with a push
//  switch.  MEGA328P at I2C 0x5E.
//
//  INPUT only — nothing to control.  Reports:
//    enc0..enc4   – cumulative quadrature count per encoder
//                   (signed; the firmware accumulates, so a
//                    slow 500 ms poll never loses steps)
//    buttons      – bitfield of the five push switches
//                   (bit 0 = encoder 0 ... bit 4 = encoder 4)
//
//  ⚠ REGISTER MAP NOT YET HARDWARE-VERIFIED.
//  M5Stack's STM32/MEGA encoder peripherals normally expose a
//  little-endian counter block followed by a button byte; this
//  plugin assumes:
//      0x00 + 4·n   int32 count for encoder n  (LSB first)
//      0x20         button bitfield
//  If the live values look wrong (stuck, wildly large, or not
//  changing as you turn the dials) the offsets/width need
//  adjusting against the Faces II Encoder firmware.  Everything
//  else in the plugin is correct regardless.
// ============================================================
#include "../src/IDevice.h"

class Plugin_FACES2_ENCODER : public IDevice {
 public:
  const char* name() const override { return "Faces II Encoder"; }
  const char* slug() const override { return "faces2enc"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x5E;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Presence check — confirm the counter block reads back.
    uint8_t tmp[20];
    return regRead(0x00, tmp, 20);
  }

  void update() override {
    // Five int32 counters, little-endian, contiguous from 0x00.
    uint8_t d[20];
    if (regRead(0x00, d, 20)) {
      for (uint8_t i = 0; i < 5; i++) {
        _count[i] = static_cast<int32_t>(
            static_cast<uint32_t>(d[i * 4]) |
            (static_cast<uint32_t>(d[i * 4 + 1]) << 8) |
            (static_cast<uint32_t>(d[i * 4 + 2]) << 16) |
            (static_cast<uint32_t>(d[i * 4 + 3]) << 24));
      }
    }
    _buttons = regRead8(0x20);
  }

  void toJson(JsonObject& o) const override {
    for (uint8_t i = 0; i < 5; i++)
      o[String("enc") + i] = _count[i];
    o["buttons"] = _buttons;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    static const char* k[5] = {"enc0", "enc1", "enc2", "enc3", "enc4"};
    for (uint8_t i = 0; i < 5; i++)
      b[i] = {k[i], static_cast<float>(_count[i]), ""};
    b[5] = {"buttons", static_cast<float>(_buttons), ""};
    n = 6;
  }

 private:
  int32_t _count[5] = {0};
  uint8_t _buttons = 0;
};
