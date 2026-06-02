#pragma once
// ============================================================
//  PinDevice_Geiger.h  –  GM-tube Geiger counter          [non-I2C]
//
//  A Geiger-Müller tube board (J305 / SBM-20 / RadiationD-v1.1
//  "CAJOE" etc.) emits one short digital pulse per ionizing-
//  radiation event on its OUT / VIN / INT line.  Electrically it
//  is just a fast digital input — like the Button / Hall units —
//  but the pulses are ~tens of µs wide and arrive in bursts, so
//  they are counted with a HARDWARE INTERRUPT, not by polling.
//
//  ── Wiring (Port-B, M5Stack Core / Core2) ──────────────────
//      Module VCC  → 5V        Module GND → GND
//      Module OUT  → GPIO36  (Port-B White)   ← any interrupt pin
//    CoreS3: Port-B White is G9 — pass it explicitly:
//      fw.addPlugin(new PinDevice_Geiger(9));
//
//  ── How a reading is built ─────────────────────────────────
//    • An IRAM_ATTR ISR (attachInterruptArg, per-instance) does
//      nothing but increment a volatile counter on each FALLING
//      edge — safe for high burst rates.
//    • update() keeps a 60-slot ring of counts-per-second and
//      reports CPM over that rolling 1-minute window.
//    • Dose rate µSv/h = CPM ÷ conversion factor:
//          SBM-20 ≈ 153.8 (≈154) · J305 ≈ 123
//    • Cumulative dose (µSv) is integrated from the dose rate.
//
//  ── Constructor ────────────────────────────────────────────
//      PinDevice_Geiger(signalPin=36,
//                       cpmPerUSv=154.0,   // tube factor
//                       buzzerPin=-1,      // -1 = no buzzer
//                       alarmUSv=5.0,      // DANGER threshold
//                       emitTrace=false,   // live sparkline (see below)
//                       clickSound=false)  // audible ticks (see below)
//    e.g.  fw.addPlugin(new PinDevice_Geiger(36, 154.0f, 25, 5.0f));
//          //                signal ─┘   SBM-20 ┘  buzzer┘  alarm┘
//
//  ── Audible per-click ticks (optional — off by default) ─────
//    With a buzzer pin set, clickSound=true chirps a ~3 ms blip on
//    each new count — the classic Geiger "tick".  It rides the same
//    self-contained LEDC channel as the alarm and yields to it: a
//    DANGER alarm tone overrides ticks, which resume once it clears.
//    Only enables fastPoll() when actually used, so it's free when
//    off.  At high count rates the ticks blur into a buzz, as on a
//    real counter.
//
//  ── Live trace (optional — off by default) ─────────────────
//    The 60-second ring already exists to compute CPM, so the
//    dashboard sparkline costs no extra device RAM — but emitting
//    it as a JSON "trace" array on every poll costs CPU + payload
//    bytes.  Leave emitTrace=false to conserve both; pass true
//    (or call setTrace(true)) only when you want the live graph.
//    The dashboard card hides the sparkline when no trace is sent.
//
//  ── Readings ───────────────────────────────────────────────
//      cpm          counts per minute (rolling 60 s)
//      usv_per_h    dose rate, µSv/h
//    + JSON: total_counts, dose_uSv, status (BACKGROUND/ELEVATED/
//      DANGER), alarm (0/1), tube factor, pin, settling flag,
//      and — only when emitTrace — trace[] (counts/sec) + trace_secs.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Geiger : public IPinDevice {
 public:
  explicit PinDevice_Geiger(uint8_t signalPin = 36,
                            float cpmPerUSv = 154.0f,
                            int8_t buzzerPin = -1,
                            float alarmUSv = 5.0f,
                            bool emitTrace = false,
                            bool clickSound = false)
      : _pin(signalPin),
        _cpmPerUSv(cpmPerUSv > 0 ? cpmPerUSv : 154.0f),
        _buzzer(buzzerPin),
        _alarmUSv(alarmUSv),
        _emitTrace(emitTrace),
        _clickSound(clickSound) {}

  const char* name() const override { return "Geiger Counter"; }
  const char* slug() const override { return "geiger"; }

  // Turn the live sparkline data on/off at runtime (default off to
  // save CPU + payload bytes — see header note).
  void setTrace(bool on) { _emitTrace = on; }
  // Audible per-click ticks on/off (needs a buzzer pin).
  void setClickSound(bool on) { _clickSound = on; }

  // Only burn a fast-poll slot when we actually emit ticks; pure
  // counting is interrupt-driven and needs no polling.
  bool wantsFastPoll() const override { return _clickSound && _buzzer >= 0; }

  bool beginPins() override {
    pinMode(_pin, INPUT);  // most GM boards drive a push-pull pulse
    // Per-instance ISR — supports more than one tube on different pins.
    attachInterruptArg(digitalPinToInterrupt(_pin), &PinDevice_Geiger::isr,
                       this, FALLING);
    if (_buzzer >= 0) {
      ledcAttach(_buzzer, 2700, 10);  // alarm tone channel (arduino-esp32 3.x)
      ledcWriteTone(_buzzer, 0);      // silent until alarm
    }
    uint32_t now = millis();
    _secMark = now;
    _startMs = now;
    _lastDoseMs = now;
    for (uint8_t i = 0; i < 60; i++) _ring[i] = 0;
    return true;
  }

  void update() override {
    uint32_t now = millis();

    // ── fold pulses into the per-second ring ──────────────
    uint32_t total = _clicks;                       // 32-bit read is atomic
    _secAccum += static_cast<uint16_t>(total - _lastClicks);
    _lastClicks = total;
    while (now - _secMark >= 1000) {
      _ring[_idx] = _secAccum;
      _secAccum = 0;
      _idx = (_idx + 1) % 60;
      if (_idx == 0) _full = true;
      _secMark += 1000;
    }

    // ── CPM over the available window (scales while settling) ──
    uint8_t secs = _full ? 60 : _idx;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < secs; i++) sum += _ring[i];
    if (secs >= 60) {
      _cpm = sum;                                   // full minute window → CPM
    } else if (secs > 0) {
      _cpm = static_cast<uint32_t>(sum * 60.0f / secs);
    } else {
      _cpm = _secAccum * 60;                        // first second, rough
    }
    _settling = (now - _startMs) < 60000;

    // ── dose rate + cumulative dose ───────────────────────
    _usv = _cpm / _cpmPerUSv;
    float dtHours = (now - _lastDoseMs) / 3600000.0f;
    _doseUSv += _usv * dtHours;
    _lastDoseMs = now;
    _total = total;

    // ── status + alarm ────────────────────────────────────
    _alarm = (_usv >= _alarmUSv);
    if (_buzzer >= 0 && _alarm != _buzzing) {
      ledcWriteTone(_buzzer, _alarm ? 2700 : 0);
      _buzzing = _alarm;
    }
  }

  // Audible "tick" per count — a short chirp that yields to the alarm.
  // Runs only when clickSound + a buzzer pin are set (wantsFastPoll).
  void fastPoll() override {
    if (_buzzer < 0 || !_clickSound) return;
    uint32_t now = millis();
    if (_ticking && now >= _tickEndMs) {
      if (!_buzzing) ledcWriteTone(_buzzer, 0);  // never cut the alarm tone
      _ticking = false;
    }
    uint32_t total = _clicks;
    if (total != _lastTickClicks) {
      _lastTickClicks = total;
      if (!_buzzing && !_ticking) {              // alarm tone takes priority
        ledcWriteTone(_buzzer, kTickFreq);
        _tickEndMs = now + kTickMs;
        _ticking = true;
      }
    }
  }

  void toJson(JsonObject& o) const override {
    o["cpm"] = _cpm;
    o["usv_per_h"] = roundf(_usv * 1000) / 1000.0f;
    o["usv_per_h_unit"] = "uSv/h";
    o["total_counts"] = _total;
    o["dose_uSv"] = roundf(_doseUSv * 100) / 100.0f;
    o["status"] = status();
    o["alarm"] = _alarm ? 1 : 0;
    o["tube_factor"] = _cpmPerUSv;
    o["settling"] = _settling ? 1 : 0;
    o["pin"] = _pin;

    // Optional live trace — counts per second, oldest → newest.
    // The ring exists anyway for CPM; this just publishes it.
    if (_emitTrace) {
      JsonArray tr = o["trace"].to<JsonArray>();
      uint8_t secs = _full ? 60 : _idx;
      uint8_t start = _full ? _idx : 0;
      for (uint8_t i = 0; i < secs; i++) tr.add(_ring[(start + i) % 60]);
      o["trace_secs"] = secs;
    }
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"cpm", static_cast<float>(_cpm), "CPM"};
    b[1] = {"usv_per_h", _usv, "uSv/h"};
    n = 2;
  }

 private:
  static void IRAM_ATTR isr(void* arg) {
    static_cast<PinDevice_Geiger*>(arg)->_clicks++;
  }

  const char* status() const {
    if (_usv >= _alarmUSv) return "DANGER";
    if (_usv >= _warnUSv) return "ELEVATED";
    return "BACKGROUND";
  }

  uint8_t _pin;
  float _cpmPerUSv;
  int8_t _buzzer;
  float _alarmUSv;
  bool _emitTrace;
  bool _clickSound;
  float _warnUSv = 1.0f;  // ELEVATED threshold, µSv/h

  static constexpr uint16_t kTickFreq = 3500;  // Hz, click pitch
  static constexpr uint16_t kTickMs = 3;       // click length

  volatile uint32_t _clicks = 0;  // ISR-incremented, never reset
  uint32_t _lastClicks = 0;
  uint16_t _ring[60] = {0};       // counts per second, rolling
  uint8_t _idx = 0;
  bool _full = false;
  uint16_t _secAccum = 0;
  uint32_t _secMark = 0;

  uint32_t _startMs = 0, _lastDoseMs = 0;
  uint32_t _cpm = 0, _total = 0;
  float _usv = 0, _doseUSv = 0;
  bool _settling = true, _alarm = false, _buzzing = false;
  uint32_t _lastTickClicks = 0, _tickEndMs = 0;
  bool _ticking = false;
};
