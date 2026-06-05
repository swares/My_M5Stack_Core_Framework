# Changelog

All notable changes to this project are recorded here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/); this project is not yet
formally versioned, so changes accumulate under **Unreleased**.

See `CODE_AUDIT.md` for the findings that motivated the entries below.

## [Unreleased]

### Added
- `src/CmdParse.h` — shared command-parameter validators (`cmd::parseBool`,
  `parseInt`, `parseFloat`, `allDigits`, `hexNibble`, `parseRgb`), pulled in via
  `IDevice.h` so every plugin has them.
- `src/HttpSse.h` — `HttpSseReader`, a streaming-HTTP reader that skips response
  headers and de-frames `Transfer-Encoding: chunked` for the SSE clients.
- `src/WebAssets.h` — the dashboard / setup-portal / settings-page PROGMEM HTML,
  moved out of `WebAPI.cpp`.
- `src/config/` — `Config.h` is now split into per-section headers
  (`01_wifi.h` … `26_lora_*.h`).
- `Config.h` flag `AP_SHOW_PASSWORD_EACH_BOOT` (default `false`).
- `Config.h` flag `WEB_CORS_ORIGIN` (default `"*"`).
- `apply_refactors.py` — one-shot, idempotent helper that performs the two bulk
  file-moves (dashboard extraction, Config.h split) with `*.bak` backups.
- `CODE_AUDIT.md` — full code-audit report.

### Changed
- **WebAPI route table de-duplicated.** The alerts-rules routes, control `/set`,
  plugin reads, settings save, and the JSON 404 used to be hand-copied across
  the HTTPS `onNotFound` and the standalone-AP `onNotFound`. They now live once
  in templated members `_routeDynamic` / `_doControl` / `_doSettingsSave` /
  `_buildSettingsDoc`, shared by both servers. Removed the now-dead
  `_route_control` / `_route_plugin` / `_route_404`.
- **Command parsing centralized.** The ~16 controllable plugins' private
  `_parseBool` / `_parseInt` / `_parseFloat` / `_allDigits` / `_parseRgb` copies
  are now thin adapters over `cmd::` (single source of truth).
- **Display overview throttled.** The ticker/overview repaints at a frame
  cadence (`OVERVIEW_FRAME_MS`, ~30 FPS) — or `POLL_MS` for the fixed grid —
  instead of every loop iteration, cutting SPI contention with the SD card.
- **`Config.h` reorganized** into ordered `src/config/*.h` section headers (via
  `apply_refactors.py`); textually identical include order, so the Secrets.h-last
  and `MQTT_TLS`-before-cert orderings are preserved.
- **Dashboard HTML relocated** from `WebAPI.cpp` to `src/WebAssets.h` (via
  `apply_refactors.py`), trimming ~1.2k lines from the server file.

### Fixed
- **AlertManager rule enums clamped.** `_ruleFromJson` now range-clamps
  `kind` / `op` / `gop` / `sev` from JSON, preventing out-of-bounds reads on the
  fixed-size string tables in `_emit` / `_route` / `toJson` from a crafted
  `POST /api/alerts/rules/save` or a corrupt NVS blob.
- **SSE chunked-encoding handling.** `NetDevice_ClaudeAPI`,
  `NetDevice_ClaudeAPI_History`, and `NetDevice_Router` previously parsed the
  raw socket line-by-line; a `Transfer-Encoding: chunked` boundary could split a
  `data:` line mid-JSON and silently drop a token. They now read through
  `HttpSseReader`, which de-frames chunks first.
- **Blocking `delay()` removed from sensor polling.** `Plugin_ULTRASONIC` and
  `Plugin_TVOC` use a non-blocking trigger-then-read-next-poll split;
  `Plugin_ADS1115` uses a non-blocking per-channel state machine in `fastPoll()`.
  No sensor stalls the cooperative loop during a poll anymore.
- **`HttpSseReader` buffer ceiling** (`CODE_AUDIT_2.md` #4). Both internal
  buffers are now capped at `kMaxBuffer` (8 KB); a malformed or non-terminating
  chunk/line/header block abandons the stream (marks it complete, frees the
  buffer) instead of growing the heap without bound.
- **TLS pre-connect heap guard** (`CODE_AUDIT_LATEST.md` M3, partial). Before
  opening any outbound TLS socket (MQTTS, escalation router, direct Claude API,
  AlertManager webhook/email), the framework checks free heap against
  `MIN_TLS_HEAP` (default 50 KB) and defers/skips gracefully when low — rather
  than fragmenting memory on a doomed handshake and starving the dashboard's
  HTTPS server. The alert queue keeps the job and retries; MQTTS retries on its
  next cycle. (The fuller fix — shrinking mbedTLS record buffers — is a build
  config change with cloud-TLS compatibility risk and is left optional.)
- **Optional hardware watchdog** (`CODE_AUDIT_LATEST.md` L2). New `WDT_ENABLE` /
  `WDT_TIMEOUT_S` (Config.h, default off). When on, the loop task is registered
  with the ESP32 Task Watchdog *after* `setup()` and fed each `loop()` pass, so a
  frozen `loop()` panics and reboots the device. Registered post-boot so the slow
  Module-LLM/TLS/SD init can't false-trip it; timeout (default 20 s) sits above
  the only bounded in-loop blocker (the ~8 s alert POST).

### Pi orchestrator (`scripts/orchestrator.py`)
- **Per-request state** (`CODE_AUDIT_2.md` #2). Each `--serve` request gets its
  own `Orchestrator`, removing the shared-`history` data race under
  `ThreadingHTTPServer`.
- **Concurrency cap** (#2). A `BoundedSemaphore(MAX_CONCURRENT_AGENTS)` limits
  simultaneous Claude Code subprocesses; requests past the cap get a "busy" note.
- **Stalled-agent deadline** (#2). A `threading.Timer` watchdog kills the agent
  at `CLAUDE_TIMEOUT`; the old `proc.wait()` timeout only fired after stdout EOF,
  which a hung child never reaches.
- **POST body guard** (#3). `do_POST` guards the `Content-Length` parse and
  rejects bodies over `MAX_POST_BYTES` (413).
- **Fail-closed `--serve`** (`CODE_AUDIT_LATEST.md` H1). The service refuses to
  start unless a bearer (`SERVE_BEARER`) or source-IP allowlist
  (`CLIENT_ALLOWLIST`) is configured — closing the default-open exposure of an
  endpoint that can run Claude Code with shell/filesystem access. Set
  `SERVE_ALLOW_INSECURE = True` to override when already firewalled.

### Security
- **Modem SMS hardened against AT-command injection.** `UartDevice_Modem`
  restricts the destination number to `+`/digits and strips CR/LF/Ctrl-Z from
  the body, so neither can break out of the `AT+CMGS="..."` quoting.
- **Generated AP password no longer re-displayed every boot** by default (shown
  only on the boot that first mints it); `AP_SHOW_PASSWORD_EACH_BOOT` restores
  the old always-show behaviour.
- **Configurable dashboard CORS origin** via `WEB_CORS_ORIGIN` (default `"*"`),
  so the API can be locked to a single origin.
- **CSRF protection on state-changing endpoints** (`CODE_AUDIT_LATEST.md` M2).
  A shared `_csrfBlocked()` guard makes every authenticated mutation POST-only
  and requires the custom header `X-Requested-With`: `/api/<slug>/set`,
  `/api/settings/save`, and `/api/alerts/{ack,rules/save,rules/delete,
  rules/reset}`. A cross-site page can't add that header without a CORS preflight
  this server doesn't allow, so a forged command from another origin is blocked
  even with cached Basic-Auth. Read endpoints (`GET /api/<slug>`,
  `/api/settings`, `/api/alerts/rules`) and the setup/recovery portal are left
  open. The dashboard sends the header automatically. Gated by
  `WEB_CSRF_PROTECT` (default `true`); the only behaviour change is that GET
  bookmarks to `/set` no longer work (state changes are POST-only).

### Not done (tracked for later)
- Make the orchestrator `--serve` fail-closed (refuse to start with no bearer
  *and* no allowlist) — `CODE_AUDIT_2.md` #1.
- Hardware verification of the unverified register maps (e.g. `Plugin_GOPLUS2`).
- Long-uptime heap/`String` fragmentation watch (monitoring, not a code change).
- Optional: delete the per-plugin command-parse adapter shims and call `cmd::`
  directly at each call site (full purity vs. current minimal-diff form).

## Historical (pre-audit)

_Imported verbatim from the README. Covers the original
CoreS3 → multi-board port and the incremental feature log that
preceded the code audit._

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

83. **Optional Host-header allowlist (`WEB_HOST_ALLOWLIST`).**  A
    lightweight extra access guard for station mode: when the
    comma-separated list is non-empty, `_requireAuth()` first checks the
    request's `Host` header (port-stripped, case-insensitive) via
    `_hostAllowed()` and returns `403` if it matches nothing on the list.
    Disabled when empty, and never enforced in AP / setup mode (so a bad
    value can't strand recovery).  Guards DNS-rebinding and access via
    unexpected hostnames; it is **not** a client-IP filter — the HTTPS
    compat library exposes no client `remoteIP()`, so source-IP
    restriction belongs at the router/firewall.  Basic Auth remains the
    primary access control.

---
