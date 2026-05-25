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
[[maybe_unused]] constexpr char WIFI_SSID[] = "";
[[maybe_unused]] constexpr char WIFI_PASSWORD[] = "";
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
[[maybe_unused]] constexpr char AP_PASSWORD[] = "m5stack-config";

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
[[maybe_unused]] constexpr char WEB_AUTH_USER[] = "user";
[[maybe_unused]] constexpr char WEB_AUTH_PASS[] = "password";

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
[[maybe_unused]] constexpr char MQTT_HOST[] = "192.168.1.229";
// plain MQTT 1883; MQTTS 8883
[[maybe_unused]] constexpr unsigned MQTT_PORT = 1883;
// empty = anonymous / cert-only
[[maybe_unused]] constexpr char MQTT_USER[] = "";
[[maybe_unused]] constexpr char MQTT_PASS[] = "";
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

#if MQTT_TLS_MUTUAL
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
#endif  // MQTT_TLS_MUTUAL

#endif  // MQTT_TLS

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
