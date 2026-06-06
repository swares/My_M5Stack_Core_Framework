#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── TLS certificates for MQTTS (MQTT_TLS = true) ──────────────
//  Paste the full PEM text — including the "-----BEGIN/END-----"
//  lines — between the R"EOF( ... )EOF" raw-string delimiters.
//  Each block is compiled in only when the switches above need it,
//  so fill in just the ones your setup uses:
//
//  MQTT_CA_CERT      — needed when MQTT_TLS_INSECURE = false.
//    The certificate the device checks the broker against:
//      • Public cloud broker → its CA / root certificate.  HiveMQ
//        Cloud / EMQX Cloud use the ISRG Root X1 (Let's Encrypt)
//        root; AWS IoT Core uses Amazon Root CA 1, available from
//        https://www.amazontrust.com/repository .
//      • Local Mosquitto with your own CA → that CA's certificate.
//
//  MQTT_CLIENT_CERT / MQTT_CLIENT_KEY
//                    — needed when MQTT_TLS_MUTUAL = true.
//    The device's own X.509 certificate and matching private key:
//      • AWS IoT Core: create a "Thing", generate a certificate,
//        and attach a policy granting at minimum iot:Connect and
//        iot:Publish (add iot:RetainPublish if MQTT_RETAIN is true,
//        or AWS drops the connection on the first retained publish;
//        iot:Subscribe / iot:Receive are NOT needed — this
//        framework is publish-only).  Set MQTT_HOST to the account
//        Device data endpoint and MQTT_CLIENT_ID to the Thing name.
//        Paste "xxxx-certificate.pem.crt" and "xxxx-private.pem.key".
//      • Self-managed broker with client-cert auth: the cert and
//        key you issued for this device from your own CA.
//
//  ⚠ SECRET: MQTT_CLIENT_KEY is a real credential — anyone with it
//  can impersonate the device.  Keep Config.h out of public version
//  control and rotate the certificate if it ever leaks.
//  ([[maybe_unused]] because Config.h is included by several source
//  files but only MQTTOut.cpp references these — the linker drops the
//  unused copies; the attribute just keeps the compiler quiet.)
#if MQTT_TLS

#if !MQTT_TLS_INSECURE
[[maybe_unused]] static const char MQTT_CA_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
PASTE THE BROKER'S CA / ROOT CERTIFICATE HERE
-----END CERTIFICATE-----
)EOF";
#endif  // !MQTT_TLS_INSECURE

#endif  // MQTT_TLS
//  ⚠ MQTT_CLIENT_CERT and MQTT_CLIENT_KEY (the device's own cert and
//  PRIVATE KEY, needed when MQTT_TLS_MUTUAL) are defined in Secrets.h
//  (git-ignored).  Only the public CA / root cert (MQTT_CA_CERT,
//  above) stays here, since it is not a secret.
