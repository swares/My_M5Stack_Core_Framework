# M5Stack  –  I2C Sensor Framework

A modular, plugin-based Arduino framework that auto-detects and manages
I2C sensors on both internal and external buses, with three independently
controllable output channels.

**Supported boards (auto-detected at runtime):**

| Board | Internal I2C | External I2C (Port-A) | Port-C (UART) | Built-in IMU | Built-in PMIC / battery |
|---|---|---|---|---|---|
| M5Stack CoreS3            | SDA=12 SCL=11 | SDA=2  SCL=1     | RX=18 TX=17 | BMI270 + BMM150 | AXP2101 (I2C @ 0x34) |
| M5Stack Core2 / v1.1      | SDA=21 SCL=22 | SDA=32 SCL=33    | RX=13 TX=14 | MPU6886         | AXP192  (I2C @ 0x34) |
| M5Stack Tough             | SDA=21 SCL=22 | SDA=32 SCL=33    | RX=13 TX=14 | (none)          | AXP192  (I2C @ 0x34) |
| M5Stack Core (Basic/Gray) | SDA=21 SCL=22 | **shared** w/ internal | RX=16 TX=17 | MPU9250 (Gray only) | IP5306 (I2C @ 0x75)  |

The same compiled binary runs on all four board families — `M5.getBoard()` is
queried on boot and the correct pin map, bus topology, and built-in chip
identities are wired up automatically. See `src/BoardInfo.h`.

> **Core1 quirks** (handled transparently by `BoardInfo`):
> - Port-A Grove and the on-board chips share the **same SDA=21/SCL=22 pair** —
>   the framework collapses both buses to a single shared `TwoWire*` so plugin
>   code doesn't have to care. Bus labels in the dashboard/serial say `shared`.
> - No I2C PMIC; battery monitored via the IP5306 plugin (gauge only — no
>   programmable rails). `Plugin_PMIC` refuses to bind on Core1.
> - No on-board RTC; `Plugin_RTC` simply doesn't bind (0x51 won't ACK).
> - Basic has no IMU; Gray has an MPU9250 handled by `M5.Imu`.
> - If you use SMBus-only Port-A units like NCIR2 (MLX90614, 100 kHz max),
>   leave `I2C_INT_FREQ` at 100000 — it's already the default. The "external
>   bus frequency" setting is ignored on Core1 (no separate controller).

---

## Features

| Feature | Detail |
|---|---|
| Auto-detect board | CoreS3 / Core2 / Tough / Core1 (Basic/Gray) chosen at runtime, no recompile |
| Auto-detect sensors | Single bus scan at boot binds plugins to addresses |
| I2C hub support | PCA9548A / PaHUB; multiple instances of same sensor type behind one hub |
| Plugin system | One `.h` file per device – zero coupling to core |
| Stackable + pluggable | Handles cabled Port-A units AND M-Bus stacking modules; each device reports its `mount` type |
| Output control | Relays / servos / LEDs commanded via `GET /api/{slug}/set`, every value validated |
| Web API | HTTPS JSON REST + live dashboard with interactive control widgets + optional Basic Auth |
| MQTT output | Publish-only JSON per plugin + LWT; optional Home Assistant auto-discovery |
| SD card logging | One CSV per boot (`/log_NNNN.csv`) with dynamic header built from active plugins |
| Serial output | Formatted table in Arduino Serial Monitor |
| Display output | Scrolling ticker **or** fixed multi-value grid on LCD |
| Threshold alarms | Rule engine (threshold + event, debounce/hysteresis/latch) over any reading → buzzer / LCD / MQTT / SD / LoRa / webhook / email / SMS; editable, NVS-persistent rules |
| All outputs toggleable | Per-channel enable/disable flags in `Config.h` |

---

## Quick Start

### 1 – Board Setup
1. Add the M5Stack board package URL in Arduino IDE
   → *Preferences → Additional boards manager URLs*:
   ```
   https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
   ```
2. Install the **M5Stack** board package via *Boards Manager*.
3. Select **Tools → Board → M5Stack CoreS3**, **M5Stack Core2**, or
   **M5Stack Core** (for original Basic / Gray).

### 2 – Library Manager
Install:
- `M5Unified`    (by M5Stack) – pulls in per-board drivers automatically
- `ArduinoJson`  (by Benoit Blanchon) **version 7.x**
- `PubSubClient` (by Nick O'Leary) – only required when `OUT_MQTT` is true
- `esp32_idf5_https_server_compat` – WebServer-compatible TLS web server
- `esp32_idf5_https_server` – the base library the compat wrapper depends on
  (install **both**; the compat wrapper alone will not compile)
- `WiFi`         (bundled with esp32 core – no install needed)

> The old separate `M5CoreS3` / `M5Core2` libraries are **not** required —
> `M5Unified` handles both boards through a single API.

### 3 – Configure WiFi
Edit `src/Config.h`:
```cpp
#define WIFI_SSID      "YourNetwork"
#define WIFI_PASSWORD  "YourPassword"
```

**Standalone access-point mode.** Leave `WIFI_SSID` empty (`""`) and the
device does not join a network — it brings up its *own* WPA2 access point
instead, so a unit with no network to join is still fully usable:

```cpp
#define WIFI_SSID    ""                   // empty → access-point mode
#define AP_SSID      "M5Stack-Framework"  // the network the device creates
#define AP_PASSWORD  "m5stack-config"     // must be 8-63 chars (WPA2)
```

Connect a phone or laptop to `AP_SSID` and open the dashboard at the AP's
IP — `192.168.4.1` by default, also shown on the LCD and the boot serial
log. NTP is skipped in this mode (no upstream internet), so timestamps
fall back to uptime, exactly as on a failed sync. `/api/config` reports
`"wifi_mode": "ap"` vs `"station"`.

> **No recompile needed.** Standalone-AP mode can also be turned on at
> runtime: both the first-boot setup portal and the dashboard's Settings
> page have a **"Run as a standalone access point (no Wi-Fi network)"**
> checkbox. Check it, save, and the device reboots serving the dashboard
> at `192.168.4.1` with no router — the choice is stored in NVS (an
> `ap_only` flag). A factory reset clears it. See the provisioning section
> below.

### 4 – Upload
Open `M5Stack_I2C_Framework.ino` and click **Upload**.

On boot you'll see one of:
```
[Board] Detected: M5Stack Core2
[I2C] Internal SDA=21 SCL=22  External SDA=32 SCL=33  @400000Hz

[Board] Detected: M5Stack CoreS3
[I2C] Internal SDA=12 SCL=11  External SDA=2  SCL=1   @400000Hz

[Board] Detected: M5Stack Core (Basic / Gray)
[I2C] Shared bus SDA=21 SCL=22 @100000Hz  (internal + Port-A on same pins)
```

---

## Configuration Reference (`src/Config.h`)

> `Config.h` is now a thin list of `#include`s — each setting block lives in
> its own header under `src/config/` (`01_wifi.h`, `12_mqtt.h`,
> `24_credentials.h`, …), pulled in by `Config.h` in the original order.
> Edit the value in its section file; everything below still applies. The
> includes concatenate exactly as the old single file did, so ordering
> dependencies (Secrets.h last; `MQTT_TLS` before the cert blocks) are
> preserved.

Pin assignments are auto-selected, but you can force a board or override
individual pins if needed (e.g. for an unrecognised board variant):

```cpp
// Optional — force a specific board:
// #define FORCE_BOARD_CORES3
// #define FORCE_BOARD_CORE2
// #define FORCE_BOARD_CORE1     // M5Stack Basic / Gray

// Optional — override individual I2C pins:
// #define I2C_INT_SDA_OVERRIDE   21
// #define I2C_INT_SCL_OVERRIDE   22
// #define I2C_EXT_SDA_OVERRIDE   32
// #define I2C_EXT_SCL_OVERRIDE   33
```

> On Core1 the EXT overrides are accepted but ignored — Port-A and internal
> share the same pair. Only the INT overrides have effect.

Everything else stays the same as before:

```cpp
// Output channels – set false to COMPILE OUT (code + libraries)
#define OUT_WEB      true   // HTTPS JSON API + dashboard
#define OUT_SERIAL   true   // per-plugin Serial Monitor dump
#define OUT_DISPLAY  true   // LCD
#define OUT_MQTT     true   // MQTT publish
#define OUT_SD_LOG   true   // SD-card CSV log

// Display layout
#define DISPLAY_SCROLL    true   // false = fixed grid
#define DISPLAY_CYCLE_MS  3000   // ms per plugin panel
#define DISPLAY_SCROLL_PX 2      // scroll speed (px/tick)

// Sensor poll interval
#define POLL_MS   500
```

> **No periodic I2C rescan.** The framework scans the buses once at
> boot and binds plugins.  Hot-plugging I2C is not safe (mid-transaction
> bus glitches, undefined chip state, MLX90614 mis-handling of bare
> quick commands).  If you plug in a sensor after boot, either reboot
> the device or hit `GET /api/rescan` to manually re-run the boot scan.

---

## MQTT Output (plain MQTT or MQTTS / TLS)

`MQTTOut` is a publish-only output channel. When `OUT_MQTT` is true and
`MQTT_HOST` is set, every active plugin gets one retained JSON document
published to `<MQTT_BASE_TOPIC>/<slug>` every `MQTT_PUBLISH_MS`, and
device liveness is tracked with a Last-Will message on
`<MQTT_BASE_TOPIC>/status` (`online` on connect, `offline` when dropped).

### Transport: plain MQTT vs MQTTS

`MQTT_TLS` in `Config.h` picks how the broker is reached. It is a
compile-time choice — set the switches, rebuild, flash.

```cpp
#define MQTT_TLS  false   // plain MQTT over TCP — Mosquitto, the HA broker
#define MQTT_TLS  true    // MQTTS — the whole connection wrapped in TLS
```

When `MQTT_TLS` is true, two further switches shape the TLS handshake:

| Switch | `false` | `true` |
|---|---|---|
| `MQTT_TLS_MUTUAL`   | device authenticates with `MQTT_USER` / `MQTT_PASS` over TLS | device presents an X.509 client certificate (no username/password) |
| `MQTT_TLS_INSECURE` | broker verified against `MQTT_CA_CERT` | broker identity not checked (`setInsecure` — encrypt only) |

| | `MQTT_TLS = false` | `MQTT_TLS = true` |
|---|---|---|
| Client       | `WiFiClient` (plain)  | `WiFiClientSecure` (encrypted) |
| Typical port | 1883                  | 8883 |
| HA discovery | honoured              | honoured, except mutual TLS (AWS rejects the topic tree) |

Everything downstream — the per-plugin JSON payloads, the LWT status
topic, `/api/mqtt` — is identical across all transports. `/api/mqtt`
reports the active transport in its `transport`, `tls`, `tls_mutual`
and `tls_verified` fields.

### Connecting an MQTTS broker

**Cloud broker with username/password** — HiveMQ Cloud, EMQX Cloud, or
a local Mosquitto with TLS; the common case:

1. `MQTT_TLS = true`, `MQTT_TLS_MUTUAL = false`, `MQTT_PORT = 8883`.
2. Set `MQTT_HOST` to the broker hostname and `MQTT_USER` / `MQTT_PASS`
   to the broker credentials — they travel encrypted inside the tunnel.
3. Leave `MQTT_TLS_INSECURE = false` and paste the broker's CA / root
   certificate into `MQTT_CA_CERT` (public providers typically use the
   ISRG Root X1 / Let's Encrypt root — check your provider's docs).
   For a local broker with a self-signed certificate you may instead
   set `MQTT_TLS_INSECURE = true` to skip verification: the link is
   still encrypted but unauthenticated, so use it on trusted LANs only.

**AWS IoT Core** — mutual TLS with an X.509 client certificate:

1. `MQTT_TLS = true`, `MQTT_TLS_MUTUAL = true`, `MQTT_PORT = 8883`.
2. **AWS IoT console → Manage → Things** — create a Thing, generate a
   certificate, and attach a policy. A publish-only device needs at
   minimum `iot:Connect` and `iot:Publish`. Add `iot:RetainPublish`
   if `MQTT_RETAIN` is true (it is by default) — without it AWS drops
   the connection on the first retained publish. `iot:Subscribe` /
   `iot:Receive` are **not** needed; the framework never subscribes.
3. Set `MQTT_HOST` to your account's *Device data endpoint* (AWS IoT →
   Settings), e.g. `abc123-ats.iot.us-east-1.amazonaws.com`, and
   `MQTT_CLIENT_ID` to the Thing name (some policy templates gate the
   connection on `clientId == thing-name`).
4. Paste the PEM downloads into the `#if MQTT_TLS` block in `Config.h`:
   `MQTT_CA_CERT` (Amazon Root CA 1 — public, same for every account),
   `MQTT_CLIENT_CERT` (`xxxx-certificate.pem.crt`) and
   `MQTT_CLIENT_KEY` (`xxxx-private.pem.key`). Leave `MQTT_USER` /
   `MQTT_PASS` empty — the certificate is the credential.

> **The client key is a real secret.** Anyone with `Config.h` can
> impersonate the device — keep it out of public version control and
> revoke/rotate the certificate if it leaks.

> **Clock matters.** TLS verifies the broker certificate's validity
> dates, so the device clock must be roughly right. The framework
> syncs NTP at boot before MQTT connects; if NTP fails the TLS
> handshake can be rejected with a date error. On a TLS connect
> failure the serial log prints the underlying mbedTLS error to help
> tell a cert/clock problem apart from an auth denial.

---

## Threshold Alarms &amp; Alerting (`AlertManager`)

The alerting system is a fifth output module — alongside Web, Serial,
Display, MQTT and SD — toggled by `OUT_ALERTS` in `Config.h`. Where the
other outputs *report* the current state every cycle, the alarm engine
watches for a **threshold crossing or an event** and fans a notification
out to one or more channels. It is the framework's one stateful output:
everything else is current-state polling, this adds "a thing just
happened."

It needs **no sensor changes**. Every plugin already exposes typed
readings through `getReadings()`; a rule is just `(slug, key, op, value)`
evaluated over those. AS3935, Geiger, the gas/ENV units — all untouched.

### How it works

Each poll (`POLL_MS`, 500 ms) the engine reads the value named by every
enabled rule and runs it through a per-rule state machine:

- **THRESHOLD** rules — a sustained comparison (e.g. `geiger usv_per_h ≥
  5.0`). Debounced (must hold for *N* samples), and they clear only after
  the reading backs off through a **hysteresis** band, so they don't
  chatter around the line.
- **EVENT** rules — an upward edge on a counter (e.g. AS3935 `strikes`
  increments), optionally **gated** by a second reading (`distance_km ≤
  10`).

Per rule you also set a **severity** (info / warn / critical), whether it
**latches until acknowledged** vs **auto-resets**, and a **cooldown** that
rate-limits repeat notifications while a condition persists.

A state change emits an event (raised / renotify / cleared / ack) into a
16-deep in-RAM ring and the serial log, and routes it to the rule's
**channels**.

### Channels (sinks)

A rule carries a channel bitmask; each event is fanned out to the
channels that are both selected *and* available. A channel whose device
or URL is absent silently disables itself.

| Channel | What it does | Config |
|---|---|---|
| **Buzzer** | Chirps the built-in M5 speaker on a raised WARN/CRITICAL | `ALERT_BUZZER`, `ALERT_BUZZER_HZ/_MS` |
| **LCD** | Severity-coloured banner across the top of the live display | — (reuses `DisplayManager`) |
| **MQTT** | Publishes the event JSON to `<base>/alert` (not retained) | reuses the MQTT broker config |
| **SD** | Appends a CSV row to `/alerts.csv` (separate from the sensor log) | — (reuses the card) |
| **Dashboard** | Surfaces in `/api/alerts` + the dashboard Alarms panel | — |
| **LoRa** | Transmits the alert text over LoRa P2P (raise/clear edges) | reuses the registered `lora` plugin |
| **Webhook** | HTTPS POST of the event JSON to any URL | `ALERT_WEBHOOK_URL`, `ALERT_WEBHOOK_AUTH` |
| **Email** | HTTPS POST of `{to,subject,text}` to your email API / relay | `ALERT_EMAIL_URL/_AUTH/_TO` |
| **SMS** | Cellular text via the modem (async AT, daily-capped) | `ALERT_SMS_TO` + a registered `modem` |

The off-device HTTPS channels (Webhook, Email) share a **serialised
outbound queue** — `update()` drains one POST per loop so two TLS
handshakes never overlap. LoRa, Webhook, Email and SMS fire on state
**edges only** (raise/clear, not every renotify) to spare airtime / cost.

### Rules: seed defaults + runtime editing

Two seed rules ship compiled in `Config.h`:

- **Radiation** — `geiger usv_per_h ≥ ALERT_RAD_USV` (5.0), CRITICAL,
  latched.
- **Lightning** — AS3935 `strikes` edge, gated `distance_km ≤
  ALERT_LIGHTNING_KM` (10), CRITICAL.

Rules are then **runtime-editable and persistent**: the active set is
stored as a JSON blob in NVS (`Settings`), hybrid with the compiled seed —
exactly like Wi-Fi/MQTT (NVS override if present, else the seed). Edits
take effect on the next poll (no reboot). **Reset** drops the NVS override
back to the seed.

Edit rules from the **dashboard** — the ⚙ Rules editor in the Alarms
panel lists every rule with edit/delete and an add/edit form (slug, key,
kind, op, threshold, gate, severity, the nine channel checkboxes, latch,
debounce, hysteresis, cooldown). The **slug**, **key** and **gate-key**
fields offer dropdown suggestions of the common plugin slugs and reading
keys (an HTML `datalist`), but stay free-text so you can target any
plugin's `getReadings()` key the list doesn't cover. Or edit over REST:

| Method · Endpoint | Action |
|---|---|
| `GET /api/alerts` | Engine state + the recent-event ring |
| `POST /api/alerts/ack` `[?rule=N]` | Acknowledge latched rule(s) (default all) |
| `GET /api/alerts/rules` | List the current rules |
| `POST /api/alerts/rules/save` | Upsert one rule (JSON body; `id:0` = new) |
| `POST /api/alerts/rules/delete?id=N` | Delete a rule |
| `POST /api/alerts/rules/reset` | Restore the compiled seed rules |

### Config reference

```cpp
#define OUT_ALERTS true                 // the engine on/off

ALERT_RAD_USV / ALERT_RAD_COOLDOWN      // radiation seed rule
ALERT_LIGHTNING_KM / _COOLDOWN          // lightning seed rule
ALERT_BUZZER / _HZ / _MS                // buzzer sink
ALERT_WEBHOOK_URL / _AUTH               // webhook sink   ("" = off)
ALERT_EMAIL_URL / _AUTH / _TO           // email sink     ("" = off)
ALERT_SMS_TO                            // SMS sink       ("" = off)
```

> Keep API keys / auth values in `Secrets.h`, not `Config.h`, if the file
> is in version control.

### Caveats

- Rules evaluate at `POLL_MS` (500 ms). Latching sensors (Geiger ISR,
  AS3935 INT) never miss a transient between polls; a slow polled ADC is
  poll-rate-limited.
- **SMS** owns the single Port-C UART — it is mutually exclusive with the
  LLM / LoRa / GPS, costs per message (the modem caps sends per ~24 h), and
  gives no delivery receipt. Email/Webhook reach a phone for free, so
  prefer those unless cellular-without-Wi-Fi is required.
- The SMS AT sequence and the LoRa radio params are the standard flows but
  may need per-firmware tuning on real hardware.

---

## I2C Hub Support (PaHUB / PCA9548A)

Register a hub in `setup()` BEFORE adding plugins:

```cpp
void setup() {
  fw.addMux(0x70);                       // M5Stack PaHUB default address
  fw.addPlugin(new Plugin_NCIR2());      // bound to first available NCIR2
  fw.addPlugin(new Plugin_NCIR2());      // bound to second
  fw.addPlugin(new Plugin_TOF());
  fw.begin();
}
```

What the framework does:

1. After the root-bus scan, probes every registered hub address.
2. For each hub present, scans all 8 downstream channels.
3. Binds unbound external plugins to `(hub, channel, addr)` tuples
   in plugin-registration order — two `Plugin_NCIR2` instances will
   bind to two different channels carrying NCIR2 units at 0x5A.
4. Before every plugin `update()`, selects that plugin's channel
   on its hub so the transaction reaches the right device.

Multiple hubs are supported (e.g. `addMux(0x70); addMux(0x71);`).

Limitations:

- The PCA9548A intercepts at its own address regardless of channel
  state, so a chip with the same address as the hub itself can't
  be reached through it — e.g. an ENV3 unit's QMP6988 lives at 0x70
  and won't be accessible behind a hub at 0x70.
- No cascading (hub-behind-a-hub).
- Plugins of the same type behind a hub share one URL slug; the
  `/api/{slug}` endpoint returns the first match.  Use `/api/all`
  to see them all.

> **A hub channel does not isolate you from the root bus.** When a
> channel is selected, the hub joins it to the upstream wires, so
> every root-bus device is on the bus at the same time. A device
> behind a hub therefore **cannot share an I2C address with any
> device on the root bus** — when the channel is selected the two
> chips short together and every transaction with either one is
> corrupted. This bites units that pick a fixed address: the Heart
> and Ultrasonic units are both at 0x57, the ToF and Color units
> both at 0x29. If you need two same-address units, put **both**
> behind the hub on **separate channels** — never one on the root
> bus and one behind the hub. The boot scan now detects this case
> (`*** 0xNN ADDRESS CONFLICT ***`), refuses to bind the hub-side
> plugin, and prints how to fix it. Note that unbinding or removing
> the *plugin* does not help — the conflict is electrical, so the
> offending *unit* must be physically moved.

---

## Web API Endpoints

All endpoints are served over **HTTPS** — use `https://<device-ip>/...`.
Plain `http://` requests on port 80 receive a 301 redirect to the HTTPS
URL, so an old bookmark still works.  Because the device's TLS
certificate is self-signed, a browser shows a one-time security warning
the first time you connect (see [HTTPS / TLS](#https--tls) below).

| Method | URL | Description |
|---|---|---|
| GET | `/`                  | Live HTML dashboard (auto-refresh ~5 s) |
| GET | `/api/all`           | All plugins + readings + board + system info as JSON |
| GET | `/api/{slug}`        | One plugin's readings (e.g. `/api/env4`, `/api/heart`) |
| POST | `/api/{slug}/set`    | Control an output device (POST + `X-Requested-With`) — see [Controlling output devices](#controlling-output-devices) |
| GET | `/api/scan`          | Live I2C scan — root bus(es) plus every registered hub channel |
| GET | `/api/config`        | Framework configuration + board info as JSON |
| GET | `/api/rescan`        | Re-run the boot scan-and-bind pass; returns fresh `/api/all` |
| GET | `/api/mqtt`          | MQTT runtime status (connected, state, attempt/publish counts, timings) |
| GET | `/api/mqtt/publish`  | Force one immediate publish cycle; returns same status doc |
| GET | `/api/sdcard`        | SD card + CSV log status (boot #, filename, rows, bytes, failures) |
| GET | `/api/sdcard/flush`  | Commit buffered SD writes now (close+reopen); returns status doc |
| GET | `/api/sdcard/eject`  | Cleanly close the log file + unmount — card safe to remove |
| GET | `/api/endpoints`     | Self-describing list of every available endpoint |

`/api/all` and `/api/config` include a `board` object with the detected
board name and resolved I2C pin assignments, so a client can tell whether
it's talking to a CoreS3, Core2, or Core1 (Basic / Gray).

> **Live introspection.**  Hit `/api/endpoints` for a self-describing
> JSON list — useful when you've forgotten a URL or are pointing a tool
> at the device.  Output is hand-maintained server-side so it's the
> single source of truth for the API surface.

> **`/api/scan` output shape** depends on bus topology:
> - **Core1 (shared bus):** one `shared` array.
> - **Core2 / CoreS3 (separate buses):** `internal` + `external` arrays.
> - **Any board with a registered hub:** plus a `hubs` array with one entry
>   per `addMux()` address, each carrying a `channels` array of device
>   addresses present on each of the 8 downstream channels.
>
> Registered mux addresses are excluded from the root bus arrays so a
> hub doesn't appear twice in the output.

### HTTPS / TLS

The dashboard and every `/api/...` endpoint are served over HTTPS.
The plain-HTTP port (80) runs a redirect-only server that 301s every
request to the HTTPS URL, so an old `http://` bookmark still works.

The TLS certificate is a **self-signed** certificate embedded in the
firmware (`src/https_cert.h`).  On-device key generation was tried
first, but the cert library's generator corrupts the heap on
arduino-esp32 3.3.7 / ESP-IDF 5.5, so a fixed pre-generated cert is
baked in instead — which also makes every boot instant.  To use your
own certificate, replace the DER arrays in `src/https_cert.h`; the
openssl commands to regenerate are in that file's header comment.

Because the certificate is self-signed, browsers show a one-time
"not secure" warning the first time you connect — accept it once and
the browser remembers.  Note the embedded private key is not a
per-device secret: anyone with the firmware can read it.

HTTPS uses `ESPWebServerSecure` from the `esp32_idf5_https_server_compat`
library — a WebServer-compatible TLS wrapper.  It depends on the base
`esp32_idf5_https_server` library, so install **both** (the compat
wrapper alone will not compile).  Both are in the Library Manager; if
the base library isn't offered automatically as a dependency, search
for and install it as well.  Heavy concurrent use is not the target —
the dashboard is meant for one client at a time.

### Optional HTTP Basic Auth

Set credentials in `Config.h` to require auth on every route:

```cpp
#define WEB_AUTH_USER  "admin"
#define WEB_AUTH_PASS  "supersecret"
```

Leave `WEB_AUTH_USER` empty to disable.  When enabled, the dashboard
and every `/api/...` endpoint return `401 Unauthorized` until the
client sends matching credentials.  Browsers prompt automatically and
remember for the session.

Because the API is now HTTPS, Basic Auth credentials travel inside
the TLS tunnel and are encrypted on the wire — a real improvement
over the old plain-HTTP server.  Still pick a dedicated password:
the certificate is self-signed, and anyone with physical access to
the SD card can read the server's private key (`/https_pk.der`).

### Host-header allowlist (optional)

An extra layer for when the device is joined to a network. Set a
comma-separated list of allowed `Host` values in `Config.h`:

```cpp
// device IP and/or any hostname you reach it by
[[maybe_unused]] constexpr char WEB_HOST_ALLOWLIST[] = "192.168.1.50,m5stack.local";
```

When non-empty, the HTTPS server answers a request **only** if its
`Host` header (port stripped, case-insensitive) matches an entry —
otherwise it returns `403 Forbidden`. Leave it `""` to disable.

This guards against **DNS-rebinding** and access via unexpected
hostnames. It is **not** a client-IP filter: a server only sees the
client's source IP (never a domain), and the underlying HTTPS library
doesn't expose it — restrict *who* can connect at your router/firewall
or with a VLAN. Basic Auth (above) remains the real access control;
this is a lightweight complement.

It is **never enforced in AP or setup mode** (the access point is the
device's own network), so a wrong value can't lock you out of recovery
— you can always reach the setup portal over the AP, or factory-reset.

### Controlling output devices

Some plugins drive hardware that can be *commanded* — relays, servos,
LEDs.  Those are marked `"controllable": true` in `/api/all` and carry a
`CTRL` badge on the dashboard.  They're driven through one endpoint:

```
POST /api/<slug>/set            body: <param>=<value>[&<param>=<value> ...]
header: X-Requested-With: <any>   (CSRF guard, when WEB_CSRF_PROTECT is true)
```

> Control is **POST-only** and requires the `X-Requested-With` header — a
> CSRF guard so another origin can't forge a command using cached
> credentials (see `WEB_CSRF_PROTECT` in `Config.h`).  The dashboard sends
> both automatically.  Set `WEB_CSRF_PROTECT = false` to restore the old
> behaviour where a plain `GET /api/<slug>/set?param=value` also worked.

Examples (body shown after the path):

```
POST /api/4relay/set    relay1=1&relay2=0     relay 1 on, relay 2 off
POST /api/4relay/set    relays=10             all four relays from a 0-15 bitmap
POST /api/servo2/set    ch0=1500&deg3=90      channel 0 → 1500 µs, channel 3 → 90°
POST /api/8angle/set    led0=FF0000           8Angle user LED 0 → red
```

Every parameter is **validated by the plugin**.  A value outside the
device's legal range, a malformed value, or an unknown parameter is
**rejected and the hardware is left untouched** — rejected parameters
come back in the response's `rejected` array and the call returns HTTP
400.  Fully-valid calls return 200 with an `applied` array and the
device's resulting `state`.

This is deliberately the **only** control path for now: output devices
are driven via the Web API, not from serial or the display.

### Interactive dashboard

Each controllable device also *describes* its controls, via
`IDevice::controlSchema()`.  The Web API serialises that description
into a `controls` array on `/api/all` and `/api/{slug}` — one entry per
control, e.g.:

```json
{ "id": "relay1", "label": "Relay 1", "type": "toggle", "value": 0 }
{ "id": "deg0",   "label": "Channel 0", "type": "slider",
  "min": 0, "max": 180, "step": 1, "unit": "deg", "value": 90 }
{ "id": "led0",   "label": "LED 0", "type": "color" }
```

The dashboard reads that schema and renders a live widget per control —
a **toggle**, **slider**, **colour picker**, **text** field, or a
quick-action **button** — under a **Controllable Devices** section,
separate from the read-only sensor cards.  Operating a widget issues the
matching `GET /api/<slug>/set` call, and an **API Request Log** panel on
the page records every call with its HTTP status.

Because the widgets are generated from the schema, the dashboard adapts
itself: a new controllable plugin gets working dashboard controls the
moment it implements `controlSchema()` — no dashboard edits required.

---

## Stackable vs Pluggable

Every plugin reports a `mount` field (in `/api/all`, `/api/{slug}`, and
as a badge on the dashboard card) describing how the device attaches:

| `mount` | Meaning | Bus |
|---|---|---|
| `builtin`   | Soldered onto the Core itself or a power base — IMU, PMIC, RTC, the IP5306 gauge | Internal |
| `stackable` | An M-Bus **module** that bolts onto the Core's stacking connector | Internal |
| `pluggable` | A cabled **Grove / Port-A unit** | External (or shared, on Core1) |

Stackable modules always sit on the **internal** I2C bus; pluggable
units are on Port-A.  On Core1 the two buses are physically one, so a
stackable module and a pluggable unit there genuinely share an address
space — watch for the address clashes noted below.

---

## Supported Plugins

### Trimming the build — device-category switches

Four switches in `src/Config.h` control which whole *categories* of device
plugin are compiled in. Each is `1` (compiled) or `0` (omitted); in the
`.ino` both the category's `#include`s and its `fw.addPlugin(...)`
registrations are wrapped in a matching `#if`, so setting one to `0`
removes that category entirely — its headers are never parsed, nothing is
registered, and it costs zero flash and zero RAM (and trims compile time).

| Switch | Covers | Table below |
|---|---|---|
| `ENABLE_OPTIONAL_I2C`      | pluggable Port-A / Grove I2C Units | *External M5Stack Units* |
| `ENABLE_STACKABLE_MODULES` | M-Bus stackable modules            | *Stackable Modules*      |
| `ENABLE_PIN_DEVICES`       | non-I2C GPIO/PWM/ADC pin devices   | *Pin devices*            |
| `ENABLE_UART_DEVICES`      | Port-C serial devices              | *UART devices*           |

All four default to `1`, so a stock build is byte-identical to one built
without the switches. Turn one off when a build will never use that
category — e.g. `ENABLE_UART_DEVICES 0` on a unit with nothing on Port-C.
The board's own built-in chips (`Plugin_IMU`, `Plugin_PMIC`,
`Plugin_IP5306`, `Plugin_RTC`, `Plugin_INA3221`) are never gated — the
framework always needs them.

> ⚠ The switch also gates the registration lines. If you uncomment a
> `fw.addPlugin(...)` for a category, that category's switch must be `1`
> or the registration is silently skipped.

#### Output-channel switches

The five `OUT_*` flags in `src/Config.h` work the same way for the output
channels — they are compile-time switches, not just runtime toggles. Set
one to `false` and that channel's code **and its libraries** are excluded
from the build:

| Switch | Channel | Libraries dropped when `false` |
|---|---|---|
| `OUT_WEB`     | HTTPS dashboard + REST API   | `esp32_idf5_https_server` (+ compat) |
| `OUT_MQTT`    | MQTT publish                 | `PubSubClient`                       |
| `OUT_SERIAL`  | per-plugin Serial dump       | —                                    |
| `OUT_DISPLAY` | LCD output                   | —                                    |
| `OUT_SD_LOG`  | SD-card CSV log              | —                                    |

Each channel's class is replaced by an inert, same-interface stub when its
switch is `false`, so the framework still compiles and links unchanged — it
just calls no-ops. All default to `true`. The USB-serial **boot log** is
always available regardless of `OUT_SERIAL` (the stub still starts the
UART); `OUT_SERIAL` only removes the periodic per-plugin readings dump.

### Internal (board built-ins, board-aware)
| Plugin file | CoreS3 chip | Core2 chip | Core1 chip | Address | Slug |
|---|---|---|---|---|---|
| `Plugin_IMU.h`    | BMI270+BMM150 | MPU6886 | MPU9250 (Gray only) | 0x68 (+ 0x10 for mag) | `imu`    |
| `Plugin_PMIC.h`   | AXP2101       | AXP192  | *(n/a — uses IP5306)* | 0x34                  | `pmic`   |
| `Plugin_IP5306.h` | —             | —       | IP5306                | 0x75                  | `ip5306` |
| `Plugin_RTC.h`    | BM8563        | BM8563  | *(n/a — no RTC)*      | 0x51                  | `rtc`    |

- `Plugin_IMU` publishes magnetometer readings only when the board provides
  one (CoreS3, or Gray via MPU9250). On Core2 and Basic the `mag_x/y/z`
  fields are omitted. Basic has no IMU at all and the plugin doesn't bind.
- `Plugin_PMIC` refuses to bind on Core1 (no I2C PMIC). Leave it registered
  in the `.ino` — it's a no-op on boards without an AXP.
- `Plugin_IP5306` is Core1-specific (gauge only — battery % and charging
  state). Harmless on Core2/CoreS3 since 0x75 isn't populated there.

### External M5Stack Units (work identically on all supported boards)
| Plugin file | Unit | Address | Slug |
|---|---|---|---|
| `Plugin_ENV4.h`             | ENV IV  (SHT40+BMP280)   | 0x44/0x76 | `env4`       |
| `Plugin_ENV3.h`             | ENV III (SHT30+QMP6988)  | 0x44/0x70 | `env3`       |
| `Plugin_NCIR2.h`            | NCIR2 (MLX90614)         | 0x5A      | `ncir2`      |
| `Plugin_TOF.h`              | ToF (VL53L0X)            | 0x29      | `tof`        |
| `Plugin_COLOR.h`            | Color (TCS3472)          | 0x29      | `color`      |
| `Plugin_EARTH.h`            | Earth (soil moisture)    | 0x08      | `earth`      |
| `Plugin_LIGHT.h`            | Light (BH1750)           | 0x23/0x5C | `light`      |
| `Plugin_TVOC.h`             | TVOC/eCO2 (SGP30)        | 0x58      | `tvoc`       |
| `Plugin_ACCEL.h`            | Accel (ADXL345)          | 0x53/0x1D | `accel`      |
| `Plugin_JOYSTICK.h`         | Joystick Unit            | 0x52      | `joystick`   |
| `Plugin_ANGLE.h`            | Angle Unit (rotary)      | 0x36      | `angle`      |
| `Plugin_ULTRASONIC.h`       | Ultrasonic (RCWL-9600)   | 0x57      | `ultrasonic` |
| `Plugin_HEART.h`            | Heart Mini (MAX30100)    | 0x57      | `heart`      |
| `Plugin_HEART_MAX30102.h`   | Heart (MAX30102 variant) | 0x57      | `heart30102` |
| `Plugin_WEIGHT.h`           | Weight (HX711 bridge)    | 0x26      | `weight`     |
| `Plugin_THERMAL.h`          | Thermal (MLX90640)       | 0x33      | `thermal`    |
| `Plugin_COMPASS.h`          | Compass (QMC5883L)       | 0x0D      | `compass`    |
| `Plugin_MPU6886.h`          | 6-Axis IMU (MPU6886)     | 0x68      | `imu6886`    |
| `Plugin_SCD4X.h`            | CO2 / CO2L (SCD40/41)    | 0x62      | `co2`        |
| `Plugin_INA226.h`           | INA226 power monitor     | 0x40      | `ina226`     |
| `Plugin_ADS1110.h`          | ADC Unit (ADS1110)       | 0x48      | `adc`        |
| `Plugin_ADS1115.h`          | Ammeter/Voltmeter (ADS1115) | 0x48   | `ads1115`    |
| `Plugin_QMP6988.h`          | Barometric Pressure (QMP6988) | 0x70 | `baro`       |
| `Plugin_GP8413.h`           | DAC 2 Unit (GP8413, controllable) | 0x59 | `dac`    |
| `Plugin_VL53L1X.h`          | ToF4M Unit (VL53L1X, library)¹ | 0x29 | `tof4m`     |
| `Plugin_MultiGas.h`         | Grove Multichannel Gas V2²    | 0x08  | `multigas`  |

> ¹ `Plugin_VL53L1X` is a thin wrapper around the Pololu **VL53L1X**
> library — the sensor has no documented register map, so it cannot be
> driven raw the way `Plugin_TOF` drives the VL53L0X. Install the library
> from the Arduino Library Manager, then uncomment its `#include` and
> registration line together (same opt-in pattern as `Plugin_FAN`). It
> shares 0x29 with the VL53L0X ToF Unit and the Color Unit — use one ToF
> plugin or the other, not both.
>
> ² `Plugin_MultiGas` is a raw driver (no external library) for Seeed's
> Grove Gas Sensor V2 — four MEMS elements (NO2 / alcohol / VOC / CO)
> behind an STM32F030. It shares 0x08 with the Earth and Faces II units
> and has no ID register, so its presence check is heuristic; it is
> registered commented-out (opt-in) and, if enabled, must precede
> `Plugin_EARTH`. Readings are raw 10-bit ADC counts (qualitative only),
> and a `warming` flag with `warmup_s` countdown covers the 3-minute
> preheat window.

> **Address conflicts:** Several units share addresses. Registration order
> in the `.ino` determines priority — the framework tries plugins in that
> order and the first one whose `begin()` succeeds claims the device. Once
> a `(bus, mux, channel, addr)` tuple is claimed, later plugins with the
> same address are politely skipped with a `skipped — claimed` log line
> rather than fighting over the chip. Register strict plugins (with a
> WHO_AM_I / PART_ID / chip-ID check in `begin()`) BEFORE permissive ones
> so a wrong-chip-on-the-shared-address always fails the strict check and
> lets the permissive plugin take over.
>
> Examples already wired up correctly in the default `.ino`:
> - 0x29: `Plugin_TOF` (strict, WHO_AM_I) before `Plugin_COLOR` (permissive)
> - 0x44: `Plugin_ENV4` (strict, BMP280 chip ID 0x58 @ 0x76) before `Plugin_ENV3`
> - 0x57: `Plugin_HEART` (strict, PART_ID 0x11) and `Plugin_HEART_MAX30102`
>   (strict, PART_ID 0x15) before `Plugin_ULTRASONIC` (permissive)

### Stackable Modules (M-Bus, internal bus)

These bolt onto the Core's stacking connector rather than plugging into
Port-A.  All sit on the **internal** I2C bus (`mount: stackable`).

| Plugin file | Module | Address | Slug | Controllable |
|---|---|---|---|---|
| `Plugin_4RELAY.h`         | 4-Relay Module (13.2) — 4× 5 A relays + LEDs | 0x26 | `4relay`    | ✅ relays + LEDs |
| `Plugin_SERVO2.h`         | Servo2 Module — 16-ch PCA9685 servo driver  | 0x40 | `servo2`    | ✅ 16 channels   |
| `Plugin_8ANGLE.h`         | 8Angle Module — 8 dials + switch + 8 LEDs   | 0x43 | `8angle`    | ✅ 8 LEDs        |
| `Plugin_FACES2.h`         | Faces II panel — QWERTY / Calc / GameBoy    | 0x08 | `faces2`    | — input only     |
| `Plugin_FACES2_ENCODER.h` | Faces II Encoder — 5 encoders + push        | 0x5E | `faces2enc` | — input only     |
| `Plugin_GOPLUS2.h`        | GoPlus2 Module — 2× DC motor + 2× servo + RGB + 2× encoder | 0x38 | `goplus2`  | ✅ motors/servos/RGB |
| `Plugin_STEPMOTOR.h`      | Step Motor Module v1.1 — I2C monitor + enable (0x27) | 0x27 | `stepmotor` | ✅ enable/reset |
| `Plugin_4IN8OUT.h`        | 4In8Out Module — 4 digital in + 8 high-side out | 0x45 | `4in8out`  | ✅ 8 outputs     |
| `Plugin_FAN.h`            | Fan Module v1.1 — PWM fan + RPM (needs M5Module-Fan lib) | 0x18 | `fan` | ✅ speed + power |

Notes:

- The QWERTY, Calculator and GameBoy **Faces II** faceplates are
  interchangeable, all at 0x08 with one shared protocol — so a *single*
  `Plugin_FACES2` covers all three.  It reports the raw key/scancode
  byte; what that byte *means* depends on which faceplate is fitted.
- The **M5GO Bottom 2** battery base is not a separate plugin — its fuel
  gauge is an IP5306 at 0x75, already handled by `Plugin_IP5306`.  (The
  Bottom 2's SK6812 RGB LEDs aren't on I2C and are out of scope.)
- **Address clashes on a shared bus:** `Plugin_4RELAY` (0x26) collides
  with `Plugin_WEIGHT`, and `Plugin_FACES2` (0x08) collides with
  `Plugin_EARTH`.  Only one of each pair can be on a bus at once; the
  Weight and Earth unit plugins are registered first, so a 4-Relay /
  Faces II only binds when its rival unit is absent.
- `Plugin_FACES2_ENCODER`'s register map is a best-guess pending
  hardware verification — see the comment block at the top of the file.
- **Register map not yet hardware-verified:** `Plugin_GOPLUS2` is an
  STM32-based module whose `REG_*` constants follow M5Stack's usual
  conventions but have *not* been confirmed against the module's
  firmware.  The file carries a prominent `⚠ REGISTER MAP NOT YET
  HARDWARE-VERIFIED` comment block; the command parsing, input
  validation and framework wiring are correct regardless — if the
  device doesn't respond, only the named `REG_*` constants at the top
  of the file need correcting.  (`Plugin_STEPMOTOR` and
  `Plugin_4IN8OUT` were in this list but are now verified against
  M5Stack's published libraries — see below.)
- **GoPlus2 control:** `motor1`/`motor2` take a signed speed
  −127..127 (0 = stop), `servo1`/`servo2` take 0..180°, `rgb` takes
  six hex digits `RRGGBB`.  Encoder counts are reported as `enc1`/`enc2`.
- **Step Motor Module v1.1** is a *hybrid* device and `Plugin_STEPMOTOR`
  covers the **I2C side only**.  Its 0x27 register map is verified
  against M5Stack's "Step Motor Module V1.1 I2C Protocol V3" document:
  the four limit-switch inputs and three driver fault flags are
  reported as readings (`limit1..limit4`, `fault1..fault3`); `enable`
  (0/1) energises/de-energises the drivers and `reset` (0–7 bitmap)
  drives the per-axis reset lines.  The actual motion — `STEP`/`DIR`
  pulse trains for the three axes — is generated on the **host's GPIO**
  through the M-Bus and is **not** an I2C function; it is out of scope
  for this plugin and belongs to the planned non-I2C pin layer.
- **4In8Out control:** `out1`..`out8` take a boolean (0/1/on/off),
  or `outs` takes a 0–255 bitmap (bit0 = out1 … bit7 = out8).  The
  four inputs are reported as `in1`..`in4`.  `Plugin_4IN8OUT`'s
  register map is verified against M5Stack's M5Module-4IN8OUT
  library: inputs are byte-per-channel at 0x10–0x13, outputs
  byte-per-channel at 0x20–0x27.

---

## Non-I2C Pin Devices (GPIO / PWM / ADC)

Not every M5Stack unit speaks I2C.  The PIR, Relay, Buzzer, Servo,
Light (CdS) and Earth (soil) units are plain GPIO / PWM / ADC devices.
They are supported through `IPinDevice` — a base class that derives
`IDevice`, so a pin device appears in the dashboard, `/api`, MQTT, the
SD log and serial output exactly like an I2C sensor, but bypasses the
I2C scan.

| `PinDevice` file | Unit | Signal | Constructor | Readings | Controllable |
|---|---|---|---|---|---|
| `PinDevice_PIR.h`    | PIR (AS312)        | GPIO in       | `(signalPin)`            | `motion`                       | — |
| `PinDevice_Relay.h`  | Relay (Mini 3 A)   | GPIO out      | `(pin[, activeHigh])`    | `state`                        | ✅ `state` |
| `PinDevice_Buzzer.h` | Buzzer             | PWM (LEDC)    | `(signalPin)`            | `freq`, `playing`              | ✅ `freq`/`state`/`beep` |
| `PinDevice_Servo.h`  | Servo (SG90)       | PWM (LEDC)    | `(signalPin)`            | `angle`, `us`                  | ✅ `angle`/`us` |
| `PinDevice_Light.h`  | Light (CdS)        | ADC           | `(adcPin)`               | `light_raw`, `light_pct`       | — |
| `PinDevice_Earth.h`  | Earth (soil)       | ADC + GPIO    | `(analogPin, digitalPin)`| `moisture_raw/_pct`, `dry`     | — |
| `PinDevice_Mic.h`    | Mic (analog)       | ADC           | `(adcPin)`               | `level`, `level_pct`           | — |
| `PinDevice_Angle.h`  | Angle (rotary pot) | ADC           | `(adcPin)`               | `raw`, `pct`, `angle`          | — |
| `PinDevice_DS18B20.h`| DS18B20 temp       | 1-Wire        | `(pin)`                  | `temp` (°C)                    | — |
| `PinDevice_IR.h`     | IR (Tx/Rx)         | IR GPIO       | `(rxPin, txPin)`         | `rx_count`, `last_bits`        | ✅ `send` |
| `PinDevice_ECG.h`    | ECG (AD8232)       | ADC + GPIO    | `(adcPin[, loPlus, loMinus])` | `signal`, `leads_off`     | — |
| `PinDevice_Hall.h`        | Hall (A3144E)   | GPIO in       | `([signalPin])`          | `magnet`                       | — |
| `PinDevice_Limit.h`       | Limit switch    | GPIO in       | `([signalPin])`          | `pressed`                      | — |
| `PinDevice_OP180.h`       | OP180 (ITR9606) | GPIO in       | `([signalPin])`          | `blocked`                      | — |
| `PinDevice_DualButton.h`  | Dual Button     | GPIO in ×2    | `([redPin, bluePin])`    | `btn_red`, `btn_blue`          | — |
| `PinDevice_TubePressure.h`| Tube Pressure   | ADC           | `([adcPin])`             | `pressure` (kPa)               | — |
| `PinDevice_Grove2Grove.h` | Grove2Grove     | GPIO out + ADC| `([pwrEnPin, sensePin])` | `current` (mA), `power`        | ✅ `power` |
| `PinDevice_Button.h`      | Button Unit     | GPIO in       | `([signalPin])`          | `pressed`, `press_count`       | — |
| `PinDevice_Motor.h`       | DC Motor / Fan  | PWM (LEDC)    | `(signalPin)`            | `speed` (%), `running`         | ✅ `speed` |
| `PinDevice_Watering.h`    | Watering Unit   | ADC + GPIO out| `(moisturePin, pumpPin)` | `moisture_raw/_pct`, `pump`    | ✅ `pump` |
| `PinDevice_MQ.h`          | MQ-series gas   | ADC           | `(adcPin, model[, warmupSec, vc, rl, divRatio])` | `sensor_v`, `rs`, `warming` | — |
| `PinDevice_Cotech.h`      | Cotech 36-7959 weather | 433 MHz OOK RF | `(rxPin)` | `temp_c`, `humidity`, `wind_mps`, `gust_mps`, `wind_dir`, `rain_mm`, `uv`, `light_lux` | ✅ `pair` |

`PinDevice_Relay` is the device for any plain on/off output unit —
a Flashlight, the Solid-State Relay units, or a laser emitter all
plug into it; `PinDevice_Button` likewise covers a laser receiver
or any bare digital-input unit.

Many of these are **Port-B units** — they plug into the single Port-B
Grove connector.  On the classic Core / Core2 that connector is
`GPIO26` Yellow + `GPIO36` White; on the **CoreS3 it is `G8` White +
`G9` Yellow**.  The no-argument constructors default to the Core/Core2
pins (`26`/`36`), so on a CoreS3 pass the actual pins (e.g.
`PinDevice_Light(8)`).  Because Port B is one physical connector, only
one Port-B unit can be plugged in at a time; the `.ino` registers them
as a commented block where you uncomment exactly one.

`PinDevice_Cotech` is the odd one out — it is a 433.92 MHz OOK radio
receiver for the Cotech 36-7959 family of outdoor weather stations
(and Sainlogic / SwitchDoc / uctech clones).  Wire any 433 MHz OOK
receiver's DATA line to a free input-capable GPIO and pass it as
`rxPin`; it is not tied to Port B.

`PinDevice_DS18B20` and `PinDevice_IR` need external libraries —
**OneWire + DallasTemperature** and **IRremoteESP8266** respectively
(install via the Library Manager).  To avoid forcing those on every
build, their `#include` lines in the `.ino` are commented out by
default; uncomment the `#include` *and* the registration line
together when you want them.  The other twelve devices need no libraries.

Register them in `setup()` (a commented example block is in the
`.ino`), giving the pin(s) each unit is wired to:

```cpp
fw.addPlugin(new PinDevice_PIR(36));         // motion in
fw.addPlugin(new PinDevice_Servo(26));       // PWM servo
fw.addPlugin(new PinDevice_Light(36));       // CdS on an ADC1 pin (Core/Core2)
fw.addPlugin(new PinDevice_Light(8));        // same, on CoreS3 Port-B White (G8)
```

Key differences from an I2C plugin:

- **No auto-detection.** A bare pin can't be probed — a pin device is
  active because you registered it, not because the framework found
  it.  A wrong pin number just produces a card of meaningless values.
- **One device per Grove port.** Port-A is I2C; Port-B is a GPIO/ADC
  pair, Port-C a UART pair.  There is no bus and no hub for these.
- **ADC1 only.** While WiFi is connected — which this framework keeps
  up — only ADC1 pins work for `analogRead()`; ADC2 pins silently
  fail.  The ADC1 range depends on the chip: **GPIO 32-39 on the
  classic ESP32 (Core / Core2)** and **GPIO 1-10 on the ESP32-S3
  (CoreS3)** — so the CoreS3's Port-B White pin, **G8**, is a valid
  ADC1 pin.  Every analog pin device (Light, Earth, Angle, Mic, ECG,
  MQ, TubePressure, Watering, Grove2Grove) warns at boot if given a
  pin outside both ranges.  On the classic ESP32, GPIO 36 and 39 are
  input-only ADC1 pins and are ideal for analog sensors.
- **Avoid reserved pins** — GPIO 21/22 (Port-A I2C) and the LCD / SD
  SPI pins.

The controllable pin devices (Relay, Buzzer, Servo) use the same
`GET /api/{slug}/set?param=value` interface and per-parameter
validation as the controllable I2C modules.

### UART (serial) devices

Some M5Stack units speak a serial protocol over a hardware UART —
GPS, barcode scanners, fingerprint readers, the cellular / LoRa
modems, RS-485 / RS-232 bridges.  `IUartDevice` covers them.  It
*extends* `IPinDevice`, so the framework activates it through the
same path with no extra wiring; it just adds ownership of a
`HardwareSerial` port.

| `UartDevice` file | Unit | Constructor | Readings | Library |
|---|---|---|---|---|
| `UartDevice_Barcode.h`   | Barcode scanner | `(port, baud [, rx, tx])` | `scan_count`, `code_len` (+`last_code` in JSON) | none |
| `UartDevice_GPS.h`       | GPS (NMEA)      | `(port, baud [, rx, tx])` | `fix`, `lat`, `lng`, `alt_m`, `sats`, `speed_kmph` | TinyGPSPlus |
| `UartDevice_Modem.h`     | Cellular modem  | `(port, baud [, rx, tx])` | `rssi_dbm`, `signal_pct`, `registered` | none |
| `UartDevice_ModuleLLM.h` | Module LLM (offline AI) | `(port, baud [, rx, tx])` | `connected`, `busy` (+`prompt`/`answer` in JSON) | M5Module-LLM |
| `UartDevice_PMSA003.h`   | PM2.5 air quality (PMSA003) | `(port, baud [, rx, tx])` | `pm1_0`, `pm2_5`, `pm10` | none |
| `UartDevice_ASR.h`       | ASR voice module (CI1302) | `(port, baud [, rx, tx])` | `last_cmd`, `cmd_count` | none |

```cpp
fw.addPlugin(new UartDevice_Barcode(Serial2, 9600));
```

**Only one UART device at a time.** The ESP32's console UART is the
USB serial monitor; the practical external port is Port-C.  The RX/TX
pins are resolved automatically for the detected board — Core1 16/17,
Core2 & Tough 13/14, CoreS3 18/17 — so the constructor takes just
`(port, baud)`; pass explicit `rx, tx` only to override.  All UART
devices share that one port, so register at most one.  `UartDevice_GPS`
(TinyGPSPlus) and `UartDevice_ModuleLLM` (M5Module-LLM) need extra
libraries, so their `.ino` `#include`s are commented out by default;
`UartDevice_Barcode`, `UartDevice_Modem`, `UartDevice_PMSA003`
(PM2.5) and `UartDevice_ASR` (voice) need no library.

> ⚠ **M5Stack Fire:** Port-C (GPIO 16/17) is wired to the Fire's PSRAM —
> do not register a Port-C UART device on a Fire.

`UartDevice_Modem` is a deliberately minimal, **read-only** monitor —
it issues the two bedrock 3GPP AT commands (`AT+CSQ`, `AT+CREG?`),
which are identical across the SIM7080G (CatM), SIM7600G (4G LTE) and
SIM7020G (NB-IoT) modems, and reports signal strength + registration.
The same device class therefore drives the COM.LTE and COM.NB-IoT
modules with no new code.  It is not a full modem driver (no data
session, SMS or GNSS).

`UartDevice_ModuleLLM` integrates the **M5Stack Module LLM** — an
offline-AI module (AX630C SoC) that runs a small language model
on-device — as a **text-chat** device, using the official
`M5Module-LLM` library to speak the StackFlow JSON-over-UART
protocol.  It is *controllable*: `command("ask", <prompt>)` (the
`GET /api/llm/set?ask=...` endpoint) starts a query, and because the
module's inference is **asynchronous** the call returns immediately —
`fastPoll()` then folds the streamed reply tokens into `answer` while
the rest of the framework keeps running.  The dashboard shows a
dedicated **LLM Chat panel** (visible only when the module is bound)
that posts prompts and polls `/api/llm` to display the reply as it
streams in.  Model loading blocks once, at boot, for a few seconds.
Its `.ino` `#include` is commented out by default since it needs the
M5Module-LLM library.  The voice units (ASR / TTS / wake word) are
not used in this text-only integration.

Some TIER-2 "serial" entries deliberately do **not** get their own
`UartDevice_*`:

- **RS-485 / RS-232 units** are *transports*, not sensors — they have
  no readings of their own.  `IUartDevice` already *is* the generic
  serial-port abstraction; to talk a specific protocol (e.g. Modbus)
  over RS-485 you subclass `IUartDevice` for that protocol.
- The **fingerprint reader** is an interactive device (enrolment is a
  place-finger / lift / repeat sequence) and does not fit a passive
  poll-for-readings model.
- The **LoRa / LoRaWAN modules** need vendor-specific AT command sets,
  join credentials, or hardware mode pins — implementing one well
  needs its protocol document, not a guess.

Any of those can still be added as a proper `UartDevice_*` — point me
at the unit's protocol document and it gets done correctly, the same
way the Step Motor I2C side was.

---

## Network Devices (Claude API + escalation router)

Two plugins extend the framework beyond the board's own buses out onto
the network. Both are pure HTTPS clients, so neither is an I2C or UART
device: they subclass `IPinDevice` (which already gives "no address,
activate once via `beginPins()`"), report no I2C address so the boot
scan skips them, and need **no changes to `Framework`**. Both are
*controllable* and stream their reply asynchronously into the same
dashboard / REST / MQTT / SD plumbing as every other device, exactly
like the Module LLM.

| `NetDevice` file | Role | Constructor | Endpoints | Settings |
|---|---|---|---|---|
| `NetDevice_ClaudeAPI.h` | Direct Anthropic Messages API client | `()` | `GET /api/claude/set?ask=…` · `?clear=1` | `CLAUDE_*` in `Config.h` |
| `NetDevice_Router.h` | On-device classify → local / cloud / agent | `(localLlm[, directApi])` | `GET /api/route/set?ask=…` · `?clear=1` | `ROUTER_*` in `Config.h` |

`NetDevice_ClaudeAPI` POSTs to `api.anthropic.com/v1/messages` with
streaming enabled and folds the SSE `content_block_delta` text into
`answer`. It returns Claude the **model** — text in, text out — **not
Claude Code**: there is no filesystem, shell, or tool loop on the
ESP32, so anything that must read or edit a codebase still has to reach
Claude Code elsewhere. The key trade-off is that `CLAUDE_API_KEY` lives
in firmware flash, which can be read off a desk device — scope it
tightly, rotate it if it leaks, and keep `Config.h` out of public
version control. Leave `CLAUDE_API_KEY` empty to keep the plugin
compiled-in but inert.

`NetDevice_Router` is the single chat entry point. On each turn it
classifies the prompt **on-device** (a fast keyword / file-extension /
path heuristic, biased toward escalating) and picks one of three routes:

- **local** — delegates to the on-board Module LLM via its public
  `command("ask", …)` and mirrors its streamed answer through
  `toJson()`. Fully decoupled — no edits to `UartDevice_ModuleLLM`.
- **direct_api** *(optional 3rd route, `ROUTER_DIRECT_API`)* — a
  non-coding turn that is too rich for the small local model but needs
  no repo goes straight to a `NetDevice_ClaudeAPI` plugin, skipping the
  Pi. This is the route that puts an API key in flash.
- **escalated** — a coding / agent task opens a `WiFiClientSecure` to
  your Orange Pi orchestrator (`ROUTER_PI_*`), POSTs a brief, and
  streams back the SSE reply. In this mode **the CoreS3 never holds the
  Anthropic key** — the Pi owns Claude Code.

The chosen route is published as `route_taken` (`local` / `direct_api`
/ `escalated`), so local-vs-cloud accounting flows to MQTT and the SD
log for free. Register the local model first so the router can delegate
to it; the `.ino` wires both architectures behind `ROUTER_DIRECT_API`:

```cpp
IDevice* moduleLLM = new UartDevice_ModuleLLM(Serial2, 115200);
fw.addPlugin(moduleLLM);                          // local model first
#if ROUTER_DIRECT_API                             // 3-way: local → API → Pi
  auto* claudeAPI = new NetDevice_ClaudeAPI();
  fw.addPlugin(claudeAPI);
  fw.addPlugin(new NetDevice_Router(moduleLLM, claudeAPI));
#else                                             // 2-way: local → Pi (preferred)
  fw.addPlugin(new NetDevice_Router(moduleLLM));
#endif
```

Both connections retry transient TLS-allocation failures (the heap can
fragment briefly mid-dashboard-handshake) and use an **inactivity**
timeout, not a total-duration cap, so a long answer that keeps
streaming stays healthy. With `ROUTER_PI_HOST` left empty the router
stays compiled-in but inert and answers every turn locally.

---

## Writing a Custom Plugin

External-unit plugins are completely board-agnostic — they only see the
`TwoWire*` the framework hands them, so the same plugin code works on
any board. Create `plugins/Plugin_MYUNIT.h`:

```cpp
#pragma once
#include "../src/IDevice.h"

class Plugin_MYUNIT : public IDevice {
public:
  const char* name() const override { return "My Sensor"; }
  const char* slug() const override { return "myunit"; }   // used in URL

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x42; n = 1;
  }

  // Optional: restrict to one bus
  // I2CBus preferredBus() const override { return I2CBus::External; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire; addr = a;
    return regRead8(0x0F) == 0xAB;   // example ID register
  }

  void update() override {
    _val = regRead8(0x00) * 0.1f;
  }

  void toJson(JsonObject& o) const override {
    o["value"] = _val;  o["value_unit"] = "units";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"value", _val, "units"};
    n = 1;
  }

private:
  float _val = 0;
};
```

If you need board-specific behaviour in your own plugin, include
`"../src/BoardInfo.h"` and call `BoardInfo::detect()` inside `begin()`.

---

## Runtime Output Toggle

Output modules are public members of `Framework` and can be toggled live:

```cpp
fw.display.enabled = false;   // turn off LCD
fw.serial.enabled  = true;    // turn on serial
fw.webApi.enabled  = false;   // stop web server
fw.mqtt.enabled    = false;   // stop publishing to broker
fw.sdlog.enabled   = false;   // stop CSV logging to SD
```

---

## Project Structure

```
M5Stack_I2C_Framework/
├── M5Stack_I2C_Framework.ino   Main sketch – plugin registration
├── src/
│   ├── Config.h                User-editable settings — a list of #includes for…
│   ├── config/                 …per-section setting headers (01_wifi.h … 26_lora_*.h)
│   ├── Secrets.h               Per-device credentials (git-ignored; copy from .example)
│   ├── BoardInfo.h/.cpp        Runtime board detection (CoreS3 / Core2 / Tough / Core1)
│   ├── IDevice.h               Abstract device interface
│   ├── IPinDevice.h            Base for non-I2C GPIO/PWM/ADC devices
│   ├── IUartDevice.h           Base for Port-C UART devices
│   ├── CmdParse.h              Shared command-parameter validators (cmd:: namespace)
│   ├── Framework.h/.cpp        Core orchestrator + I2C hub support
│   ├── Settings.h/.cpp         NVS-backed runtime settings (WiFi / login / MQTT / key)
│   ├── Security.h              Boot-time credential hygiene (default-cred guard, AP pw)
│   ├── DisplayManager.h/.cpp   LCD output (templates, scroll, grid)
│   ├── WebAPI.h/.cpp           HTTPS server + HTTP→HTTPS redirect + dashboard
│   ├── WebAssets.h             Embedded dashboard / setup / settings HTML (PROGMEM)
│   ├── HttpSse.h               Streaming-HTTP reader (skips headers, de-chunks SSE)
│   ├── MQTTOut.h/.cpp          MQTT publisher + optional HA discovery
│   ├── SDLogger.h/.cpp         microSD CSV logger (one file per boot)
│   ├── AlertManager.h/.cpp     Threshold / event alarm engine + channel sinks
│   ├── PpgBeatDetector.h       Shared HR + SpO2 DSP (both Heart plugins)
│   └── SerialOut.h/.cpp        Serial monitor output
├── plugins/
│   ├── Plugin_IMU.h            Board-aware: BMI270+BMM150 / MPU6886 / MPU9250
│   ├── Plugin_PMIC.h           Board-aware: AXP2101 / AXP192 (no-op on Core1)
│   ├── Plugin_IP5306.h         Core1-only battery gauge (no-op elsewhere)
│   ├── Plugin_RTC.h            BM8563 (Core2/CoreS3; no-op on Core1)
│   ├── Plugin_INA3221.h        Core2 v1.1 power monitor @ 0x40 (die-ID checked)
│   ├── Plugin_ENV4.h           ENV IV — SHT40 + BMP280 (temp/humid/press)
│   ├── Plugin_ENV3.h           ENV III — SHT30 + QMP6988 (temp/humid/press)
│   ├── Plugin_NCIR2.h          NCIR2 (MLX90614) — non-contact IR thermometer
│   ├── Plugin_TOF.h            ToF (VL53L0X) — laser time-of-flight distance
│   ├── Plugin_COLOR.h          Color Unit (TCS3472) — RGB + clear
│   ├── Plugin_EARTH.h          Earth Unit — analog soil-moisture probe
│   ├── Plugin_LIGHT.h          Light Unit (BH1750) — ambient illuminance (lux)
│   ├── Plugin_TVOC.h           TVOC/eCO2 Unit (SGP30) — indoor air quality
│   ├── Plugin_ACCEL.h          Accel Unit (ADXL345) — 3-axis accelerometer
│   ├── Plugin_JOYSTICK.h       Joystick Unit — X/Y position + button
│   ├── Plugin_ANGLE.h          Angle Unit — rotary potentiometer
│   ├── Plugin_ULTRASONIC.h     Ultrasonic Unit (RCWL-9600) — distance
│   ├── Plugin_HEART.h          Mini Heart Rate Unit (MAX30100) — live BPM + SpO2
│   ├── Plugin_HEART_MAX30102.h Heart Rate variant (MAX30102) — live BPM + SpO2
│   ├── Plugin_WEIGHT.h         Weight Unit (HX711) — load-cell bridge
│   ├── Plugin_THERMAL.h        Thermal Unit (MLX90640) — 32×24 IR camera array
│   ├── Plugin_COMPASS.h        Compass Unit (QMC5883L) — 3-axis magnetometer
│   ├── Plugin_MPU6886.h        6-Axis IMU Unit (MPU6886) — accel + gyro + temp
│   ├── Plugin_SCD4X.h          CO2 / CO2L Unit (SCD40/41) — CO2 + temp/humidity
│   ├── Plugin_INA226.h         INA226 power monitor — bus V + current + power
│   ├── Plugin_ADS1110.h        ADC Unit (ADS1110) — 16-bit single-channel ADC
│   ├── Plugin_ADS1115.h        Ammeter/Voltmeter Unit (ADS1115) — 4-channel ADC
│   ├── Plugin_QMP6988.h        Barometric Pressure Unit (QMP6988) — temp + pressure
│   ├── Plugin_GP8413.h         DAC 2 Unit (GP8413) — dual 0-10 V output — controllable
│   ├── Plugin_VL53L1X.h        ToF4M Unit (VL53L1X) — 4 m laser ranging — Pololu lib
│   ├── Plugin_MultiGas.h       Grove Multichannel Gas V2 — NO2/alcohol/VOC/CO (raw)
│   ├── Plugin_4RELAY.h         Stackable: 4-Relay Module — controllable
│   ├── Plugin_2RELAY.h         Stackable: Module13.2 2Relay (0x25) — controllable
│   ├── Plugin_SERVO2.h         Stackable: Servo2 16-ch (PCA9685) — controllable
│   ├── Plugin_8ANGLE.h         Stackable: 8Angle Module — dials + controllable LEDs
│   ├── Plugin_FACES2.h         Stackable: Faces II key panel (QWERTY/Calc/GameBoy)
│   ├── Plugin_FACES2_ENCODER.h Stackable: Faces II Encoder — 5 encoders + buttons
│   ├── Plugin_GOPLUS2.h        Stackable: GoPlus2 — 2× motor/servo + RGB + encoders
│   ├── Plugin_STEPMOTOR.h      Stackable: Step Motor v1.1 — I2C monitor/enable (motion is host-GPIO)
│   ├── Plugin_4IN8OUT.h        Stackable: 4In8Out — 4 digital in / 8 high-side out
│   ├── Plugin_AIN420MA.h       Stackable: AIN4-20mA (0x55) — 4-ch 4-20mA analog input
│   ├── Plugin_PPS.h            Stackable: PPS (0x35) — programmable power supply — controllable
│   ├── Plugin_HMI.h            Stackable: HMI (0x41) — encoder + buttons + 2 LEDs — controllable
│   └── Plugin_FAN.h            Stackable: Fan Module v1.1 (0x18) — PWM fan + RPM — controllable
└── README.md                   This file
```

---

## Settings reference — by category &amp; capability

Every configurable area resolves to one **Mode** (a mutually-exclusive choice),
a set of independent **Options**, read-only **Status** the device reports, and a
**Source** — the file the setting lives in, which also tells you whether it is
live-editable or needs a rebuild:

| Source | Meaning |
|---|---|
| **NVS** | Runtime, stored in flash NVS — editable from the setup portal / Settings page, **no reflash** |
| **Config.h** | Compile-time — edit and **reflash** |
| **Secrets.h** | Compile-time secret (git-ignored) — edit and **reflash** |
| **.ino** | Plugin registration in the sketch — edit and **reflash** |
| **reported** | Read-only, surfaced by the device (`/api/config`, `/api/all`) |

> An interactive version of this table (click a mode to see the exact variables)
> lives in `Settings_Taxonomy.html` and in the bundled `index.html` docs.

### Categories

| Category | Mode (pick one) | Options | Status (read-only) | Source |
|---|---|---|---|---|
| **Network** | Station (join SSID) · Standalone AP · _Setup/recovery portal_ | AP SSID / pass | `wifi_mode`, IP, `ap_only` | NVS · Secrets |
| **Dashboard &amp; API** | HTTPS + redirect · Plain-HTTP AP | Basic Auth · Ports | auth user, ports | Secrets · Config |
| **MQTT telemetry** | Off · Plain (TCP) · TLS (user/pass) · Mutual TLS (AWS IoT) · TLS-insecure | HA discovery · Retain/LWT · Host/topic | connected, `transport`, `tls_verified` | NVS · Config · Secrets |
| **Local outputs** | _Display off_ · Scroll ticker · Fixed grid | Serial · SD log | SD mounted, log file | Config |
| **Security &amp; recovery** | Strict (halt) · Warn-only | Factory-reset hold · Random AP pass | default-cred warnings | Config · Secrets |

### AI assistant (router) — tier ladder &amp; classifier

The router is not a single mode but a **ladder of tiers** the classifier picks
between per turn, plus the classifier itself:

| Layer | Choice | Status | Source |
|---|---|---|---|
| **Tiers enabled** | Local LLM · Direct Claude API · Pi orchestrator | `route_taken` | .ino · Config · Secrets |
| **Classifier** | Keyword prefilter · LLM tiebreaker · Tiebreak trace | serial log | Config |
| **Fallback** | Escalate if local down | — | Config |

### Variables by selection

For each mode/option, the files and variables to change:

**Network**

- _Station (join SSID)_ — `Secrets.h`: `WIFI_SSID`, `WIFI_PASSWORD`; or set Wi-Fi from the setup portal at runtime (NVS), with `ap_only` = false.
- _Standalone AP_ — NVS: `ap_only` = true (the portal/Settings "standalone AP" checkbox); `Config.h`: `AP_SSID`; `Secrets.h`: `AP_PASSWORD` (8–63 chars, or leave default for a random per-device one).
- _Setup/recovery portal_ — automatic; entered when `isProvisioned()` is false or a Wi-Fi join fails, exits once credentials are saved.
- _AP identity_ — `Config.h`: `AP_SSID`; `Secrets.h`: `AP_PASSWORD`.

**Dashboard &amp; API**

- _HTTPS + redirect_ — `Config.h`: `WEB_HTTPS_PORT` (443), `WEB_HTTP_REDIRECT_PORT` (80); cert in `https_cert.h`.
- _Plain-HTTP AP_ — `Config.h`: `WEB_AP_PLAIN_HTTP` (default `false`). When `true` **and** the device is a provisioned standalone AP (`ap_only`), the full dashboard + REST API are served over plain HTTP on `WEB_HTTP_REDIRECT_PORT` (80) and the HTTPS server is not started — no self-signed-cert warning over the device's own WPA2 AP, and the TLS RAM stays free. No effect in station mode (always HTTPS + redirect) or setup mode (captive portal is already plain HTTP).
- _Basic Auth_ — `Secrets.h`: `WEB_AUTH_USER` (empty disables auth), `WEB_AUTH_PASS`; or edit the web login at runtime from the Settings page (NVS).
- _CORS origin_ — `Config.h`: `WEB_CORS_ORIGIN` (default `"*"`). Set to the one origin you serve the dashboard from (e.g. `"https://192.168.1.50"`) so the browser rejects cross-origin pages. Basic Auth is still the real access control; this is a lightweight extra and is not a CSRF token.
- _CSRF guard_ — `Config.h`: `WEB_CSRF_PROTECT` (default `true`). Makes `/api/<slug>/set` POST-only and requires the `X-Requested-With` header, blocking forged cross-origin control commands. The dashboard sends both automatically; set `false` if your HTTPS library can't surface custom headers (controls would 403) — then re-test.

**MQTT telemetry**

- _Off_ — `Config.h`: `OUT_MQTT` = false (drops PubSubClient + all MQTT code), or `MQTT_HOST` = "" (compiled in but inert).
- _Plain (TCP)_ — `Config.h`: `MQTT_TLS` = false, `MQTT_PORT` = 1883, `MQTT_HOST`; `Secrets.h`: `MQTT_USER` / `MQTT_PASS` (optional); host/port/user/pass also runtime-editable (NVS).
- _TLS (user/pass)_ — `Config.h`: `MQTT_TLS` = true, `MQTT_TLS_MUTUAL` = false, `MQTT_TLS_INSECURE` = false, `MQTT_PORT` = 8883, `MQTT_CA_CERT`; `Secrets.h`: `MQTT_USER` / `MQTT_PASS`.
- _Mutual TLS (AWS IoT)_ — `Config.h`: `MQTT_TLS` = true, `MQTT_TLS_MUTUAL` = true, `MQTT_PORT` = 8883, `MQTT_HOST` (AWS data endpoint), `MQTT_CLIENT_ID` (Thing name); `Secrets.h`: `MQTT_CLIENT_CERT`, `MQTT_CLIENT_KEY`.
- _TLS-insecure_ — `Config.h`: `MQTT_TLS` = true, `MQTT_TLS_INSECURE` = true (skips `MQTT_CA_CERT` check), `MQTT_PORT` = 8883.
- _HA discovery_ — `Config.h`: `MQTT_HA_DISCOVERY` = true, `MQTT_HA_PREFIX`, `MQTT_DEVICE_NAME`.
- _Retain / LWT_ — `Config.h`: `MQTT_RETAIN`, `MQTT_KEEPALIVE`, `MQTT_PUBLISH_MS`.
- _Host / topic_ — `Config.h`: `MQTT_HOST`, `MQTT_PORT`, `MQTT_BASE_TOPIC`, `MQTT_CLIENT_ID`; host/port runtime-editable (NVS).

**Local outputs**

- _Display off / Scroll / Grid_ — `Config.h`: `OUT_DISPLAY`, `DISPLAY_SCROLL` (true = scroll, false = grid), `DISPLAY_CYCLE_MS`, `DISPLAY_SCROLL_PX`.
- _Serial_ — `Config.h`: `OUT_SERIAL` (boot log is always on regardless).
- _SD log_ — `Config.h`: `OUT_SD_LOG`.

**Security &amp; recovery**

- _Strict / Warn-only_ — `Config.h`: `SECURITY_STRICT` (true = halt on default creds, false = warn).
- _Factory-reset hold_ — `Config.h`: `FACTORY_RESET_HOLD_DISABLED` (false = enabled), `FACTORY_RESET_WINDOW_MS`.
- _Watchdog_ — `Config.h`: `WDT_ENABLE` (default false), `WDT_TIMEOUT_S` (default 20). When enabled, registers the loop task with the ESP32 Task Watchdog after boot and feeds it each loop; a frozen `loop()` longer than the timeout reboots the device. Keep the timeout above the longest legitimate loop pass (the ~8 s alert POST is the only in-loop blocker).
- _Random AP pass_ — `Secrets.h`: `AP_PASSWORD` (set a real value to override the auto-minted random one). A minted random password is shown on the LCD/serial only on the boot that first generates it; `Config.h`: `AP_SHOW_PASSWORD_EACH_BOOT` (default `false`) shows it on every boot instead.

**AI assistant (router)**

- _Local LLM tier_ — `M5Stack_I2C_Framework.ino`: register `new UartDevice_ModuleLLM(Serial2)` and pass it as the router's 1st arg; tunables `SYSTEM_PROMPT` / `MAX_TOKENS` in `plugins/UartDevice_ModuleLLM.h`.
- _Direct Claude API tier_ — `Config.h`: `ROUTER_DIRECT_API` = true, `CLAUDE_MODEL`, `CLAUDE_MAX_TOKENS`; `Secrets.h`: `CLAUDE_API_KEY` (⚠ billable key in flash); `.ino`: register `new NetDevice_ClaudeAPI()` as the router's 2nd arg.
- _Pi orchestrator tier_ — `Config.h`: `ROUTER_PI_HOST` (set to enable), `ROUTER_PI_PORT`, `ROUTER_PI_PATH`, `ROUTER_TLS_INSECURE`; `Secrets.h`: `ROUTER_BEARER` (optional).
- _Keyword prefilter_ — `Config.h`: `ROUTER_ESCALATE_KEYWORDS`, `ROUTER_DIRECT_KEYWORDS`, `ROUTER_DIRECT_MIN_WORDS`.
- _LLM tiebreaker_ — `Config.h`: `ROUTER_LLM_TIEBREAK` = true, `ROUTER_TIEBREAK_PROMPT` (requires a local LLM and an escalation target, else skipped).
- _Tiebreak trace_ — `Config.h`: `ROUTER_TIEBREAK_TRACE` = true (needs `ROUTER_LLM_TIEBREAK`).
- _Escalate if local down_ — `Config.h`: `ROUTER_FALLBACK_ESCALATE` = true (needs `ROUTER_PI_HOST` set).

---

## Project history

The original CoreS3 → multi-board port notes and the incremental
"Added since" feature log have moved to [`CHANGELOG.md`](CHANGELOG.md)
(see *Historical (pre-audit)*). Recent maintenance changes are tracked
there as well; the code audit itself is in [`CODE_AUDIT.md`](CODE_AUDIT.md).

## License
MIT – free for personal and commercial use.
