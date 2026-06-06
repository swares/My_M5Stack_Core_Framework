#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── LoRa P2P radio (UartDevice_LoRaWAN) ───────────────────────
//  Bench-test parameters for the M5 LoRaWAN Unit US915 (RAK3172) in
//  raw LoRa P2P mode.  Macros so the plugin's #ifndef defaults defer
//  to these.  ⚠ EVERY value must MATCH the peer radio (e.g. the SX1262
//  on the Cardputer) or the link is silent with no error.
//  Frequency: keep within the US915 (902–928) ∩ SX1262 (868–923) =
//  ~902–923 MHz overlap; 915.0 MHz is safe, don't exceed 923 MHz.
#define LORA_P2P_FREQ_HZ  915000000UL  // 915.0 MHz
#define LORA_P2P_SF       7            // spreading factor 7..12
#define LORA_P2P_BW       125          // bandwidth kHz (125/250/500)
#define LORA_P2P_CR       0            // coding rate 0=4/5 1=4/6 2=4/7 3=4/8
#define LORA_P2P_PREAMBLE 8            // preamble symbols
#define LORA_P2P_TX_POWER 14           // TX power dBm (respect local limits)
