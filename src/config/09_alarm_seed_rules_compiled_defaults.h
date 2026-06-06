#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Alarm seed rules (compiled defaults) ──────────────────────
//  The two named "critical" hazards the feature ships with.  Runtime
//  (NVS) rules + a dashboard editor land in a later milestone; these
//  are the compiled fallbacks the engine seeds at boot.
[[maybe_unused]] constexpr float    ALERT_RAD_USV            = 5.0f; // geiger usv_per_h >= → CRITICAL
[[maybe_unused]] constexpr unsigned ALERT_RAD_COOLDOWN       = 60;   // s between radiation re-notifies
[[maybe_unused]] constexpr float    ALERT_LIGHTNING_KM       = 10.0f;// lightning gated: distance_km <=
[[maybe_unused]] constexpr unsigned ALERT_LIGHTNING_COOLDOWN = 30;   // s between strike notifications
// Buzzer sink — the built-in M5 speaker chirps on a raised/renotified
// WARN or CRITICAL alert.  Set ALERT_BUZZER false to silence it.
[[maybe_unused]] constexpr bool     ALERT_BUZZER    = true;
[[maybe_unused]] constexpr unsigned ALERT_BUZZER_HZ = 3000;  // tone pitch, Hz
[[maybe_unused]] constexpr unsigned ALERT_BUZZER_MS = 180;   // tone length, ms
// Webhook sink — POST the alert event JSON to this URL on raise/clear.
// "" = off.  Any HTTPS endpoint (Discord/Slack webhook, IFTTT, your Pi…).
[[maybe_unused]] constexpr char ALERT_WEBHOOK_URL[]  = "";
[[maybe_unused]] constexpr char ALERT_WEBHOOK_AUTH[] = "";  // optional Authorization header value
// Email sink — POST {"to","subject","text"} JSON to your email API / relay
// (a transactional-email provider, or a small cloud function).  "" = off.
[[maybe_unused]] constexpr char ALERT_EMAIL_URL[]  = "";
[[maybe_unused]] constexpr char ALERT_EMAIL_AUTH[] = "";    // e.g. "Bearer <key>" — keep real keys in Secrets.h
[[maybe_unused]] constexpr char ALERT_EMAIL_TO[]   = "";    // recipient address
// SMS sink — recipient for alert texts via the cellular modem ("" = off).
// ⚠ Per-message cost (the modem caps sends per ~24 h); the modem owns the
// single Port-C UART, so SMS is mutually exclusive with LLM / LoRa / GPS.
[[maybe_unused]] constexpr char ALERT_SMS_TO[] = "";
