#pragma once
// ============================================================
//  IDevice.h  –  Abstract base class every sensor plugin
//               must implement.
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>

// Which physical bus(es) a plugin may appear on.
//   Internal = Wire  – built-in chips on the host board
//                      (CoreS3: SDA12/SCL11, Core2: SDA21/SCL22)
//   External = Wire1 – Port-A / Grove
//                      (CoreS3: SDA2/SCL1,   Core2: SDA32/SCL33)
//   Both     = search both buses
enum class I2CBus : uint8_t { Internal = 0, External = 1, Both = 2 };

// How a device is physically attached to the host board.  Surfaced
// in the Web API + dashboard so a client can tell a cabled Grove
// unit from a board-stacked module from a soldered-on chip.
//   Builtin   – soldered onto the Core itself (IMU, PMIC, RTC) or
//               into a power base (the M5GO Bottom's IP5306).
//   Stackable – an M-Bus stacking module (4Relay, Servo2, Faces II,
//               8Angle ...).  These sit on the INTERNAL I2C bus.
//   Pluggable – a Grove / Port-A unit on the end of a cable.
enum class MountType : uint8_t { Builtin = 0, Stackable = 1, Pluggable = 2 };

// ── Typed sensor reading ──────────────────────────────────────
struct SensorVal {
  const char* key;  // short label, e.g. "temp"
  float value;
  const char* unit;  // e.g. "°C", "%", "lux"
};

// ============================================================
class IDevice {
 public:
  virtual ~IDevice() = default;

  // ── Identity ──────────────────────────────────────────────
  /** Full human-readable name, e.g. "ENV III Unit" */
  virtual const char* name() const = 0;

  /** Short URL/JSON slug, e.g. "env3"  (no spaces, lowercase) */
  virtual const char* slug() const = 0;

  /** Fill buf[] with every I2C address this device can use.
   *  count must be set to the number of entries written.     */
  virtual void i2cAddresses(uint8_t* buf, uint8_t& count) const = 0;

  /** Which bus(es) to probe for this plugin */
  virtual I2CBus preferredBus() const { return I2CBus::Both; }

  /** How the device is physically attached — see MountType.
   *  Default Pluggable: most plugins are cabled Grove units.
   *  Built-in chips and stacking modules override this.          */
  virtual MountType mount() const { return MountType::Pluggable; }

  /** True for non-I2C "pin" devices (GPIO / PWM / ADC / 1-Wire).
   *  The boot I2C scan skips these (they advertise no address);
   *  the framework activates them via IPinDevice::beginPins().
   *  Default false — every ordinary I2C plugin is correct as-is.   */
  virtual bool isPinDevice() const { return false; }

  // ── Lifecycle ─────────────────────────────────────────────
  /** Called once when the device is confirmed present.
   *  @param wire  resolved TwoWire instance
   *  @param addr  actual address found
   *  @return true on successful initialisation             */
  virtual bool begin(TwoWire* wire, uint8_t addr) = 0;

  /** Called every POLL_MS when active – update internal state */
  virtual void update() = 0;

  /** Opt-in high-rate servicing.
   *
   *  update() is gated by POLL_MS (default 500 ms) — fine for slow
   *  sensors, far too slow for anything that needs a waveform.  A
   *  plugin that returns true from wantsFastPoll() additionally gets
   *  fastPoll() called on EVERY loop() iteration, with its mux
   *  channel already selected by the framework.  Use it to drain a
   *  hardware FIFO and feed a signal-processing pipeline — e.g. the
   *  MAX30100 beat detector, where the sensor FIFO would overflow at
   *  the slow poll rate.  Keep fastPoll() cheap: it runs in the hot
   *  loop alongside the web server, MQTT and display updates.       */
  virtual bool wantsFastPoll() const { return false; }
  virtual void fastPoll() {
    // Default: no-op.  Only plugins whose wantsFastPoll() returns
    // true override this (see the note above); the rest inherit
    // this empty body intentionally.
  }

  /** Optional liveness check.
   *
   *  Originally added so the framework's periodic external rescan
   *  could verify a bound chip without disturbing it.  The periodic
   *  rescan has since been removed (hot-plugging I2C isn't safe and
   *  the rescan was the single largest source of bus instability),
   *  so isAlive() is no longer called automatically.  Left in the
   *  interface because it's still useful for:
   *    - manual diagnostics ("is this chip still talking?")
   *    - future health-monitoring features that watch for chips
   *      that go silent during a long run
   *
   *  Default implementation: bare quick-command probe.  Fine for
   *  ordinary I2C peripherals, unsafe for SMBus-only chips like
   *  MLX90614.  Plugin_NCIR2, Plugin_HEART, and Plugin_HEART_MAX30102
   *  override with proper register reads — keep that pattern when
   *  adding new plugins for chips that mishandle quick commands.
   */
  virtual bool isAlive() {
    bus->beginTransmission(addr);
    return bus->endTransmission() == 0;
  }

  // ── Data access ───────────────────────────────────────────
  /** Serialise current readings into a JSON object */
  virtual void toJson(JsonObject& obj) const = 0;

  /** Return flat array of typed readings for serial/display.
   *  @param buf    caller-provided buffer (16 elements max)
   *  @param count  set to number of values written           */
  virtual void getReadings(SensorVal* buf, uint8_t& count) const = 0;

  // ── Control (output devices) ───────────────────────────────
  /** True if this plugin drives hardware that can be commanded —
   *  relays, servos, LEDs.  Controllable plugins accept commands
   *  via the Web API endpoint  GET /api/<slug>/set?<param>=<value> .
   *  There is deliberately NO serial/display control path: for now
   *  output devices are driven through the Web API only.          */
  virtual bool controllable() const { return false; }

  /** Apply one control command.  The Web API calls this once per
   *  query parameter on /api/<slug>/set.  Both arguments are the
   *  raw strings from the URL.
   *
   *  The plugin MUST validate them and reject anything invalid —
   *  an unknown param name, a malformed value, or a value outside
   *  the device's legal range.  Rejected commands must NOT touch
   *  the hardware.
   *
   *  @return true  – param recognised AND value valid AND applied
   *          false – unknown param, or value rejected as invalid  */
  virtual bool command(const String& param, const String& value) {
    (void)param;
    (void)value;
    return false;
  }

  /** Describe this device's controls so the Web dashboard can
   *  render interactive widgets for them.  The Web API calls this
   *  for every controllable() device and serialises the result
   *  into the "controls" array of /api/<slug> and /api/all.
   *
   *  Push one JSON object per control into `out`.  Recognised keys:
   *    "id"      command() parameter the widget drives (every type
   *              except "button")
   *    "label"   human-readable widget label
   *    "type"    "toggle" | "slider" | "color" | "text" | "button"
   *    "value"   current value — the widget's initial state
   *              (optional; omit for write-only controls)
   *    "min","max","step","unit"   "slider" only
   *    "placeholder"               "text" only (input hint)
   *    "query"   "button" only — raw "k=v&k=v" query string sent
   *              verbatim to /api/<slug>/set
   *    "group"   optional heading the dashboard clusters controls under
   *
   *  A widget's value is sent as
   *    GET /api/<slug>/set?<id>=<widget value>
   *  (or the literal "query" for a button), so every value a widget
   *  can produce MUST be one this plugin's command() accepts.
   *  Only called when controllable() is true; default: no controls. */
  virtual void controlSchema(JsonArray& out) const { (void)out; }

  // ── Runtime state (set by Framework) ─────────────────────
  bool active = false;
  uint8_t addr = 0x00;
  TwoWire* bus = nullptr;

  // Mux context — populated by the framework when binding a plugin
  // behind a PCA9548A-style I2C hub (M5Stack PaHUB or similar).
  // muxAddr == 0 means the plugin lives on the root bus and no
  // channel switching is required.  Otherwise muxChannel (0-7) is
  // the channel that must be selected on the mux at muxAddr before
  // any transaction with this plugin's chip.  Plugins don't need
  // to read or react to these — the framework selects the channel
  // before calling begin() and before each update().
  uint8_t muxAddr = 0;
  uint8_t muxChannel = 0;

 protected:
  // ── Convenience I2C helpers ───────────────────────────────
  bool regWrite(uint8_t reg, uint8_t val) {
    bus->beginTransmission(addr);
    bus->write(reg);
    bus->write(val);
    return bus->endTransmission() == 0;
  }

  bool regRead(uint8_t reg, uint8_t* dst, uint8_t len) {
    bus->beginTransmission(addr);
    bus->write(reg);
    if (bus->endTransmission(false) != 0)
      return false;
    if (bus->requestFrom(static_cast<int>(addr), static_cast<int>(len)) != len)
      return false;
    for (uint8_t i = 0; i < len; i++)
      dst[i] = bus->read();
    return true;
  }

  uint8_t regRead8(uint8_t reg) {
    uint8_t v = 0;
    regRead(reg, &v, 1);
    return v;
  }

  int16_t regRead16BE(uint8_t reg) {
    uint8_t b[2] = {0};
    regRead(reg, b, 2);
    return static_cast<int16_t>((b[0] << 8) | b[1]);
  }
};
