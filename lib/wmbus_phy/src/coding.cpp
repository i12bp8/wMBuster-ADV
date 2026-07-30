#include "wmbus_phy/coding.h"

namespace wmb {

// Nibble -> 6-chip code, per EN 13757-4 table (exactly three 1-bits each).
static const uint8_t ENC_TAB[16] = {
    0x16, 0x0D, 0x0E, 0x0B, 0x1C, 0x19, 0x1A, 0x13,
    0x2C, 0x25, 0x26, 0x23, 0x34, 0x31, 0x32, 0x29
};

// Reverse map, built once: 6-bit code -> nibble, 0xFF = invalid.
static uint8_t DEC_TAB[64];
static bool dec_tab_ready = false;

static void build_dec_tab() {
    for (int i = 0; i < 64; ++i) DEC_TAB[i] = 0xFF;
    for (int i = 0; i < 16; ++i) DEC_TAB[ENC_TAB[i]] = (uint8_t)i;
    dec_tab_ready = true;
}

size_t coding_3outof6_decode(const uint8_t* enc, size_t enc_len,
                             uint8_t* out, size_t out_max,
                             bool* ok) {
    if (!dec_tab_ready) build_dec_tab();
    if (ok) *ok = true;

    size_t n_out = 0;
    // Each group: 24 chips = 4 nibbles = 2 plain bytes, from 3 encoded bytes.
    for (size_t i = 0; i + 2 < enc_len; i += 3) {
        uint32_t chips = ((uint32_t)enc[i] << 16) | ((uint32_t)enc[i + 1] << 8) | enc[i + 2];
        uint8_t n0 = DEC_TAB[(chips >> 18) & 0x3F];
        uint8_t n1 = DEC_TAB[(chips >> 12) & 0x3F];
        uint8_t n2 = DEC_TAB[(chips >> 6) & 0x3F];
        uint8_t n3 = DEC_TAB[chips & 0x3F];
        if ((n0 | n1 | n2 | n3) & 0xF0) {
            if (ok) *ok = false;
            return n_out;
        }
        if (n_out + 2 > out_max) return n_out;
        out[n_out++] = (uint8_t)((n0 << 4) | n1);
        out[n_out++] = (uint8_t)((n2 << 4) | n3);
    }
    return n_out;
}

size_t coding_3outof6_encode(const uint8_t* in, size_t in_len,
                             uint8_t* out, size_t out_max) {
    size_t n_out = 0;
    for (size_t i = 0; i < in_len; i += 2) {
        uint8_t b0 = in[i];
        uint8_t b1 = (i + 1 < in_len) ? in[i + 1] : 0x00;
        if (n_out + 3 > out_max) return n_out;
        uint32_t chips = ((uint32_t)ENC_TAB[b0 >> 4] << 18) |
                         ((uint32_t)ENC_TAB[b0 & 0x0F] << 12) |
                         ((uint32_t)ENC_TAB[b1 >> 4] << 6) |
                         ((uint32_t)ENC_TAB[b1 & 0x0F]);
        out[n_out++] = (uint8_t)(chips >> 16);
        out[n_out++] = (uint8_t)(chips >> 8);
        out[n_out++] = (uint8_t)chips;
    }
    return n_out;
}

} // namespace wmb
