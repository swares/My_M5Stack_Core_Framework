#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

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

//  RTC fallback: when NTP can't be reached (offline / standalone boot),
//  seed the system clock from the battery-backed hardware RTC (BM8563 on
//  Core2 / CoreS3 / Tough) if it holds a plausible, previously-set time.
//  This gives the SD log a real wall-clock "datetime" column and a
//  timestamped filename even with no Wi-Fi — the RTC was last set from
//  NTP on an earlier online boot.  The API reports the source as "rtc"
//  vs "ntp" so nothing implies NTP-grade accuracy.  Boards with no RTC
//  chip (Core1) just keep uptime-only timestamps.  Default true; set
//  false to require NTP for any wall-clock at all.
[[maybe_unused]] constexpr bool RTC_TIME_FALLBACK = true;
