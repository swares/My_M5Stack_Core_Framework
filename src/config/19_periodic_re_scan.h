#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Periodic re-scan ─────────────────────────────────────────
//  Hot-plugging I2C devices is not safe — the bus can glitch
//  mid-transaction, the chip you just plugged in may not be in a
//  defined state, and several chips (notably the MLX90614) react
//  badly to bare quick-command probes against a powered slave.
//  This framework therefore does NOT periodically rescan.  Plugins
//  are bound once at boot, and that's it.
//
//  If you need to re-bind after plugging something in, either:
//    - reboot the device, or
//    - hit GET /api/rescan via the web API (manual trigger that
//      runs the same scan/bind logic the boot does).
