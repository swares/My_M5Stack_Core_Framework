#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

//  ── Transport: plain MQTT vs MQTTS (TLS) ─────────────────────
//  MQTT_TLS selects how the framework reaches the broker:
//
//    MQTT_TLS = false → plain MQTT over TCP (Mosquitto, the Home
//                       Assistant broker, etc).  No encryption.
//                       The framework's original behaviour.  Pair
//                       it with MQTT_PORT 1883.
//
//    MQTT_TLS = true  → MQTTS: the whole connection is wrapped in
//                       TLS, so everything on the wire is encrypted.
//                       Pair it with MQTT_PORT 8883 (standard MQTTS).
//                       Two further switches shape the handshake:
//
//    MQTT_TLS_MUTUAL — how the DEVICE proves its identity:
//        false → username / password over TLS.  Standard for cloud
//                brokers (HiveMQ Cloud, EMQX Cloud) and a local
//                Mosquitto with TLS.  Set MQTT_USER / MQTT_PASS
//                below — they travel encrypted inside the tunnel.
//        true  → mutual TLS.  The device presents an X.509 client
//                certificate and sends no username / password (the
//                cert IS the credential — leave MQTT_USER / PASS
//                empty).  This is the mode AWS IoT Core requires.
//
//    MQTT_TLS_INSECURE — whether the device verifies the BROKER:
//        false → verify the broker against MQTT_CA_CERT (below).
//                Rejects an impostor broker.  Recommended for
//                anything reachable over an untrusted network.
//        true  → skip verification (setInsecure): traffic is still
//                encrypted but the broker identity is NOT checked.
//                Convenient for a local broker with a self-signed
//                cert; do not use across an untrusted network.
//
//  Transport is a compile-time choice: set the switches, point
//  MQTT_HOST / MQTT_PORT at the broker, rebuild, flash.  Everything
//  downstream (per-plugin JSON publishes, the LWT status topic,
//  /api/mqtt) behaves identically across all transports.
//
//  TLS checks the broker certificate's validity dates, so the
//  device clock must be roughly right.  The framework syncs NTP at
//  boot before MQTT connects; if NTP fails the handshake may be
//  rejected with a date error (unless MQTT_TLS_INSECURE skips it).
#define MQTT_TLS false  // false = plain MQTT/TCP, true = MQTTS/TLS
#define MQTT_TLS_MUTUAL \
  false  // MQTTS only: false = user/pass over TLS,
         //             true  = X.509 client cert
#define MQTT_TLS_INSECURE \
  false  // MQTTS only: false = verify broker against
         //             MQTT_CA_CERT, true = skip check
// plain: broker IP/hostname.  MQTTS: the broker hostname (should
// match its cert when MQTT_TLS_INSECURE is false).
[[maybe_unused]] constexpr char MQTT_HOST[] = "";
// plain MQTT 1883; MQTTS 8883
[[maybe_unused]] constexpr unsigned MQTT_PORT = 1883;
// ⚠ MQTT_USER / MQTT_PASS (empty = anonymous / cert-only) are
// defined in Secrets.h (git-ignored).
// must be unique per device on the broker.  On AWS IoT this should
// match the Thing name — some policies gate the connection on
// clientId == thing-name.
[[maybe_unused]] constexpr char MQTT_CLIENT_ID[] = "m5stack-i2c";
// topic prefix; no trailing slash
[[maybe_unused]] constexpr char MQTT_BASE_TOPIC[] = "m5stack";
// ms between full sensor publishes
[[maybe_unused]] constexpr unsigned long MQTT_PUBLISH_MS = 5000;
// retain readings so late subscribers get the last value.  ⚠ On AWS
// IoT Core a retained publish needs iot:RetainPublish in the Thing's
// policy — if it only grants iot:Publish, set this false or AWS drops
// the connection on the first publish.
#define MQTT_RETAIN true
// seconds; broker drops us after ~1.5×.  AWS IoT Core accepts 30-1200 s.
[[maybe_unused]] constexpr unsigned MQTT_KEEPALIVE = 30;
