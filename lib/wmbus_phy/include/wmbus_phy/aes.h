// AES-128 (FIPS-197) decrypt + wM-Bus TPL security mode 5 (AES-CBC-IV).
// Fresh implementation; S-box/round constants are generated programmatically
// at first use. Tables verified against FIPS-197 / NIST SP800-38A vectors.
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace wmb {

// Raw AES-128-CBC decrypt of len bytes (len must be a multiple of 16).
void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t* in, uint8_t* out, size_t len);

// wM-Bus mode 5: builds the IV (mfct + A-field + acc x8) from the telegram
// and decrypts the payload in place. Returns true if decryption succeeded
// and the 0x2F 0x2F sanity prefix is present.
// num_bytes follows EN 13757: tpl_num_encr_blocks*16, or the whole payload
// when num_encr_blocks == 0. Trailing unencrypted bytes are left untouched.
struct Telegram; // fwd (telegram.h)
bool aes_decrypt_mode7(const Telegram& t, uint8_t* payload, size_t payload_len, const uint8_t key[16]);

bool aes_decrypt_mode5(const Telegram& t, uint8_t* payload, size_t payload_len,
                       const uint8_t key[16]);

} // namespace wmb
