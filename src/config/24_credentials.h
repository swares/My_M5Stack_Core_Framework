#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Credentials ───────────────────────────────────────────────
//  All real secrets (WiFi, AP password, dashboard login, MQTT
//  user/pass + client cert/key, router bearer, Claude API key) live
//  in Secrets.h, which is git-ignored.  It is included LAST so the
//  MQTT_TLS / MQTT_TLS_MUTUAL macros above are defined before its
//  conditional cert blocks are evaluated.  Copy Secrets.h.example to
//  Secrets.h on a fresh checkout and fill in this device's values.
#include "../Secrets.h"
