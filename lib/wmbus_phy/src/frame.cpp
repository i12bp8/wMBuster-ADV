#include "wmbus_phy/frame.h"
#include "wmbus_phy/crc.h"
#include <string.h>

namespace wmb {

static bool trim_serial_frame(Frame* f) {
    if (f->len < 12) return false;
    if (f->data[0] != 0x68 || f->data[3] != 0x68) return false;
    if (f->data[1] != f->data[2]) return false;
    uint8_t udata_len = f->data[1];
    if ((size_t)(udata_len + 6) > f->len) return false; // 68 LL LL 68 + data + CS + 16
    // Verify checksum
    uint8_t cs = 0;
    for (int i = 4; i < 4 + udata_len; ++i) cs += f->data[i];
    if (cs != f->data[4 + udata_len]) return false;
    // Verify stop byte
    if (f->data[4 + udata_len + 1] != 0x16) return false;
    // Build wireless-format frame: [L-field] [C M M A A A A V T CI data...]
    uint8_t out[WMBUS_FRAME_CAP];
    out[0] = udata_len; // L field = user data length
    memcpy(out + 1, f->data + 4, udata_len);
    memcpy(f->data, out, udata_len + 1);
    f->len = udata_len + 1;
    return true;
}

// Format A: CRC over first 10 bytes, then CRC after every 16-byte block,
// plus a final CRC over the remaining bytes.
static bool trim_format_a(Frame* f) {
    if (f->len < 12) return false;

    uint8_t l_field = f->data[0];
    size_t actual_len = 0;
    if (l_field < 10) {
        actual_len = (l_field + 1) + 2;
    } else {
        size_t remaining = l_field - 9;
        size_t num_data_blocks = (remaining + 15) / 16;
        actual_len = (l_field + 1) + 2 * (1 + num_data_blocks);
    }
    
    if (actual_len > f->len) return false; // Not enough data captured

    uint8_t out[WMBUS_FRAME_CAP];
    size_t out_len = 0;

    uint16_t calc = crc16_en13757(f->data, 10);
    uint16_t check = ((uint16_t)f->data[10] << 8) | f->data[11];
    if (calc != check) return false;
    memcpy(out, f->data, 10);
    out_len = 10;

    size_t pos = 12;
    for (; pos + 18 <= actual_len; pos += 18) {
        calc = crc16_en13757(f->data + pos, 16);
        check = ((uint16_t)f->data[pos + 16] << 8) | f->data[pos + 17];
        if (calc != check) return false;
        memcpy(out + out_len, f->data + pos, 16);
        out_len += 16;
    }

    if (pos < actual_len - 2) {
        size_t blen = actual_len - 2 - pos;
        calc = crc16_en13757(f->data + pos, blen);
        check = ((uint16_t)f->data[actual_len - 2] << 8) | f->data[actual_len - 1];
        if (calc != check) return false;
        memcpy(out + out_len, f->data + pos, blen);
        out_len += blen;
    }

    out[0] = (uint8_t)(out_len - 1);
    memcpy(f->data, out, out_len);
    f->len = out_len;
    return true;
}

// Format B: single CRC over the whole frame (<=128 bytes), or CRC after the
// first 126 bytes and another over the remainder.
static bool trim_format_b(Frame* f) {
    if (f->len < 12) return false;

    uint8_t l_field = f->data[0];
    if (l_field < 10) return false;
    size_t actual_len = l_field + 1;
    if (actual_len > f->len) return false;

    uint8_t out[WMBUS_FRAME_CAP];
    size_t out_len = 0;

    size_t crc1_pos, crc2_pos;
    if (actual_len <= 128) {
        crc1_pos = actual_len - 2;
        crc2_pos = 0;
    } else {
        crc1_pos = 126;
        crc2_pos = actual_len - 2;
    }

    uint16_t calc = crc16_en13757(f->data, crc1_pos);
    uint16_t check = ((uint16_t)f->data[crc1_pos] << 8) | f->data[crc1_pos + 1];
    if (calc != check) return false;
    memcpy(out, f->data, crc1_pos);
    out_len = crc1_pos;

    if (crc2_pos > 0) {
        size_t from = crc1_pos + 2;
        size_t blen = crc2_pos - from;
        calc = crc16_en13757(f->data + from, blen);
        check = ((uint16_t)f->data[crc2_pos] << 8) | f->data[crc2_pos + 1];
        if (calc != check) return false;
        memcpy(out + out_len, f->data + from, blen);
        out_len += blen;
    }

    out[0] = (uint8_t)(out_len - 1);
    memcpy(f->data, out, out_len);
    f->len = out_len;
    return true;
}

bool frame_trim_crc(Frame* frame) {
    if (frame->len < 12) return false;
    if (trim_serial_frame(frame)) return true;
    // NOTE: the L-field is NOT checked here. Real meters disagree on whether
    // L counts CRC bytes / the L byte itself; wmbusmeters ignores it too and
    // lets the CRCs be the integrity check. L is rewritten after trimming.
    if (trim_format_a(frame)) return true;
    return trim_format_b(frame);
}

} // namespace wmb
