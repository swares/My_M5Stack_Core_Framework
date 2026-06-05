#pragma once
// ============================================================
//  CmdParse.h  –  Shared command-parameter validators
//
//  Controllable plugins validate every value handed to their
//  command() before touching hardware (see IDevice::command).  The
//  same handful of parsers — boolean tokens, ranged ints/floats,
//  hex colours — used to be re-coded privately in each output-device
//  plugin.  They live here ONCE instead, so a fix or tweak to the
//  validation logic applies everywhere and the copies can't drift.
//
//  All functions are pure and side-effect-free: they REJECT anything
//  malformed or out of range (returning -1 / false), so a plugin can
//  treat a true/non-negative result as "safe to apply".
// ============================================================
#include <Arduino.h>

namespace cmd {

// "1" / "on" / "true"  -> 1
// "0" / "off" / "false" -> 0
// anything else         -> -1   (caller rejects the command)
inline int parseBool(const String& v) {
  String t = v;
  t.toLowerCase();
  if (t == "1" || t == "on" || t == "true") return 1;
  if (t == "0" || t == "off" || t == "false") return 0;
  return -1;
}

// Signed decimal within [lo, hi].  A single leading '-' is allowed;
// any other non-digit, an empty string, or an out-of-range value
// returns false (and leaves `out` untouched on a format error).
inline bool parseInt(const String& v, int32_t lo, int32_t hi, int32_t& out) {
  if (v.length() == 0) return false;
  uint16_t i = 0;
  if (v.charAt(0) == '-') {
    if (v.length() == 1) return false;  // bare "-"
    i = 1;
  }
  for (; i < v.length(); i++)
    if (!isDigit(v.charAt(i))) return false;
  int32_t n = v.toInt();
  if (n < lo || n > hi) return false;
  out = n;
  return true;
}

// Decimal within [lo, hi]: optional leading sign, at most one '.'.
// Rejects any other character, an empty/sign-only string, or an
// out-of-range value.
inline bool parseFloat(const String& s, float lo, float hi, float& out) {
  uint16_t len = s.length();
  if (len == 0) return false;
  uint16_t i = 0;
  if (s.charAt(0) == '-' || s.charAt(0) == '+') i = 1;
  bool dot = false, digit = false;
  for (; i < len; i++) {
    char ch = s.charAt(i);
    if (ch == '.') {
      if (dot) return false;
      dot = true;
    } else if (isDigit(ch)) {
      digit = true;
    } else {
      return false;
    }
  }
  if (!digit) return false;
  float f = s.toFloat();
  if (f < lo || f > hi) return false;
  out = f;
  return true;
}

// True iff `v` is non-empty and every character is a decimal digit.
inline bool allDigits(const String& v) {
  if (v.length() == 0) return false;
  for (uint16_t i = 0; i < v.length(); i++)
    if (!isDigit(v.charAt(i))) return false;
  return true;
}

// One hex digit -> 0..15, anything else -> -1.
inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Exactly six hex digits "RRGGBB" -> 0xRRGGBB, anything else -> -1.
inline int32_t parseRgb(const String& v) {
  if (v.length() != 6) return -1;
  int32_t out = 0;
  for (uint8_t i = 0; i < 6; i++) {
    int h = hexNibble(v.charAt(i));
    if (h < 0) return -1;
    out = (out << 4) | h;
  }
  return out;
}

}  // namespace cmd
