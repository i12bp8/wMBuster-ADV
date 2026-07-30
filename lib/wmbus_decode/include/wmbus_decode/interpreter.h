// The generic OMS driver interpreter: matches a telegram's DV records
// against a driver's field rules, computes values (scaling, lookups, dates),
// evaluates calculate formulas, and renders the JSON-compatible field list.
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "wmbus_decode/driver_table.h"
#include "wmbus_phy/telegram.h"
#include "wmbus_phy/difvif.h"

namespace wmb {

#define WMBUS_MAX_OUT_FIELDS 40

struct OutField {
    char   name[56];      // output key, e.g. "total_m3", "consumption_at_set_date_1_hca"
    double value;         // numeric value (NaN when is_text)
    char   text[64];      // text value (status, dates, lookup results)
    bool   is_text;
    bool   hidden;        // HIDE attribute: computed for formulas, not displayed
};

struct DecodeResult {
    const DriverDef* driver;      // nullptr when no driver matched
    char   id[9];                 // meter id (8 digits)
    char   media[32];             // "water", "heat", "warm water", ...
    char   mfct[4];               // "TCH", ...
    OutField fields[WMBUS_MAX_OUT_FIELDS];
    int    num_fields;
};

// A hand-ported manufacturer payload decoder: turns a proprietary payload
// into synthetic DV records that the generic engine then processes normally.
typedef size_t (*PayloadDecoderFn)(const Telegram& t,
                                   const uint8_t* payload, size_t len,
                                   DVRecord* out, size_t max_out);

void register_payload_decoder(const char* driver_name, PayloadDecoderFn fn);

// Decode a telegram against a driver (forced == nullptr for auto-detect).
// payload must point to a WRITABLE buffer (decryption happens in place) and
// payload_len is its length. key may be nullptr when the meter is unencrypted.
// Returns false when no driver matched or decryption failed.
bool decode_telegram(const Telegram& t, uint8_t* payload, size_t payload_len,
                     const uint8_t* key, const DriverDef* forced,
                     DecodeResult* out);

// Media string per mediaTypeJSON (type byte + manufacturer for TCH specials).
const char* media_type_json(uint8_t device_type, uint16_t mfct, char* buf, size_t buf_len);

} // namespace wmb
