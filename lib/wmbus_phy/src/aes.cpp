#include "wmbus_phy/aes.h"
#include "wmbus_phy/telegram.h"
#include <string.h>

namespace wmb {

// ---------------------------------------------------------------------------
// GF(2^8) arithmetic with the AES reduction polynomial x^8+x^4+x^3+x+1 (0x11B)
// ---------------------------------------------------------------------------
static uint8_t gf_mul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) {
        if (b & 1) r ^= a;
        a = (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1B : 0x00));
        b >>= 1;
    }
    return r;
}

static uint8_t gf_pow(uint8_t a, uint16_t e) {
    uint8_t r = 1;
    while (e) {
        if (e & 1) r = gf_mul(r, a);
        a = gf_mul(a, a);
        e >>= 1;
    }
    return r;
}

static uint8_t rotl8(uint8_t x, int n) { return (uint8_t)((x << n) | (x >> (8 - n))); }

// ---------------------------------------------------------------------------
// Tables (generated once)
// ---------------------------------------------------------------------------
static uint8_t SBOX[256];
static uint8_t INV_SBOX[256];
static bool tables_ready = false;

static void aes_init_tables() {
    if (tables_ready) return;
    for (int i = 0; i < 256; ++i) {
        uint8_t inv = (i == 0) ? 0 : gf_pow((uint8_t)i, 254); // multiplicative inverse
        uint8_t s = (uint8_t)(inv ^ rotl8(inv, 1) ^ rotl8(inv, 2) ^ rotl8(inv, 3) ^ rotl8(inv, 4) ^ 0x63);
        SBOX[i] = s;
        INV_SBOX[s] = (uint8_t)i;
    }
    tables_ready = true;
}

// ---------------------------------------------------------------------------
// Key expansion (AES-128: 11 round keys of 16 bytes)
// ---------------------------------------------------------------------------
static void aes128_expand_key(const uint8_t key[16], uint8_t round_keys[176]) {
    aes_init_tables();
    memcpy(round_keys, key, 16);
    uint8_t rcon = 0x01;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4] = { round_keys[i - 4], round_keys[i - 3], round_keys[i - 2], round_keys[i - 1] };
        if ((i % 16) == 0) {
            uint8_t tmp = t[0]; t[0] = t[1]; t[1] = t[2]; t[2] = t[3]; t[3] = tmp; // RotWord
            for (int j = 0; j < 4; ++j) t[j] = SBOX[t[j]];                        // SubWord
            t[0] ^= rcon;
            rcon = gf_mul(rcon, 0x02);
        }
        for (int j = 0; j < 4; ++j) round_keys[i + j] = (uint8_t)(round_keys[i - 16 + j] ^ t[j]);
    }
}

// ---------------------------------------------------------------------------
// Inverse cipher primitives (state = 16 bytes, column-major)
// ---------------------------------------------------------------------------
static void add_round_key(uint8_t* st, const uint8_t* rk) {
    for (int i = 0; i < 16; ++i) st[i] ^= rk[i];
}

static void inv_sub_bytes(uint8_t* st) {
    for (int i = 0; i < 16; ++i) st[i] = INV_SBOX[st[i]];
}

static void inv_shift_rows(uint8_t* st) {
    uint8_t t;
    // row 1: shift right by 1
    t = st[13]; st[13] = st[9]; st[9] = st[5]; st[5] = st[1]; st[1] = t;
    // row 2: shift right by 2
    t = st[2]; st[2] = st[10]; st[10] = t; t = st[6]; st[6] = st[14]; st[14] = t;
    // row 3: shift right by 3 (== left by 1)
    t = st[3]; st[3] = st[7]; st[7] = st[11]; st[11] = st[15]; st[15] = t;
}

static void inv_mix_columns(uint8_t* st) {
    for (int c = 0; c < 4; ++c) {
        uint8_t* col = st + c * 4;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = (uint8_t)(gf_mul(a0, 0x0E) ^ gf_mul(a1, 0x0B) ^ gf_mul(a2, 0x0D) ^ gf_mul(a3, 0x09));
        col[1] = (uint8_t)(gf_mul(a0, 0x09) ^ gf_mul(a1, 0x0E) ^ gf_mul(a2, 0x0B) ^ gf_mul(a3, 0x0D));
        col[2] = (uint8_t)(gf_mul(a0, 0x0D) ^ gf_mul(a1, 0x09) ^ gf_mul(a2, 0x0E) ^ gf_mul(a3, 0x0B));
        col[3] = (uint8_t)(gf_mul(a0, 0x0B) ^ gf_mul(a1, 0x0D) ^ gf_mul(a2, 0x09) ^ gf_mul(a3, 0x0E));
    }
}

static void aes128_decrypt_block(const uint8_t round_keys[176], const uint8_t in[16], uint8_t out[16]) {
    uint8_t st[16];
    memcpy(st, in, 16);
    add_round_key(st, round_keys + 160);
    for (int round = 9; round >= 1; --round) {
        inv_shift_rows(st);
        inv_sub_bytes(st);
        add_round_key(st, round_keys + round * 16);
        inv_mix_columns(st);
    }
    inv_shift_rows(st);
    inv_sub_bytes(st);
    add_round_key(st, round_keys);
    memcpy(out, st, 16);
}

// ---------------------------------------------------------------------------
// CBC decrypt
// ---------------------------------------------------------------------------
void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t* in, uint8_t* out, size_t len) {
    if (len == 0 || (len % 16) != 0) return;
    uint8_t round_keys[176];
    aes128_expand_key(key, round_keys);

    uint8_t prev[16];
    memcpy(prev, iv, 16);
    for (size_t off = 0; off < len; off += 16) {
        uint8_t block[16];
        aes128_decrypt_block(round_keys, in + off, block);
        for (int i = 0; i < 16; ++i) out[off + i] = (uint8_t)(block[i] ^ prev[i]);
        memcpy(prev, in + off, 16);
    }
}

// ---------------------------------------------------------------------------
// wM-Bus mode 5 (AES-CBC-IV)
// ---------------------------------------------------------------------------
bool aes_decrypt_mode5(const Telegram& t, uint8_t* payload, size_t payload_len,
                       const uint8_t key[16]) {
    size_t n = payload_len;
    if (t.num_encr_blocks > 0) {
        n = (size_t)t.num_encr_blocks * 16;
        if (n > payload_len) n = payload_len;
    }
    n -= (n % 16);
    if (n < 16) return false;

    uint8_t iv[16];
    int i = 0;
    if (t.tpl_long) {
        iv[i++] = (uint8_t)(t.tpl_mfct & 0xFF);
        iv[i++] = (uint8_t)(t.tpl_mfct >> 8);
        for (int j = 0; j < 6; ++j) iv[i++] = t.tpl_a[j];
    } else {
        iv[i++] = (uint8_t)(t.dll_mfct & 0xFF);
        iv[i++] = (uint8_t)(t.dll_mfct >> 8);
        for (int j = 0; j < 4; ++j) iv[i++] = t.dll_id_raw[j];
        iv[i++] = t.dll_version;
        iv[i++] = t.dll_type;
    }
    for (int j = 0; j < 8; ++j) iv[i++] = t.tpl_acc;

    uint8_t buf[256];
    if (n > sizeof(buf)) return false;
    aes128_cbc_decrypt(key, iv, payload, buf, n);
    memcpy(payload, buf, n);

    // Decrypted content must start with the 0x2F 0x2F marker.
    return payload[0] == 0x2F && payload[1] == 0x2F;
}



bool aes_decrypt_mode7(const Telegram& t, uint8_t* payload, size_t payload_len,
                       const uint8_t key[16]) {
    size_t n = payload_len;
    if (t.num_encr_blocks > 0) {
        n = (size_t)t.num_encr_blocks * 16;
        if (n > payload_len) n = payload_len;
    }
    n -= (n % 16);
    if (n < 16) return false;

    uint8_t iv[16] = {0}; // mode 7 uses zero IV

    uint8_t buf[256];
    if (n > sizeof(buf)) return false;
    aes128_cbc_decrypt(key, iv, payload, buf, n);
    memcpy(payload, buf, n);
    return true;
}
} // namespace wmb
