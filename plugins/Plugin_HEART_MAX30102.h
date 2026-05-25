#pragma once
// ============================================================
//  Plugin_HEART_MAX30102.h  –  M5Stack Heart Unit (MAX30102 rev)
//
//  Companion to Plugin_HEART.h, which handles the older
//  MAX30100-based Heart Unit.  Both register at 0x57 and the
//  framework picks the one whose PART_ID check matches the
//  silicon actually on the bus.  Ultrasonic Unit (also 0x57)
//  must be registered AFTER both of these strict plugins.
//
//  Like Plugin_HEART, this reports a live reading:
//
//    bpm   – beats per minute      (PpgBeatDetector)
//    spo2  – blood-oxygen estimate (PpgBeatDetector, ratio-of-ratios)
//    ir    – latest raw IR sample  (diagnostic, 18-bit)
//    red   – latest raw RED sample (diagnostic, 18-bit)
//
//  The signal processing is the SHARED PpgBeatDetector class
//  (src/PpgBeatDetector.h) — identical maths to the MAX30100
//  plugin.  Only the chip plumbing differs.
//
//  MAX30102 vs MAX30100 – the differences that matter here:
//    • PART_ID (0xFF)        : 0x15  (vs 0x11)
//    • MODE_CONFIG           : 0x09  (vs 0x06)
//    • FIFO_WR_PTR / OVF /
//      FIFO_RD_PTR           : 0x04 / 0x05 / 0x06
//                              (vs 0x02 / 0x03 / 0x04)
//    • FIFO_DATA             : 0x07  (vs 0x05)
//    • FIFO depth            : 32 samples (vs 16)
//    • Sample width          : 18-bit, 3 bytes/channel
//                              (vs 16-bit, 2 bytes/channel)
//    • SpO2 FIFO order       : RED then IR  (vs IR then RED!)
//    • LED current           : LED1_PA 0x0C (RED)
//                              LED2_PA 0x0D (IR), 0.2 mA/step
//                              (vs single LED_CONFIG 0x09)
// ============================================================
#include "../src/IDevice.h"
#include "../src/PpgBeatDetector.h"

class Plugin_HEART_MAX30102 : public IDevice {
 public:
  const char* name() const override { return "Heart Unit (MAX30102)"; }
  const char* slug() const override { return "heart30102"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x57;
    n = 1;
  }

  // Drain the FIFO every loop iteration — see fastPoll().
  bool wantsFastPoll() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;

    // ── Part ID check ─────────────────────────────────────────
    //   0xFF = PART_ID register.  MAX30102 reports 0x15.
    //   MAX30100 reports 0x11 and is handled by Plugin_HEART;
    //   returning false here lets that plugin claim the slot.
    if (regRead8(0xFF) != 0x15)
      return false;

    // ── Soft reset ────────────────────────────────────────────
    //   MODE_CONFIG bit 6 = RESET; chip self-clears when done.
    //   Register address is 0x09 on MAX30102 (NOT 0x06, which is
    //   FIFO_RD_PTR on this chip).
    regWrite(0x09, 0x40);
    for (uint8_t i = 0; i < 50; i++) {
      delay(2);
      if ((regRead8(0x09) & 0x40) == 0)
        break;
    }

    // ── Clear FIFO pointers ───────────────────────────────────
    regWrite(0x04, 0x00);  // FIFO_WR_PTR
    regWrite(0x05, 0x00);  // OVF_COUNTER
    regWrite(0x06, 0x00);  // FIFO_RD_PTR

    // ── FIFO config (0x08) ────────────────────────────────────
    //   bits 7-5 : SMP_AVE          (000 = no averaging)
    //   bit  4   : FIFO_ROLLOVER_EN (1 = wrap on overflow)
    //   bits 3-0 : FIFO_A_FULL      (0 = unused, we poll)
    //   → 0x10.  No on-chip averaging: we want the full 50 Hz
    //   sample stream feeding the beat detector, not a decimated
    //   one.  Rollover-on means a momentary stall can't wedge
    //   the FIFO.
    regWrite(0x08, 0x10);

    // ── SpO2 configuration (0x0A) ─────────────────────────────
    //   bits 6-5 : SPO2_ADC_RGE   (01 = 4096 nA full-scale)
    //   bits 4-2 : SPO2_SR        (000 = 50 samples/sec)
    //   bits 1-0 : LED_PW         (11  = 411 µs, 18-bit res)
    //   → 0x23.  50 sps matches Plugin_HEART and the detector's
    //   20 ms sample period; the 32-deep FIFO then takes ~640 ms
    //   to overflow — ample margin for fastPoll draining.
    regWrite(0x0A, 0x23);

    // ── Disable all interrupts (we poll the FIFO) ─────────────
    regWrite(0x02, 0x00);  // INTR_ENABLE_1
    regWrite(0x03, 0x00);  // INTR_ENABLE_2

    // ── LED currents ──────────────────────────────────────────
    //   Two separate registers.  Each step 0.2 mA; 0x00 = OFF,
    //   0xFF ≈ 51 mA.  0x24 ≈ 7.2 mA per LED — good SNR through
    //   a fingertip, easy on the Grove 3V3 rail.  Turn DOWN if
    //   IR pins near 0x3FFFF (18-bit saturation).
    regWrite(0x0C, 0x24);  // LED1_PA  (RED)
    regWrite(0x0D, 0x24);  // LED2_PA  (IR)

    // ── Enable SpO2 mode (both LEDs pulse) ────────────────────
    //   MODE bits 2-0:  010 = HR only,  011 = SpO2.
    regWrite(0x09, 0x03);

    delay(10);       // let the analog front-end settle
    _det.begin(20);  // 50 Hz sensor → 20 ms per sample
    return true;
  }

  // Bare quick-command probes can desync the chip while the LEDs
  // pulse.  PART_ID is a static 0x15 and answers independent of
  // the AFE state.
  bool isAlive() override { return regRead8(0xFF) == 0x15; }

  // ── fastPoll ──────────────────────────────────────────────
  //  Called every loop iteration.  Drains the MAX30102 FIFO and
  //  feeds each IR/RED pair to the shared beat detector.
  //
  //  The MAX30102 FIFO is 32 deep with 5-bit pointers, so
  //  count = (WR_PTR − RD_PTR) & 0x1F.  Capped at 16 samples per
  //  call (16 × 6 = 96 bytes) to stay within the Arduino Wire RX
  //  buffer; anything beyond is picked up on the next fastPoll.
  //  In normal operation only 0–2 samples are waiting.
  void fastPoll() override {
    uint8_t wr = regRead8(0x04) & 0x1F;  // FIFO_WR_PTR
    uint8_t rd = regRead8(0x06) & 0x1F;  // FIFO_RD_PTR
    uint8_t count = static_cast<uint8_t>((wr - rd) & 0x1F);
    if (count == 0)
      return;
    if (count > 16)
      count = 16;

    // 6 bytes/sample in SpO2 mode: RED (3 bytes) then IR (3
    // bytes), each an 18-bit big-endian value with the top 6
    // bits zero.  Reading FIFO_DATA (0x07) auto-advances RD_PTR.
    uint8_t d[96];
    if (!regRead(0x07, d, static_cast<uint8_t>(count * 6)))
      return;

    for (uint8_t i = 0; i < count; i++) {
      uint32_t red = (static_cast<uint32_t>(d[i * 6]) << 16) |
                     (static_cast<uint32_t>(d[i * 6 + 1]) << 8) |
                     static_cast<uint32_t>(d[i * 6 + 2]);
      uint32_t ir = (static_cast<uint32_t>(d[i * 6 + 3]) << 16) |
                    (static_cast<uint32_t>(d[i * 6 + 4]) << 8) |
                    static_cast<uint32_t>(d[i * 6 + 5]);
      red &= 0x3FFFF;
      ir &= 0x3FFFF;
      _ir = ir;
      _red = red;
      _det.addSample(static_cast<float>(ir), static_cast<float>(red));
    }
  }

  // All real work happens in fastPoll(); update() has nothing to
  // do.  Left empty intentionally.
  void update() override {}

  void toJson(JsonObject& o) const override {
    o["bpm"] = _det.bpm();
    o["bpm_unit"] = "bpm";
    o["spo2"] = _det.spo2();
    o["spo2_unit"] = "%";
    o["ir"] = _ir;
    o["red"] = _red;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"bpm", _det.bpm(), "bpm"};
    b[1] = {"spo2", _det.spo2(), "%"};
    b[2] = {"ir", static_cast<float>(_ir), "raw"};
    b[3] = {"red", static_cast<float>(_red), "raw"};
    n = 4;
  }

 private:
  PpgBeatDetector _det;        // shared HR + SpO2 signal processing
  uint32_t _ir = 0, _red = 0;  // latest raw 18-bit samples (diagnostic)
};
