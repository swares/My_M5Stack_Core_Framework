#pragma once
// ============================================================
//  Settings.h  –  Runtime settings store (security approach B, core)
//
//  NVS-backed runtime credentials with the compiled Secrets.h values
//  as fallback (the HYBRID model):
//
//      effective = NVS[key]   if set    else   Secrets.h default
//
//  This lets a factory-fresh unit be provisioned from the first-boot
//  setup portal — over its own Wi-Fi access point, no reflash — while
//  a unit that has credentials baked into Secrets.h keeps working
//  exactly as before.  Approach A (Secrets.h + boot guard) still
//  applies; B layers a runtime override on top of it.
//
//  Scope (core pass): Wi-Fi SSID/password and the dashboard login.
//  MQTT / Claude / router secrets still read their compiled Secrets.h
//  values; moving those into the portal is a later (full-B) step.
// ============================================================
#include <Arduino.h>

namespace Settings {

// Open NVS and cache values in RAM.  Call once, early in begin(),
// before anything reads Wi-Fi or web-login settings.
void begin();

// True once the device has been provisioned through the setup portal
// OR Wi-Fi was baked into Secrets.h at compile time.  When false,
// Framework comes up as an access point and WebAPI serves the setup
// portal at "/" instead of the dashboard.
bool isProvisioned();

// True when the device has been deliberately provisioned to run as its
// own standalone Wi-Fi access point (no upstream network), serving the
// DASHBOARD — not the setup portal — at the AP IP (192.168.4.1).  This
// is what tells an empty stored SSID apart from "user hasn't set Wi-Fi
// yet": the latter shows the setup portal, the former shows the
// dashboard.
bool apOnlyMode();

// Effective values — NVS override first, then the Secrets.h fallback.
String wifiSsid();
String wifiPass();
String webUser();
String webPass();
String   mqttHost();
uint16_t mqttPort();
String mqttUser();
String mqttPass();
String claudeKey();

// "Configured?" helpers for the secret values — true when a non-empty
// value is in effect (NVS or compiled).  Used by the Settings page so
// it can show set/unset badges WITHOUT ever returning the secret.
bool mqttConfigured();
bool claudeConfigured();

// Persist new values to NVS.  These do NOT reboot — the caller
// decides when to restart.
void setWifi(const String& ssid, const String& pass);
// Persist the standalone-AP-dashboard choice (see apOnlyMode()).
void setApOnly(bool on);
void setWebLogin(const String& user, const String& pass);
void setMqttServer(const String& host, uint16_t port);
void setMqtt(const String& user, const String& pass);
void setClaudeKey(const String& key);

// Alarm-rule set, persisted as a serialized JSON blob.  Empty means
// "not overridden" → AlertManager falls back to the Config.h seed
// rules (same hybrid model as Wi-Fi/MQTT).
String alertRules();
bool   alertRulesSet();
void   setAlertRules(const String& json);

// Mark the device provisioned (persisted).  Called after a
// successful setup submit.
void markProvisioned();

// True if NVS currently holds any provisioning (the provisioned flag
// or any stored WiFi/login value).  Used to tell a portal-provisioned
// unit apart from one that only has compiled Secrets.h defaults.
bool hasStoredCredentials();

// Wipe every NVS setting in this store, returning the device to its
// compiled Secrets.h defaults (and hence to the unprovisioned setup
// portal on the next boot).  The recovery escape hatch.
void factoryReset();

}  // namespace Settings

