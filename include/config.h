// wM-Buster ADV — hardware & application configuration
// M5Stack Cardputer-Adv + Cap LoRa-1262 — GPL-3.0
#pragma once

#include <stdint.h>

// ---- SX1262 radio (Cap LoRa-1262) ----
#define PIN_LORA_NSS    5
#define PIN_LORA_IRQ    4
#define PIN_LORA_RESET  3
#define PIN_LORA_BUSY   6

// ---- CC1101 radio (Hydra RF) ----
#define PIN_CC1101_CS   13
#define PIN_CC1101_IRQ  5

// ---- Shared SPI bus (radio + microSD) ----
#define PIN_SPI_SCK     40
#define PIN_SPI_MOSI    14
#define PIN_SPI_MISO    39
#define PIN_SD_CS       12

// ---- GNSS (AT6668/ATGM336H) ----
#define PIN_GNSS_RX     15
#define PIN_GNSS_TX     13
#define GNSS_BAUD       115200

// ---- I2C (keyboard TCA8418, IMU BMI270, audio ES8311, RF switch PI4IOE) ----
#define PIN_I2C_SDA     8
#define PIN_I2C_SCL     9

// PI4IOE5V6408 expander: P0 must be driven HIGH before radio init or the
// RF path loses ~30 dB (Cap LoRa-1262 hardware note).
#define PI4IOE_I2C_ADDR  0x43
#define PI4IOE_REG_IO_DIR   0x03
#define PI4IOE_REG_OUTPUT   0x05
#define PI4IOE_PIN_RF_SW    0

// ---- Display ST7789V2 240x135 ----
#define PIN_DISP_RST    33
#define PIN_DISP_RS     34
#define PIN_DISP_DAT    35
#define PIN_DISP_SCK    36
#define PIN_DISP_CS     37
#define PIN_BL_ENABLE   38

// ---- Battery ----
#define PIN_BATT_ADC    10

// ---- wM-Bus PHY constants (EN 13757-4) ----
#define WMBUS_FREQ_CT        868.95f   // C1/T1 MHz
#define WMBUS_CHIPRATE_CT    100.0f    // kchip/s
#define WMBUS_FDEV_CT        50.0f     // kHz
#define WMBUS_RXBW_CT        312.0f    // kHz

#define WMBUS_FREQ_S         868.30f   // S1 MHz
#define WMBUS_CHIPRATE_S     32.768f
#define WMBUS_RXBW_S         156.2f

// Logical sync bytes (C-mode on-air; T-mode on-air is their 3-out-of-6 encoding)
static const uint8_t WMBUS_SYNC_CT[] = {0x54, 0x3D};
static const uint8_t WMBUS_SYNC_S[]  = {0x76, 0x96};
#define WMBUS_SYNC_CT_LEN   2
#define WMBUS_SYNC_S_LEN    2
#define WMBUS_PREAMBLE_CT   32
#define WMBUS_PREAMBLE_S    48

// ---- WiFi AP defaults (Phase 4) ----
#define WIFI_AP_SSID    "CardputerWMBus"
#define WIFI_AP_PASS    "wmbus1234"

// ---- Application limits (no-PSRAM budget) ----
#define WMBUS_MAX_FRAME_LEN     290   // largest sane L-field + margin
#define MAX_TELEGRAM_HISTORY    50

// ---- System statistics (shared between main, web, display) ----
// Guarded: these structs use stdint types already included above, but the
// extern g_stats declaration is only valid in embedded builds (not native tests).
struct WMBStats {
    uint32_t radio_rx_total;      // Total radio packets received (ISR count)
    uint32_t radio_rx_good;       // Good frames (CRC valid, driver matched)
    uint32_t radio_rx_bad;        // Bad frames (CRC failed or decode error)
    uint32_t radio_rx_encrypted;  // Frames that were encrypted (decrypt attempted)
    float    radio_rssi_live;     // Most recent packet RSSI dBm
    float    radio_snr_live;      // Most recent packet SNR dB
    uint32_t uptime_s;            // Device uptime in seconds
    double   gnss_lat;            // Latest GPS latitude
    double   gnss_lon;            // Latest GPS longitude
    bool     gnss_fix;            // True if GPS has a valid fix
    uint8_t  gnss_sats;           // Approximate satellite count (0 if unknown)
};

#ifndef NATIVE_TEST
// Forward declaration — defined in src/main.cpp inside namespace wmb.
namespace wmb { extern WMBStats g_stats; }
#endif
