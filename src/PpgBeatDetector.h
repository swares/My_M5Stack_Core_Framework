#pragma once
// ============================================================
//  PpgBeatDetector.h  –  Heart-rate + SpO2 from a PPG stream.
//                        Hardware-agnostic, no I2C, no framework.
//
//  Photoplethysmography (PPG): an optical pulse sensor pulses
//  IR + red LEDs and measures the light that returns.  Your
//  heartbeat is a small ripple riding on a large DC baseline.
//  Feed this class raw IR/RED sample pairs at a fixed rate and
//  it produces:
//
//    bpm()  – beats per minute, from adaptive peak detection
//    spo2() – blood-oxygen estimate, red/IR ratio-of-ratios
//
//  Shared by Plugin_HEART (MAX30100) and Plugin_HEART_MAX30102
//  so the (fiddly) signal processing lives in exactly ONE place
//  — a bug fixed here is fixed for both sensors.
//
//  The pipeline is scale-agnostic: it works on the relative
//  AC/DC structure of the signal, so 16-bit MAX30100 counts and
//  18-bit MAX30102 counts both feed in without rescaling.
//
//  Tuning constants below assume ~50 Hz sampling.  Both current
//  callers run their sensor at 50 Hz; sampling much faster or
//  slower would want the filter cutoffs and timing windows
//  revisited.
//
//  Accuracy: BPM settles within a few seconds of a still
//  fingertip and is reasonably good.  SpO2 is a genuine
//  ratio-of-ratios computation, but these are uncalibrated
//  hobby sensors — treat spo2() as a rough estimate only.
// ============================================================
#include <Arduino.h>

class PpgBeatDetector {
 public:
  // sampleMs — milliseconds between samples fed to addSample()
  //            (20 for 50 Hz).  Used to convert peak spacing
  //            into a beat interval.
  void begin(uint32_t sampleMs) {
    _sampleMs = sampleMs ? sampleMs : 20;
    reset();
  }

  // Clear all DSP state.  Called by begin(); call directly if a
  // plugin re-initialises its sensor.
  void reset() {
    _bpm = _bpmAvg = _spo2 = 0;
    _dcInit = false;
    _dcIr = _dcRed = 0;
    _lpIr = _lpPrev = 0;
    _rising = false;
    _ampEnv = 0;
    _irMax = _redMax = -1e9f;
    _irMin = _redMin = 1e9f;
    _sampleCount = 0;
    _lastBeatSample = 0;
    _acIrPp = 0;
    _winLpMin = 1e9f;
    _winLpMax = -1e9f;
    _settleUntil = WARMUP_SAMPLES;
    _snapAt = 0;
  }

  // Feed one IR/RED sample pair.  Must be called at the fixed
  // rate declared via begin() — i.e. once per sensor sample.
  void addSample(float ir, float red) {
    _sampleCount++;

    // Seed the DC trackers from the first sample so they don't
    // spend a second crawling up from zero.
    if (!_dcInit) {
      _dcIr = ir;
      _dcRed = red;
      _dcInit = true;
    }

    // A finger placed or removed (or a hard knock) shows up as a big
    // step in the raw level.  The slow DC tracker would lag that step
    // by a second-plus, and the lag is a huge false AC swing the beat
    // detector reads as frantic beats — historically a finger-landing
    // spike slammed the envelope so high that the threshold then sat
    // above every real pulse for minutes.  Snap the DC baseline onto
    // the new level, clear the filters, and re-arm the settle window
    // instead of riding the lag out.
    if (_dcIr > 1.0f && fabsf(ir - _dcIr) > _dcIr * STEP_FRAC) {
      _dcIr = ir;
      _dcRed = red;
      _lpIr = _lpPrev = 0;
      _rising = false;
      _winLpMin = 1e9f;
      _winLpMax = -1e9f;
      _settleUntil = _sampleCount + WARMUP_SAMPLES;
      _snapAt = _sampleCount;
    }

    // DC baseline = slow exponential mean.  AC = sample − DC.
    _dcIr += (ir - _dcIr) * DC_ALPHA;
    _dcRed += (red - _dcRed) * DC_ALPHA;
    float acIr = ir - _dcIr;
    float acRed = red - _dcRed;

    // After a finger lands, perfusion keeps ramping for several
    // seconds.  The slow DC tracker lags that ramp, leaving an AC
    // swing far bigger than a real pulse — the snap catches the sharp
    // edge but not this slower tail, which would otherwise be
    // miscounted as a burst of fast beats.  Keep detection parked
    // while the swing exceeds SETTLE_AC_FRAC of the DC level, but
    // never longer than SETTLE_MAX so an unusually strong pulse can't
    // stall BPM forever.
    if (_dcIr > FINGER_DC_MIN && fabsf(acIr) > _dcIr * SETTLE_AC_FRAC &&
        _sampleCount - _snapAt < SETTLE_MAX) {
      _settleUntil = _sampleCount + WARMUP_SAMPLES;
    }

    // Low-pass the IR AC so peak detection isn't fooled by noise.
    _lpIr += (acIr - _lpIr) * LP_ALPHA;

    // Rolling-window peak-to-peak of the filtered IR AC.  This is the
    // exact signal _onPeak() measures beats against, so acIrPp() lets
    // a diagnostic compare the available swing to MIN_PEAK_AMP and
    // tell "no finger / too weak" apart from a genuine plumbing fault.
    // Snapshot once per AC_WINDOW_SAMPLES (~1 s at 50 Hz) so the value
    // tracks the most recent second rather than drifting all run.
    if (_lpIr < _winLpMin)
      _winLpMin = _lpIr;
    if (_lpIr > _winLpMax)
      _winLpMax = _lpIr;
    if (_sampleCount % AC_WINDOW_SAMPLES == 0) {
      _acIrPp = _winLpMax - _winLpMin;
      _winLpMin = 1e9f;
      _winLpMax = -1e9f;
    }

    // Track per-beat AC extremes on both channels for SpO2.
    if (acIr > _irMax)
      _irMax = acIr;
    if (acIr < _irMin)
      _irMin = acIr;
    if (acRed > _redMax)
      _redMax = acRed;
    if (acRed < _redMin)
      _redMin = acRed;

    // Hold off detection until the filters have settled — also
    // re-armed by the raw-level-step snap above.
    if (_sampleCount < _settleUntil) {
      _lpPrev = _lpIr;
      return;
    }

    // Peak detection: a local maximum is a rising sample
    // followed by a non-rising one.
    if (_lpIr > _lpPrev) {
      _rising = true;
    } else if (_rising) {
      _rising = false;
      _onPeak(_lpPrev);  // _lpPrev was the local maximum
    }
    _lpPrev = _lpIr;

    // No beat for a while → finger gone / bad contact: zero out.
    if (_sampleCount - _lastBeatSample > NO_BEAT_SAMPLES) {
      _bpm = _bpmAvg = _spo2 = 0;
    }
  }

  float bpm() const { return _bpm; }
  float spo2() const { return _spo2; }

  // Peak-to-peak of the filtered IR AC over the last ~1 s — the swing
  // the beat detector sees.  Compare against MIN_PEAK_AMP (25): well
  // below it means no usable pulse (no finger, light contact, or LED
  // current too low); near/above it with bpm()==0 points at tuning.
  float acIrPp() const { return _acIrPp; }

 private:
  // ── Tuning constants (assume ~50 Hz sampling) ─────────────
  // DC tracker cutoff ~0.16 Hz
  static constexpr float DC_ALPHA = 0.02f;
  // AC smoothing cutoff ~3 Hz
  static constexpr float LP_ALPHA = 0.35f;
  // amplitude envelope: rise fast
  static constexpr float ENV_RISE = 0.50f;
  // amplitude envelope: decay slow
  static constexpr float ENV_DECAY = 0.05f;
  // one peak lifts the envelope ≤2×
  static constexpr float ENV_RISE_CAP = 2.0f;
  // beat threshold = 0.45 × envelope
  static constexpr float THRESH_FRAC = 0.45f;
  // absolute noise floor for a beat
  static constexpr float MIN_PEAK_AMP = 25.0f;
  // peak >6× envelope = motion artifact
  static constexpr float ARTIFACT_RATIO = 6.0f;
  // raw jump >30% of DC = finger on/off
  static constexpr float STEP_FRAC = 0.30f;
  // IR DC floor for "something present"
  static constexpr float FINGER_DC_MIN = 1000.0f;
  // BPM display: weight kept on history
  static constexpr float BPM_SMOOTH = 0.85f;
  // let filters settle (~0.5 s)
  static constexpr uint32_t WARMUP_SAMPLES = 25;
  // 3 s with no beat → drop to 0
  static constexpr uint32_t NO_BEAT_SAMPLES = 150;
  // acIrPp() window (~1 s)
  static constexpr uint32_t AC_WINDOW_SAMPLES = 50;
  // AC >2.5% of DC = still settling
  static constexpr float SETTLE_AC_FRAC = 0.025f;
  // cap the settle hold-off (~8 s)
  static constexpr uint32_t SETTLE_MAX = 400;

  uint32_t _sampleMs = 20;

  // ── Outputs ───────────────────────────────────────────────
  float _bpm = 0, _bpmAvg = 0, _spo2 = 0;

  // ── DSP state ─────────────────────────────────────────────
  bool _dcInit = false;
  float _dcIr = 0, _dcRed = 0;   // per-channel DC baseline
  float _lpIr = 0, _lpPrev = 0;  // low-passed IR AC + previous sample
  bool _rising = false;
  float _ampEnv = 0;                    // envelope of local-peak amplitudes
  float _irMax = -1e9f, _irMin = 1e9f;  // per-beat AC extremes (SpO2)
  float _redMax = -1e9f, _redMin = 1e9f;
  uint32_t _sampleCount = 0;
  uint32_t _lastBeatSample = 0;

  // ── Filtered-AC peak-to-peak window (diagnostic, see acIrPp()) ──
  float _acIrPp = 0;       // last completed window's pp
  float _winLpMin = 1e9f;  // current window extremes
  float _winLpMax = -1e9f;

  // Peak detection is held off until _sampleCount reaches this — set
  // at startup, re-armed by the raw-level-step snap, and extended
  // while the post-snap perfusion settle is still swinging.
  uint32_t _settleUntil = WARMUP_SAMPLES;
  uint32_t _snapAt = 0;  // _sampleCount of the last DC snap

  // ── _onPeak ───────────────────────────────────────────────
  //  Called at every local maximum of the filtered IR signal.
  //  Decides whether it's a real heartbeat and, if so, updates
  //  BPM and SpO2.
  void _onPeak(float peakVal) {
    // Motion-artifact guard: a peak many times the running envelope
    // is a finger shift or knock, not a beat.  Drop it whole — it is
    // neither counted as a beat nor allowed to pull the envelope up.
    if (_ampEnv > MIN_PEAK_AMP && peakVal > _ampEnv * ARTIFACT_RATIO)
      return;

    // Amplitude envelope: follow peaks up fast, decay slowly, with the
    // beat threshold riding at a fraction of it so detection adapts to
    // signal strength (finger pressure, skin tone...).  The rise is
    // capped at ENV_RISE_CAP× so no single large peak can hijack the
    // envelope and strand the threshold above genuine beats.
    float envIn = peakVal;
    float cap = _ampEnv * ENV_RISE_CAP + MIN_PEAK_AMP;
    if (envIn > cap)
      envIn = cap;
    if (envIn > _ampEnv)
      _ampEnv += (envIn - _ampEnv) * ENV_RISE;
    else
      _ampEnv += (envIn - _ampEnv) * ENV_DECAY;
    float threshold = _ampEnv * THRESH_FRAC;

    bool fingerLikely = (_dcIr > FINGER_DC_MIN);
    bool realPeak =
        fingerLikely && peakVal > threshold && peakVal > MIN_PEAK_AMP;

    if (realPeak) {
      uint32_t dSamples = _sampleCount - _lastBeatSample;
      uint32_t dMs = dSamples * _sampleMs;
      // Accept only physiologically plausible intervals
      // (300 ms = 200 BPM, 2000 ms = 30 BPM).
      if (dMs >= 300 && dMs <= 2000) {
        float bpm = 60000.0f / static_cast<float>(dMs);
        if (_bpmAvg <= 0)
          _bpmAvg = bpm;  // first beat
        else
          _bpmAvg = _bpmAvg * BPM_SMOOTH + bpm * (1.0f - BPM_SMOOTH);
        _bpm = _bpmAvg;
        _computeSpo2();
      }
      _lastBeatSample = _sampleCount;
    }

    // Start a fresh AC window for the next beat regardless — the
    // SpO2 ratio should reflect one cardiac cycle.
    _irMax = _redMax = -1e9f;
    _irMin = _redMin = 1e9f;
  }

  // ── _computeSpo2 ──────────────────────────────────────────
  //  Ratio-of-ratios oxygen estimate.  R compares the pulsatile
  //  fraction (AC/DC) of the red channel to that of IR; SpO2 is
  //  an empirical function of R (Maxim's published polynomial).
  //  Uncalibrated hardware — rough estimate only.
  void _computeSpo2() {
    float acIrPP = _irMax - _irMin;
    float acRedPP = _redMax - _redMin;
    if (acIrPP <= 1.0f || _dcIr <= 0 || _dcRed <= 0)
      return;

    float r = (acRedPP / _dcRed) / (acIrPP / _dcIr);
    float spo2 = -45.060f * r * r + 30.354f * r + 94.845f;
    spo2 = constrain(spo2, 70.0f, 100.0f);

    if (_spo2 <= 0)
      _spo2 = spo2;  // first estimate
    else
      _spo2 = _spo2 * 0.7f + spo2 * 0.3f;  // smooth
  }
};
