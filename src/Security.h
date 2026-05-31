#pragma once
// ============================================================
//  Security.h  –  Config-hygiene helpers (approach A)
//
//  Small, dependency-light helpers used by Framework::begin():
//    • detect credentials still at their guessable shipped defaults
//    • mint a random per-device AP password on first boot and keep
//      it in NVS, so a unit that ships with the default AP password
//      doesn't expose a known WPA2 key
//
//  These do NOT encrypt anything at rest — they only stop the easy
//  leaks (guessable defaults, secrets in git).  See approaches B–D
//  in the security doc for hardware-backed / off-device protection.
// ============================================================
#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>   // esp_random() — hardware RNG (ESP-IDF 5 / core 3.x)
#include <cstring>
#include "Config.h"   // pulls in Secrets.h values + SECURITY_STRICT

namespace Security {

// True when the dashboard login is still the shipped user/password.
// An empty WEB_AUTH_USER means "auth deliberately disabled" — that
// is a choice, not a weak default, so it returns false here.
inline bool usingDefaultWebLogin() {
  return WEB_AUTH_USER[0] != '\0' &&
         std::strcmp(WEB_AUTH_USER, "user") == 0 &&
         std::strcmp(WEB_AUTH_PASS, "password") == 0;
}

// True when the configured AP password is the shipped default or is
// too short to be a valid WPA2 key (< 8 chars).  Either case means
// we should mint a random one instead.
inline bool apPasswordNeedsRandom() {
  return std::strcmp(AP_PASSWORD, "m5stack-config") == 0 ||
         std::strlen(AP_PASSWORD) < 8;
}

// Return the AP password to actually use.  When the configured
// AP_PASSWORD is a real, user-set value it is returned unchanged and
// `generated` is left false.  Otherwise a random 12-char password is
// minted once, stored in NVS (namespace "sec", key "appw"), reused on
// every later boot, and `generated` is set true so the caller can
// surface it on the LCD + serial.
inline String effectiveApPassword(bool& generated) {
  generated = false;
  if (!apPasswordNeedsRandom()) return String(AP_PASSWORD);

  Preferences prefs;
  prefs.begin("sec", /*readOnly=*/false);
  String pw = prefs.getString("appw", "");
  if (pw.length() < 8) {
    // Ambiguity-free alphabet (no 0/O, 1/l/I) — it gets typed by hand.
    static const char cs[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
    const size_t n = sizeof(cs) - 1;
    pw = "";
    for (int i = 0; i < 12; ++i) pw += cs[esp_random() % n];
    prefs.putString("appw", pw);
  }
  prefs.end();
  generated = true;
  return pw;
}

}  // namespace Security
