#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

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
