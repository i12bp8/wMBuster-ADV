// wM-Buster ADV — SX1262 FSK Radio Hardware Interface
// Configures SX1262 on Cap LoRa-1262 for wM-Bus C1/T1 reception
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifndef NATIVE_TEST

namespace wmb {

// Initialize RF antenna switch via PI4IOE I2C expander & setup SX1262/CC1101 FSK radio
int  radio_init(int hardware_type = -1);

// Switch between C1/T1 mode (868.95 MHz, 100 kchip/s) and S1 mode (868.30 MHz, 32.768 kchip/s).
// Stops current RX, reinitialises the radio, and restarts RX.  Returns false on failure.
bool radio_switch_mode(bool c1t1_mode);

// Start continuous background receive (DIO1 interrupt-driven)
void radio_start_receive();

// Check if a packet has arrived in the interrupt buffer
bool radio_packet_available();

// Read received raw packet bytes, RSSI, and SNR from radio
int radio_read_packet(uint8_t* buffer, size_t buffer_size, int16_t* rssi_out, float* snr_out);

// Returns status string of radio
const char* radio_status_str();

} // namespace wmb

#endif // NATIVE_TEST
