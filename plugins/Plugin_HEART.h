#pragma once
// ============================================================
//  Plugin_HEART.h  –  M5Stack Mini Heart Rate Unit (MAX30100)
//
//  The MAX30100 is an optical pulse sensor: it pulses IR + red
//  LEDs and reports how much light returns.  Your pulse is a
//  small ripple on that signal.  This plugin turns the ripple
//  into a live reading:
//
//    bpm   – beats per minute      (PpgBeatDetector)
//    spo2  – blood-oxygen estimate (PpgBeatDetector, ratio-of-ratios)
//    ir    – latest raw IR sample  (diagnostic)
//    red   – latest raw RED sample (diagnostic)
//
//  How it works:
//    • The chip is configured for 50 samples/sec into its
//      16-deep FIFO.
//    • wantsFastPoll() returns true, so the framework calls
//      fastPoll() every loop iteration — far faster than the
//      500 ms POLL_MS — and we drain the FIFO each time.  At
//      50 Hz the FIFO needs ~320 ms to overflow; draining
//      every few ms leaves a huge margin.
//    • Each IR/RED pair is handed to PpgBeatDetector, the
//      shared signal-processing class (see src/PpgBeatDetector.h)
//      — the same one Plugin_HEART_MAX30102 uses.
//
//  Accuracy note: this is a hobby-grade reflectance sensor.
//  BPM settles within a few seconds of a still fingertip.
//  SpO2 is a genuine ratio-of-ratios computation but the
//  MAX30100 is uncalibrated — treat spo2 as a rough estimate.
//
//  Address 0x57.  If an Ultrasonic unit shares 0x57 on the
//  same bus, only the first-registered plugin binds.
// ============================================================
#include "../src/IDevice.h"
#include "../src/PpgBeatDetector.h"
#include "../src/Config.h"  // HEART_DEBUG

class Plugin_HEART : public IDevice {
 public:
  const char* name() const override { return "Heart Unit (MAX30100)"; }
  const char* slug() const override { return "heart"; }

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
    //   0xFF = PART_ID register, must read 0x11 for MAX30100.
    //   (MAX30102, a different chip on similar Grove units,
    //    reports 0x15 — handled by a separate plugin, not here.)
    if (regRead8(0xFF) != 0x11)
      return false;
    _lastSampleMs = millis();  // start the FIFO-stall watchdog clock

    // ── Configure, then PROVE the sensor is sampling ──────────
    //   _configure() now checks every register write, and
    //   _samplingStarted() confirms the FIFO write-pointer is
    //   actually advancing.  Historically begin() fired the config
    //   writes blind and returned true on the PART_ID read alone —
    //   so a sensor whose mode-enable write was dropped would bind,
    //   show a card, and report nothing but zeros.  One retry here
    //   recovers a single transient failed write on a marginal bus.
    for (uint8_t attempt = 1; attempt <= 2; attempt++) {
      bool cfgOk = _configure();
      bool live = _samplingStarted();
      if (cfgOk && live) {
        _det.begin(20);  // 50 Hz sensor → 20 ms per sample
        Serial.printf(
            "[Heart] MAX30100 ready — sampling confirmed "
            "(attempt %u)\n",
            (unsigned)attempt);
        return true;
      }
      Serial.printf("[Heart] attempt %u: config %s, FIFO %s\n",
                    (unsigned)attempt, cfgOk ? "ok" : "had a FAILED write",
                    live ? "advancing" : "NOT advancing");
    }

    // Still not sampling.  Bind anyway (return true) so the card and
    // this diagnostic stay visible — a vanished card tells the user
    // less than a visible card reading 0 plus the read-back below.
    Serial.println(
        F("[Heart] WARNING: MAX30100 not sampling — readings "
          "will stay 0.  Check the Grove cable and that the "
          "unit has power."));
    _dumpConfig();
    _det.begin(20);
    return true;
  }

  // MAX30100 with LEDs actively pulsing can desync on a bare
  // quick-command probe.  Use a PART_ID read instead — register
  // 0xFF is static (0x11) and answers independent of AFE state.
  bool isAlive() override { return regRead8(0xFF) == 0x11; }

  // ── fastPoll ──────────────────────────────────────────────
  //  Called every loop iteration by the framework.  Drains every
  //  sample currently in the MAX30100 FIFO and feeds each one to
  //  the shared beat detector.
  //
  //  Sample count = (WR_PTR − RD_PTR) & 0x0F.  We deliberately do
  //  NOT consult OVF_COUNTER: the MAX30100 doesn't clear it
  //  reliably, so trusting it can make us re-read stale samples.
  //  At 50 Hz the FIFO takes ~320 ms to lap; fastPoll runs every
  //  few ms, so a true overflow only happens if the main loop
  //  stalls for >0.3 s — a brief glitch, no lockup.
  void fastPoll() override {
    uint8_t wr = regRead8(0x02) & 0x0F;  // FIFO_WR_PTR
    uint8_t rd = regRead8(0x04) & 0x0F;  // FIFO_RD_PTR
    uint8_t count = static_cast<uint8_t>((wr - rd) & 0x0F);
    uint32_t now = millis();

#if HEART_DEBUG
    // Throttled (~1 Hz) so the hot loop isn't flooded.  Shows whether
    // the FIFO is producing samples and what raw values arrive.
    static uint32_t _dbgLast = 0;
    static uint32_t _drained = 0;
    _drained += count;
    if (now - _dbgLast >= 1000) {
      _dbgLast = now;
      Serial.printf(
          "[Heart] dbg: wr=%u rd=%u drained/s=%u "
          "ir=%u red=%u ac_pp=%.1f bpm=%.0f recov=%u\n",
          (unsigned)wr, (unsigned)rd, static_cast<uint32_t>(_drained),
          (unsigned)_ir, (unsigned)_red, _det.acIrPp(), _det.bpm(),
          (unsigned)_recoverTries);
      _drained = 0;
    }
#endif

    // ── FIFO-stall watchdog ───────────────────────────────────
    //  begin() proved the sensor was sampling, but a MAX30100 can
    //  later go quiet — a power blip, or an undefined state after a
    //  long I2C-silent gap (the multi-second WiFi / MQTT / SD boot).
    //  A stalled FIFO never advances, so every reading freezes at 0.
    //  If no sample has arrived for STALL_MS, re-run the init
    //  sequence to restart it.  Bounded by MAX_RECOVERIES so a truly
    //  dead unit can't hitch the loop forever.
    if (count > 0) {
      _lastSampleMs = now;
      _recoverTries = 0;
    } else {
      if (_recoverTries < MAX_RECOVERIES && now - _lastSampleMs > STALL_MS) {
        _recoverTries++;
        Serial.printf(
            "[Heart] FIFO stalled ~%us — re-configuring "
            "sensor (recovery %u/%u)\n",
            static_cast<uint32_t>((now - _lastSampleMs) / 1000),
            (unsigned)_recoverTries, (unsigned)MAX_RECOVERIES);
        if (_configure())
          _det.reset();
        _lastSampleMs = millis();  // restart the stall window
      }
      return;  // FIFO empty — nothing to drain
    }

    // 4 bytes/sample: IR hi, IR lo, RED hi, RED lo.  Reading
    // FIFO_DATA (0x05) auto-advances the read pointer.
    uint8_t d[64];
    if (!regRead(0x05, d, static_cast<uint8_t>(count * 4)))
      return;

    for (uint8_t i = 0; i < count; i++) {
      uint16_t ir = static_cast<uint16_t>((d[i * 4] << 8) | d[i * 4 + 1]);
      uint16_t red = static_cast<uint16_t>((d[i * 4 + 2] << 8) | d[i * 4 + 3]);
      _ir = ir;
      _red = red;
      _det.addSample(static_cast<float>(ir), static_cast<float>(red));
    }
  }

  // All real work happens in fastPoll(); update() (the 500 ms
  // cycle) has nothing to do.  Left empty intentionally.
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
  uint16_t _ir = 0, _red = 0;  // latest raw samples (diagnostic)

  // FIFO-stall watchdog state — see fastPoll().
  uint32_t _lastSampleMs = 0;  // millis() of the last FIFO sample
  uint8_t _recoverTries = 0;   // re-configure attempts since last sample
  static constexpr uint32_t STALL_MS = 3000;  // FIFO quiet this long → recover
  static constexpr uint8_t MAX_RECOVERIES = 3;  // then stop trying

  // ── _configure ────────────────────────────────────────────
  //  Soft-reset the MAX30100 and write its register set.  Unlike
  //  the old begin(), every write's ACK is checked — returns false
  //  if ANY write was not acknowledged, so begin() can retry.
  bool _configure() {
    bool ok = true;

    // Soft reset — MODE_CONFIG bit 6 = RESET; chip clears it when done.
    ok &= regWrite(0x06, 0x40);
    for (uint8_t i = 0; i < 50; i++) {
      delay(2);
      if ((regRead8(0x06) & 0x40) == 0)
        break;
    }

    // Clear FIFO pointers — without this the first reads are stale.
    ok &= regWrite(0x02, 0x00);  // FIFO_WR_PTR
    ok &= regWrite(0x03, 0x00);  // OVF_COUNTER
    ok &= regWrite(0x04, 0x00);  // FIFO_RD_PTR

    // SPO2_CONFIG (0x07): bit6 SPO2_HI_RES_EN, bits4-2 SPO2_SR,
    // bits1-0 LED_PW.  0x43 → 50 sps, 1600 µs pulse width, hi-res.
    // 50 sps (not 100) doubles the ~320 ms FIFO-overflow margin so a
    // brief main-loop stall can't cost samples, and still oversamples
    // a 0.5–3.3 Hz pulse waveform ~15–100×.
    ok &= regWrite(0x07, 0x43);

    // Disable all interrupts — we poll the FIFO.
    ok &= regWrite(0x01, 0x00);

    // LED_CONFIG (0x09): high nibble RED_PA, low nibble IR_PA.  Each
    // step ~3 mA.  0x48 → RED ~14 mA, IR ~27 mA.  RED is turned down
    // from the old 0x88 because it pinned at 0xFFFF whenever a finger
    // was on — that saturation clipped the RED AC and made SpO2 read
    // optimistically high.  IR keeps the stronger drive: it is the
    // heart-rate channel and reads ~38-40k, comfortably below full
    // scale.  Turn either nibble DOWN further if its channel still
    // saturates (e.g. on lighter skin).
    ok &= regWrite(0x09, 0x48);

    // MODE_CONFIG (0x06) bits 2-0: 010 = HR only, 011 = SpO2.  SpO2
    // mode is required — the RED channel feeds the ratio-of-ratios
    // oxygen estimate.  Written LAST: this is what starts sampling.
    ok &= regWrite(0x06, 0x03);

    delay(10);  // let the analog front-end settle
    return ok;
  }

  // ── _samplingStarted ──────────────────────────────────────
  //  After _configure() the chip should fill its FIFO at 50 Hz.
  //  Give it ~60 ms (≈3 samples) and confirm FIFO_WR_PTR moved off
  //  the zero we cleared it to.  If it's still 0 the chip isn't
  //  sampling — a dropped mode-enable write, or no power to the unit.
  bool _samplingStarted() {
    delay(60);
    return (regRead8(0x02) & 0x0F) != 0;
  }

  // ── _dumpConfig ───────────────────────────────────────────
  //  Print the key registers so a non-sampling sensor can be
  //  diagnosed from the serial log.  A healthy MAX30100 reads back
  //  MODE=0x03, SPO2=0x43, LED=0x88, PART=0x11, with WR climbing.
  //  PART reading 0x00 instead of 0x11 means I2C reads are failing
  //  outright (cable / pull-ups); the config bytes reading back as
  //  0x00 means the writes aren't landing.
  void _dumpConfig() {
    Serial.printf(
        "[Heart]   readback: MODE(0x06)=0x%02X "
        "SPO2(0x07)=0x%02X LED(0x09)=0x%02X "
        "WR=%u RD=%u PART(0xFF)=0x%02X\n",
        regRead8(0x06), regRead8(0x07), regRead8(0x09),
        (unsigned)(regRead8(0x02) & 0x0F), (unsigned)(regRead8(0x04) & 0x0F),
        regRead8(0xFF));
  }
};
