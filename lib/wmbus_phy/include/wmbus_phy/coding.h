// 3-out-of-6 line coding (EN 13757-4 T-mode).
// Each data nibble is transmitted as a 6-chip group with exactly three 1s.
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace wmb {

// Decode a 3-out-of-6 encoded byte stream back to plain bytes.
// 3 encoded bytes -> 2 plain bytes. enc_len must be a multiple of 3
// (trailing partial group is ignored). Returns number of plain bytes written.
// Sets *ok=false if any chip group is not a valid 3-out-of-6 code.
size_t coding_3outof6_decode(const uint8_t* enc, size_t enc_len,
                             uint8_t* out, size_t out_max,
                             bool* ok);

// Encode plain bytes to 3-out-of-6 chip stream (2 bytes -> 3 bytes).
// Used for tests and to derive the T-mode on-air sync word. Returns encoded length.
size_t coding_3outof6_encode(const uint8_t* in, size_t in_len,
                             uint8_t* out, size_t out_max);

} // namespace wmb
