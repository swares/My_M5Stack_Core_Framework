#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── WiFi ─────────────────────────────────────────────────────
//  Normal mode: the device joins WIFI_SSID as a station (STA).
//  ⚠ Leave WIFI_SSID empty ("") and the device instead brings up
//  its OWN Wi-Fi access point (the AP block below) rather than
//  joining a network — for a standalone unit with no network to
//  join.  Connect a phone/laptop to that AP and open the
//  dashboard at the AP's IP.
//  ⚠ WIFI_SSID and WIFI_PASSWORD are now defined in Secrets.h
//  (git-ignored).  This file no longer carries any credential.
// give up after this many ms
[[maybe_unused]] constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
