// wM-Buster ADV — SX1262 FSK Radio Hardware Interface Implementation
// GPL-3.0
#ifndef NATIVE_TEST

#include "radio_sx1262/wmbus_radio.h"
#include "config.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>

#define PI4IOE_REG_ID   0x01
#define PI4IOE_REG_HIZ  0x04

namespace wmb {

static SPIClass* radio_spi = nullptr;
static Module*   radio_module = nullptr;

static SX1262* radio_sx = nullptr;
static CC1101* radio_cc = nullptr;
static PhysicalLayer* radio_phy = nullptr;

static volatile bool isr_rx_flag = false;
static bool radio_is_ready = false;

static void IRAM_ATTR on_rx_isr() {
    isr_rx_flag = true;
}

static bool pi4ioe_write_reg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(PI4IOE_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool pi4ioe_read_reg(uint8_t reg, uint8_t* value) {
    Wire.beginTransmission(PI4IOE_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)PI4IOE_I2C_ADDR, (uint8_t)1) != 1) return false;
    *value = Wire.read();
    return true;
}

static bool enable_rf_switch() {
    Serial.println("[I2C] Scanning I2C bus (G8/G9)...");
    uint8_t found_addr = 0;
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C] Device found at 0x%02X\n", addr);
            if (addr == 0x43 || addr == 0x40 || addr == 0x41 || addr == 0x44) {
                found_addr = addr;
            }
        }
    }

    uint8_t target_i2c = found_addr ? found_addr : PI4IOE_I2C_ADDR;
    uint8_t id = 0;
    Wire.beginTransmission(target_i2c);
    Wire.write(PI4IOE_REG_ID);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(target_i2c, (uint8_t)1) == 1) {
        id = Wire.read();
        Serial.printf("[RF] PI4IOE5V6408 ID at 0x%02X: 0x%02X\n", target_i2c, id);
    } else {
        Serial.printf("[RF] WARNING: PI4IOE5V6408 RF expander not responding at 0x%02X\n", target_i2c);
        return false;
    }

    // Disable high-impedance mode on P0
    uint8_t hiz = 0;
    pi4ioe_read_reg(PI4IOE_REG_HIZ, &hiz);
    hiz &= ~(1 << PI4IOE_PIN_RF_SW);
    pi4ioe_write_reg(PI4IOE_REG_HIZ, hiz);

    // Set P0 as output (clear bit 0)
    uint8_t dir = 0xFF;
    pi4ioe_read_reg(PI4IOE_REG_IO_DIR, &dir);
    dir &= ~(1 << PI4IOE_PIN_RF_SW);
    pi4ioe_write_reg(PI4IOE_REG_IO_DIR, dir);

    // Drive P0 HIGH to enable RF antenna switch
    uint8_t out = 0;
    pi4ioe_read_reg(PI4IOE_REG_OUTPUT, &out);
    out |= (1 << PI4IOE_PIN_RF_SW);
    if (!pi4ioe_write_reg(PI4IOE_REG_OUTPUT, out)) {
        Serial.println("[RF] ERROR: Failed to drive PI4IOE P0 HIGH");
        return false;
    }

    Serial.println("[RF] Antenna switch enabled (PI4IOE P0 = HIGH)");
    return true;
}

int radio_init(int hardware_type) {
    Serial.println("[RF] Auto-detecting radio hardware...");

    // Deselect SD Card
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    // Start SPI bus
    radio_spi = &SPI;
    radio_spi->begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

    // PROBE 1: Safely probe CC1101 (Hydra RF) using CS=13. 
    // GPIO 13 is safe to drive on both boards.
    pinMode(PIN_CC1101_CS, OUTPUT);
    digitalWrite(PIN_CC1101_CS, HIGH);

    radio_module = new Module(PIN_CC1101_CS, PIN_CC1101_IRQ, RADIOLIB_NC, RADIOLIB_NC, *radio_spi);
    radio_cc = new CC1101(radio_module);
    
    int16_t state = radio_cc->begin(WMBUS_FREQ_CT, WMBUS_CHIPRATE_CT, WMBUS_FDEV_CT, WMBUS_RXBW_CT, 10, WMBUS_PREAMBLE_CT);
    bool is_hydra = false;
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[RF] Detected Hydra RF (CC1101) successfully!");
        is_hydra = true;
        radio_phy = radio_cc;
        radio_cc->setPacketReceivedAction(on_rx_isr);
    } else {
        Serial.printf("[RF] CC1101 probe failed (code %d), assuming Cap LoRa-1262\n", state);
        delete radio_cc;
        delete radio_module;
        radio_cc = nullptr;

        // PROBE 2: LoRa Cap (SX1262). Now we know Hydra is not connected, so GPIO 5 is safe to use as CS.
        pinMode(PIN_LORA_NSS, OUTPUT);
        digitalWrite(PIN_LORA_NSS, HIGH);
        enable_rf_switch(); // I2C antenna switch

        radio_module = new Module(PIN_LORA_NSS, PIN_LORA_IRQ, PIN_LORA_RESET, PIN_LORA_BUSY, *radio_spi);
        radio_sx = new SX1262(radio_module);
        radio_phy = radio_sx;

        state = radio_sx->beginFSK(WMBUS_FREQ_CT, WMBUS_CHIPRATE_CT, WMBUS_FDEV_CT, WMBUS_RXBW_CT, 10, WMBUS_PREAMBLE_CT);
        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("[RF] ERROR: SX1262 beginFSK failed with code %d\n", state);
            return -1;
        }
        radio_sx->setDio1Action(on_rx_isr);
    }

    // Set 2-byte sync word 0x543D (matches preamble/sync bit streams for both C1 and T1)
    uint8_t sync_word[] = { WMBUS_SYNC_CT[0], WMBUS_SYNC_CT[1] };
    state = radio_phy->setSyncWord(sync_word, WMBUS_SYNC_CT_LEN);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[RF] ERROR: setSyncWord failed with code %d\n", state);
        return -1;
    }

    // Set NRZ encoding if supported (CC1101 uses NRZ by default for FSK)
    if (radio_sx) radio_sx->setEncoding(RADIOLIB_ENCODING_NRZ);
    if (radio_cc) radio_cc->setEncoding(RADIOLIB_ENCODING_NRZ); // CC1101 may not support this but we can try

    // Disable hardware CRC (wM-Bus uses per-block EN 13757-4 CRC)
    if (radio_sx) radio_sx->setCRC(0);
    if (radio_cc) radio_cc->setCrcFiltering(false); // Optional depending on driver, but usually ok

    // Fixed packet length for SX1262 (has 256B FIFO), 61 for CC1101 (fits in 64B FIFO to prevent overflow)
    if (radio_sx) radio_sx->fixedPacketLengthMode(255);
    if (radio_cc) radio_cc->fixedPacketLengthMode(61);

    radio_is_ready = true;
    Serial.printf("[RF] %s Active — 868.95 MHz C1/T1 FSK\n", is_hydra ? "CC1101" : "SX1262");

    return is_hydra ? 1 : 0;
}

void radio_start_receive() {
    if (!radio_is_ready || !radio_phy) return;
    isr_rx_flag = false;
    int16_t state = radio_phy->startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[RF] ERROR: startReceive failed with code %d\n", state);
    }
}

bool radio_packet_available() {
    if (isr_rx_flag) {
        isr_rx_flag = false;
        return true;
    }
    return false;
}

int radio_read_packet(uint8_t* buffer, size_t buffer_size, int16_t* rssi_out, float* snr_out) {
    if (!radio_phy) return -1;

    int16_t state = radio_phy->readData(buffer, buffer_size);
    if (state == RADIOLIB_ERR_NONE) {
        size_t len = radio_phy->getPacketLength();
        if (rssi_out) *rssi_out = (int16_t)radio_phy->getRSSI();
        if (snr_out) {
            // SNR is not supported by CC1101
            if (radio_sx) *snr_out = radio_sx->getSNR();
            else *snr_out = 0.0f;
        }
        return (int)len;
    } else {
        Serial.printf("[RF] readData error code: %d\n", state);
        return -1;
    }
}

const char* radio_status_str() {
    if (!radio_is_ready) return "NOT_INIT";
    return "RX_ACTIVE";
}

bool radio_switch_mode(bool c1t1) {
    if (!radio_phy || !radio_is_ready) return false;
    radio_phy->standby();

    float freq   = c1t1 ? WMBUS_FREQ_CT    : WMBUS_FREQ_S;
    float brate  = c1t1 ? WMBUS_CHIPRATE_CT : WMBUS_CHIPRATE_S;
    float fdev   = c1t1 ? WMBUS_FDEV_CT    : (WMBUS_CHIPRATE_S / 2.0f);
    float bw     = c1t1 ? WMBUS_RXBW_CT    : WMBUS_RXBW_S;
    uint8_t plen = c1t1 ? WMBUS_PREAMBLE_CT : WMBUS_PREAMBLE_S;
    const uint8_t* sync = c1t1 ? WMBUS_SYNC_CT : WMBUS_SYNC_S;
    int    slen  = c1t1 ? WMBUS_SYNC_CT_LEN : WMBUS_SYNC_S_LEN;

    int16_t st = -1;
    if (radio_sx) st = radio_sx->beginFSK(freq, brate, fdev, bw, 10, plen);
    if (radio_cc) st = radio_cc->begin(freq, brate, fdev, bw, 10, plen);

    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[RF] switch_mode error %d\n", st);
        return false;
    }

    if (radio_sx) radio_sx->setEncoding(RADIOLIB_ENCODING_NRZ);
    if (radio_cc) radio_cc->setEncoding(RADIOLIB_ENCODING_NRZ);

    uint8_t sw[4]; memcpy(sw, sync, slen);
    radio_phy->setSyncWord(sw, slen);

    if (radio_sx) radio_sx->setCRC(0);
    if (radio_cc) radio_cc->setCrcFiltering(false);

    if (radio_sx) radio_sx->fixedPacketLengthMode(255);
    if (radio_cc) radio_cc->fixedPacketLengthMode(61);

    if (radio_sx) radio_sx->setDio1Action(on_rx_isr);
    if (radio_cc) radio_cc->setPacketReceivedAction(on_rx_isr);

    Serial.printf("[RF] Switched to %s — %.2f MHz  %.3f kbps\n",
                  c1t1 ? "C1/T1" : "S1", (double)freq, (double)brate);

    radio_start_receive();
    return true;
}

} // namespace wmb


#endif // NATIVE_TEST
