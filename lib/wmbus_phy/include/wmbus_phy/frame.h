// wM-Bus data-link frame handling (EN 13757-4): L-field check + DLL CRC
// trimming for frame formats A and B.
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace wmb {

enum class LinkMode : uint8_t { C1, T1, S1 };

#define WMBUS_FRAME_CAP 290  // L-field + up to 255 payload bytes + DLL CRC bytes

struct Frame {
    uint8_t data[WMBUS_FRAME_CAP];
    size_t  len;         // valid bytes in data[]
    LinkMode mode;
    int16_t rssi;        // dBm, filled by radio layer
    float   snr;         // dB,  filled by radio layer (FSK: often 0)
};

// Validate L-field vs actual length and strip DLL CRC bytes in place.
// Tries frame format A first, then format B (same order as wmbusmeters).
// On success: frame->len is reduced to L+1 (no CRC bytes), returns true.
bool frame_trim_crc(Frame* frame);

} // namespace wmb
