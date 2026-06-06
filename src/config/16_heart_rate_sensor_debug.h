#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Heart-rate sensor debug ───────────────────────────────────
//  HEART_DEBUG = true makes Plugin_HEART (the MAX30100 Heart Unit)
//  print a throttled (~1 Hz) line from fastPoll() showing the FIFO
//  pointers, how many samples were drained in the last second, the
//  latest raw IR/RED counts and the current BPM.
//
//  Use it when the heart unit reads 0:
//    - "drained/s" stays 0  → the sensor FIFO is not filling; the
//      chip isn't sampling (wiring, power, or a failed config write
//      — begin() already prints a register read-back in that case).
//    - IR/RED move but BPM stays 0 → samples are flowing fine, the
//      beat detector just isn't seeing a clean pulse (press a still
//      fingertip on the sensor and wait a few seconds).
//
//  Leave false for normal use — it is hot-loop logging.
#define HEART_DEBUG false
