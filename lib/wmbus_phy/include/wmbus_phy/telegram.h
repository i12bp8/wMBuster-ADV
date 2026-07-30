// wM-Bus telegram layer: data-link header (DLL) + transport header (TPL)
// parsing, manufacturer/ID formatting, security-mode extraction.
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "wmbus_phy/frame.h"

namespace wmb {

// Security modes (from TPL cfg bits 8..12)
enum SecMode : uint8_t {
    SEC_NONE = 0,
    SEC_AES_CBC_IV = 5,    // mode 5, the common one
    SEC_AES_CBC_NO_IV = 7, // mode 7 (OMS4)
};

struct Telegram {
    // ---- DLL ----
    uint8_t  c_field;
    uint16_t dll_mfct;          // raw, little-endian as transmitted
    char     mfct_str[4];       // decoded 3-letter code, e.g. "TCH"
    uint8_t  dll_id_raw[4];     // BCD bytes as transmitted
    char     dll_id_str[9];     // 8-digit display form (big-endian BCD)
    uint8_t  dll_version;
    uint8_t  dll_type;          // device/meter type byte
    uint8_t  ci;

    // ---- ELL ----
    uint8_t  ell_cc;            // communication control
    uint8_t  ell_acc;           // ELL access number
    bool     ell_present;

    // ---- TPL ----
    bool     tpl_present;
    bool     tpl_long;          // full 12-byte header (CI 0x72)
    uint16_t tpl_mfct;
    uint8_t  tpl_a[6];          // id(4 raw) + version + type (for AES IV)
    uint8_t  tpl_version;
    uint8_t  tpl_type;
    uint8_t  tpl_acc;           // access number
    uint8_t  tpl_status;
    uint16_t tpl_cfg;           // raw cfg word
    int      sec_mode;          // (cfg >> 8) & 0x1f
    int      num_encr_blocks;   // (cfg >> 4) & 0x0f, valid for modes 5/7

    // Manufacturer-specific CI range 0xA0..0xB7: no TPL, payload is raw.
    bool     ci_is_mfct_specific;

    // ---- Application payload (points into the Frame buffer) ----
    const uint8_t* payload;
    size_t         payload_len;
    size_t         header_size; // offset of payload in frame data
};

// Decode the 2-byte manufacturer word into its 3-letter code.
void mfct_to_str(uint16_t m, char out[4]);

// Parse DLL + TPL headers from a CRC-trimmed frame. Returns false if the
// frame is too short or uses a CI layout we do not support.
bool telegram_parse(const Frame* frame, Telegram* t);

} // namespace wmb
