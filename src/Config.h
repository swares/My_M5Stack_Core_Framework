#pragma once
// ============================================================
//  Config.h  –  User-editable settings for the I2C Framework
//
//  This framework auto-detects the host board (M5Stack CoreS3
//  or M5Stack Core2) at runtime and picks the correct I2C pins
//  automatically.  See BoardInfo.h / BoardInfo.cpp.
// ============================================================

// ── Configuration is split into ordered section headers ──────
//  Each #include below is one former section of this file, in
//  the SAME order — concatenating them is identical to the old
//  single Config.h, so every ordering dependency (Secrets.h
//  last, MQTT_TLS before the cert blocks) is preserved.
#include "config/01_wifi.h"
#include "config/02_standalone_access_point_used_only_when_wifi_ssid_is.h"
#include "config/03_time_ntp.h"
#include "config/04_web_api_https.h"
#include "config/05_security_posture.h"
#include "config/06_serial.h"
#include "config/07_i2c_buses.h"
#include "config/08_output_channel_build_switches.h"
#include "config/09_alarm_seed_rules_compiled_defaults.h"
#include "config/10_device_category_build_switches.h"
#include "config/11_sd_card_csv_logging.h"
#include "config/12_mqtt.h"
#include "config/13_transport_plain_mqtt_vs_mqtts_tls.h"
#include "config/14_tls_certificates_for_mqtts_mqtt_tls_true.h"
#include "config/15_home_assistant_mqtt_discovery.h"
#include "config/16_heart_rate_sensor_debug.h"
#include "config/17_display_layout.h"
#include "config/18_sensor_poll_interval.h"
#include "config/19_periodic_re_scan.h"
#include "config/20_claude_escalation_local_cloud_router_netdevice_router.h"
#include "config/21_optional_llm_yes_no_tiebreaker_the_02_model_judged_middle.h"
#include "config/22_optional_3rd_route_direct_claude_api_from_the_router.h"
#include "config/23_claude_direct_api_netdevice_claudeapi.h"
#include "config/24_credentials.h"
#include "config/25_claude_conversation_memory_netdevice_claudeapi_history.h"
#include "config/26_lora_p2p_radio_uartdevice_lorawan.h"
