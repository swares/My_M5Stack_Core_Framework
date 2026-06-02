#pragma once
// ============================================================
//  Plugin_AS3935.h  –  AMS AS3935 Franklin lightning detector
//  (SparkFun Qwiic AS3935)                              0x03
//
//  An I2C lightning sensor that estimates the distance to the head
//  of an approaching storm and the "energy" of each strike.  It is a
//  HYBRID device: register access over I2C *plus* an interrupt line.
//  When it senses an event the IRQ pin goes HIGH and stays high until
//  the interrupt register (0x03) is read — that read tells you the
//  event type (lightning / disturber / noise) and, for a strike, the
//  distance and energy.
//
//  ── Address (0x03) ─────────────────────────────────────────
//    SparkFun's Qwiic board defaults to 0x03 (the two address pads
//    select 0x00–0x03).  That's in the "reserved" low I2C range many
//    scanners skip — but this framework probes the FULL 1..126 range,
//    so it auto-detects normally.  No WHO_AM_I; begin() verifies the
//    chip by issuing PRESET_DEFAULT and checking reg 0x00 reads its
//    reset value (0x24), which rejects anything else sitting at 0x03.
//
//  ── Wiring ─────────────────────────────────────────────────
//    Grove/Port-A carries only I2C (SDA/SCL), so run the sensor's I2C
//    on Port-A (Wire1) AND jump its **IRQ** pin to any free input GPIO
//    — pass that pin to the constructor.  Pass irqPin = -1 to skip the
//    interrupt wire and poll the INT register each update() instead
//    (fine — events are infrequent and the register latches until read).
//
//  ── Constructor ────────────────────────────────────────────
//      Plugin_AS3935(irqPin=36, outdoor=true, tuneCap=0,
//                    maskDisturbers=false)
//    outdoor       — AFE gain preset (outdoor = less sensitive, fewer
//                    false events; indoor = more sensitive).
//    tuneCap       — antenna tuning capacitor 0..15 (×8 pF).  0 = leave
//                    at the board's tuning; bump only if you've measured
//                    the LCO off 500 kHz.
//    maskDisturbers— hide man-made "disturber" events (cuts noise; may
//                    also hide the earliest hints of a storm).
//
//  ── Readings ───────────────────────────────────────────────
//      distance_km   estimated distance to storm head (km); -1 = the
//                    sensor reports "out of range"
//      energy        raw strike "energy" (20-bit, NOT joules)
//      strikes       lightning count since boot
//    + JSON: last_event (none/lightning/disturber/noise), disturbers,
//      noise_events, mode (indoor/outdoor), tune_cap.
// ============================================================
#include "../src/IDevice.h"

class Plugin_AS3935 : public IDevice {
 public:
  explicit Plugin_AS3935(int8_t irqPin = 36, bool outdoor = true,
                         uint8_t tuneCap = 0, bool maskDisturbers = false)
      : _irqPin(irqPin),
        _outdoor(outdoor),
        _tuneCap(tuneCap & 0x0F),
        _mask(maskDisturbers) {}

  const char* name() const override { return "Lightning Detector (AS3935)"; }
  const char* slug() const override { return "lightning"; }

  // SparkFun Qwiic default.  (Alternate pad settings give 0x00–0x02;
  // change here if you've re-strapped the board.)
  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x03;
    n = 1;
  }
  // Qwiic / Port-A unit.
  I2CBus preferredBus() const override { return I2CBus::External; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;

    // Reset to defaults, then verify reg 0x00 == its reset value (0x24,
    // AFE gain 18 / powered).  Anything else at 0x03 fails this.
    regWrite(REG_PRESET, 0x96);   // PRESET_DEFAULT
    delay(3);
    if (regRead8(REG_AFE) != 0x24) {
      Serial.println(F("[AS3935] reg0x00 != reset default — not an AS3935"));
      return false;
    }
    regWrite(REG_CALIB, 0x96);    // CALIB_RCO — calibrate internal RCs
    delay(3);

    setIndoorOutdoor(_outdoor);
    if (_tuneCap) setTuneCap(_tuneCap);
    setMaskDisturbers(_mask);

    // IRQ line: HIGH on event until the INT register is read.
    if (_irqPin >= 0) {
      pinMode(_irqPin, INPUT);
      attachInterruptArg(digitalPinToInterrupt(_irqPin),
                         &Plugin_AS3935::isr, this, RISING);
    }
    delay(3);
    regRead8(REG_INT);   // clear any pending interrupt latched at boot

    Serial.printf("[AS3935] ready — %s mode, IRQ %s, tuneCap=%u\n",
                  _outdoor ? "outdoor" : "indoor",
                  _irqPin >= 0 ? String(_irqPin).c_str() : "poll",
                  _tuneCap);
    return true;
  }

  void update() override {
    // Service an event when the IRQ fired, or (poll mode) every update.
    if (_irqPin >= 0 && !_irqFlag)
      return;
    _irqFlag = false;

    // Datasheet wants ~2 ms after IRQ before reading INT; the poll
    // interval (>=POLL_MS) already covers that.
    uint8_t intg = regRead8(REG_INT) & 0x0F;
    if (intg == INT_LIGHTNING) {
      _lastEvent = "lightning";
      uint8_t d = regRead8(REG_DISTANCE) & 0x3F;
      _distanceKm = (d == 0x3F) ? -1 : static_cast<int>(d);  // 0x3F = out of range
      uint8_t e0 = regRead8(REG_ENERGY_L);
      uint8_t e1 = regRead8(REG_ENERGY_M);
      uint8_t e2 = regRead8(REG_ENERGY_MM) & 0x1F;
      _energy = (static_cast<uint32_t>(e2) << 16) |
                (static_cast<uint32_t>(e1) << 8) | e0;
      _strikes++;
    } else if (intg == INT_DISTURBER) {
      _lastEvent = "disturber";
      _disturbers++;
    } else if (intg == INT_NOISE) {
      _lastEvent = "noise";
      _noise++;
    }
  }

  void toJson(JsonObject& o) const override {
    o["last_event"] = _lastEvent;
    o["distance_km"] = _distanceKm;
    o["distance_km_unit"] = "km";
    o["energy"] = _energy;
    o["strikes"] = _strikes;
    o["disturbers"] = _disturbers;
    o["noise_events"] = _noise;
    o["mode"] = _outdoor ? "outdoor" : "indoor";
    o["tune_cap"] = _tuneCap;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"distance_km", static_cast<float>(_distanceKm), "km"};
    b[1] = {"energy", static_cast<float>(_energy), ""};
    b[2] = {"strikes", static_cast<float>(_strikes), ""};
    n = 3;
  }

  // ── Runtime config helpers (also handy from the .ino) ─────
  void setIndoorOutdoor(bool outdoor) {
    _outdoor = outdoor;
    uint8_t gain = outdoor ? AFE_OUTDOOR : AFE_INDOOR;  // field value
    uint8_t v = regRead8(REG_AFE);
    v = (v & ~0x3E) | ((gain & 0x1F) << 1);  // AFE_GB = bits 5:1
    regWrite(REG_AFE, v);
  }
  void setTuneCap(uint8_t cap) {
    _tuneCap = cap & 0x0F;
    uint8_t v = regRead8(REG_TUNE);
    v = (v & 0xF0) | _tuneCap;
    regWrite(REG_TUNE, v);
  }
  void setMaskDisturbers(bool on) {
    _mask = on;
    uint8_t v = regRead8(REG_INT);
    if (on) v |= 0x20; else v &= ~0x20;  // MASK_DIST = bit 5
    regWrite(REG_INT, v);
  }

 private:
  // ── Register map ──────────────────────────────────────────
  static constexpr uint8_t REG_AFE       = 0x00;  // AFE_GB + PWD
  static constexpr uint8_t REG_INT       = 0x03;  // INT bits + MASK_DIST
  static constexpr uint8_t REG_ENERGY_L  = 0x04;
  static constexpr uint8_t REG_ENERGY_M  = 0x05;
  static constexpr uint8_t REG_ENERGY_MM = 0x06;  // bits 4:0
  static constexpr uint8_t REG_DISTANCE  = 0x07;  // bits 5:0
  static constexpr uint8_t REG_TUNE      = 0x08;  // TUN_CAP bits 3:0
  static constexpr uint8_t REG_CALIB     = 0x3C;  // CALIB_RCO
  static constexpr uint8_t REG_PRESET    = 0x3D;  // PRESET_DEFAULT

  static constexpr uint8_t INT_NOISE     = 0x01;
  static constexpr uint8_t INT_DISTURBER = 0x04;
  static constexpr uint8_t INT_LIGHTNING = 0x08;

  static constexpr uint8_t AFE_INDOOR    = 0x12;  // 18 — more sensitive
  static constexpr uint8_t AFE_OUTDOOR   = 0x0E;  // 14 — fewer false events

  static void IRAM_ATTR isr(void* arg) {
    static_cast<Plugin_AS3935*>(arg)->_irqFlag = true;
  }

  int8_t  _irqPin;
  bool    _outdoor;
  uint8_t _tuneCap;
  bool    _mask;

  volatile bool _irqFlag = false;
  const char* _lastEvent = "none";
  int      _distanceKm = -1;
  uint32_t _energy = 0;
  uint32_t _strikes = 0, _disturbers = 0, _noise = 0;
};
