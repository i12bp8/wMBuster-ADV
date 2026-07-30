// wM-Buster ADV — Persistent Configuration Store
// Backed by ESP32 NVS via Arduino Preferences API (no extra partition needed).
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define CONFIG_MAX_METERS 50

namespace wmb {

// ── Per-meter config ────────────────────────────────────────────────────────
struct MeterConfig {
    char    id[9];       // 8-digit meter ID
    char    name[32];    // user-defined name  ("Kitchen Water")
    uint8_t key[16];     // AES-128 key; all-zero = not configured
    bool    has_key;     // true when key[] is meaningful
    char    driver[32];  // driver override, or "auto"
};

// ── Device settings ─────────────────────────────────────────────────────────
struct DeviceSettings {
    uint8_t radio_hardware;   // 0 = LoRa Cap (SX1262), 1 = Hydra RF (CC1101)
    char    radio_mode[8];    // "CT" = C1/T1 868.95 MHz  |  "S1" = S1 868.30 MHz | "LoRa"
    char    mqtt_host[64];
    uint16_t mqtt_port;
    char    mqtt_user[32];
    char    mqtt_pass[32];
    char    wifi_ssid[32];    // optional STA SSID (join home network for MQTT)
    char    wifi_pass[32];
    bool    ha_discovery;     // publish HA auto-discovery config

    // UI and System Settings
    char    webui_ap_pass[16];
    bool    webui_enabled;
    uint8_t webui_mode;       // 0: AP+STA (default), 1: AP Only, 2: STA Only
    char    ntfy_url[64];     // ntfy.sh topic URL for push notifications
    int     theme_idx;
    bool    sd_logging;
    bool    mute;
    bool    charge_mode;
};

extern DeviceSettings g_settings;

// ── API ─────────────────────────────────────────────────────────────────────

// Call once in setup().  Always succeeds on ESP32 (NVS is always mounted).
void cs_init();

// Meter config ---------------------------------------------------------------
bool   cs_save_meter(const MeterConfig& mc);
void   cs_import_keys_from_sd();
bool   cs_delete_meter(const char* id);
bool   cs_find_meter(const char* id, MeterConfig* out);
size_t cs_get_all_meters(MeterConfig* out, size_t max_out);

// Device settings ------------------------------------------------------------
void cs_save_settings(const DeviceSettings& s);
void cs_load_settings(DeviceSettings* s);

// Helpers --------------------------------------------------------------------
// Convert 32-char hex string → 16-byte key.  Returns false on bad input.
bool cs_hex_to_key(const char* hex, uint8_t* out16);
// Convert 16-byte key → 32-char hex string (null-terminated).
void cs_key_to_hex(const uint8_t* key16, char* out33);

} // namespace wmb
