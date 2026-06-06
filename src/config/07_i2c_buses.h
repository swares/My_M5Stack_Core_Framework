#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

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
