#pragma once
// ============================================================
//  Config.h  –  User-editable settings for the I2C Framework
//
//  This framework auto-detects the host board (M5Stack CoreS3
//  or M5Stack Core2) at runtime and picks the correct I2C pins
//  automatically.  See BoardInfo.h / BoardInfo.cpp.
// ============================================================

// ── WiFi ─────────────────────────────────────────────────────
//  Normal mode: the device joins WIFI_SSID as a station (STA).
//  ⚠ Leave WIFI_SSID empty ("") and the device instead brings up
//  its OWN Wi-Fi access point (the AP block below) rather than
//  joining a network — for a standalone unit with no network to
//  join.  Connect a phone/laptop to that AP and open the
//  dashboard at the AP's IP.
//  ⚠ WIFI_SSID and WIFI_PASSWORD are now defined in Secrets.h
//  (git-ignored).  This file no longer carries any credential.
// give up after this many ms
[[maybe_unused]] constexpr unsigned long WIFI_TIMEOUT_MS = 15000;

// ── Standalone access point (used only when WIFI_SSID is "") ──
//  When WIFI_SSID is blank the device runs as a WPA2-protected
//  access point with these credentials instead of joining a
//  network.  Its IP (the ESP32 default is 192.168.4.1) is shown
//  on the LCD and printed to the boot serial log.  NTP is skipped
//  in this mode — there is no upstream internet — so timestamps
//  fall back to uptime, exactly as on a failed NTP sync.
//  ⚠ AP_PASSWORD must be 8-63 characters — WPA2's minimum.  A
//  shorter or empty password makes the access point fail to start.
[[maybe_unused]] constexpr char AP_SSID[] = "M5Stack-Framework";
//  ⚠ AP_PASSWORD is defined in Secrets.h (git-ignored).  If it is
//  left at the shipped default "m5stack-config" (or shorter than the
//  WPA2 8-char minimum), the framework generates a RANDOM per-device
//  AP password at first boot, persists it in NVS, and shows it on the
//  LCD + serial.  Set a real value in Secrets.h to use your own.

// ── Time / NTP ───────────────────────────────────────────────
//  After WiFi connects, the framework syncs the ESP32's internal
//  RTC against NTP_SERVER.  Synced time is used to:
//    - name the SD log file (/log_YYYYMMDD_HHMMSS.csv)
//    - timestamp every SD log row with ISO 8601 wall-clock
//    - populate a "datetime" field in /api/all etc.
//
//  If NTP fails (offline boot, server unreachable), SDLogger
//  falls back to /log_boot0001.csv style filenames and rows
//  carry uptime seconds only.  No other features regress.
//
//  NTP_TZ is a POSIX TZ string — full grammar at
//    https://man7.org/linux/man-pages/man3/tzset.3.html
//  Common values:
//    "UTC0"                        UTC, no DST
//    "EST5EDT,M3.2.0,M11.1.0"      US Eastern (DST-aware)
//    "PST8PDT,M3.2.0,M11.1.0"      US Pacific
//    "MST7MDT,M3.2.0,M11.1.0"      US Mountain (Denver area)
//    "CET-1CEST,M3.5.0,M10.5.0/3"  Central Europe
[[maybe_unused]] constexpr char NTP_SERVER[] = "pool.ntp.org";
[[maybe_unused]] constexpr char NTP_TZ[] = "UTC0";
// give up sync after this many ms
[[maybe_unused]] constexpr unsigned long NTP_TIMEOUT_MS = 8000;

// ── Web API (HTTPS) ───────────────────────────────────────────
//  The dashboard and REST API are served over HTTPS/TLS on
//  WEB_HTTPS_PORT.  A second, plain-HTTP server on
//  WEB_HTTP_REDIRECT_PORT serves no content — it only issues a 301
//  redirect to the HTTPS URL, so an old "http://<ip>" bookmark
//  still lands the user on the secure page.
//
//  Required libraries: HTTPS uses ESPWebServerSecure from
//  "esp32_idf5_https_server_compat", which itself depends on
//  "esp32_idf5_https_server".  BOTH must be installed — the compat
//  wrapper alone won't compile.  See the README for install notes.
//
//  TLS certificate: a self-signed certificate is embedded in the
//  firmware (see src/https_cert.h) and used as-is — there is no
//  on-device key generation (the cert library's generator is
//  unstable on this ESP-IDF, and a fixed cert keeps boot instant).
//  Because it is self-signed, a browser shows a one-time "not
//  secure" warning the first time you connect — accept it once.
//  To use your own certificate, replace the DER byte arrays in
//  src/https_cert.h (regeneration commands are in that file).
[[maybe_unused]] constexpr unsigned WEB_HTTPS_PORT = 443;
[[maybe_unused]] constexpr unsigned WEB_HTTP_REDIRECT_PORT = 80;

// Plain-HTTP access-point dashboard.  When true AND the device is
// running as a STANDALONE access point (apOnlyMode — provisioned with
// no upstream Wi-Fi), the full dashboard + REST API are served over
// plain HTTP on WEB_HTTP_REDIRECT_PORT (80) and the HTTPS server is
// NOT started.  Rationale: on the device's own WPA2 AP the link is
// already encrypted, so a self-signed cert only adds a browser
// warning with no security benefit; plain HTTP also frees the TLS
// RAM.  No effect in station (joined-Wi-Fi) mode — that always uses
// HTTPS + the port-80 redirect.  Default false: HTTPS everywhere,
// behavior unchanged.  (Setup-mode captive portal is already plain
// HTTP regardless of this flag.)
[[maybe_unused]] constexpr bool WEB_AP_PLAIN_HTTP = false;

// Back-compat alias: older code/docs that say WEB_SERVER_PORT mean
// "the port the dashboard is on", which is now the HTTPS port.
#define WEB_SERVER_PORT WEB_HTTPS_PORT

// Optional HTTP Basic Auth for the web API and dashboard.
//   - Leave WEB_AUTH_USER empty ("") to disable authentication
//     entirely.  All endpoints will be open, same as before.
//   - Set BOTH to enable: every route (including "/" and every
//     "/api/..." endpoint) will return 401 unless the request
//     provides matching credentials.  Browsers prompt automatically.
//
// Because the API is now HTTPS, these credentials travel inside the
// TLS tunnel and are encrypted on the wire — a big improvement over
// the old plain-HTTP server.  Still pick a dedicated password: the
// certificate is self-signed, and anyone with physical access to
// the SD card can read the server's private key (/https_pk.der).
//  ⚠ WEB_AUTH_USER / WEB_AUTH_PASS are defined in Secrets.h
//  (git-ignored).  The shipped default user / password pair is
//  REFUSED at boot when SECURITY_STRICT (below) is true — change it
//  in Secrets.h before deploying.

// ── Security posture ──────────────────────────────────────────
//  Credentials now live in Secrets.h (git-ignored); see that file.
//
//  SECURITY_STRICT controls what happens at boot when a credential
//  is still at its guessable shipped default (dashboard login
//  user / password):
//    true  → REFUSE TO BOOT.  A full-screen red warning is shown on
//            the LCD and printed to serial, and the framework halts
//            before bringing up WiFi / the web server.  Forces you
//            to set real credentials in Secrets.h.  Recommended for
//            anything that leaves the bench.
//    false → boot anyway, but print a loud one-time warning on the
//            LCD + serial.  Convenient while developing.
//
//  Note: leaving WEB_AUTH_USER empty ("") in Secrets.h is a
//  DELIBERATE "auth disabled" choice, not a weak default — it does
//  not trip the strict halt (it only logs an informational note).
#define SECURITY_STRICT true

//  FACTORY_RESET_HOLD_DISABLED controls the hardware escape hatch:
//  touching the screen (CoreS3 / Core2) or holding any of BtnA/B/C
//  (Core1 / Core2) during a short window at power-on (see
//  FACTORY_RESET_WINDOW_MS) wipes the saved NVS settings and reboots
//  into the setup portal.
//    false → hold-to-reset is ENABLED (the default — a guaranteed
//            recovery path if the network or passwords are wrong).
//    true  → hold-to-reset is DISABLED.  Use on a deployed/kiosk
//            unit where you don't want a bystander able to wipe its
//            settings from the front panel.  (NVS can still be
//            cleared by reflashing with flash-erase enabled.)
#define FACTORY_RESET_HOLD_DISABLED false

//  How long (ms) the boot factory-reset window stays open watching
//  for a touch / button press.  The press is caught the instant it's
//  seen (boot continues immediately once detected), so this is the
//  MAXIMUM added boot delay when no one is pressing.  Bigger = more
//  forgiving timing for the finger-hold; smaller = faster normal
//  boot.  Ignored when FACTORY_RESET_HOLD_DISABLED is true.
[[maybe_unused]] constexpr unsigned long FACTORY_RESET_WINDOW_MS = 2500;

//  CAPTIVE_DNS_ENABLED runs a tiny DNS responder in setup mode that
//  points every hostname at the device, so a phone auto-opens the setup
//  portal (like a public-WiFi login).  It uses WiFiUDP (the lwIP
//  SOCKETS layer — socket/bind/recvfrom), which is thread-safe; the
//  bundled DNSServer and AsyncUDP both call raw udp_new and trip a
//  fatal "Required to lock TCPIP core functionality" assert on this
//  ESP-IDF build.  Default true.  Set false to skip the auto-open; the
//  portal is still reachable by browsing to the AP IP (http://192.168.4.1/).
#define CAPTIVE_DNS_ENABLED true

// ── Serial ────────────────────────────────────────────────────
[[maybe_unused]] constexpr unsigned long SERIAL_BAUD = 115200;

// ── I2C buses ─────────────────────────────────────────────────
//  Pin assignments are chosen automatically based on the detected
//  board.  Defaults:
//    CoreS3 : internal SDA=12 SCL=11  |  external SDA=2  SCL=1
//    Core2  : internal SDA=21 SCL=22  |  external SDA=32 SCL=33
//
//  If you want to FORCE a board (e.g. for unit testing without
//  hardware, or to support an unrecognised variant), uncomment
//  exactly one of the following:
//
// #define FORCE_BOARD_CORES3
// #define FORCE_BOARD_CORE2
// #define FORCE_BOARD_CORE1
// #define FORCE_BOARD_TOUGH    // M5Stack Tough (Core2-family pin map)
//
//  Notes on Core1 (the original M5Stack Basic / Gray):
//    • Port-A Grove and the on-board chips share the same SDA=21
//      SCL=22 pair.  The framework collapses both buses to a single
//      shared bus via BoardInfo's intBus/extBus pointers.
//    • No on-board RTC — Plugin_RTC's BM8563 won't bind (the chip
//      isn't present and 0x51 won't ACK).  Harmless.
//    • Battery is managed by an IP5306 (not I2C-accessible).
//      Plugin_PMIC refuses to bind via the hasI2cPmic flag.
//    • Gray has an MPU9250 IMU — M5.Imu handles it transparently
//      so Plugin_IMU works unchanged.  Basic has no IMU at all and
//      Plugin_IMU will simply report disabled.
//
//  Or override individual pins (these take precedence over the
//  per-board defaults):
//
// #define I2C_INT_SDA_OVERRIDE   12
// #define I2C_INT_SCL_OVERRIDE   11
// #define I2C_EXT_SDA_OVERRIDE    2
// #define I2C_EXT_SCL_OVERRIDE    1

// Bus speeds.  Internal bus runs at 400 kHz (all built-in chips
// — AXP, IMU, RTC, touch — support fast-mode).
//
// External bus is currently at 10 kHz as a diagnostic / fallback
// for flaky Port-A signal integrity.  Symptoms that point here:
// the boot scan ACKs a sensor address but subsequent reads or
// writes fail (err=4 from endTransmission, regRead returning
// false on a chip that worked moments earlier).  Dropping from
// 100 kHz to 10 kHz gives the bus 10x more time per bit and
// almost always rescues a marginal cable / weak pull-up setup.
//
// Once the bus is reliable, raise this back to 100000 (standard
// SMBus speed, required by the MLX90614 NCIR2 unit) or 400000
// (fast-mode, only if every connected device supports it and the
// cabling is short and clean).
// Note: on Core1 (single shared bus) only I2C_INT_FREQ is used — the
// "external" speed setting is ignored because there's no separate
// controller to set it on.  If you plan to use SMBus-only Port-A
// units like the NCIR2 (MLX90614, 100 kHz max) on a Core1, keep
// I2C_INT_FREQ at 100000 or lower.  The Core1's built-in MPU9250
// supports 400 kHz but works fine at 100 kHz, so the slow setting
// is a safe default.  Raise back to 400000 if you're on a Core2 /
// CoreS3 (separate buses) or only using fast-mode peripherals.
[[maybe_unused]] constexpr unsigned long I2C_INT_FREQ = 100000;
[[maybe_unused]] constexpr unsigned long I2C_EXT_FREQ = 10000;

// Backwards-compat: anything that still says I2C_FREQ means
// "the internal bus speed".
#define I2C_FREQ I2C_INT_FREQ

// ── Output channel build switches ─────────────────────────────
//  Each flag compiles in (true) or completely omits (false) one
//  output channel.  Set one to false and that channel's code AND
//  its libraries are excluded from the build — OUT_WEB false drops
//  the HTTPS server libraries, OUT_MQTT false drops PubSubClient.
//  The channel's class is replaced by an inert, same-interface
//  stub, so the rest of the framework still compiles and links
//  unchanged (it just calls no-ops).
//
//  All default to true (every channel compiled in), so a stock
//  build is byte-identical to before these became compile-time.
//
//  ⚠ The USB-serial BOOT diagnostics are always available —
//  OUT_SERIAL only governs the periodic per-plugin readings dump,
//  not the boot log (the stub still brings the UART up).
#define OUT_WEB true
#define OUT_SERIAL true
#define OUT_DISPLAY true
#define OUT_MQTT true
#define OUT_SD_LOG true
// Threshold / event alarm engine (AlertManager).  Reads what plugins
// already publish, runs rules through a state machine, and (from
// milestone 2) routes events to channel sinks.
#define OUT_ALERTS true

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

// ── Device-category build switches ────────────────────────────
//  Each switch compiles in (1) or completely omits (0) a whole
//  CATEGORY of device plugins.  In the .ino both the category's
//  `#include`s and its `fw.addPlugin(...)` registrations are
//  wrapped in a matching `#if`, so setting one to 0 removes that
//  category from the build entirely — the headers are never
//  parsed and nothing is registered, costing zero flash and zero
//  RAM (and shaving compile time).
//
//  Defaults are all 1, so a stock build is byte-identical to a
//  build without these switches.  Turn one OFF when you know a
//  build will never use that category — e.g. set
//  ENABLE_UART_DEVICES to 0 on a unit with nothing on Port-C.
//
//  ⚠ The switch also gates the registration lines.  If you
//  uncomment a `fw.addPlugin(...)` for a category, that category's
//  switch must be 1 or the registration is silently skipped.
//
//  The board's own built-in chips (IMU, PMIC, IP5306, RTC,
//  INA3221) are NEVER gated — the framework always needs them to
//  manage the host board.
//
//    ENABLE_OPTIONAL_I2C      pluggable Port-A / Grove I2C Units
//                             (ENV, ToF, Heart, Weight, gas, ...)
//    ENABLE_STACKABLE_MODULES M-Bus stackable modules
//                             (4Relay, Servo2, GoPlus2, PPS, ...)
//    ENABLE_PIN_DEVICES       non-I2C GPIO/PWM/ADC pin devices
//                             (PIR, Relay, Light, MQ gas, ...)
//    ENABLE_UART_DEVICES      Port-C serial devices
//                             (Barcode, Modem, PMSA003, ASR, ...)
#define ENABLE_OPTIONAL_I2C 1
#define ENABLE_STACKABLE_MODULES 1
#define ENABLE_PIN_DEVICES 1
#define ENABLE_UART_DEVICES 1

// ── SD card CSV logging ──────────────────────────────────────
//  When OUT_SD_LOG is true and a microSD card is detected at boot,
//  the framework writes one CSV file per boot to the card root.
//  Filename: SD_LOG_PREFIX + four-digit boot number + ".csv",
//  e.g. "log_0001.csv", "log_0002.csv", ... The boot number is
//  persisted across reboots in NVS (via the Arduino Preferences
//  library) so each power-cycle starts a fresh file.
//
//  Format:
//    First line  – header: "time_s,<slug1>_<key1>,<slug1>_<key2>,..."
//    Each subsequent line – uptime seconds + one column per
//    reading from every active plugin, in registration order.
//
//  Cadence: SD_LOG_INTERVAL_MS controls how often a row is
//  appended.  Set this independent of POLL_MS (the framework's
//  sensor poll cycle).  Default 5000 ms matches the MQTT publish
//  cadence so the log file mirrors what the broker sees.
//
//  Commit: every SD_LOG_FLUSH_EVERY_N rows the logger does a full
//  close + reopen of the file.  close() is the only operation that
//  reliably writes the FAT directory entry across all arduino-esp32
//  core versions — a plain fsync()/flush() sometimes leaves the
//  data clusters written but the directory never updated, so a card
//  read on a PC shows no file at all.  close+reopen eliminates that.
//
//  Higher N = fewer close/reopen cycles (less card wear) but up to
//  N rows at risk if power is lost between commits.  Lower N = each
//  row hits the card sooner.  Default 10 (~50 s at 5 s cadence).
//  Set to 1 for every-row durability; the close/reopen cost is
//  negligible at this logging rate.
//
//  You can also force a commit any time via GET /api/sdcard/flush,
//  or cleanly close + unmount for safe card removal via
//  GET /api/sdcard/eject.
[[maybe_unused]] constexpr unsigned long SD_LOG_INTERVAL_MS = 5000;
[[maybe_unused]] constexpr unsigned SD_LOG_FLUSH_EVERY_N = 10;
// leading slash = root of SD
[[maybe_unused]] constexpr char SD_LOG_PREFIX[] = "/log_";

// SD card SPI clock.  On Core1/Core2 the microSD shares the SPI
// bus with the LCD; the extra trace length + capacitance can make
// a fast clock unreliable for WRITES even when card detection
// (slow-clock reads) works fine — the classic symptom is byte
// counters that climb while the card stays empty.  4 MHz is a safe
// default on the shared bus.  Drop to 1000000 (1 MHz) if the boot
// SD self-test still fails; raise toward 20000000 only on a board
// with a dedicated, short SD bus.
[[maybe_unused]] constexpr unsigned long SD_SPI_HZ = 4000000;

// SD_SELFTEST = true runs a write/read-back round-trip at boot
// (creates /sdtest.txt, reads it back, verifies byte-for-byte).
// Leave it on — /sdtest.txt is also handy to check from a PC: if
// the serial log says PASS but /sdtest.txt isn't on the card when
// you read it, something is wrong downstream of the firmware.
#define SD_SELFTEST true

// ── MQTT ──────────────────────────────────────────────────────
//  Optional publish-only MQTT output.  Each active plugin gets one
//  retained JSON message published to "<MQTT_BASE_TOPIC>/<slug>"
//  every MQTT_PUBLISH_MS milliseconds.  Device liveness is tracked
//  via LWT on "<MQTT_BASE_TOPIC>/status" ("online" on connect,
//  "offline" if the broker loses us).  No inbound subscriptions —
//  use the WebAPI's /api/rescan if you need to re-bind.
//
//  Requires the "PubSubClient" library by Nick O'Leary, installable
//  from the Arduino Library Manager.  Build will fail with an
//  #include error if OUT_MQTT is true and the library is missing.
//
//  To enable:
//    1. Install PubSubClient via Library Manager
//    2. Set MQTT_HOST to your broker's IP or hostname
//    3. (Optional) set MQTT_USER / MQTT_PASS if the broker is authed
//
//  Leave MQTT_HOST empty ("") to keep MQTT compiled in but inert —
//  the framework logs "[MQTT] disabled (MQTT_HOST empty)" at boot
//  and never attempts to connect.  Useful when you want the same
//  binary on devices that should and shouldn't talk to a broker.
//
//  ── Transport: plain MQTT vs MQTTS (TLS) ─────────────────────
//  MQTT_TLS selects how the framework reaches the broker:
//
//    MQTT_TLS = false → plain MQTT over TCP (Mosquitto, the Home
//                       Assistant broker, etc).  No encryption.
//                       The framework's original behaviour.  Pair
//                       it with MQTT_PORT 1883.
//
//    MQTT_TLS = true  → MQTTS: the whole connection is wrapped in
//                       TLS, so everything on the wire is encrypted.
//                       Pair it with MQTT_PORT 8883 (standard MQTTS).
//                       Two further switches shape the handshake:
//
//    MQTT_TLS_MUTUAL — how the DEVICE proves its identity:
//        false → username / password over TLS.  Standard for cloud
//                brokers (HiveMQ Cloud, EMQX Cloud) and a local
//                Mosquitto with TLS.  Set MQTT_USER / MQTT_PASS
//                below — they travel encrypted inside the tunnel.
//        true  → mutual TLS.  The device presents an X.509 client
//                certificate and sends no username / password (the
//                cert IS the credential — leave MQTT_USER / PASS
//                empty).  This is the mode AWS IoT Core requires.
//
//    MQTT_TLS_INSECURE — whether the device verifies the BROKER:
//        false → verify the broker against MQTT_CA_CERT (below).
//                Rejects an impostor broker.  Recommended for
//                anything reachable over an untrusted network.
//        true  → skip verification (setInsecure): traffic is still
//                encrypted but the broker identity is NOT checked.
//                Convenient for a local broker with a self-signed
//                cert; do not use across an untrusted network.
//
//  Transport is a compile-time choice: set the switches, point
//  MQTT_HOST / MQTT_PORT at the broker, rebuild, flash.  Everything
//  downstream (per-plugin JSON publishes, the LWT status topic,
//  /api/mqtt) behaves identically across all transports.
//
//  TLS checks the broker certificate's validity dates, so the
//  device clock must be roughly right.  The framework syncs NTP at
//  boot before MQTT connects; if NTP fails the handshake may be
//  rejected with a date error (unless MQTT_TLS_INSECURE skips it).
#define MQTT_TLS false  // false = plain MQTT/TCP, true = MQTTS/TLS
#define MQTT_TLS_MUTUAL \
  false  // MQTTS only: false = user/pass over TLS,
         //             true  = X.509 client cert
#define MQTT_TLS_INSECURE \
  false  // MQTTS only: false = verify broker against
         //             MQTT_CA_CERT, true = skip check
// plain: broker IP/hostname.  MQTTS: the broker hostname (should
// match its cert when MQTT_TLS_INSECURE is false).
[[maybe_unused]] constexpr char MQTT_HOST[] = "";
// plain MQTT 1883; MQTTS 8883
[[maybe_unused]] constexpr unsigned MQTT_PORT = 1883;
// ⚠ MQTT_USER / MQTT_PASS (empty = anonymous / cert-only) are
// defined in Secrets.h (git-ignored).
// must be unique per device on the broker.  On AWS IoT this should
// match the Thing name — some policies gate the connection on
// clientId == thing-name.
[[maybe_unused]] constexpr char MQTT_CLIENT_ID[] = "m5stack-i2c";
// topic prefix; no trailing slash
[[maybe_unused]] constexpr char MQTT_BASE_TOPIC[] = "m5stack";
// ms between full sensor publishes
[[maybe_unused]] constexpr unsigned long MQTT_PUBLISH_MS = 5000;
// retain readings so late subscribers get the last value.  ⚠ On AWS
// IoT Core a retained publish needs iot:RetainPublish in the Thing's
// policy — if it only grants iot:Publish, set this false or AWS drops
// the connection on the first publish.
#define MQTT_RETAIN true
// seconds; broker drops us after ~1.5×.  AWS IoT Core accepts 30-1200 s.
[[maybe_unused]] constexpr unsigned MQTT_KEEPALIVE = 30;

// ── TLS certificates for MQTTS (MQTT_TLS = true) ──────────────
//  Paste the full PEM text — including the "-----BEGIN/END-----"
//  lines — between the R"EOF( ... )EOF" raw-string delimiters.
//  Each block is compiled in only when the switches above need it,
//  so fill in just the ones your setup uses:
//
//  MQTT_CA_CERT      — needed when MQTT_TLS_INSECURE = false.
//    The certificate the device checks the broker against:
//      • Public cloud broker → its CA / root certificate.  HiveMQ
//        Cloud / EMQX Cloud use the ISRG Root X1 (Let's Encrypt)
//        root; AWS IoT Core uses Amazon Root CA 1, available from
//        https://www.amazontrust.com/repository .
//      • Local Mosquitto with your own CA → that CA's certificate.
//
//  MQTT_CLIENT_CERT / MQTT_CLIENT_KEY
//                    — needed when MQTT_TLS_MUTUAL = true.
//    The device's own X.509 certificate and matching private key:
//      • AWS IoT Core: create a "Thing", generate a certificate,
//        and attach a policy granting at minimum iot:Connect and
//        iot:Publish (add iot:RetainPublish if MQTT_RETAIN is true,
//        or AWS drops the connection on the first retained publish;
//        iot:Subscribe / iot:Receive are NOT needed — this
//        framework is publish-only).  Set MQTT_HOST to the account
//        Device data endpoint and MQTT_CLIENT_ID to the Thing name.
//        Paste "xxxx-certificate.pem.crt" and "xxxx-private.pem.key".
//      • Self-managed broker with client-cert auth: the cert and
//        key you issued for this device from your own CA.
//
//  ⚠ SECRET: MQTT_CLIENT_KEY is a real credential — anyone with it
//  can impersonate the device.  Keep Config.h out of public version
//  control and rotate the certificate if it ever leaks.
//  ([[maybe_unused]] because Config.h is included by several source
//  files but only MQTTOut.cpp references these — the linker drops the
//  unused copies; the attribute just keeps the compiler quiet.)
#if MQTT_TLS

#if !MQTT_TLS_INSECURE
[[maybe_unused]] static const char MQTT_CA_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
PASTE THE BROKER'S CA / ROOT CERTIFICATE HERE
-----END CERTIFICATE-----
)EOF";
#endif  // !MQTT_TLS_INSECURE

#endif  // MQTT_TLS
//  ⚠ MQTT_CLIENT_CERT and MQTT_CLIENT_KEY (the device's own cert and
//  PRIVATE KEY, needed when MQTT_TLS_MUTUAL) are defined in Secrets.h
//  (git-ignored).  Only the public CA / root cert (MQTT_CA_CERT,
//  above) stays here, since it is not a secret.

// ── Home Assistant MQTT Discovery ─────────────────────────────
//  When MQTT_HA_DISCOVERY is true, the framework publishes one
//  retained config message per reading to
//      <MQTT_HA_PREFIX>/sensor/<unique_id>/config
//  on every successful broker connect.  HA auto-creates the
//  entities and groups them under one "device" card identified
//  by the ESP32's MAC suffix.  Entity availability is tied to
//  the same <base>/status LWT topic — when the device drops off
//  the broker, HA marks every entity unavailable automatically.
//
//  MQTT_HA_PREFIX must match HA's "discovery_prefix" (default
//  "homeassistant" — only change if you've customised the HA
//  MQTT integration).  MQTT_DEVICE_NAME shows up as the device
//  card title in HA.
#define MQTT_HA_DISCOVERY false
[[maybe_unused]] constexpr char MQTT_HA_PREFIX[] = "homeassistant";
[[maybe_unused]] constexpr char MQTT_DEVICE_NAME[] = "M5Stack I2C Framework";
[[maybe_unused]] constexpr char MQTT_DEVICE_MODEL[] =
    "M5Stack Core (I2C Framework)";

// MQTT_DEBUG = true prints one line per successful publish (topic +
// payload size) plus disconnect/reconnect edges.  Default true while
// you're getting MQTT set up; flip to false once it's working to keep
// the serial log clean.  The /api/mqtt status endpoint stays available
// regardless of this flag.
#define MQTT_DEBUG false

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

// ── Sensor poll interval ──────────────────────────────────────
[[maybe_unused]] constexpr unsigned long POLL_MS = 500;

// ── Periodic re-scan ─────────────────────────────────────────
//  Hot-plugging I2C devices is not safe — the bus can glitch
//  mid-transaction, the chip you just plugged in may not be in a
//  defined state, and several chips (notably the MLX90614) react
//  badly to bare quick-command probes against a powered slave.
//  This framework therefore does NOT periodically rescan.  Plugins
//  are bound once at boot, and that's it.
//
//  If you need to re-bind after plugging something in, either:
//    - reboot the device, or
//    - hit GET /api/rescan via the web API (manual trigger that
//      runs the same scan/bind logic the boot does).

// ── Claude escalation: local→cloud router (NetDevice_Router) ──
//  Settings for the NetDevice_Router plugin — the "router + voice"
//  tier that makes this device the single chat entry point.  It
//  classifies each turn on-device, answers trivial ones from the
//  on-board Module LLM, and ESCALATES hard ones (anything that
//  needs a filesystem, a shell, or multi-file reasoning) to an
//  orchestrator running on another box — typically an Orange Pi —
//  which fronts Claude Code.
//
//  This device never holds an Anthropic key in this mode: it only
//  knows how to reach your orchestrator; the orchestrator owns
//  Claude Code.  The chain is  this device → Pi → Claude Code.
//
//  To enable: install NetDevice_Router.h into plugins/, register it
//  in the .ino AFTER the Module LLM (so the router can delegate to
//  it), and point ROUTER_PI_* at your orchestrator's /delegate
//  endpoint.  The orchestrator is expected to answer a streamed
//  text/event-stream reply (data: {"delta":"..."} ... data: [DONE]).
//
//    fw.addPlugin(llm);                        // Module LLM first
//    fw.addPlugin(new NetDevice_Router(llm));  // router second
//
//  Leave ROUTER_PI_HOST empty ("") to keep the plugin compiled in
//  but inert — every turn is then answered locally and nothing is
//  ever escalated.
[[maybe_unused]] constexpr char     ROUTER_PI_HOST[] = "";  // set to your Pi orchestrator (e.g. "pi1.local") to enable escalation
[[maybe_unused]] constexpr unsigned ROUTER_PI_PORT = 443;
[[maybe_unused]] constexpr char     ROUTER_PI_PATH[] = "/delegate";
// ⚠ ROUTER_BEARER (the token the orchestrator checks; "" = no
// Authorization header) is defined in Secrets.h (git-ignored).
// TLS verification for the orchestrator connection.  A LAN box with
// a self-signed cert → true (encrypt only, skip the cert check).
// For a pinned CA, set false and add ROUTER_CA_CERT + setCACert()
// in the plugin's beginPins(), mirroring the MQTT_CA_CERT pattern.
#define ROUTER_TLS_INSECURE true
// Abandon a reply only after this long with NO token at all — an
// INACTIVITY timeout, not a total cap (a long answer that keeps
// streaming stays healthy).  Matches the Module LLM's REPLY_IDLE_MS.
[[maybe_unused]] constexpr unsigned long ROUTER_REPLY_IDLE_MS = 60000;
// Ceiling on the in-RAM answer buffer (bytes).
[[maybe_unused]] constexpr unsigned ROUTER_ANSWER_MAX = 4096;
// Opening the outbound TLS socket to the Pi allocates a large mbedTLS
// buffer.  If the dashboard's HTTPS server is mid-handshake on several
// browser sockets at that instant, heap can be momentarily too
// fragmented and connect() fails (the same condition behind the
// dashboard's intermittent "SSL_new failed" log lines).  Retry a few
// times with a short backoff so a transient burst doesn't drop an
// escalation.  ROUTER_CONNECT_TRIES total attempts; ROUTER_CONNECT_
// BACKOFF_MS is the gap, doubled each retry.
[[maybe_unused]] constexpr unsigned ROUTER_CONNECT_TRIES = 3;
[[maybe_unused]] constexpr unsigned ROUTER_CONNECT_BACKOFF_MS = 250;
// If the on-board Module LLM is unavailable (e.g. it failed to load
// at boot, or is wedged), a "local" turn would normally dead-end with
// "[local LLM busy or unavailable]".  Turn this on to instead ESCALATE
// those turns to the Pi orchestrator, so the device stays useful while
// the local model is down.  Only helps if ROUTER_PI_HOST is set and the
// Pi is up.  Off by default (a down local model fails the turn rather
// than silently sending everything to the cloud).
#define ROUTER_FALLBACK_ESCALATE false
// The on-device escalation prefilter (the §02 heuristic).  A turn is
// escalated if it matches any of these words, contains a path-like
// "/", or names a source-file extension.  Comma-separated, lower
// case; bias toward escalating — deflecting an easy turn is cheaper
// than under-serving a hard one.  Edit freely to tune routing.
[[maybe_unused]] constexpr char ROUTER_ESCALATE_KEYWORDS[] =
    "refactor,debug,test,implement,rewrite,fix,build,run,compile,"
    "grep,commit,deploy,stack trace,exception,migrate,function,class";

// ── Optional LLM yes/no tiebreaker (the §02 "model-judged middle") ──
//  The keyword/extension prefilter above is fast but blunt: it escalates
//  only on an EXPLICIT signal, so a genuinely hard request worded without
//  any trigger word silently stays on the tiny local model.  Turn this on
//  to let the ON-DEVICE model adjudicate the ambiguous middle — the turns
//  the prefilter did NOT flag as hard.  Such a turn first runs ONE short
//  yes/no classification inference on the local Module LLM; "yes" routes
//  it to the escalation target (Pi, else the direct API), "no" answers it
//  locally as usual.
//
//  Decisive keyword/path/extension hits skip the tiebreaker entirely (no
//  added latency on the obvious cases).  The tiebreaker is also skipped
//  when there is nowhere to escalate to (no Pi and no direct API) or when
//  no local model is wired.  Cost: one extra short local inference before
//  an ambiguous turn is dispatched.  Requires a local Module LLM.
#define ROUTER_LLM_TIEBREAK false
//  The classification prompt.  Steer the model to answer with a single
//  decisive token; the reply is matched (case-insensitive) for
//  "yes"/"escalate"/"code" to mean "escalate", anything else means
//  "handle locally".  Keep it short — the 0.5B model follows terse,
//  explicit instructions best.
[[maybe_unused]] constexpr char ROUTER_TIEBREAK_PROMPT[] =
    "Classify the request below. Answer with ONE word only: YES if it "
    "needs a powerful coding or agent assistant (writing/editing code, "
    "multi-step problem solving, running tools, reading files), or NO if "
    "a small on-device assistant can answer it directly. YES or NO only.";
//  Trace switch for tuning the tiebreaker.  When true the router prints
//  the model's RAW classification reply (not just the yes/no verdict) to
//  the serial console — so you can see exactly what the 0.5B said and
//  refine ROUTER_TIEBREAK_PROMPT.  Leave false in normal use; ignored
//  unless ROUTER_LLM_TIEBREAK is true.
#define ROUTER_TIEBREAK_TRACE false

// ── Optional 3rd route: direct Claude API from the router ─────
//  By default the router is two-way: trivial → local Module LLM,
//  hard → Pi orchestrator (Claude Code).  Turn ROUTER_DIRECT_API on
//  to add a THIRD route for "smart text" turns — non-coding requests
//  the 0.5B local model would botch but that need no repo (explain,
//  summarise, draft, translate...).  These go straight to the
//  Anthropic API via a NetDevice_ClaudeAPI plugin, skipping the Pi.
//
//  This is the "A + B together" shape (see the doc, §15).  Two cases
//  earn it: (1) LATENCY/COST — a one-hop API answer beats spinning
//  up the full Claude Code agent for a question that produces no
//  diff; (2) RESILIENCE — smart-text answers keep working when the
//  Pi is powered down.  The cost is a real one: enabling this means
//  CLAUDE_API_KEY now lives in this device's flash (see that block).
//  If you don't need to survive a Pi outage, prefer leaving this OFF
//  and letting the Pi orchestrator own all cloud calls (no key here).
//
//  To enable: set this true, register a NetDevice_ClaudeAPI plugin,
//  and pass it to the router as its 2nd constructor argument:
//    auto* llm = new UartDevice_ModuleLLM(Serial2);
//    auto* api = new NetDevice_ClaudeAPI();
//    fw.addPlugin(llm); fw.addPlugin(api);
//    fw.addPlugin(new NetDevice_Router(llm, api));
#define ROUTER_DIRECT_API true
// A non-coding turn is sent to the direct API (instead of the local
// model) when it matches one of these words OR runs longer than
// ROUTER_DIRECT_MIN_WORDS — i.e. it's too rich for the 0.5B but needs
// no codebase.  Comma-separated, lower case.  Ignored unless
// ROUTER_DIRECT_API is true.
[[maybe_unused]] constexpr char ROUTER_DIRECT_KEYWORDS[] =
    "explain,summarise,summarize,draft,translate,rewrite,brainstorm,"
    "compare,outline,reword,paraphrase,why,how,what if";
[[maybe_unused]] constexpr unsigned ROUTER_DIRECT_MIN_WORDS = 12;

// ── Claude direct API (NetDevice_ClaudeAPI) ──────────────────
//  Settings for the OPTIONAL NetDevice_ClaudeAPI plugin, which lets
//  this device call the Anthropic Messages API directly over HTTPS —
//  no orchestrator in between.
//
//  ⚠ MODEL, NOT AGENT.  This returns Claude the *model*: text in,
//  text out ("explain this trace", "draft this regex").  It is NOT
//  Claude *Code* — there is no filesystem, shell, git repo, or tool
//  loop, because this device cannot host one.  Anything that must
//  read or edit a codebase still has to go to Claude Code on the Pi
//  via NetDevice_Router above.  Use this as the MIDDLE of a 3-way
//  router (local model → Claude API → Claude Code), never the default.
//
//  ⚠ SECRET: CLAUDE_API_KEY is a real credential stored in firmware
//  on a desk device whose flash can be read.  Scope the key as
//  tightly as you can and rotate it if it leaks.  If the key matters,
//  prefer routing through the Pi (which keeps it on a real machine).
//  Keep Config.h out of public version control.
//
//  Leave CLAUDE_API_KEY empty ("") to keep the plugin compiled in
//  but inert — it logs a warning at boot and answers every query
//  with "[api key not set]" instead of calling out.
//  ⚠ CLAUDE_API_KEY is defined in Secrets.h (git-ignored).
// Model id.  A small, fast, cheap model suits a gadget; bump up only
// if you need stronger text answers.
[[maybe_unused]] constexpr char CLAUDE_MODEL[] = "claude-haiku-4-5";
// Optional persona / instruction prepended to every query ("" = none).
[[maybe_unused]] constexpr char CLAUDE_SYSTEM_PROMPT[] =
    "You are a concise assistant answering from a small desk device. "
    "Keep replies short and plain-text.";
// Longest reply the model will generate.
[[maybe_unused]] constexpr int CLAUDE_MAX_TOKENS = 512;
// Inactivity timeout (ms) and answer-buffer ceiling (bytes), as above.
[[maybe_unused]] constexpr unsigned long CLAUDE_REPLY_IDLE_MS = 30000;
[[maybe_unused]] constexpr unsigned CLAUDE_ANSWER_MAX = 4096;
// Anthropic API version header.  See docs.anthropic.com for the
// current value; this rarely needs changing.
[[maybe_unused]] constexpr char CLAUDE_API_VERSION[] = "2023-06-01";

// ── Credentials ───────────────────────────────────────────────
//  All real secrets (WiFi, AP password, dashboard login, MQTT
//  user/pass + client cert/key, router bearer, Claude API key) live
//  in Secrets.h, which is git-ignored.  It is included LAST so the
//  MQTT_TLS / MQTT_TLS_MUTUAL macros above are defined before its
//  conditional cert blocks are evaluated.  Copy Secrets.h.example to
//  Secrets.h on a fresh checkout and fill in this device's values.
#include "Secrets.h"

// ── Claude conversation memory (NetDevice_ClaudeAPI_History) ──
//  Read only by the history-keeping variant of the Claude plugin.
//  Macros (not constexpr) so the plugin's #ifndef defaults defer to
//  these.  Total messages (user+assistant) resent each turn — keep
//  EVEN so trimming stays user-first + alternating.  CHARS bounds both
//  the heap and the tokens you resend.
#define CLAUDE_HISTORY_MAX_MSGS  8
#define CLAUDE_HISTORY_MAX_CHARS 4000

// ── LoRa P2P radio (UartDevice_LoRaWAN) ───────────────────────
//  Bench-test parameters for the M5 LoRaWAN Unit US915 (RAK3172) in
//  raw LoRa P2P mode.  Macros so the plugin's #ifndef defaults defer
//  to these.  ⚠ EVERY value must MATCH the peer radio (e.g. the SX1262
//  on the Cardputer) or the link is silent with no error.
//  Frequency: keep within the US915 (902–928) ∩ SX1262 (868–923) =
//  ~902–923 MHz overlap; 915.0 MHz is safe, don't exceed 923 MHz.
#define LORA_P2P_FREQ_HZ  915000000UL  // 915.0 MHz
#define LORA_P2P_SF       7            // spreading factor 7..12
#define LORA_P2P_BW       125          // bandwidth kHz (125/250/500)
#define LORA_P2P_CR       0            // coding rate 0=4/5 1=4/6 2=4/7 3=4/8
#define LORA_P2P_PREAMBLE 8            // preamble symbols
#define LORA_P2P_TX_POWER 14           // TX power dBm (respect local limits)
