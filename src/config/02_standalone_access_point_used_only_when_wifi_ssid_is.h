#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

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
//  ⚠ AP_PASSWORD is defined in Secrets.h (git-ignored).  If it is
//  left at the shipped default "m5stack-config" (or shorter than the
//  WPA2 8-char minimum), the framework generates a RANDOM per-device
//  AP password at first boot, persists it in NVS, and shows it on the
//  LCD + serial.  Set a real value in Secrets.h to use your own.
//
//  A generated AP password is shown ONLY on the boot that first mints
//  it (so a persisted secret isn't re-displayed at every power-up).
//  Set this true to surface it on EVERY boot instead — handy on a
//  headless unit you reconnect to often.
[[maybe_unused]] constexpr bool AP_SHOW_PASSWORD_EACH_BOOT = false;
