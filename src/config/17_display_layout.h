#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Display layout ────────────────────────────────────────────
//  DISPLAY_SCROLL  true  = single scrolling ticker line
//                  false = fixed multi-value grid
#define DISPLAY_SCROLL true

//  Fixed-grid origin (used when DISPLAY_SCROLL = false)
[[maybe_unused]] constexpr int DISPLAY_FIX_X = 0;
[[maybe_unused]] constexpr int DISPLAY_FIX_Y = 0;

//  How long each plugin panel is shown before cycling (ms)
[[maybe_unused]] constexpr unsigned long DISPLAY_CYCLE_MS = 3000;

//  Scroll speed in pixels per tick (lower = slower)
[[maybe_unused]] constexpr int DISPLAY_SCROLL_PX = 2;

//  LCD brightness 0-255
[[maybe_unused]] constexpr int DISPLAY_BRIGHTNESS = 180;
