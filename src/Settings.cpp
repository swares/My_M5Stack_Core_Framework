// ============================================================
//  Settings.cpp  –  NVS-backed runtime settings (approach B, core)
// ============================================================
#include "Settings.h"
#include <Preferences.h>
#include <cstring>    // strlen
#include "Config.h"   // compiled Secrets.h fallbacks

namespace {
Preferences g_prefs;
bool   g_open = false;

// RAM cache of the NVS values (empty string = "not set in NVS").
String c_ssid, c_wpass, c_user, c_pass;
String c_mqttHost, c_mqttUser, c_mqttPass, c_claude;
String c_alertRules;   // serialized alarm-rule set (empty = use Config seed)
uint16_t c_mqttPort = 0;   // 0 = not set in NVS → use compiled default
bool   c_prov = false;
bool   c_apOnly = false;      // run as standalone AP serving the dashboard
                              // (no upstream Wi-Fi) — set via the portal
bool   c_forceSetup = false;  // set by factoryReset() → force the portal
                              // on next boot, even if Secrets.h has WiFi

// NVS override if present, otherwise the compiled fallback.
String pick(const String& nvsVal, const char* fallback) {
  return nvsVal.length() ? nvsVal : String(fallback);
}
}  // namespace

void Settings::begin() {
  // namespace "cfg"; Security uses "sec", SDLogger its own — no clash.
  g_open = g_prefs.begin("cfg", /*readOnly=*/false);
  if (!g_open) {
    Serial.println(F("[Settings] NVS open failed — compiled defaults only"));
    return;
  }
  c_ssid     = g_prefs.getString("wifi_ssid", "");
  c_wpass    = g_prefs.getString("wifi_pass", "");
  c_user     = g_prefs.getString("web_user",  "");
  c_pass     = g_prefs.getString("web_pass",  "");
  c_mqttHost = g_prefs.getString("mqtt_host", "");
  c_mqttPort = g_prefs.getUShort("mqtt_port", 0);
  c_mqttUser = g_prefs.getString("mqtt_user", "");
  c_mqttPass = g_prefs.getString("mqtt_pass", "");
  c_claude   = g_prefs.getString("claude_key", "");
  c_prov     = g_prefs.getBool  ("prov", false);
  c_apOnly   = g_prefs.getBool  ("ap_only", false);
  c_forceSetup = g_prefs.getBool("force_setup", false);
  c_alertRules = g_prefs.getString("alert_rules", "");
  Serial.printf("[Settings] NVS loaded (provisioned=%s, wifi=%s)\n",
                isProvisioned() ? "yes" : "no",
                wifiSsid().length() ? "set" : "unset");
}

bool Settings::isProvisioned() {
  // An explicit factory reset forces the setup portal on the next
  // boot — even if Secrets.h bakes in a WiFi SSID — until the user
  // re-provisions (markProvisioned clears the flag).
  if (c_forceSetup) return false;
  // Otherwise: provisioned via the portal (NVS), or a baked-in WiFi
  // SSID means the device was configured at build time.  Standalone-AP
  // mode counts as provisioned too — it has no SSID by design.
  return c_prov || c_apOnly || c_ssid.length() > 0 || strlen(WIFI_SSID) > 0;
}

bool Settings::apOnlyMode() { return c_apOnly; }

String Settings::wifiSsid() { return pick(c_ssid,  WIFI_SSID); }
String Settings::wifiPass() { return pick(c_wpass, WIFI_PASSWORD); }
String Settings::webUser()  { return pick(c_user,  WEB_AUTH_USER); }
String Settings::webPass()  { return pick(c_pass,  WEB_AUTH_PASS); }
String   Settings::mqttHost() { return pick(c_mqttHost, MQTT_HOST); }
uint16_t Settings::mqttPort() { return c_mqttPort ? c_mqttPort : (uint16_t)MQTT_PORT; }
String Settings::mqttUser() { return pick(c_mqttUser, MQTT_USER); }
String Settings::mqttPass() { return pick(c_mqttPass, MQTT_PASS); }
String Settings::claudeKey(){ return pick(c_claude,   CLAUDE_API_KEY); }

bool Settings::mqttConfigured()   { return mqttUser().length() > 0; }
bool Settings::claudeConfigured() { return claudeKey().length() > 0; }

void Settings::setWifi(const String& ssid, const String& pass) {
  c_ssid  = ssid;
  c_wpass = pass;
  if (g_open) {
    g_prefs.putString("wifi_ssid", ssid);
    g_prefs.putString("wifi_pass", pass);
  }
}

void Settings::setApOnly(bool on) {
  c_apOnly = on;
  if (g_open) g_prefs.putBool("ap_only", on);
}

void Settings::setWebLogin(const String& user, const String& pass) {
  c_user = user;
  c_pass = pass;
  if (g_open) {
    g_prefs.putString("web_user", user);
    g_prefs.putString("web_pass", pass);
  }
}

void Settings::setMqttServer(const String& host, uint16_t port) {
  c_mqttHost = host;
  c_mqttPort = port;
  if (g_open) {
    g_prefs.putString("mqtt_host", host);
    g_prefs.putUShort("mqtt_port", port);
  }
}

void Settings::setMqtt(const String& user, const String& pass) {
  c_mqttUser = user;
  c_mqttPass = pass;
  if (g_open) {
    g_prefs.putString("mqtt_user", user);
    g_prefs.putString("mqtt_pass", pass);
  }
}

void Settings::setClaudeKey(const String& key) {
  c_claude = key;
  if (g_open) g_prefs.putString("claude_key", key);
}

// ── Alarm rules (serialized JSON blob; "" = use the Config seed) ──
String Settings::alertRules() { return c_alertRules; }
bool   Settings::alertRulesSet() { return c_alertRules.length() > 0; }
void Settings::setAlertRules(const String& json) {
  c_alertRules = json;
  if (g_open) g_prefs.putString("alert_rules", json);
}

void Settings::markProvisioned() {
  c_prov = true;
  c_forceSetup = false;   // clear any pending forced-portal request
  if (g_open) {
    g_prefs.putBool("prov", true);
    g_prefs.putBool("force_setup", false);
  }
}

bool Settings::hasStoredCredentials() {
  return c_prov || c_ssid.length() || c_wpass.length() ||
         c_user.length() || c_pass.length();
}

void Settings::factoryReset() {
  c_ssid = c_wpass = c_user = c_pass = "";
  c_mqttHost = c_mqttUser = c_mqttPass = c_claude = "";
  c_mqttPort = 0;
  c_prov = false;
  c_apOnly = false;
  c_forceSetup = true;   // force the setup portal on the next boot
  if (g_open) {
    g_prefs.clear();                       // wipe every key in "cfg"
    g_prefs.putBool("force_setup", true);  // ...then re-arm the portal
  }
}
