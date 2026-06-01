#pragma once
// ============================================================
//  Secrets.h  –  Per-device credentials.   ⚠ GIT-IGNORED ⚠
//
//  Every REAL credential the framework uses lives here, and ONLY
//  here.  This file is excluded from version control by .gitignore
//  so a secret can never be committed by accident.
//
//  Setup on a fresh checkout:
//    1. copy  src/Secrets.h.example  →  src/Secrets.h
//    2. fill in the values for this device
//    3. never commit Secrets.h (the .example template is the only
//       version-controlled copy)
//
//  Config.h does  #include "Secrets.h"  at its very end, so every
//  existing reference to these names (WIFI_PASSWORD, CLAUDE_API_KEY,
//  WEB_AUTH_PASS, …) keeps working unchanged across the codebase.
//  This header is meant to be reached THROUGH Config.h — it relies on
//  the MQTT_TLS / MQTT_TLS_MUTUAL macros defined there for the cert
//  blocks below.  Do not #include it directly.
//
//  ⚠ These are compiled straight into the firmware image.  Config
//  hygiene (this split) stops them leaking through git and stops
//  guessable defaults shipping — it does NOT protect a unit whose
//  flash is dumped.  For that, see approaches B–D in the security
//  doc (runtime provisioning / flash encryption / off-device keys).
// ============================================================

// ── WiFi (station mode) ──────────────────────────────────────
//  Leave WIFI_SSID empty ("") to run as a standalone access point
//  instead of joining a network (see the AP block in Config.h).
[[maybe_unused]] constexpr char WIFI_SSID[]     = "";
[[maybe_unused]] constexpr char WIFI_PASSWORD[] = "";

// ── Standalone access-point password ─────────────────────────
//  Used only when WIFI_SSID is empty.  Must be 8–63 chars (WPA2).
//  ⚠ If this is left at the shipped default "m5stack-config" (or is
//  too short), the framework generates a RANDOM per-device password
//  at first boot, stores it in NVS, and prints it on the LCD +
//  serial.  Set a real value here to use your own instead.
[[maybe_unused]] constexpr char AP_PASSWORD[]   = "m5stack-config";

// ── Dashboard / Web API login (HTTP Basic Auth) ──────────────
//  Leave WEB_AUTH_USER empty ("") to disable auth entirely.
//  ⚠ The shipped default user / password is REFUSED at boot (see
//  SECURITY_STRICT in Config.h) — change it before deploying.
[[maybe_unused]] constexpr char WEB_AUTH_USER[] = "admin";
[[maybe_unused]] constexpr char WEB_AUTH_PASS[] = "84ZP8GnuE8P4UdHyfDG7";

// ── MQTT broker login ────────────────────────────────────────
//  Empty = anonymous / cert-only.  Travel encrypted when MQTT_TLS.
[[maybe_unused]] constexpr char MQTT_USER[] = "";
[[maybe_unused]] constexpr char MQTT_PASS[] = "";

// ── Router → Pi orchestrator bearer token ────────────────────
//  "" = send no Authorization header.
[[maybe_unused]] constexpr char ROUTER_BEARER[] = "";

// ── Claude direct-API key (NetDevice_ClaudeAPI) ──────────────
//  ⚠ A real billable credential when set.  Leave "" to keep the
//  plugin inert.  Prefer routing through the Pi (which keeps the key
//  off this device).  Rotate immediately if it ever leaks.
[[maybe_unused]] constexpr char CLAUDE_API_KEY[] = "placeholder";

// ── MQTT mutual-TLS client certificate + private key ─────────
//  Only compiled in when MQTT_TLS && MQTT_TLS_MUTUAL (Config.h).
//  The CA / root cert (MQTT_CA_CERT) is NOT secret and stays in
//  Config.h — only the device's own cert and PRIVATE KEY live here.
#if MQTT_TLS && MQTT_TLS_MUTUAL
[[maybe_unused]] static const char MQTT_CLIENT_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
PASTE THIS DEVICE'S CLIENT CERTIFICATE (xxxx-certificate.pem.crt) HERE
-----END CERTIFICATE-----
)EOF";

[[maybe_unused]] static const char MQTT_CLIENT_KEY[] = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
PASTE THIS DEVICE'S PRIVATE KEY (xxxx-private.pem.key) HERE
-----END RSA PRIVATE KEY-----
)EOF";
#endif  // MQTT_TLS && MQTT_TLS_MUTUAL
