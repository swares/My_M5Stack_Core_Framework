// ============================================================
//  BoardInfo.cpp  –  Resolve the host board once at startup.
// ============================================================
#include "BoardInfo.h"
#include "Config.h"

const BoardInfo& BoardInfo::detect() {
  static BoardInfo info;
  static bool      resolved = false;
  if (resolved) return info;

  // ── 1. Honour compile-time override (if set in Config.h) ────
#if defined(FORCE_BOARD_CORES3)
  info.kind = BoardKind::CoreS3;
#elif defined(FORCE_BOARD_CORE2)
  info.kind = BoardKind::Core2;
#elif defined(FORCE_BOARD_CORE1)
  info.kind = BoardKind::Core1;
#elif defined(FORCE_BOARD_TOUGH)
  info.kind = BoardKind::Tough;
#else
  // ── 2. Otherwise ask M5Unified what we're running on ────────
  switch (M5.getBoard()) {
    case m5::board_t::board_M5StackCoreS3:
      info.kind = BoardKind::CoreS3;
      break;
    case m5::board_t::board_M5StackCore2:
      info.kind = BoardKind::Core2;
      break;
    case m5::board_t::board_M5Tough:
      // The Tough is a Core2-family industrial board; M5Unified gives
      // it its own enum.  Its I2C / SD / Port-C pin map matches the
      // Core2, so it gets a dedicated entry mainly for the name.
      info.kind = BoardKind::Tough;
      break;
    case m5::board_t::board_M5Stack:
      // M5Unified reports board_M5Stack for both the Basic and the
      // Gray (the Gray adds an MPU9250; the Basic has no IMU).
      // We treat them both as "Core1" — Plugin_IMU's M5.Imu wrapper
      // will simply report disabled on the Basic and skip cleanly.
      info.kind = BoardKind::Core1;
      break;
    default:
      info.kind = BoardKind::Unknown;
      break;
  }
#endif

  // ── 3. Fill in per-board defaults ───────────────────────────
  switch (info.kind) {
    case BoardKind::CoreS3:
      info.shortName  = "CoreS3";
      info.longName   = "M5Stack CoreS3";
      info.i2cIntSda  = 12;  info.i2cIntScl  = 11;
      info.i2cExtSda  =  2;  info.i2cExtScl  =  1;
      info.portCRx    = 18;  info.portCTx    = 17;
      // ⚠ On the ESP32-S3, M5Unified runs the CoreS3's internal
      // system I2C on the SECOND I2C peripheral — Arduino Wire1 —
      // and leaves Wire for Port-A.  intBus must therefore be Wire1:
      // if the framework drove Wire onto the internal pins (12/11)
      // it would collide with M5Unified's Wire1 already there, kill
      // the bus, and every built-in chip would vanish from the scan.
      // (Core1 / Core2 are classic ESP32 — M5Unified uses Wire for
      // their internal bus, which is why they need no such swap.)
      info.intBus     = &Wire1;
      info.extBus     = &Wire;
      info.imuName    = "BMI270 + BMM150";
      info.pmicName   = "AXP2101";
      info.hasI2cPmic = true;
      info.sdMosi     = 37; info.sdMiso = 35; info.sdSck = 36; info.sdCs = 4;
      info.hasSdCard  = true;
      // CoreS3 built-in internal-bus chips that have no plugin —
      // reserved so module plugins can't false-bind to them:
      //   0x21 GC0308 camera    0x23 LTR-553 ALS / proximity
      //   0x36 AW88298 audio    0x38 FT6x36 touch
      //   0x40 ES7210 mic ADC   0x58 AW9523 GPIO expander
      // (0x34 AXP2101, 0x51 BM8563, 0x69 BMI270 have plugins.)
      info.reservedAddr[0] = 0x21; info.reservedAddr[1] = 0x23;
      info.reservedAddr[2] = 0x36; info.reservedAddr[3] = 0x38;
      info.reservedAddr[4] = 0x40; info.reservedAddr[5] = 0x58;
      info.reservedCount   = 6;
      break;

    case BoardKind::Core2:
      info.shortName  = "Core2";
      info.longName   = "M5Stack Core2";
      info.i2cIntSda  = 21;  info.i2cIntScl  = 22;
      info.i2cExtSda  = 32;  info.i2cExtScl  = 33;
      info.portCRx    = 13;  info.portCTx    = 14;
      info.intBus     = &Wire;
      info.extBus     = &Wire1;
      info.imuName    = "MPU6886";
      info.pmicName   = "AXP192";
      info.hasI2cPmic = true;
      info.sdMosi     = 23; info.sdMiso = 38; info.sdSck = 18; info.sdCs = 4;
      info.hasSdCard  = true;
      // FT6336U touch controller (0x38) — built-in, no plugin.
      // (The Core2 v1.1's INA3221 at 0x40 is NOT reserved — it is
      //  claimed by Plugin_INA3221, which positively IDs the chip.)
      info.reservedAddr[0] = 0x38; info.reservedCount = 1;
      break;

    case BoardKind::Core1:
      // Original M5Stack Basic / Gray.  Port-A and the on-board
      // chips share the same SDA=21 / SCL=22 pair, so the extBus
      // pointer aliases intBus — there is only one physical bus.
      // Battery is managed by an IP5306 which isn't I2C-accessible
      // in a useful way; no on-board RTC; Gray has an MPU9250 (M5.Imu
      // handles transparently), Basic has no IMU at all.
      info.shortName  = "Core";
      info.longName   = "M5Stack Core (Basic / Gray)";
      info.i2cIntSda  = 21;  info.i2cIntScl  = 22;
      info.i2cExtSda  = 21;  info.i2cExtScl  = 22;
      info.portCRx    = 16;  info.portCTx    = 17;
      info.intBus     = &Wire;
      info.extBus     = &Wire;        // shared with internal
      info.imuName    = "MPU9250";    // Gray; Basic shows as disabled
      info.pmicName   = "IP5306";
      info.hasI2cPmic = false;        // not usefully I2C-readable
      info.sdMosi     = 23; info.sdMiso = 19; info.sdSck = 18; info.sdCs = 4;
      info.hasSdCard  = true;
      break;

    case BoardKind::Tough:
      // M5Stack Tough — dustproof industrial controller in the Core2
      // family.  I2C, SD and Port-C pin maps are identical to the
      // Core2; it carries no on-board IMU and uses an AXP192 PMU.
      info.shortName  = "Tough";
      info.longName   = "M5Stack Tough";
      info.i2cIntSda  = 21;  info.i2cIntScl  = 22;
      info.i2cExtSda  = 32;  info.i2cExtScl  = 33;
      info.portCRx    = 13;  info.portCTx    = 14;
      info.intBus     = &Wire;
      info.extBus     = &Wire1;
      info.imuName    = "(none)";
      info.pmicName   = "AXP192";
      info.hasI2cPmic = true;
      info.sdMosi     = 23; info.sdMiso = 38; info.sdSck = 18; info.sdCs = 4;
      info.hasSdCard  = true;
      // Capacitive touch controller — built-in, no plugin.
      info.reservedAddr[0] = 0x38; info.reservedCount = 1;
      break;

    default:
      // Unknown — assume Core2 layout (most common) and warn.
      info.shortName  = "Unknown";
      info.longName   = "M5Stack (unknown)";
      info.i2cIntSda  = 21;  info.i2cIntScl  = 22;
      info.i2cExtSda  = 32;  info.i2cExtScl  = 33;
      info.portCRx    = 13;  info.portCTx    = 14;
      info.intBus     = &Wire;
      info.extBus     = &Wire1;
      info.imuName    = "IMU";
      info.pmicName   = "PMIC";
      info.hasI2cPmic = true;
      // Core2-layout fallback — reserve the touch controller.
      info.reservedAddr[0] = 0x38; info.reservedCount = 1;
      Serial.println(F("[BoardInfo] WARNING: board not recognised, "
                       "falling back to Core2 pin layout."));
      break;
  }

  // ── 4. Allow Config.h to override individual pins ───────────
#ifdef I2C_INT_SDA_OVERRIDE
  info.i2cIntSda = I2C_INT_SDA_OVERRIDE;
#endif
#ifdef I2C_INT_SCL_OVERRIDE
  info.i2cIntScl = I2C_INT_SCL_OVERRIDE;
#endif
#ifdef I2C_EXT_SDA_OVERRIDE
  info.i2cExtSda = I2C_EXT_SDA_OVERRIDE;
#endif
#ifdef I2C_EXT_SCL_OVERRIDE
  info.i2cExtScl = I2C_EXT_SCL_OVERRIDE;
#endif

  resolved = true;
  return info;
}
