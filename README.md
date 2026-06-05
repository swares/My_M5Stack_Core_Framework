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
debounce, hysteresis, cooldown) — or over REST:

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
| GET | `/api/{slug}/set`    | Control an output device — see [Controlling output devices](#controlling-output-devices) |
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

### Controlling output devices

Some plugins drive hardware that can be *commanded* — relays, servos,
LEDs.  Those are marked `"controllable": true` in `/api/all` and carry a
`CTRL` badge on the dashboard.  They're driven through one endpoint:

```
GET /api/<slug>/set?<param>=<value>[&<param>=<value> ...]
```

Examples:

```
/api/4relay/set?relay1=1&relay2=0     relay 1 on, relay 2 off
/api/4relay/set?relays=10             all four relays from a 0-15 bitmap
/api/servo2/set?ch0=1500&deg3=90      channel 0 → 1500 µs, channel 3 → 90°
/api/8angle/set?led0=FF0000           8Angle user LED 0 → red
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
│   ├── Config.h                User-editable settings
│   ├── BoardInfo.h/.cpp        Runtime board detection (CoreS3 / Core2 / Tough / Core1)
│   ├── IDevice.h               Abstract device interface
│   ├── Framework.h/.cpp        Core orchestrator + I2C hub support
│   ├── DisplayManager.h/.cpp   LCD output (templates, scroll, grid)
│   ├── WebAPI.h/.cpp           HTTPS server + HTTP→HTTPS redirect + dashboard
│   ├── MQTTOut.h/.cpp          MQTT publisher + optional HA discovery
│   ├── SDLogger.h/.cpp         microSD CSV logger (one file per boot)
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
- _Random AP pass_ — `Secrets.h`: `AP_PASSWORD` (set a real value to override the auto-minted random one).

**AI assistant (router)**

- _Local LLM tier_ — `M5Stack_I2C_Framework.ino`: register `new UartDevice_ModuleLLM(Serial2)` and pass it as the router's 1st arg; tunables `SYSTEM_PROMPT` / `MAX_TOKENS` in `plugins/UartDevice_ModuleLLM.h`.
- _Direct Claude API tier_ — `Config.h`: `ROUTER_DIRECT_API` = true, `CLAUDE_MODEL`, `CLAUDE_MAX_TOKENS`; `Secrets.h`: `CLAUDE_API_KEY` (⚠ billable key in flash); `.ino`: register `new NetDevice_ClaudeAPI()` as the router's 2nd arg.
- _Pi orchestrator tier_ — `Config.h`: `ROUTER_PI_HOST` (set to enable), `ROUTER_PI_PORT`, `ROUTER_PI_PATH`, `ROUTER_TLS_INSECURE`; `Secrets.h`: `ROUTER_BEARER` (optional).
- _Keyword prefilter_ — `Config.h`: `ROUTER_ESCALATE_KEYWORDS`, `ROUTER_DIRECT_KEYWORDS`, `ROUTER_DIRECT_MIN_WORDS`.
- _LLM tiebreaker_ — `Config.h`: `ROUTER_LLM_TIEBREAK` = true, `ROUTER_TIEBREAK_PROMPT` (requires a local LLM and an escalation target, else skipped).
- _Tiebreak trace_ — `Config.h`: `ROUTER_TIEBREAK_TRACE` = true (needs `ROUTER_LLM_TIEBREAK`).
- _Escalate if local down_ — `Config.h`: `ROUTER_FALLBACK_ESCALATE` = true (needs `ROUTER_PI_HOST` set).

---

## What changed from the CoreS3-only version

1. **New `BoardInfo` module** detects the host board via `M5.getBoard()`
   and exposes per-board I2C pin maps, bus topology (separate vs shared),
   and chip identity strings.
2. **`Framework::_initBuses()`** now reads pins from `BoardInfo` instead
   of `#define`s in `Config.h`. The old `I2C_INT_*` / `I2C_EXT_*` macros
   were removed; optional `*_OVERRIDE` macros are available for edge cases.
3. **`Plugin_AXP2101.h` → `Plugin_PMIC.h`** — slug stays `pmic`, so API
   URLs are unchanged. Driven through `M5.Power` which abstracts both
   AXP2101 and AXP192.
4. **`Plugin_IMU.h`** updated — driven through `M5.Imu`, which handles
   BMI270+BMM150 on CoreS3 and MPU6886 on Core2. Magnetometer readings
   are emitted only when the board provides one.
5. **Dashboard + JSON API** now report the detected board name and
   resolved pin map. The HTML title is filled in by JavaScript so the
   same binary serves the right page on either board.

## Added since the initial Core2 port

1. **Core1 (Basic / Gray) support** — `BoardInfo` recognises
   `board_M5Stack`, aliases `intBus`/`extBus` to the same `TwoWire*`
   because Port-A and the on-board chips share SDA21/SCL22, and
   default `I2C_INT_FREQ` is 100 kHz so SMBus-only Port-A units like
   MLX90614 work on the shared bus. Dashboard/serial bus labels now
   say `shared` on Core1 and `internal`/`external` on the others.
2. **`Plugin_IP5306`** — Core1 has no I2C PMIC; this plugin reports
   battery % and charging state from the IP5306 fuel gauge at 0x75.
3. **`Plugin_ENV4`** — SHT40 + BMP280 (the ENV IV unit). Registers
   before `Plugin_ENV3` since both share 0x44; the BMP280 chip-ID
   check (`0xD0 == 0x58`) lets the right plugin win.
4. **`Plugin_HEART_MAX30102`** — strict variant for the newer MAX30102
   (PART_ID 0x15). Coexists with `Plugin_HEART` (MAX30100, PART_ID 0x11)
   at the same 0x57 address.
5. **I2C hub support** — `fw.addMux(0x70)` (or any PCA9548A address);
   the framework probes each channel at boot, binds plugins to
   `(hub, channel, addr)` tuples, and transparently selects the right
   channel before every transaction. Multiple plugins of the same type
   bind to different channels in registration order.
6. **`(bus, mux, channel, addr)` claim check** — once a plugin claims a
   physical chip, later plugins sharing that address are skipped with a
   `skipped — claimed` log line instead of corrupting each other's
   transactions.
7. **No periodic rescan** — I2C isn't designed for hot-plugging, and
   the old rescan was the largest source of bus instability. Plugins
   bind once at boot. Manual re-bind via `GET /api/rescan` if needed.
8. **`MQTTOut` module** — optional publish-only output to any MQTT
   broker. Publishes one retained JSON document per active plugin to
   `<base>/<slug>` and tracks device liveness via LWT on `<base>/status`.
   Optional Home Assistant MQTT Discovery auto-creates one entity per
   reading under a single device card, identified by the ESP32's MAC.
   The transport is selectable via `MQTT_TLS`: plain MQTT for a local
   broker, or MQTTS/TLS — username/password or mutual X.509 (see below).
9. **Optional Web API Basic Auth** — set `WEB_AUTH_USER`/`WEB_AUTH_PASS`
   in `Config.h` to gate every route behind HTTP Basic Auth.
10. **SD card CSV logging** — `SDLogger` mounts the microSD at boot
    (per-board SPI pins live in `BoardInfo`), increments a boot
    counter in NVS, opens `/log_NNNN.csv`, writes a dynamic header
    derived from every active plugin's `getReadings()`, and appends
    one row every `SD_LOG_INTERVAL_MS` (default 5 s). Flushes every
    `SD_LOG_FLUSH_EVERY_N` rows for crash safety. Live status at
    `GET /api/sdcard`; manual flush at `GET /api/sdcard/flush`.
11. **Live heart rate** — `Plugin_HEART` / `Plugin_HEART_MAX30102`
    compute real BPM + SpO2 via the shared `PpgBeatDetector` DSP,
    fed by a per-loop `fastPoll()` hook that drains the sensor FIFO
    faster than the 500 ms poll cycle.
12. **Stackable modules + output control** — `IDevice` gained a
    `mount` classifier (builtin / stackable / pluggable, surfaced in
    the API and dashboard) and a validated `command()` interface.
    Five M-Bus modules are now supported (`Plugin_4RELAY`,
    `Plugin_SERVO2`, `Plugin_8ANGLE`, `Plugin_FACES2`,
    `Plugin_FACES2_ENCODER`).  Output modules are driven through
    `GET /api/{slug}/set`, with every parameter range-checked by the
    plugin.  The dashboard now hides inactive (unbound) cards.
13. **Three more M-Bus modules** — `Plugin_GOPLUS2` (0x38 — 2× DC
    motor, 2× servo, RGB LED, 2× encoder), `Plugin_STEPMOTOR` (0x27 —
    Step Motor Module v1.1) and `Plugin_4IN8OUT` (0x45 — 4 digital
    inputs, 8 high-side outputs).  All controllable through
    `GET /api/{slug}/set` with full input validation.  `Plugin_GOPLUS2`
    is an STM32-based module whose `REG_*` map follows M5Stack
    conventions but is flagged `⚠ NOT YET HARDWARE-VERIFIED` — only
    the named register constants need correcting if it doesn't
    respond.  (`Plugin_4IN8OUT` was here too but is now verified —
    see item 20.)
14. **Servo2 corrected + Step Motor protocol verified** —
    `Plugin_SERVO2` now drives all 16 PCA9685 channels (it was
    capped at 8; the M5Stack docs confirm the module is 16-channel).
    `Plugin_STEPMOTOR` was rewritten against M5Stack's "Step Motor
    Module V1.1 I2C Protocol V3" document: its 0x27 register map is
    now verified — limit/fault monitoring plus `enable`/`reset`
    control — and the previously guessed `dir`/`speed`/`move` I2C
    registers were removed, since motion is host-GPIO, not I2C.
15. **MQTTS (TLS) transport** — `MQTTOut` connects over
    `WiFiClientSecure` when `MQTT_TLS` is true.  Two further switches
    shape the handshake: `MQTT_TLS_MUTUAL` chooses username/password
    over TLS (cloud brokers like HiveMQ Cloud, or Mosquitto+TLS)
    versus X.509 client-certificate auth (AWS IoT Core), and
    `MQTT_TLS_INSECURE` chooses broker verification against
    `MQTT_CA_CERT` versus an encrypt-only `setInsecure` connection.
    Plain-TCP local-broker behaviour is unchanged when false.  HA
    discovery is honoured over MQTTS too, suppressed only for a
    mutual-TLS broker; TLS handshake errors are logged, and
    `/api/mqtt` reports `transport` / `tls` / `tls_mutual` /
    `tls_verified`.
16. **Non-I2C pin device layer** — a new `IPinDevice` base (deriving
    `IDevice`) lets GPIO / PWM / ADC units run in the framework
    alongside the I2C plugins, appearing in every output channel
    without special-casing.  The boot scan skips them — they have no
    address — and the framework activates them via `beginPins()`.
    Nine devices ship: GPIO/PWM/ADC units `PinDevice_PIR`,
    `PinDevice_Relay`, `PinDevice_Buzzer`, `PinDevice_Servo`,
    `PinDevice_Light` (CdS), `PinDevice_Earth` (soil) and
    `PinDevice_Mic` need no libraries; `PinDevice_DS18B20` (1-Wire)
    and `PinDevice_IR` need OneWire+DallasTemperature and
    IRremoteESP8266 respectively, so their `.ino` includes are
    commented out by default.  Pin devices are declared with their
    pin numbers in `setup()` — there is no auto-detection.
17. **UART device layer** — `IUartDevice` (extending `IPinDevice`,
    so no further framework changes) is the base for serial units.
    `UartDevice_Barcode` and `UartDevice_Modem` (no library) and
    `UartDevice_GPS` (NMEA via TinyGPSPlus) ship.  Only one UART
    device can be active — they share the single Port-C serial port.
    The cellular CatM/4G units are covered by the standards-based
    `UartDevice_Modem` monitor; RS-485/232 are transports rather than
    sensor devices, and the fingerprint / LoRa units need their own
    protocol drivers (see the UART section above).
18. **Tough board + board-aware Port-C** — `BoardInfo` now recognises
    the M5Stack Tough (`board_M5Tough`, Core2-family pin layout) as a
    named board instead of falling through to the "unknown board"
    warning, and resolves every board's Port-C UART pins.  `IUartDevice`
    uses them, so `UartDevice_*` constructors need no hard-coded GPIOs
    — `(Serial2, baud)` is enough on Core1 / Core2 / Tough / CoreS3.
19. **`IPlugin` renamed to `IDevice`** — the device base class is now
    `IDevice` (file `src/IDevice.h`).  A pure rename across every
    device file, no behaviour change; `IPinDevice` / `IUartDevice`
    derive from it directly.
20. **4In8Out register map verified** — `Plugin_4IN8OUT` was checked
    against M5Stack's M5Module-4IN8OUT library.  Its register
    addresses were wrong (input base `0x00`→`0x10`, output base
    `0x10`→`0x20`) and so was the access model — it packed a bitmap,
    but the module is byte-per-channel (inputs `0x10`-`0x13`, outputs
    `0x20`-`0x27`).  Both are now corrected and the plugin is verified.
21. **Interactive dashboard + control schema** — `IDevice` gained a
    `controlSchema()` hook; every controllable device (the six M-Bus
    modules — 4Relay, Servo2, 8Angle, GoPlus2, 4In8Out, Stepmotor —
    plus the Buzzer / Relay / Servo / IR pin devices) describes its
    controls, which the Web API emits as a `controls` array on
    `/api/all` and `/api/{slug}`.  The embedded dashboard was rebuilt
    around it: a **Controllable Devices** section renders a live
    widget per control (toggle / slider / colour / text / button),
    separate from the read-only sensor cards, and an **API Request
    Log** panel records every `/set` call and its HTTP status.  Still
    a single self-contained vanilla-JS page — no external scripts, no
    internet needed.
22. **Module13.2 2Relay support** — `Plugin_2RELAY` drives the
    dual-channel AC/DC relay module (STM32F030F4P6, I2C `0x25`),
    implemented against the verified `M5Module-2Relay-13.2` register
    map: relays 1 and 2 are byte-per-channel registers `0x00` / `0x01`
    (`0xFF` on, `0x00` off).  Controllable via
    `GET /api/2relay/set?relay1=1` (or `relays=` as a 0-3 bitmap), and
    it ships a `controlSchema()` so the interactive dashboard renders
    its toggles automatically.  Unlike the 4-Relay it has no mode
    register or status LEDs; at `0x25` it does not clash with the
    4-Relay or Weight units (both `0x26`).  The plugin is write-only
    — plain STOP-terminated register writes, no repeated-start reads
    — because the module's STM32F030 firmware is unreliable with the
    repeated-start I2C framing; relay state is tracked in a mirror.
23. **Module13.2 AIN4-20mA support** — `Plugin_AIN420MA` reads the
    four-channel 4-20 mA current-loop acquisition module
    (STM32G030F6, I2C `0x55`), implemented against the verified
    `M5Module-4-20mA` library: per channel a 12-bit ADC count at
    `0x00`+2·ch and a calibrated current value at `0x20`+2·ch, both
    little-endian uint16.  It is a read-only sensor — no controls —
    so it shows up under the dashboard's Read-only Sensors section
    reporting `chN_mA` and `chN_adc` for all four channels.  Reads
    use the standard repeated-start `regRead`: M5Stack's library
    reads the same way and this module's STM32G030 handles it (the
    2-Relay's STM32F030 did not, hence that plugin's write-only
    treatment).  The current register's milliamp scaling
    (`CURRENT_PER_MA`) is a documented assumption to verify against
    a known 4-20 mA source.
24. **Module13.2 PPS support** — `Plugin_PPS` drives the programmable
    power-supply module (STM32G030F6, I2C `0x35`, 0.5-30 V @ 0-5 A,
    100 W), implemented against the verified `M5Module-PPS` library:
    1-byte ENABLE / MODE registers plus 4-byte little-endian float
    registers for the measured V / I / Vin / temperature readbacks
    and the voltage / current setpoints.  It is controllable —
    `controlSchema()` gives the dashboard an output-enable toggle and
    voltage / current-limit sliders — and `begin()` forces the output
    **disabled** as a safe power-on state.  Reads use the standard
    repeated-start `regRead` (the library reads the same way); float
    setpoint writes are plain STOP transactions.  Driven via
    `GET /api/pps/set?enable=1&vset=12.0&iset=1.5`, every value
    range-checked.  The module needs its external DC 9-36 V input
    connected or it will not appear on I2C at all.
25. **Module HMI support** — `Plugin_HMI` reads the HMI module
    (STM32F030F4P6, I2C `0x41`) — a rotary encoder, encoder
    push-button and two input buttons — against the verified
    `M5Module-HMI` register map: a 4-byte little-endian int32 at
    `0x00` (absolute encoder count) and `0x10` (increment), plus
    1-byte button registers at `0x20` / `0x21` / `0x22`.  It is a
    read-only sensor — no controls — reporting `encoder`,
    `increment`, `buttonS`, `button1` and `button2` under the
    dashboard's Read-only Sensors section.  The module's two LEDs
    and reset-counter command are writes and are deliberately not
    exposed.  Reads use the standard repeated-start `regRead`.
26. **Port-B unit support** — six new `IPinDevice` units for the
    single Port-B Grove connector (`GPIO26` Yellow + `GPIO36` White
    on an M5Stack Core).  Five are read-only sensors: `PinDevice_Hall`
    (A3144E magnetic switch, low-active), `PinDevice_Limit` (mechanical
    travel switch), `PinDevice_OP180` (ITR9606 IR break-beam),
    `PinDevice_DualButton` (two active-low push-buttons, using both
    Port-B lines) and `PinDevice_TubePressure` (MCP-H10-B200KPPN gas
    gauge — analog `0.1-3.1 V` mapped to `-100..200 kPa` via
    `P = 100·V − 110`).  The sixth, `PinDevice_Grove2Grove`, is
    controllable: a `PWR_EN` digital output gates 5 V to a downstream
    Grove port and an ADC line measures its current draw (0-1000 mA).
    `beginPins()` forces the output **off** and captures the
    zero-current baseline voltage; `controlSchema()` gives the
    dashboard a power toggle.  Constructor pins default to the Port-B
    pair, so each registers with no arguments.  Port B is one physical
    connector, so the `.ino` lists all six commented — uncomment
    exactly one.  OP180's beam polarity is a documented assumption to
    verify on hardware.
27. **Control-endpoint routing fix** — `/api/<slug>/set?param=value`
    was unreachable on the HTTPS server: `ESPWebServerSecure::uri()`
    returns the full request target *including* the query string
    (unlike the standard Arduino `WebServer`, whose `uri()` is
    path-only), so the dispatcher's `endsWith("/set")` test failed
    whenever a query was present and every control request fell
    through to the read handler ("plugin not found"). A new
    `_reqPath()` helper strips the `?query` before the path is
    inspected. This had silently broken **all** controllable
    devices' `/set` endpoints — relays, servos, the PPS, the
    Grove2Grove, the dashboard's interactive widgets — since the
    HTTPS migration; plain `/api/<slug>` reads were unaffected.
28. **Module LLM support** — `UartDevice_ModuleLLM` integrates the
    M5Stack Module LLM (offline AI, AX630C SoC) as a text-chat device
    via the official `M5Module-LLM` library and its StackFlow
    JSON-over-UART protocol on Port-C.  It is asynchronous:
    `command("ask", <prompt>)` calls the non-blocking `llm.inference()`
    and returns at once, while `fastPoll()` pumps the UART and
    accumulates the streamed reply into `answer` — so sensor polling
    and the web server keep running during inference.  `toJson()`
    exposes `connected`/`busy`/`done`/`prompt`/`answer`; the dashboard
    gains an **LLM Chat panel** (shown only when the module is bound)
    that posts prompts and polls `/api/llm` to render the reply as it
    streams.  Model loading blocks once at boot.  Shares the single
    Port-C UART slot with the GPS/Barcode/Modem devices; needs the
    M5Module-LLM library so the `.ino` `#include` is commented by
    default.  Voice units (ASR/TTS/wake word) are not used.
29. **Board reserved-address guard** — fixed phantom devices on the
    Core2: `Plugin_GoPlus2` (`0x38`) and `Plugin_SERVO2` (`0x40`)
    were binding to the Core2's *built-in* chips — the FT6336U touch
    controller at `0x38` and (on the Core2 v1.1) the INA3221 current
    monitor at `0x40` — because detection was a bare I2C address
    ACK.  `BoardInfo` now carries a per-board `reservedAddr[]` list
    of internal-bus addresses owned by built-in silicon that has no
    plugin, and `_scanAndBind()` strips those from the internal bind
    list before offering addresses to plugins (they still appear in
    the raw `[Scan] Addrs` dump).  Core2/Tough reserve `0x38`
    (+`0x40` on Core2); CoreS3 reserves `0x38`; the Core1 has no
    touch so reserves nothing.  Built-in chips that *do* have a
    plugin (PMIC/RTC/IMU) and external Port-A units are unaffected.
30. **NTP → hardware RTC sync** — after a successful NTP sync at
    boot, `_syncTime()` now writes the wall-clock into the board's
    BM8563 hardware RTC via `M5.Rtc.setDateTime()`.  Previously the
    BM8563 kept whatever it was last set to, so `Plugin_RTC` and the
    dashboard could show a stale clock; now the hardware RTC tracks
    real time and gives the board a sensible fallback if a later
    boot can't reach NTP.  Guarded by `M5.Rtc.isEnabled()`, so the
    Core1 (no RTC chip) skips it cleanly.
31. **INA3221 power-monitor plugin** — `Plugin_INA3221` surfaces the
    3-channel INA3221 built into the **Core2 v1.1** (the AXP2101 +
    INA3221 power solution) at `0x40` — previously detected but
    unused.  `begin()` positively identifies the chip via its
    die-ID register (`0xFF` == `0x3220`), so unlike the bare-ACK
    detection that caused the earlier phantom Servo2, it cannot
    false-bind; registered before `Plugin_SERVO2`, it claims `0x40`
    on a Core2 v1.1 while a genuine Servo2 elsewhere still binds.
    It reports each channel's bus voltage (8 mV LSB) and shunt
    voltage (40 µV LSB) — both absolute, no calibration needed —
    plus the **battery current** from `M5.Power.getBatteryCurrent()`,
    which M5Stack already calibrates against this INA3221.  Because
    a real driver now owns `0x40`, it is no longer in the board
    reserved-address list (item 29); only the `0x38` touch
    controller remains reserved.
32. **Gyro zero-rate calibration** — `Plugin_IMU` now measures the
    gyro's zero-rate bias at boot: `begin()` samples the gyro ~100×
    over ~1 s while the board is still, averages each axis, and
    `update()` subtracts that bias from every reading.  Without it
    the MPU6886 / BMI270 gyros report a temperature- and
    unit-dependent offset — tens of °/s on a motionless board.  A
    spread check skips the calibration (and warns) if the board is
    disturbed during the sample window, leaving the bias at zero
    rather than baking in a bad value.  The measured bias is
    published in the `/api/imu` JSON as `gyro_bias_x/y/z`.
33. **CoreS3 I2C bus-assignment fix** — on the CoreS3 every built-in
    chip vanished from the scan (`Internal: 0`, a false "bus HELD
    LOW" warning) while the chips turned up on the *external* scan.
    Cause: on the ESP32-S3, M5Unified runs the CoreS3's internal
    system I2C on the second I2C peripheral (Arduino `Wire1`), but
    `BoardInfo` had the CoreS3 with `intBus = &Wire` — so the
    framework drove `Wire` onto the internal pins `11/12`, colliding
    with M5Unified's `Wire1` already there and killing the bus.
    `BoardInfo` now gives the CoreS3 `intBus = &Wire1` / `extBus =
    &Wire`, matching the peripheral M5Unified uses; the pin values
    were already correct.  Core1 / Core2 are classic-ESP32 and use
    `Wire` for their internal bus, so they were never affected.
34. **CoreS3 reserved-address list** — with the CoreS3's internal
    bus now scanning correctly (item 33), its built-in chips became
    visible — and, like the Core2's touch controller, the ones with
    no plugin were being false-bound by module plugins (`Servo2`
    grabbed the ES7210 mic ADC at `0x40`, `Light` the LTR-553 at
    `0x23`, `TVOC` the AW9523 at `0x58`, `Angle` the AW88298 at
    `0x36`).  The CoreS3 `reservedAddr[]` list is now the full set
    of built-in chips that lack a plugin — `0x21` (GC0308 camera),
    `0x23` (LTR-553), `0x36` (AW88298), `0x38` (FT6x36 touch),
    `0x40` (ES7210), `0x58` (AW9523) — leaving only `0x34`/`0x51`/
    `0x69` (PMIC / RTC / IMU, which have plugins) to bind.  The
    `BoardInfo` reserved array was widened from 6 to 8 slots.
35. **Module LLM error diagnostics** — a web query
    (`/api/llm/set?ask=...`) that the module rejected used to fail
    silently: `_ingest()` saw the module's non-zero error code, set
    `_busy=false`, and discarded the reason, so the dashboard chat
    panel only ever showed `[no reply]`.  `UartDevice_ModuleLLM` now
    surfaces every failure mode — `command("ask")` logs each reject
    reason and the `inference()` return code; `_ingest()` prints the
    module's error `code` + `message` and copies them into `answer`
    (so the dashboard shows `[module error N: ...]`); reply
    completion and reply timeout are logged; and a `TRACE_RX` tunable
    (default on) echoes every StackFlow message addressed to the LLM
    unit to Serial.  No behaviour change to a working query — this
    only makes a failing one visible.
36. **Module LLM `sys.reset()` before setup (fixes error -4)** — the
    `TRACE_RX` diagnostics from item 35 caught the real fault: every
    query came back `error -4 "inference data push false"`.  Cause:
    the Module LLM (AX630C) runs its own OS and keeps running while
    the host ESP32 reboots.  Each `llm.setup()` allocates a StackFlow
    unit and loads the model into the module's limited RAM, and an
    ESP32 reboot does **not** free it.  After several host reboots
    the module's memory is full, `setup()` quietly fails, and instead
    of an allocated instance id (`llm.1003`) it returns the bare unit
    *type* `llm` — so every later inference is addressed to a unit
    that does not exist and is rejected with error -4.  `beginUart()`
    now calls `_llm.sys.reset()` before `llm.setup()` — restarting
    the module's StackFlow service and freeing every stale unit, the
    same thing the official M5Module-LLM examples do.  It also
    validates the work_id `setup()` returns: a value with no `.NNNN`
    instance suffix is treated as a failed setup (logged, device
    left inactive) rather than a false "ready".
37. **Module LLM setup-failure watch** — `sys.reset()` (item 36) did
    not cure the failed setup: `setup()` still returns the bare
    `llm`, so the cause is not stale units but the model itself not
    loading.  The library's `setup()` discards the module's response,
    so `beginUart()` now does its own watch: after a `setup()` that
    returns a non-instance work_id it pumps the module for up to 20 s
    and prints every StackFlow message verbatim as `[LLM] setup-rx:`
    lines.  If a late setup-success arrives (model load simply outran
    the library's internal timeout) its allocated work_id is adopted
    and the LLM comes up normally; if the module reports an error
    code the watch stops early and prints it.  Either way the real
    reason the model will not load is now on the serial console.
38. **Module LLM UART RX buffer + quieter trace** — with the LLM
    confirmed working (a query streams a full reply), two finishing
    touches.  (a) The ESP32's default 256-byte UART RX buffer could
    overflow between `fastPoll()` calls when the main loop was busy
    (an HTTPS handshake, an SD write), splitting a streamed
    StackFlow message and dropping the odd reply token.
    `IUartDevice` gained a `rxBufferSize()` hook — applied before
    `begin()`, since `setRxBufferSize()` is a no-op afterwards — and
    `UartDevice_ModuleLLM` overrides it to 4 KB, which absorbs the
    token bursts.  Other UART devices (GPS, barcode, modem) keep the
    256-byte default.  (b) `TRACE_RX` now defaults to `false`: the
    per-token `[LLM] rx[...]` logging stays available for debugging
    but no longer floods the console in normal use.
39. **Module LLM inactivity timeout** — a longer query (a code
    snippet) was killed mid-stream after exactly 30 s.  `REPLY_TIMEOUT_MS`
    was a *total-duration* cap, but the on-device qwen2.5 model
    streams only ~3-4 tokens/s, so a long reply legitimately runs for
    minutes — the earlier short answers finished right at the 30 s
    edge by luck.  The timer is now an **inactivity** timeout
    (`REPLY_IDLE_MS`): `_lastRxMs` is refreshed on every message the
    LLM unit sends, and the reply is abandoned only after 30 s with
    *no token at all* — a genuine stall.  A steadily-streaming answer
    of any length is left alone.  The timeout log line now also
    reports how many characters were received, so a real stall is
    distinguishable from a never-started reply without re-enabling
    the per-token trace.
40. **UART probe log wording** — the `[UART] <name> Port-C RX=.. TX=..`
    line prints the instant `IUartDevice::beginPins()` *opens* the
    serial port — before `beginUart()` has probed whether anything is
    actually on the other end — so it appears identically whether the
    device is plugged in or not, and reads like a successful
    detection.  It now says `probing Port-C` to make clear it is a
    probe-in-progress; the genuine verdict remains the later
    `[Pin] <name> ready` / `[Pin] <name> beginPins() FAILED` line (and,
    for the Module LLM, `[LLM] no response …` on a failed probe).  A
    UART device with no detected module is still left inactive and
    kept out of the readings — the only change is the log wording.
41. **Four new device drivers** — PM2.5 air quality, ECG, fan
    control and offline voice, all picked because they map cleanly
    onto the existing device shapes:
    - `UartDevice_PMSA003` — the PM2.5 Air Quality Module (Plantower
      PMSA003).  A pure passive UART parser: it assembles the fixed
      32-byte measurement frame, checksum-validates it, and reports
      `pm1_0` / `pm2_5` / `pm10` (µg/m³).  No library, no commands.
    - `PinDevice_ECG` — the ECG Module (AD8232).  A pin device that,
      like `PinDevice_Mic`, fast-samples the analog ECG output and
      reports the peak-to-peak `signal` over a ~1 s window plus
      `leads_off` from the AD8232's LO+/LO- electrode-detect pins.
      Constructor takes the ADC pin and the two (optional) LO pins.
    - `UartDevice_ASR` — the ASR Module (CI1302) offline voice
      recogniser.  A passive UART listener that parses the module's
      fixed 5-byte `AA 55 <ID> 55 AA` command frames and reports
      `last_cmd` / `cmd_count`.  No library.
    - `Plugin_FAN` — the Fan Module v1.1 (STM32F030) at I2C `0x18`.
      A controllable I2C plugin: `power` (on/off) and `speed`
      (0-100 % PWM duty) via the Web API, with `rpm` read back.  It
      drives the module through the official **M5Module-Fan**
      library (same opt-in pattern as the GPS / Module-LLM UART
      devices), so its `.ino` `#include` is commented out by default.

    All four are registered commented-out in the `.ino` — uncomment
    the one(s) you have wired.  Two more list items needed *no* new
    code: the COM.LTE (SIM7600G) and COM.NB-IoT (SIM7020G) modules
    are AT-command modems already covered by `UartDevice_Modem`.
42. **Five add-on Port-A sensor units** — drivers for standalone-chip
    Units with clean, canonical register maps:
    - `Plugin_MPU6886` — 6-Axis IMU Unit (MPU6886) at `0x68`.  The
      *standalone* IMU unit, distinct from the on-board IMU that
      `Plugin_IMU` reads via `M5.Imu`; bound on the external bus
      only and gated by the `0x19` WHO_AM_I.  Accel ±8 g, gyro
      ±2000 °/s, plus die temperature.
    - `Plugin_SCD4X` — CO2 Unit (SCD40) and CO2L Unit (SCD41) at
      `0x62`.  Sensirion command protocol with CRC-8 validation;
      reports CO2 ppm, temperature and humidity (periodic mode, a
      fresh sample every ~5 s).
    - `Plugin_INA226` — INA226 power-monitor units at `0x40`, gated
      by a positive die-ID check (`0x2260`) so it never mis-binds
      to an INA3221.  Reports bus voltage, current and power; the
      shunt resistance is a tunable (`R_SHUNT`) since the INA226-1A
      and INA226-10A units differ.
    - `Plugin_ADS1110` / `Plugin_ADS1115` — the ADC Unit and the
      Ammeter/Voltmeter Units.  Both chips sit at `0x48` with no ID
      register, so they are registered commented-out in the `.ino`
      — uncomment the one you have.
    `Plugin_MPU6886`, `Plugin_SCD4X` and `Plugin_INA226` auto-detect
    like the other Port-A units.  (The Barometric Pressure 2 Unit's
    QMP6988 still needs its full compensation polynomial and is held
    for a later change rather than shipped as an approximation.)
43. **Three new pin devices** — completing the easy non-I2C units,
    no libraries needed:
    - `PinDevice_Button` — a momentary push-button (Mechanical Key /
      Mini Button) on a pull-up input; reports `pressed` plus a
      `press_count` edge counter sampled in `fastPoll()`.
    - `PinDevice_Motor` — a brushed DC motor on an LEDC PWM pin
      (Vibration Motor, Mini Fan); controllable `speed` 0-100 %.
    - `PinDevice_Watering` — the Watering Unit: a soil-moisture ADC
      input plus a controllable `pump` GPIO output.
    The plain on/off-output units (Flashlight, the Solid-State Relay
    units, a laser emitter) need no new driver — `PinDevice_Relay`
    already is the controllable-GPIO-output device and drives them
    all; a laser receiver or any bare digital input uses
    `PinDevice_Button`.
44. **QMP6988 barometric-pressure plugin** — `Plugin_QMP6988` drives
    the standalone Barometric Pressure Unit (and is the chip ENV III
    pairs with an SHT30) at I2C `0x70`.  Unlike the rough `raw/100`
    approximation `Plugin_ENV3` uses for its QMP6988, this plugin
    does the **full** datasheet compensation: it reads the 25-byte
    OTP coefficient block, resolves all thirteen terms (the 16-bit
    ones via the datasheet's A/S conversion factors, the 20-bit
    a0/b00 offsets directly), and evaluates the temperature and
    pressure polynomials — so it reports real hPa.  Bound by a
    positive `CHIP_ID = 0x5C` check, and registered after
    `Plugin_ENV3` so an ENV III unit's QMP6988 still belongs to
    ENV3.  (The other three raw sensors — VL53L1X, PAJ7620U2,
    GP8413 — are still pending: their datasheets don't carry the
    register-level init data needed to drive them raw.)
45. **GP8413 DAC plugin** — `Plugin_GP8413` drives the DAC 2 Unit, a
    dual-channel 15-bit I2C DAC, at `0x59`.  Controllable: `v0` and
    `v1` set each channel's 0-10 V output via the Web API.  The
    write format is from the GP8413 datasheet §3.3 — register `0x02`
    / `0x04` select VOUT0 / VOUT1, the 15-bit code is left-aligned
    into 16 bits and sent low byte first, `VOUT = code/0x7FFF·VMAX`.
    No ID register and an output device, so it is registered
    commented-out (opt-in).
46. **VL53L1X ToF4M plugin** — `Plugin_VL53L1X` drives the ToF4M Unit,
    ST's 4 m laser time-of-flight sensor, at `0x29`.  The VL53L1X has
    no documented register map — ST ships its ranging core as an
    opaque firmware blob, so it cannot be driven raw the way
    `Plugin_TOF` drives the VL53L0X.  This plugin is therefore a
    **thin wrapper** around the Pololu **VL53L1X** library: `begin()`
    calls `init()` (which also verifies model ID `0x010F = 0xEACC`,
    doubling as the presence check), selects Long distance mode, a
    50 ms timing budget, and continuous ranging; `update()` reads a
    new sample non-blocking only when `dataReady()`.  Needs the
    library installed, so its `#include` and registration are
    commented-out (opt-in, same pattern as `Plugin_FAN`).  It shares
    0x29 with `Plugin_TOF` / `Plugin_COLOR`, so its registration line
    sits just before `Plugin_TOF` — uncomment it to use a VL53L1X
    *instead of* a VL53L0X ToF Unit.  The PAJ7620U2 gesture sensor is
    not implemented: PixArt does not publish its gesture-mode init
    array, and no datasheet route exists.
47. **Gas-sensor plugins (Grove Multi-Gas V2 + MQ-series)** — two new
    drivers, each with a preheat/warmup mechanism.
    - `Plugin_MultiGas` — a raw I2C driver for Seeed's Grove
      Multichannel Gas Sensor V2 at `0x08`: an STM32F030 fronting
      four MEMS elements (GM-102B NO2, GM-302B alcohol, GM-502B VOC,
      GM-702B CO).  The command bytes (`0x01/0x03/0x05/0x07`) and the
      4-byte little-endian read format are transcribed from Seeed's
      open-source `Multichannel_Gas_GMXXX` library — no external
      library is linked.  Readings are raw 10-bit ADC counts; the
      sensor is qualitative by design.  `0x08` is shared with the
      Earth and Faces II units and the STM32 has no ID register, so
      `begin()` uses a heuristic presence check (all four channels
      must answer with a plausible, non-uniform-zero value) and the
      plugin is registered commented-out, before `Plugin_EARTH`.
    - `PinDevice_MQ` — one generic analog pin device for the
      constant-heater MQ sensors (MQ-2/3/4/5/6/8/135), selected by a
      `model` constructor argument.  It reads the load-resistor
      voltage on an ADC1 pin, reconstructs the sensor output voltage
      through a configurable input divider, and reports an Rs
      estimate.  MQ-7 / MQ-9 are deliberately excluded — their cycled
      heater needs separate handling.  ppm output is left for a later
      calibration pass (it needs a clean-air Ro and per-gas curves).
    - Warmup: both devices treat preheat as a firmware-side timer —
      the heaters run from power-on, so each records a start time and
      exposes a `warming` flag plus a `warmup_s` countdown (default
      180 s) until the window elapses.  Readings publish throughout,
      just flagged as not-yet-stable.  A brand-new sensor's long
      initial burn-in (24-48 h) cannot be enforced and is documented
      for the operator.
48. **Device-category build switches** — four `#define`s in `Config.h`
    (`ENABLE_OPTIONAL_I2C`, `ENABLE_STACKABLE_MODULES`,
    `ENABLE_PIN_DEVICES`, `ENABLE_UART_DEVICES`) let a whole category
    of device plugin be compiled out.  In the `.ino` each category's
    `#include` block and its `fw.addPlugin(...)` registration block
    are wrapped in a matching `#if`, so a switch set to `0` removes
    that category entirely — the headers are never parsed and nothing
    is registered, costing zero flash/RAM and trimming compile time.
    All four default to `1`, so the stock build is byte-identical to
    before; the board's own built-in chips (IMU / PMIC / IP5306 / RTC
    / INA3221) are never gated.  See "Trimming the build" above.
    Background: a plugin whose header is `#include`d but never
    instantiated already contributes nothing to the binary (an unused
    header-only class emits no code), so these switches mainly help
    by dropping the *registered-but-absent* default plugins and by
    speeding compiles — not by fixing a footprint bug.
49. **Output-channel build switches** — the five `OUT_*` flags in
    `Config.h` (`OUT_WEB`, `OUT_SERIAL`, `OUT_DISPLAY`, `OUT_MQTT`,
    `OUT_SD_LOG`) became true compile-time switches.  Previously they
    were runtime-only — each output class carried a `bool enabled`
    and the code plus its libraries were compiled in regardless.  Now
    each output header is split: the real class lives under
    `#if OUT_X`, and an `#else` branch defines a same-interface stub
    (every method an inert no-op, no heavy `#include`s).  Each output
    `.cpp` body is wrapped in `#if OUT_X` so a disabled channel
    compiles to an empty translation unit.  The upshot: `OUT_WEB
    false` drops the `esp32_idf5_https_server` libraries and all the
    web/dashboard code; `OUT_MQTT false` drops `PubSubClient`.
    Because the stub mirrors the real class's full public surface
    (including `MQTTOut::Stats` / `SDLogger::Stats` and the static
    transport helpers), `Framework` and `WebAPI` compile unchanged
    against whichever variant is selected.  All five default to
    `true`, so the stock build is unchanged.  One deliberate carve-
    out: the `SerialOut` stub's `begin()` still starts the UART, so
    the boot diagnostics survive `OUT_SERIAL false` — only the
    periodic per-plugin readings dump is removed.
50. **Standalone access-point mode** — when `WIFI_SSID` is left empty
    in `Config.h` the device no longer just fails to connect; it
    brings up its own WPA2 access point instead.  `_connectWiFi()`
    branches on an empty `WIFI_SSID` to a new `_startAccessPoint()`,
    which calls `WiFi.softAP(AP_SSID, AP_PASSWORD)` and, on success,
    sets an `_apMode` flag and shows the AP SSID + IP (default
    `192.168.4.1`) on the LCD and serial log.  NTP is skipped in AP
    mode (no upstream internet — timestamps fall back to uptime).  A
    failed `softAP()` — almost always an `AP_PASSWORD` shorter than
    WPA2's 8-character minimum — disables the web API with a clear
    message, mirroring the station-failure path.  Station behaviour
    is unchanged whenever `WIFI_SSID` is set; a configured network
    that is merely unreachable still fails as before (it does not
    fall back to AP).  `/api/config` now reports `wifi_mode`
    (`"ap"` / `"station"`) and `/api/endpoints` reports the correct
    IP for whichever mode is active.
51. **Navigable sensor-detail display** — the LCD now opens on a
    full-screen detail view of a single read-only sensor: the title
    line shows the device name flanked by left/right navigation
    arrows, and the body shows that sensor's readings (one big value,
    or a 2-column grid of up to eight).  Cycle between read-only
    sensors with the outer tactile buttons — `BtnA` = previous,
    `BtnC` = next — on a Core1 / Core2, or by tapping the title-line
    arrows on any touch board (Core2 / CoreS3).  The middle button
    (`BtnB`), or a tap on the title-bar centre, toggles to the
    all-sensors ticker overview and back.  Navigation is manual: the
    view stays pinned on the chosen sensor until the next press.
    Controllable devices (relays, servos, DACs, ...) are excluded —
    only read-only sensors appear in the cycle.  `DisplayManager`
    gained a `handleInput()` (called every loop, right after
    `M5.update()`) plus `_renderDetail` / `_renderReadings` /
    `_headerNav`; the detail view self-throttles to `POLL_MS` and
    repaints immediately on a button or touch event.  A clock panel
    is appended to the cycle whenever the framework has a synced
    wall-clock time (`Framework::timeSynced()`) — it shows the
    current time and date, and is omitted in AP mode or after a
    failed NTP sync.
52. **HMI Module LED + reset controls** — `Plugin_HMI` is now
    controllable.  Alongside the encoder / button readings it
    exposes the module's two indicator LEDs as toggles and a
    one-shot "reset count" button via the Web API (`?led1=1`,
    `?led2=off`, `?reset=1`).  LED writes are plain one-byte
    register writes to `0x30` / `0x31`, and the reset writes `1` to
    `0x40` — both confirmed against M5Stack's `M5Module-HMI`
    library.  `update()` reads the LED registers back, so the
    dashboard toggles track the module's real state even across a
    host reboot (the HMI keeps its own battery).  Because it now
    advertises `controllable()`, the HMI moves from the read-only
    sensor group into the controllable-device group — so it is no
    longer one of the cyclable panels in the sensor-detail display.
53. **Device IP on the LCD footer** — the footer status bar now
    shows the device's IP address, centred between the uptime
    counter (left) and the detail-view position indicator (right).
    `DisplayManager` caches it from `showWiFi()`, which the
    framework calls with `WiFi.localIP()` in station mode and
    `WiFi.softAPIP()` in AP mode — so the address that previously
    appeared only on the 3-second boot splash now stays on screen
    on every panel and the ticker.

54. **Chip-aware ADC1 pin validation** — the boot-time "not an ADC1
    pin" warning in the analog Port-B devices used to hard-code the
    classic ESP32 range (GPIO 32-39), so on the CoreS3 (ESP32-S3) a
    correctly-wired Port-B White pin, **G8**, falsely tripped it.
    Every analog pin device (Light, Earth, Angle, Mic, ECG, MQ,
    TubePressure, Watering, Grove2Grove) now accepts both ranges —
    GPIO 32-39 on the ESP32 and GPIO 1-10 on the ESP32-S3 — and only
    warns when the pin is outside both, a near-certain wiring mistake.
55. **`PinDevice_Angle`** — the classic Angle Unit (U016, a rotary
    potentiometer on a single ADC line; not the I2C variant at 0x36).
    Reports `raw` (0-4095), `pct` (0-100 %) and `angle` (mapped onto a
    configurable `SWEEP_DEG`, default 300°).
56. **`PinDevice_Cotech`** — a 433.92 MHz OOK + Manchester receiver
    for the Cotech 36-7959 8-in-1 weather station and its Sainlogic /
    SwitchDoc / uctech clones.  Decodes the 112-bit frame (CRC8) into
    temperature, humidity, wind speed / gust / direction, rainfall,
    UV and light, with a `pair` control to lock onto one sensor ID.
57. **Network device tier — direct Claude API + escalation router** —
    two pure-HTTPS plugins that subclass `IPinDevice` (no I2C address,
    no Framework changes).  `NetDevice_ClaudeAPI` streams the Anthropic
    Messages API directly (`GET /api/claude/set?ask=…`) for smart
    *text* answers — the model, not Claude Code — accepting that
    `CLAUDE_API_KEY` then lives in flash.  `NetDevice_Router` is a
    single chat entry point that classifies each turn on-device and
    routes it: trivial → the on-board Module LLM, optional "smart text"
    → the direct Claude API (`ROUTER_DIRECT_API`), hard / coding → a
    `WiFiClientSecure` POST to an Orange Pi orchestrator that owns
    Claude Code (so the device holds no key in that mode).  The chosen
    route is published as `route_taken` to the dashboard, MQTT and the
    SD log.  Both retry transient TLS-alloc failures and use an
    inactivity (not total-duration) reply timeout.  Settings live in
    the new `CLAUDE_*` / `ROUTER_*` blocks in `Config.h`.
58. **Config hygiene — secrets split + default-credential guard**
    (security approach A).  Every real credential (WiFi, AP password,
    dashboard login, MQTT user/pass + client cert/key, router bearer,
    Claude API key) moved out of `Config.h` into a new git-ignored
    `src/Secrets.h`; `Config.h` `#include`s it last and a tracked
    `src/Secrets.h.example` template carries placeholders.  A new
    `_securityAudit()` runs at boot: when the dashboard login is still
    the shipped `user` / `password`, it warns on the LCD + serial and,
    with `SECURITY_STRICT` (default on, `Config.h`), refuses to boot.
    When the AP password is left at the shipped default (or too short),
    `Security::effectiveApPassword()` mints a random per-device WPA2
    password, persists it in NVS, and shows it on a new `showNotice()`
    LCD banner.  `.gitignore` now excludes `Secrets.h`.  This stops the
    easy leaks (secrets in git, guessable defaults); it does not encrypt
    secrets at rest — see approaches B–D for that.
59. **Runtime provisioning — first-boot setup portal** (security
    approach B, core).  New `src/Settings.{h,cpp}` is an NVS-backed
    credential store with a HYBRID fallback: the effective value is
    the NVS entry if set, otherwise the compiled `Secrets.h` default.
    `Framework` loads it (`Settings::begin()`) before WiFi/auth, and
    `_connectWiFi()` + the dashboard auth now read these runtime
    values.  An unprovisioned unit (no WiFi in NVS or `Secrets.h`)
    comes up as its own access point and serves a self-contained
    **setup portal** at `/` (no auth — there's no password yet);
    submitting WiFi + an optional dashboard login to `GET /api/setup`
    saves them to NVS, marks the device provisioned, and reboots to
    join the network.  Once provisioned, `/` is the dashboard and
    `/api/setup` is auth-gated.  The approach-A boot guard now defers
    to the portal while unprovisioned instead of halting.  `/api/config`
    gained a `provisioned` flag and reports the effective SSID.  MQTT /
    Claude / router secrets still read `Secrets.h` (moving those into
    the portal + an editable Settings tab is full-B, not yet done).
60. **Provisioning recovery — no more lock-outs** (hardening of 58/59).
    Two escape hatches so a bad credential entered through the portal
    can never strand the device: (a) `Settings::factoryReset()` +
    `hasStoredCredentials()`, and `_securityAudit()` now, when a
    *portal-provisioned* unit carries the guessable `user` / `password`
    default under `SECURITY_STRICT`, wipes its provisioning and reboots
    into the setup portal instead of dead-halting (it still halts only
    when the default is compiled into `Secrets.h`); (b) a provisioned
    unit that **fails to join its Wi-Fi** now falls back to the AP and
    serves the setup portal (`Framework::forceSetupPortal()` →
    `WebAPI::_setupMode()`) so the network can be re-entered without
    erasing flash — covering a wrong password, a 5 GHz-only SSID
    (the ESP32-S3 is 2.4 GHz only), or an out-of-range/offline router.
61. **Hold-at-boot factory reset** — a hardware escape hatch
    (`Framework::_checkFactoryResetHold()`).  Hold the touch screen
    (CoreS3 / Core2) or any of BtnA/B/C (Core1 / Core2) for ~1.2 s
    while the device powers on to wipe the NVS settings and reboot
    into the setup portal — a guaranteed recovery even if the network
    and both passwords are wrong, without erasing flash from a PC.
    A brief tap is ignored; the hold must be sustained.  Gated by
    `FACTORY_RESET_HOLD_DISABLED` (Config.h, default `false` = the
    hold is enabled); set it `true` to disable the front-panel wipe on
    a deployed/kiosk unit.  Touch detection polls a short window
    (`FACTORY_RESET_WINDOW_MS`) after a warm-up that ignores the
    CoreS3 controller's boot-time phantom contacts, and debounces a
    real press.
62. **Setup portal hardened to POST** — the first-boot provisioning
    form now submits to `/api/setup` via `POST` with a url-encoded
    body instead of a query string, so the Wi-Fi / dashboard password
    never lands in the HTTPS server's request-line serial log.  The
    device decodes the fields itself (`_urlDecode` / `_formField`) so
    any password character round-trips exactly, with query- and
    named-arg fallbacks, and `onNotFound` also routes `/api/setup` in
    case the server registered the path for GET only.
63. **Approach B completed — runtime MQTT/Claude creds, captive
    portal, Settings page.**  The runtime `Settings` store now also
    covers MQTT user/password and the Claude API key (hybrid fallback
    to `Secrets.h`); `MQTTOut` and `NetDevice_ClaudeAPI` read them at
    runtime.  The first-boot portal gained optional MQTT/Claude fields.
    In setup mode the device runs a **captive portal**: a `DNSServer`
    resolves every host to the AP IP and the plain-HTTP (port 80)
    server serves the portal directly, so a phone auto-opens setup on
    connect (no cert warning while provisioning).  A new auth-gated
    **Settings page** at `/settings` (linked from the dashboard) edits
    Wi-Fi / dashboard / MQTT / Claude credentials over HTTPS without
    reflashing: `GET /api/settings` returns only non-secret state plus
    set/unset badges (secrets are never sent to the browser),
    `POST /api/settings/save` merges (blank = keep) and reboots.  The
    setup-submit handler is shared between the HTTPS and captive-HTTP
    servers via a server-generic template.
64. **MQTT broker host/port runtime-configurable** — `mqttHost` /
    `mqttPort` joined the `Settings` store (NVS, fallback to the
    `Config.h` values), and the portal + Settings page gained host/port
    fields, so a broker can be pointed at and enabled entirely from the
    UI without reflashing.  `MQTTOut` reads them at boot (the host is
    held in a member because `PubSubClient::setServer()` stores the
    pointer, not a copy).  Transport (plain vs TLS) remains the
    compile-time `MQTT_TLS` switch — the port field is labelled
    accordingly (1883 plain / 8883 TLS).
65. **Factory reset always returns to the setup portal.**  A wipe now
    sets a persistent `force_setup` flag so the next boot shows the
    captive portal even when `Secrets.h` bakes in a WiFi SSID (which
    `isProvisioned()` otherwise treats as configured); `_connectWiFi()`
    goes to the AP whenever the device is unprovisioned, so a
    compiled/leftover SSID can't silently auto-join and skip the
    portal.  `markProvisioned()` clears the flag once new secrets are
    entered.  The hold-to-reset confirm now tolerates brief touch
    dropouts (cancels only after ~350 ms of continuous release) so a
    steady finger-hold reliably completes the wipe.  The reset window
    is skipped on an already-unprovisioned device, so a finger still
    held (or a boot-time phantom touch) right after a wipe can't loop
    the reset — the device proceeds straight to the setup portal.
66. **Captive portal DNS on WiFiUDP (sockets layer).**  Both the
    bundled `DNSServer` and `AsyncUDP` call raw `udp_new` without the
    TCP/IP core lock — a fatal assert ("Required to lock TCPIP core
    functionality") on this ESP-IDF build that crash-looped at boot.
    The responder now uses `WiFiUDP` (the lwIP SOCKETS layer —
    `socket`/`bind`/`recvfrom`), which is thread-safe.
    `_serviceCaptiveDns()` is polled from `update()`, answers every A
    query with the AP IP (echo question + one answer, EDNS dropped),
    and replies via `WiFiUDP`.  Gated by `CAPTIVE_DNS_ENABLED`
    (Config.h, default true); set false to skip the auto-open (portal
    still reachable at the AP IP, e.g. http://192.168.4.1/).
67. **`WiFiUDP` forward-declaration build fix.**  `WebAPI.h` forward-
    declared `class WiFiUDP;`, but on the current esp32 core (3.3.x)
    `WiFiUDP` is a `typedef` for `NetworkUDP`, not a class — so the
    declaration broke the build with *"using typedef-name 'WiFiUDP'
    after 'class'"*.  Since `<WiFi.h>` is already included in the header,
    the type is in scope and the forward declaration was simply removed.
68. **Settings save POST routing fix.**  The dashboard Settings page
    submits `POST /api/settings/save`, but the server's `on()`
    registration only matches `GET`, so the save fell through to
    `onNotFound`, was mistaken for a plugin slug, and returned
    *"plugin not found: settings/save"*.  `onNotFound` now catches
    `/api/settings/save` (and `/api/settings`) explicitly before the
    plugin lookup — the same workaround already used for `/api/setup`.
69. **Standalone-AP dashboard mode is now runtime-selectable.**  A new
    persisted `ap_only` flag in `Settings` (NVS) lets the device run as
    its own access point serving the **dashboard** — not the setup
    portal — at `192.168.4.1`, with no upstream Wi-Fi.  Both the
    first-boot portal and the Settings page gained a "standalone access
    point" checkbox (when checked the Wi-Fi fields hide and become
    optional); the submit clears any stored SSID and sets `ap_only`.
    `isProvisioned()` treats `ap_only` as provisioned (so `/` serves the
    dashboard), while `_connectWiFi()` takes the access-point path
    whenever `ap_only` is set, regardless of any compiled `WIFI_SSID`.
    `/api/config` and `/api/settings` report `ap_only`, and `/api/all`
    now returns the softAP IP when in AP mode.  A factory reset clears
    the flag, returning the device to the normal first-boot portal.
70. **Router LLM yes/no tiebreaker (optional classifier).**  The
    escalation router (`NetDevice_Router`) previously decided local-vs-
    escalate purely from the keyword/path/extension prefilter, which
    only escalates on an EXPLICIT signal — a genuinely hard request
    worded without a trigger word silently stayed on the tiny local
    model.  A new `ROUTER_LLM_TIEBREAK` switch (Config.h, default
    `false`) adds a model-judged decision for the ambiguous middle:
    turns the prefilter did NOT flag first run ONE short yes/no
    classification inference on the on-board Module LLM, and only a
    "yes" escalates (to the Pi, else the direct API).  Decisive
    prefilter hits skip the tiebreaker entirely (no added latency on the
    obvious cases); it is also skipped when no local model is wired or
    there's nowhere to escalate to.  Implemented as a new asynchronous
    `CLASSIFYING` route that reuses the existing decoupled
    `command("ask")`/`toJson()` mirroring — no new transport code, and
    the keyword path remains the instant, offline-safe fallback.  The
    classification prompt is tunable via `ROUTER_TIEBREAK_PROMPT`, and
    `route_taken` reports `"classifying"` while the verdict is pending.

71. **Control endpoint accepts POST (GET still works).**  A
    `/api/<slug>/set` call changes physical state (and can bill an
    LLM/cloud token), so it shouldn't ride a cacheable/prefetchable GET
    — a browser pre-connect or link probe could misfire a relay or spend
    a token, and the values land in URL logs.  The dashboard widgets and
    the LLM/router panels now `POST` the params in a url-encoded body;
    `_route_control` reads params from the query string AND the POST body
    (decoded the same way the setup/settings handlers do, since the
    secure server doesn't fold a POST body into `arg()/args()`).  The old
    `GET ?param=value` form is unchanged, so existing bookmarks and
    scripts keep working — nothing breaks.  CORS now advertises
    `GET,POST,OPTIONS`; `/api/endpoints` lists the route as POST.

72. **Plain-HTTP standalone-AP dashboard (`WEB_AP_PLAIN_HTTP`).**  A new
    `Config.h` switch (default `false`) that, when set **and** the device
    is a provisioned standalone access point (`ap_only`), serves the full
    dashboard + REST API over plain HTTP on `WEB_HTTP_REDIRECT_PORT` (80)
    and does **not** start the TLS server at all.  Over the device's own
    WPA2 AP the link is already encrypted, so the self-signed cert only
    added a browser warning with no security benefit; skipping TLS also
    frees its RAM.  Implemented as an early branch in `WebAPI::begin()`
    plus a server-generic `_wirePlainAp<Srv>()` that mirrors the HTTPS
    route table and reuses the same `_buildSensorObj` / `_buildMqttStatus`
    / `_buildSdStatus` / `scanReport` builders (and the `serveSetupSubmit`
    template pattern), so the JSON shapes stay identical — only the
    transport and the `/api/all` `scheme`/`port` differ.  No effect in
    station mode (always HTTPS + redirect) or setup mode (the captive
    portal is already plain HTTP); default builds are unchanged.

73. **GM-tube Geiger counter (`PinDevice_Geiger`) + dashboard card.**  A
    new interrupt-driven pin device for a Geiger-Müller board (J305 /
    SBM-20 / "CAJOE" RadiationD-v1.1).  An `IRAM_ATTR` ISR
    (`attachInterruptArg`, FALLING) counts pulses on any interrupt-capable
    input (Port-B White — GPIO36 / CoreS3 G8); `update()` keeps a 60-slot
    counts-per-second ring → rolling **CPM**, **µSv/h** (CPM ÷ tube factor:
    SBM-20 ≈ 154, J305 ≈ 123), cumulative **dose (µSv)**, and a
    BACKGROUND/ELEVATED/DANGER **status** with an optional buzzer **alarm**
    and audible per-click **ticks** (LEDC).  `toJson` emits `cpm`,
    `usv_per_h`, `total_counts`, `dose_uSv`, `status`, `alarm`,
    `tube_factor`, `pin`, `settling`, and — only when `emitTrace` is on — a
    `trace[]` counts/sec array, so `/api/geiger` + MQTT + CSV + serial all
    work for free.  A **special-cased dashboard card** (built from the
    *Geiger Dashboard Card* design handoff) renders a big µSv/h hero, CPM,
    a green/orange/red zone bar, an alarm-pulse animation, and a live
    counts/sec sparkline (drawn only when `trace[]` is present).
    Registration is a commented Port-B line (exclusive with the other
    Port-B units); pass `-1` for the buzzer pin to disable it.  ⚠ Wiring an
    Arduino/5 V GM board to the 3.3 V ESP32 input requires getting the OUT
    line to a 0–3.3 V swing first (3.3 V pull-up for an open-collector
    output, or a divider / BSS138 / opto for a push-pull 5 V output);
    GPIO36 is input-only with no internal pull-up.

74. **Grove Laser PM2.5 Dust Sensor (`Plugin_HM3301`).**  An I2C driver
    for Seeed's HM3301 / HM330X laser particulate sensor.  The address is
    7-bit **`0x40`** (the datasheet's `0x80`/`0x81` are the 8-bit
    write/read forms).  `begin()` sends the `0x88` "select I2C" command
    and validates one 29-byte frame (checksum = sum of bytes 0–27); since
    the HM3301 has no `WHO_AM_I`, that checksum-valid read doubles as
    **permissive detection**, so it only claims `0x40` when a real sensor
    answers — and it must be registered **after** the strict, die-ID-gated
    `0x40` devices (`INA226` / `INA3221` / `SERVO2`), the same
    strict-before-permissive ordering used at 0x29 and 0x57.  `update()`
    keeps the last good values on any bad/short frame.  Surfaces
    `pm2_5` / `pm10` / `pm1_0` (atmospheric, µg/m³) as readings, plus the
    CF=1 `*_std` variants and all six particle-count bins (`pc_0_3` …
    `pc_10`) in `toJson` — `/api/pm25`, MQTT, CSV, serial and a dashboard
    card for free.  Slug `pm25`; registration is a commented line in the
    `ENABLE_OPTIONAL_I2C` block.  (Adds an I2C PM2.5 option alongside the
    existing UART `UartDevice_PMSA003`.)

75. **AS3935 lightning detector (`Plugin_AS3935`).**  An I2C driver for
    the AMS AS3935 Franklin lightning sensor (SparkFun Qwiic board) — a
    HYBRID device: register access over I2C plus an interrupt line.
    Default address **`0x03`** (in the reserved low range many scanners
    skip, but this framework probes the full 1..126 range, so it
    auto-detects).  `begin()` issues `PRESET_DEFAULT` and verifies reg
    `0x00` reads its reset value `0x24` (rejects non-AS3935 chips at
    0x03), runs `CALIB_RCO`, applies indoor/outdoor AFE gain, an optional
    antenna tuning cap and disturber mask, then attaches a RISING
    interrupt on the constructor's **IRQ pin** (`IRAM_ATTR` ISR sets a
    flag, same pattern as `PinDevice_Geiger`).  `update()` reads the INT
    register on the flag, decodes lightning / disturber / noise, and on a
    strike pulls distance (reg `0x07`; `0x3F` → -1 "out of range") and the
    20-bit energy (regs `0x04`–`0x06`).  `irqPin = -1` polls the INT
    register instead.  Readings `distance_km` / `energy` / `strikes`,
    plus `last_event`, `disturbers`, `noise_events`, `mode`, `tune_cap` in
    `toJson` — `/api/lightning` + MQTT + CSV + serial + card for free.
    Slug `lightning`; constructor `(irqPin=36, outdoor=true, tuneCap=0,
    maskDisturbers=false)`; commented registration in the
    `ENABLE_OPTIONAL_I2C` block.  ⚠ Grove carries only I2C — run I2C on
    Port-A and jump the IRQ pin to a free GPIO.

76. **Threshold / event alarm engine — `AlertManager` (milestone 1).**  A
    new toggleable output module (`OUT_ALERTS`), the same shape as
    `MQTTOut` / `SDLogger`: a `Framework` member that runs in the loop,
    reads `(slug, key)` values from each plugin's `getReadings()`, and
    runs them through a per-rule **state machine** — the framework's one
    genuinely new primitive (everything else is current-state polling;
    this adds edges/events).  Two rule kinds: **THRESHOLD** (sustained
    compare with debounce + hysteresis clear) and **EVENT** (an upward
    edge on a counter, optionally gated by a second reading).  Per rule:
    severity, target-channel bitmask, latch-until-ack vs auto-reset, and
    a notification cooldown.  State changes emit an `Event` into a
    16-deep in-RAM ring + a serial line; `ack()` releases latched rules
    and `toJson()` exposes the state (ready for the dashboard).  Ships two
    seed rules: geiger `usv_per_h ≥ 5.0` (latched critical) and AS3935
    `strikes` edge gated by `distance_km ≤ 10`.  **Milestone 1 is
    headless** — events go to serial + the ring only; the channel sinks
    (SMS / LoRa / Email / Webhook / Buzzer / LCD / MQTT / Dashboard / SD)
    and the NVS rule store + dashboard editor are later milestones.  Split
    `src/AlertManager.{h,cpp}` (breaks the `Framework` include cycle like
    `MQTTOut`); `Framework` gains an `alerts` member + begin/update calls;
    `Config.h` adds `OUT_ALERTS` and the seed-rule thresholds.

77. **Alarm "cheap" sinks — buzzer / LCD / MQTT / SD (milestone 2).**
    `AlertManager::_route()` now fans each event out to the channels named
    in the rule's bitmask.  Four sinks, each reusing an existing module:
    **Buzzer** — the built-in M5 speaker (`M5.Speaker.tone`) chirps on a
    raised/renotified WARN/CRITICAL (gated by `ALERT_BUZZER`); **LCD** —
    `DisplayManager::setAlert/clearAlert` overlays a severity-coloured
    strip (header/amber/red) on the top of the live view, cleared on
    resolve/ack; **MQTT** — `MQTTOut::publishAlert()` posts one event JSON
    to `<base>/alert` (not retained); **SD** — `SDLogger::logAlert()`
    appends a CSV row to a separate `/alerts.csv` (open+close per write so
    it never corrupts the sensor log).  Each touched module gained a small
    public method plus a matching stub so it still builds with that output
    compiled out, and the router skips unavailable channels.  `CH_DASH` is
    served by the engine's event ring (milestone 3); `CH_LORA` / `EMAIL` /
    `WEBHOOK` / `SMS` are no-ops until their milestones (the bits are
    already set on the seed rules).  Config adds `ALERT_BUZZER` +
    pitch/length.

78. **Alarm REST + dashboard panel (milestone 3a).**  `GET /api/alerts`
    returns the engine state + recent-event ring (straight from
    `AlertManager::toJson()`); `POST /api/alerts/ack` (optional `?rule=N`,
    default all) releases latched rules — both wired on the HTTPS server
    and the plain-HTTP-AP path.  A new **⚠ Alarms** dashboard panel polls
    `/api/alerts`, shows itself only when the engine is enabled, renders
    the event ring newest-first with severity-coloured rows, and has an
    **Ack all** button.  The `AlertManager` stub gained `toJson()` so the
    routes compile with `OUT_ALERTS` off.  (Milestone 3b — NVS rule
    storage + a runtime rule editor — is deferred to its own pass; the
    engine runs the compiled seed rules until then.)

79. **Alarm rule persistence + editor (milestone 3b).**  Rules are now
    runtime-editable and survive reboots.  `Settings` gained an
    `alert_rules` NVS blob (`alertRules()` / `setAlertRules()`), hybrid
    with the Config seed like Wi-Fi/MQTT.  `AlertManager` got compact
    JSON (de)serialisation, an NVS-or-seed load in `begin()`, and
    `rulesToJson` / `upsertRule` / `deleteRule` / `resetRules` — edits
    rebuild rule state and persist immediately (no reboot; rules eval
    every poll).  REST: `GET /api/alerts/rules`, `POST
    /api/alerts/rules/{save,delete,reset}`, on both server paths.  A
    **⚙ Rules** editor in the dashboard Alarms panel lists rules with
    edit/delete and a full add/edit form (slug, key, kind, op, threshold,
    gate, severity, the nine channel checkboxes, latch, debounce,
    hysteresis, cooldown) + Reset-to-defaults.

80. **LoRa alert sink (milestone 4).**  `AlertManager::_route()` now fans
    `CH_LORA` events out over LoRa P2P by calling the existing LoRa
    plugin's `command("send", …)` — found by slug, the same decoupling
    `NetDevice_Router` uses, so an absent radio auto-disables the channel.
    Sends on state edges only (raise/clear, not every renotify) to spare
    airtime; no new transmit code.  First off-device alert channel — add
    `lora` to a rule's channels (via the new editor) to use it.

81. **Email + Webhook alert sinks (milestone 5).**  Off-device alerts over
    HTTPS, through a **serialised outbound queue** so TLS handshakes never
    overlap (or contend for heap more than one at a time — the caveat from
    the feature's verdict): `AlertManager` sinks call `_enqueueHttp()`, and
    `update()` drains exactly one job per loop via `_pumpHttp()` →
    `_httpPost()` (a bounded, one-shot `WiFiClientSecure` POST that parses
    the `https://host[:port]/path` URL, sends the body with an optional
    `Authorization` header, reads the status line, 8 s timeout).  **Webhook**
    (`CH_WEBHOOK`) POSTs the event JSON to `ALERT_WEBHOOK_URL` (any HTTPS
    endpoint — Discord/Slack webhook, IFTTT, a Pi); **Email** (`CH_EMAIL`)
    POSTs `{"to","subject","text"}` to `ALERT_EMAIL_URL` with
    `ALERT_EMAIL_AUTH`.  Both fire on state edges only (raise/clear, not
    renotify).  Config adds the URL/auth/recipient constants (empty = the
    channel auto-disables).  Five of the six alarm milestones done — only
    SMS (the modem AT state machine) remains.

82. **SMS alert sink (milestone 6) — completes the alarm feature.**
    `UartDevice_Modem` is now controllable with `command("sms",
    "<number>,<text>")` driving a non-blocking **AT send state machine**
    (`AT+CMGF=1` → `OK` → `AT+CMGS="<num>"` → the raw `>` prompt → body +
    Ctrl-Z → `+CMGS`/`OK`), driven by `fastPoll()`, with a 30 s stuck-send
    timeout, a per-~24 h **daily cap** (`SMS_DAILY_MAX`) as the cost
    ceiling, `sms_busy`/`sms_sent`/`sms_failed` status, and a Send-SMS box
    on the dashboard for manual testing.  `AlertManager::_route()` handles
    `CH_SMS` on raise/clear edges by calling the modem's command with
    `ALERT_SMS_TO`.  ⚠ The modem owns the single Port-C UART (mutually
    exclusive with LLM/LoRa/GPS), SMS costs per message, and the AT flow is
    the standard SIMCom sequence but may need per-firmware tuning.  All six
    milestones of the threshold-alarm + multi-channel-routing feature are
    now in: engine → cheap sinks → REST/dashboard → persistent editable
    rules → LoRa / Webhook / Email / SMS.

---

## License
MIT – free for personal and commercial use.
