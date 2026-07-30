#include "wmbus_phy/telegram.h"
#include <stdio.h>

namespace wmb {

void mfct_to_str(uint16_t m, char out[4]) {
    out[0] = (char)(((m >> 10) & 0x1F) + 64);
    out[1] = (char)(((m >> 5) & 0x1F) + 64);
    out[2] = (char)((m & 0x1F) + 64);
    out[3] = '\0';
}

static void id_to_str(const uint8_t raw[4], char out[9]) {
    // 4 packed-BCD bytes, least significant byte first on air;
    // display form is big-endian (as users know it from the meter label).
    snprintf(out, 9, "%02x%02x%02x%02x", raw[3], raw[2], raw[1], raw[0]);
}

bool telegram_parse(const Frame* f, Telegram* t) {
    if (f->len < 11) return false;

    const uint8_t* d = f->data;
    t->c_field = d[1];
    t->dll_mfct = (uint16_t)d[2] | ((uint16_t)d[3] << 8);
    mfct_to_str(t->dll_mfct, t->mfct_str);
    for (int i = 0; i < 4; ++i) t->dll_id_raw[i] = d[4 + i];
    id_to_str(t->dll_id_raw, t->dll_id_str);
    t->dll_version = d[8];
    t->dll_type = d[9];
    t->ci = d[10];

    t->tpl_present = false;
    t->tpl_long = false;
    t->tpl_mfct = 0;
    t->tpl_version = 0;
    t->tpl_type = 0;
    t->tpl_acc = 0;
    t->tpl_status = 0;
    t->tpl_cfg = 0;
    t->sec_mode = SEC_NONE;
    t->num_encr_blocks = 0;
    t->ci_is_mfct_specific = false;
    
    t->ell_present = false;
    t->ell_cc = 0;
    t->ell_acc = 0;

    size_t pos = 11;

parse_ci:
    switch (t->ci) {
    case 0x8C: // short ELL
        if (f->len < pos + 3) return false;
        t->ell_present = true;
        t->ell_cc = d[pos];
        t->ell_acc = d[pos + 1];
        pos += 2;
        t->ci = d[pos];
        pos++;
        goto parse_ci;

    case 0x8D: // long ELL
        if (f->len < pos + 9) return false;
        t->ell_present = true;
        t->ell_cc = d[pos];
        t->ell_acc = d[pos + 1];
        // skip SN and CRC for now
        pos += 8;
        t->ci = d[pos];
        pos++;
        goto parse_ci;

    case 0x8E: // ELL III
        if (f->len < pos + 11) return false;
        t->ell_present = true;
        t->ell_cc = d[pos];
        t->ell_acc = d[pos + 1];
        pos += 10;
        t->ci = d[pos];
        pos++;
        goto parse_ci;

    case 0x8F: // ELL IV
        if (f->len < pos + 17) return false;
        t->ell_present = true;
        t->ell_cc = d[pos];
        t->ell_acc = d[pos + 1];
        pos += 16;
        t->ci = d[pos];
        pos++;
        goto parse_ci;

    case 0x90: // AFL
        if (f->len < pos + 1) return false;
        {
            uint8_t afl_len = d[pos];
            if (f->len < pos + 1 + afl_len + 1) return false;
            pos += 1 + afl_len;
            t->ci = d[pos];
            pos++;
            goto parse_ci;
        }

    case 0x72: // TPL long header
    case 0x73:
        if (f->len < pos + 12) return false;
        t->tpl_present = true;
        t->tpl_long = true;
        t->tpl_mfct = (uint16_t)d[pos] | ((uint16_t)d[pos + 1] << 8);
        for (int i = 0; i < 6; ++i) t->tpl_a[i] = d[pos + 2 + i];
        t->tpl_version = d[pos + 6];
        t->tpl_type = d[pos + 7];
        t->tpl_acc = d[pos + 8];
        t->tpl_status = d[pos + 9];
        t->tpl_cfg = (uint16_t)d[pos + 10] | ((uint16_t)d[pos + 11] << 8);
        pos += 12;
        break;

    case 0x7A: // TPL short header
    case 0x77:
        if (f->len < pos + 4) return false;
        t->tpl_present = true;
        t->tpl_acc = d[pos];
        t->tpl_status = d[pos + 1];
        t->tpl_cfg = (uint16_t)d[pos + 2] | ((uint16_t)d[pos + 3] << 8);
        pos += 4;
        break;

    case 0x70:
    case 0x78: // TPL no header
        t->tpl_present = true;
        break;

    default:
        if (t->ci >= 0xA0 && t->ci <= 0xB7) {
            t->ci_is_mfct_specific = true;
            // The original parser returned true, but with tpl_present = false.
        } else {
            t->tpl_present = true; // treat as no TPL header, rest is payload
        }
        break;
    }

    if (t->tpl_present && !t->ci_is_mfct_specific) {
        t->sec_mode = (t->tpl_cfg >> 8) & 0x1F;
        if (t->sec_mode == SEC_AES_CBC_IV || t->sec_mode == SEC_AES_CBC_NO_IV) {
            t->num_encr_blocks = (t->tpl_cfg >> 4) & 0x0F;
        }
    }

    t->payload = d + pos;
    t->payload_len = f->len - pos;
    t->header_size = pos;
    return true;
}

} // namespace wmb
