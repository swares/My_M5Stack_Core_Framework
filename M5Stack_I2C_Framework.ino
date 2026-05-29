/**
 * ============================================================
 *  M5Stack  –  I2C Sensor Framework
 * ============================================================
 *  Auto-detects and manages M5Stack I2C components on both the
 *  internal and external (Port-A / Grove) buses.  Runs on:
 *      • M5Stack CoreS3
 *      • M5Stack Core2 / Core2 v1.1c:\Users\wares\OneDrive\Documents\Claude\Projects\M5Stack I2C Device Framework\M5Stack Device Framework Refactor for Core2\M5Stack_I2C_Framework\src\DisplayManager.cpp
 *  The host board is detected at runtime via M5.getBoard() and
 *  the correct pin map + built-in chip identities are chosen
 *  automatically — same binary works on either board.
 *
 *  Output channels (each independently enable/disable):
 *    • Web API  – HTTPS JSON REST endpoints + live HTML dashboard
 *    • Serial   – Arduino Serial Monitor
 *    • Display  – LCD, scrolling ticker OR fixed grid
 *    • MQTT     – publish-only; one retained JSON doc per plugin
 *                 to <base>/<slug>, with LWT on <base>/status
 *    • SD log   – one CSV per boot, /log_NNNN.csv, dynamic header
 *
 *  ── Board setup ──────────────────────────────────────────
 *  Board package : "M5Stack"
 *    (add https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json)
 *  Board target  : "M5Stack CoreS3"  OR  "M5Stack Core2"
 *
 *  ── Required libraries (Library Manager) ─────────────────
 *    • M5Unified     (M5Stack)        – board abstraction
 *    • ArduinoJson   (Benoit Blanchon) >= 7.0
 *    • PubSubClient  (Nick O'Leary)   – MQTT client (only needed
 *                                        when OUT_MQTT is true)
 *    • esp32_idf5_https_server_compat – WebServer-compatible TLS
 *      + esp32_idf5_https_server        server for the HTTPS
 *                                        dashboard / API.  Install
 *                                        BOTH — the compat wrapper
 *                                        needs the base library.
 *    • SD / SPI / Preferences         (bundled with esp32 core —
 *                                        used by SDLogger when
 *                                        OUT_SD_LOG is true)
 *    • WiFi                           (bundled with esp32 core)
 *
 *  M5Unified pulls in the per-board drivers automatically, so
 *  no separate M5CoreS3 / M5Core2 library is required.
 * ============================================================
 */

#include "src/Framework.h"

// ── Internal (built-in) hardware ──────────────────────────────
//  Board-aware plugins — the same plugin handles either chip
//  variant (BMI270+BMM150 on CoreS3, MPU6886 on Core2; AXP2101
//  on CoreS3, AXP192 on Core2).
#include "plugins/Plugin_IMU.h"
#include "plugins/Plugin_PMIC.h"
#include "plugins/Plugin_IP5306.h"     // Core1 battery chip (0x75)
#include "plugins/Plugin_RTC.h"
#include "plugins/Plugin_INA3221.h"    // Core2 v1.1 power monitor (0x40)

// ── Pluggable M5Stack Units (external Port-A / Grove) ────────
//  NOTE: when two plugins share an I2C address (Heart vs Ultrasonic
//  at 0x57, ToF vs Color at 0x29), register the one with the
//  STRICTER begin()/ID check FIRST.  The framework tries plugins in
//  registration order and only one can claim a given address, so a
//  strict plugin gets the chance to reject a wrong chip and let the
//  permissive plugin take over.
//  Gated by ENABLE_OPTIONAL_I2C (Config.h) — set it to 0 to drop
//  every pluggable Port-A Unit plugin from the build.
#if ENABLE_OPTIONAL_I2C
#include "plugins/Plugin_ENV4.h"       // strict: BMP280 chip-id @ 0x76
#include "plugins/Plugin_ENV3.h"       // permissive: SHT30 cmd  @ 0x44
#include "plugins/Plugin_NCIR2.h"
#include "plugins/Plugin_TOF.h"        // strict: WHO_AM_I check    (0x29)
#include "plugins/Plugin_COLOR.h"      // permissive               (0x29)
//  Plugin_VL53L1X (ToF4M Unit) needs the Pololu "VL53L1X" library —
//  uncomment this #include AND the matching registration line in
//  setup() together.  Also shares 0x29 with TOF / COLOR.
// #include "plugins/Plugin_VL53L1X.h"   // ToF4M Unit (VL53L1X) @ 0x29
#include "plugins/Plugin_EARTH.h"
#include "plugins/Plugin_LIGHT.h"
#include "plugins/Plugin_TVOC.h"
#include "plugins/Plugin_ACCEL.h"
#include "plugins/Plugin_JOYSTICK.h"
#include "plugins/Plugin_ANGLE.h"
#include "plugins/Plugin_HEART.h"          // strict: PART_ID 0x11      (0x57)
#include "plugins/Plugin_HEART_MAX30102.h" // strict: PART_ID 0x15      (0x57)
#include "plugins/Plugin_ULTRASONIC.h"     // permissive               (0x57)
#include "plugins/Plugin_WEIGHT.h"
#include "plugins/Plugin_THERMAL.h"
#include "plugins/Plugin_COMPASS.h"
#include "plugins/Plugin_MPU6886.h"   // 6-Axis IMU Unit (MPU6886) @ 0x68
#include "plugins/Plugin_SCD4X.h"     // CO2 / CO2L Unit (SCD40/41) @ 0x62
#include "plugins/Plugin_INA226.h"    // INA226 power monitor @ 0x40
#include "plugins/Plugin_ADS1110.h"   // ADC Unit (ADS1110) @ 0x48
#include "plugins/Plugin_ADS1115.h"   // Ammeter/Voltmeter Unit (ADS1115) @ 0x48
#include "plugins/Plugin_QMP6988.h"   // Barometric Pressure Unit (QMP6988) @ 0x70
#include "plugins/Plugin_GP8413.h"    // DAC 2 Unit (GP8413) @ 0x59
#include "plugins/Plugin_MultiGas.h"  // Grove Multichannel Gas Sensor V2 @ 0x08
#endif  // ENABLE_OPTIONAL_I2C

// ── Stackable M-Bus modules (internal I2C bus) ───────────────
//  These bolt onto the Core's stacking connector rather than
//  plugging into Port-A.  Output modules (4Relay, Servo2, the
//  8Angle LEDs) are driven through the Web API — see README.
//  NOTE: Plugin_4RELAY shares 0x26 with Plugin_WEIGHT, and
//  Plugin_FACES2 shares 0x08 with Plugin_EARTH — only one of
//  each pair can be on a bus at once.
//  Gated by ENABLE_STACKABLE_MODULES (Config.h).
#if ENABLE_STACKABLE_MODULES
#include "plugins/Plugin_4RELAY.h"
#include "plugins/Plugin_2RELAY.h"     // Module13.2 2Relay @ 0x25
#include "plugins/Plugin_SERVO2.h"
#include "plugins/Plugin_8ANGLE.h"
#include "plugins/Plugin_FACES2.h"
#include "plugins/Plugin_FACES2_ENCODER.h"
#include "plugins/Plugin_GOPLUS2.h"
#include "plugins/Plugin_STEPMOTOR.h"
#include "plugins/Plugin_4IN8OUT.h"
#include "plugins/Plugin_AIN420MA.h"    // Module13.2 AIN4-20mA @ 0x55
#include "plugins/Plugin_PPS.h"         // Module13.2 PPS power supply @ 0x35
#include "plugins/Plugin_HMI.h"         // Module HMI encoder + buttons @ 0x41
//  Plugin_FAN needs the "M5Module-Fan" library — uncomment this
//  #include AND the matching registration line in setup() together:
// #include "plugins/Plugin_FAN.h"       // Module Fan v1.1 @ 0x18
#endif  // ENABLE_STACKABLE_MODULES

// ── Non-I2C "pin" devices (GPIO / PWM / ADC) ─────────────────
//  These M5Stack units have no I2C interface, so they cannot be
//  auto-detected — you register each one in setup() with the
//  pin(s) it is wired to.  See the registration block in setup().
//  Gated by ENABLE_PIN_DEVICES (Config.h).
#if ENABLE_PIN_DEVICES
#include "plugins/PinDevice_PIR.h"
#include "plugins/PinDevice_Relay.h"
#include "plugins/PinDevice_Buzzer.h"
#include "plugins/PinDevice_Servo.h"
#include "plugins/PinDevice_Light.h"
#include "plugins/PinDevice_Angle.h"   // Angle Unit (rotary pot) — analog ADC
#include "plugins/PinDevice_Earth.h"
#include "plugins/PinDevice_Mic.h"
#include "plugins/PinDevice_ECG.h"      // ECG Module (AD8232) — analog + lead-off
#include "plugins/PinDevice_Button.h"   // Button Unit — digital input
#include "plugins/PinDevice_Motor.h"    // DC Motor / Vibration / Mini Fan — PWM
#include "plugins/PinDevice_Watering.h" // Watering Unit — moisture + pump
#include "plugins/PinDevice_MQ.h"      // MQ-series gas sensors (analog ADC)
//  Port-B units (M5Stack Core Port-B = GPIO26 Yellow + GPIO36 White).
//  Plain GPIO/ADC, no extra libraries.  Port B is a SINGLE physical
//  connector, so only one of these can be plugged in at a time.
#include "plugins/PinDevice_Cotech.h" // Weather Station
#include "plugins/PinDevice_Hall.h"
#include "plugins/PinDevice_Limit.h"
#include "plugins/PinDevice_OP180.h"
#include "plugins/PinDevice_DualButton.h"
#include "plugins/PinDevice_TubePressure.h"
#include "plugins/PinDevice_Grove2Grove.h"
//  The two below need extra libraries — uncomment the #include AND
//  the matching registration line in setup() together:
//    PinDevice_DS18B20  → "OneWire" + "DallasTemperature"
//    PinDevice_IR       → "IRremoteESP8266"
// #include "plugins/PinDevice_DS18B20.h"
// #include "plugins/PinDevice_IR.h"
#endif  // ENABLE_PIN_DEVICES

// ── UART (serial) devices — at most ONE, on Port-C ───────────
//  Gated by ENABLE_UART_DEVICES (Config.h).
#if ENABLE_UART_DEVICES
#include "plugins/UartDevice_Barcode.h"
#include "plugins/UartDevice_Modem.h"
#include "plugins/UartDevice_PMSA003.h"
#include "plugins/UartDevice_ASR.h"
//  These two need extra libraries — uncomment the #include AND the
//  matching registration line in setup() together:
//    UartDevice_GPS        → "TinyGPSPlus"
//    UartDevice_ModuleLLM  → "M5Module-LLM"  (offline AI text chat)
// #include "plugins/UartDevice_GPS.h"
#include "plugins/UartDevice_ModuleLLM.h"
#endif  // ENABLE_UART_DEVICES

Framework fw;

void setup() {
  // ── (Optional) Register I2C hubs on the external bus ──────
  //  Uncomment if you have a PCA9548A-based hub like the M5Stack
  //  PaHUB plugged into Port-A.  Default PaHUB address is 0x70.
  //  After this call the framework auto-scans each of the hub's
  //  8 downstream channels at boot and binds sensors found on
  //  them.  Register a hub BEFORE the plugins that live behind
  //  it.  Multiple hubs are allowed (e.g. 0x70 + 0x71).
  //
  fw.addMux(0x70);

  // ── Register all plugins (order = detection priority) ─────
  //  If you have multiple instances of the same sensor type on
  //  different hub channels (e.g. two NCIR2 units), register
  //  one Plugin_X per physical sensor — the framework will bind
  //  them to (hub, channel, addr) tuples in registration order.
  fw.addPlugin(new Plugin_IMU());
  fw.addPlugin(new Plugin_PMIC());       // AXP192 / AXP2101 @ 0x34 (Core2 / CoreS3)
  fw.addPlugin(new Plugin_IP5306());     // IP5306 @ 0x75 (Core1)
  fw.addPlugin(new Plugin_RTC());
  fw.addPlugin(new Plugin_INA3221());    // 0x40 power monitor (Core2 v1.1) — die-ID
                                         // checked, so it MUST precede Plugin_SERVO2
#if ENABLE_OPTIONAL_I2C
  fw.addPlugin(new Plugin_ENV4());        // strict @ 0x44 + BMP280 0x76
  fw.addPlugin(new Plugin_ENV3());        // permissive @ 0x44 + QMP6988 0x70
  fw.addPlugin(new Plugin_NCIR2());
  //  ToF4M Unit (VL53L1X) needs the Pololu "VL53L1X" library (see
  //  #include above).  Uncomment to use it INSTEAD of a VL53L0X ToF
  //  Unit — register it here, before TOF, so its strict ID check
  //  runs first on the shared 0x29 address:
  // fw.addPlugin(new Plugin_VL53L1X());   // strict @ 0x29 (ToF4M)
  fw.addPlugin(new Plugin_TOF());         // strict @ 0x29 — must precede COLOR
  fw.addPlugin(new Plugin_COLOR());       // permissive @ 0x29
  //  Grove Multichannel Gas Sensor V2 shares 0x08 with the Earth /
  //  Faces II units and has no ID register, so its presence check is
  //  heuristic.  Uncomment to use it — kept before Plugin_EARTH so
  //  its check runs first on the shared 0x08 address:
  // fw.addPlugin(new Plugin_MultiGas());    // 0x08  Grove Multi-Gas V2
  fw.addPlugin(new Plugin_EARTH());
  fw.addPlugin(new Plugin_LIGHT());
  fw.addPlugin(new Plugin_TVOC());
  fw.addPlugin(new Plugin_ACCEL());
  fw.addPlugin(new Plugin_JOYSTICK());
  fw.addPlugin(new Plugin_ANGLE());
  fw.addPlugin(new Plugin_HEART());          // strict @ 0x57 (PART_ID 0x11)
  fw.addPlugin(new Plugin_HEART_MAX30102()); // strict @ 0x57 (PART_ID 0x15)
  fw.addPlugin(new Plugin_ULTRASONIC());     // permissive @ 0x57 — must come last
  fw.addPlugin(new Plugin_WEIGHT());
  fw.addPlugin(new Plugin_THERMAL());
  fw.addPlugin(new Plugin_COMPASS());
  //  Add-on Port-A sensor units (standalone-chip Units).
  fw.addPlugin(new Plugin_MPU6886());        // 0x68  6-axis IMU (external bus)
  fw.addPlugin(new Plugin_SCD4X());          // 0x62  CO2 / CO2L
  fw.addPlugin(new Plugin_INA226());         // 0x40  power monitor (die-ID checked)
  fw.addPlugin(new Plugin_QMP6988());        // 0x70  barometric pressure (after ENV3)
  //  ADS1110 and ADS1115 BOTH sit at 0x48 with no ID register, so
  //  the scan cannot tell them apart — uncomment exactly the ONE
  //  you have (0x48 is a common address; both off is the safe
  //  default):
  // fw.addPlugin(new Plugin_ADS1110());     // 0x48  ADC Unit
  // fw.addPlugin(new Plugin_ADS1115());     // 0x48  Ammeter / Voltmeter
  //  GP8413 DAC has no ID register and is an output device — opt in:
  // fw.addPlugin(new Plugin_GP8413());      // 0x59  DAC 2 Unit (dual 0-10 V)
#endif  // ENABLE_OPTIONAL_I2C

  // ── Stackable M-Bus modules ─────────────────────────────
  //  Registered AFTER the Port-A units.  Plugin_WEIGHT (0x26)
  //  and Plugin_EARTH (0x08) are registered first, so on a bus
  //  that genuinely has those units they win the address; a
  //  4Relay / Faces II only binds when its rival unit is absent.
#if ENABLE_STACKABLE_MODULES
  fw.addPlugin(new Plugin_4RELAY());         // 0x26 (shares w/ WEIGHT)
  fw.addPlugin(new Plugin_2RELAY());         // 0x25  Module13.2 2Relay
  fw.addPlugin(new Plugin_SERVO2());         // 0x40
  fw.addPlugin(new Plugin_8ANGLE());         // 0x43
  fw.addPlugin(new Plugin_FACES2());         // 0x08 (shares w/ EARTH)
  fw.addPlugin(new Plugin_FACES2_ENCODER()); // 0x5E
  fw.addPlugin(new Plugin_GOPLUS2());        // 0x38  motors/servos/RGB/encoders
  fw.addPlugin(new Plugin_STEPMOTOR());      // 0x27  stepper driver
  fw.addPlugin(new Plugin_4IN8OUT());        // 0x45  4 in / 8 out digital I/O
  fw.addPlugin(new Plugin_AIN420MA());       // 0x55  4-ch 4-20mA analog input
  fw.addPlugin(new Plugin_PPS());            // 0x35  programmable power supply
  fw.addPlugin(new Plugin_HMI());            // 0x41  HMI encoder + buttons
  //  Fan Module needs the "M5Module-Fan" library (see #include above):
  // fw.addPlugin(new Plugin_FAN());          // 0x18  PWM fan speed + RPM
#endif  // ENABLE_STACKABLE_MODULES

  // ── Non-I2C pin devices (optional) ───────────────────────
  //  Uncomment the lines for whatever you have wired and set the
  //  pin numbers.  Unlike I2C plugins these are NOT auto-detected
  //  — the device is bound to the exact pin(s) you name here, so a
  //  wrong pin just yields a card of meaningless values.
  //
  //  Pin rules:
  //    • Avoid GPIO 21/22 (Port-A I2C) and the LCD / SD SPI pins.
  //    • Analog units (Light, Earth) MUST use an ADC1 pin — GPIO
  //      32-39 — because ADC2 pins fail while WiFi is on.  36 and
  //      39 are input-only ADC1 pins, ideal for sensors.
  //
  //  This whole block is gated by ENABLE_PIN_DEVICES (Config.h) —
  //  it must be 1 for an uncommented line below to take effect.
#if ENABLE_PIN_DEVICES
  // fw.addPlugin(new PinDevice_PIR(36));            // motion in
  // fw.addPlugin(new PinDevice_Relay(9));          // relay / SSR / flashlight / laser emitter — any on/off output
  // fw.addPlugin(new PinDevice_Buzzer(26));         // PWM buzzer
  // fw.addPlugin(new PinDevice_Servo(26));          // PWM servo
  // fw.addPlugin(new PinDevice_Light(8));          // CdS  — ADC1 pin
  // fw.addPlugin(new PinDevice_Angle(8));          // rotary pot — ADC1 pin
  // fw.addPlugin(new PinDevice_Earth(8, 9));      // soil — ADC1 + GPIO
  // fw.addPlugin(new PinDevice_Mic(8));            // mic  — ADC1 pin
  // fw.addPlugin(new PinDevice_ECG(36, 26, 25));    // ECG (AD8232): out, LO+, LO-
  // fw.addPlugin(new PinDevice_Button(36));         // momentary button — also a Laser Rx
  // fw.addPlugin(new PinDevice_Motor(26));          // DC motor / vibration / mini fan (PWM)
  // fw.addPlugin(new PinDevice_Watering(36, 26));   // Watering: moisture ADC + pump
  // fw.addPlugin(new PinDevice_MQ(36, PinDevice_MQ::MQ2));  // MQ-series gas sensor — AOUT on an ADC1 pin
  // fw.addPlugin(new PinDevice_Cotech(26));         // RX data pin

  //  These two also need their #include uncommented above:
  // fw.addPlugin(new PinDevice_DS18B20(26));        // 1-Wire temp
  // fw.addPlugin(new PinDevice_IR(36, 26));         // IR rx,tx

  // ── Port-B units (optional — register at most ONE) ───────
  //  Port B is a SINGLE connector (Yellow=GPIO26, White=GPIO36),
  //  so uncomment AT MOST ONE line below — they all share the same
  //  two pins.  Default pins are baked in; the framework trusts the
  //  registration, so the one you uncomment must match whatever is
  //  physically plugged into Port B.
  // fw.addPlugin(new PinDevice_Hall(8));             // hall magnet switch — G36
  // fw.addPlugin(new PinDevice_Limit(8));            // mechanical limit switch — G36
  // fw.addPlugin(new PinDevice_OP180(8));            // IR break-beam switch — G36
  // fw.addPlugin(new PinDevice_DualButton(9,8));       // two push-buttons — G26 + G36
  // fw.addPlugin(new PinDevice_TubePressure(8));     // gas pressure gauge — G36 (ADC)
  // fw.addPlugin(new PinDevice_Grove2Grove(8,9));      // switched 5V out + current — G26 + G36
#endif  // ENABLE_PIN_DEVICES

  // ── UART device (optional — register at most ONE) ────────
  //  A UART device needs a HardwareSerial port — use Serial2 for
  //  Port-C.  The Port-C RX/TX pins are auto-selected for the
  //  detected board (Core1 16/17, Core2 & Tough 13/14, CoreS3
  //  18/17), so no pins are passed below; add explicit pins as the
  //  3rd/4th argument only to override.  Only one UART device can
  //  be active — they all share that single port.
  //  ⚠ On an M5Stack Fire, Port-C (GPIO 16/17) is wired to PSRAM —
  //    do not register a Port-C UART device on a Fire.
  //  This block is gated by ENABLE_UART_DEVICES (Config.h) — it
  //  must be 1 for an uncommented line below to take effect.
#if ENABLE_UART_DEVICES
  // fw.addPlugin(new UartDevice_Barcode(Serial2, 9600));
  // fw.addPlugin(new UartDevice_Modem(Serial2, 115200));   // CatM / 4G — SIM7080G / SIM7600G / SIM7020G
  // fw.addPlugin(new UartDevice_PMSA003(Serial2, 9600));   // PM2.5 air-quality sensor
  // fw.addPlugin(new UartDevice_ASR(Serial2, 115200));     // CI1302 offline voice
  //  These also need their #include uncommented above:
  // fw.addPlugin(new UartDevice_GPS(Serial2, 9600));
//  fw.addPlugin(new UartDevice_ModuleLLM(Serial2, 115200));  // offline AI text chat
#endif  // ENABLE_UART_DEVICES

  fw.begin();
}

void loop() {
  fw.update();
}
