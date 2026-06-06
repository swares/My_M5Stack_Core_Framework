#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Security posture ──────────────────────────────────────────
//  Credentials now live in Secrets.h (git-ignored); see that file.
//
//  SECURITY_STRICT controls what happens at boot when a credential
//  is still at its guessable shipped default (dashboard login
//  user / password):
//    true  → REFUSE TO BOOT.  A full-screen red warning is shown on
//            the LCD and printed to serial, and the framework halts
//            before bringing up WiFi / the web server.  Forces you
//            to set real credentials in Secrets.h.  Recommended for
//            anything that leaves the bench.
//    false → boot anyway, but print a loud one-time warning on the
//            LCD + serial.  Convenient while developing.
//
//  Note: leaving WEB_AUTH_USER empty ("") in Secrets.h is a
//  DELIBERATE "auth disabled" choice, not a weak default — it does
//  not trip the strict halt (it only logs an informational note).
#define SECURITY_STRICT true

//  FACTORY_RESET_HOLD_DISABLED controls the hardware escape hatch:
//  touching the screen (CoreS3 / Core2) or holding any of BtnA/B/C
//  (Core1 / Core2) during a short window at power-on (see
//  FACTORY_RESET_WINDOW_MS) wipes the saved NVS settings and reboots
//  into the setup portal.
//    false → hold-to-reset is ENABLED (the default — a guaranteed
//            recovery path if the network or passwords are wrong).
//    true  → hold-to-reset is DISABLED.  Use on a deployed/kiosk
//            unit where you don't want a bystander able to wipe its
//            settings from the front panel.  (NVS can still be
//            cleared by reflashing with flash-erase enabled.)
#define FACTORY_RESET_HOLD_DISABLED false

//  How long (ms) the boot factory-reset window stays open watching
//  for a touch / button press.  The press is caught the instant it's
//  seen (boot continues immediately once detected), so this is the
//  MAXIMUM added boot delay when no one is pressing.  Bigger = more
//  forgiving timing for the finger-hold; smaller = faster normal
//  boot.  Ignored when FACTORY_RESET_HOLD_DISABLED is true.
[[maybe_unused]] constexpr unsigned long FACTORY_RESET_WINDOW_MS = 2500;

//  CAPTIVE_DNS_ENABLED runs a tiny DNS responder in setup mode that
//  points every hostname at the device, so a phone auto-opens the setup
//  portal (like a public-WiFi login).  It uses WiFiUDP (the lwIP
//  SOCKETS layer — socket/bind/recvfrom), which is thread-safe; the
//  bundled DNSServer and AsyncUDP both call raw udp_new and trip a
//  fatal "Required to lock TCPIP core functionality" assert on this
//  ESP-IDF build.  Default true.  Set false to skip the auto-open; the
//  portal is still reachable by browsing to the AP IP (http://192.168.4.1/).
#define CAPTIVE_DNS_ENABLED true

//  Hardware watchdog (Task WDT).  When true, the main loop task is
//  registered with the ESP32 Task Watchdog after setup() finishes and
//  fed once per loop() iteration; if loop() ever wedges for longer than
//  WDT_TIMEOUT_S the chip panics and reboots, so a frozen device
//  recovers on its own.  The watchdog is added AFTER begin() so the
//  slow boot work (Module-LLM model load, TLS/SD init) can't trip it.
//    true  → auto-reboot on a hung loop().  Recommended for deployed
//            units.
//    false → no software-managed watchdog (default — unchanged
//            behaviour).
//  Pick WDT_TIMEOUT_S comfortably above the longest legitimate single
//  loop() pass.  The only bounded in-loop blocker is the AlertManager
//  webhook/email POST (~8 s), so 20 s leaves margin; don't go below ~12.
[[maybe_unused]] constexpr bool          WDT_ENABLE    = false;
[[maybe_unused]] constexpr unsigned long WDT_TIMEOUT_S = 20;
