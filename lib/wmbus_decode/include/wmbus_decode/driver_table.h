// Driver rule table — types for the generated driver database
// (drivers_generated.cpp, produced by tools/convert_drivers.py).
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "wmbus_phy/difvif.h"

#ifndef NATIVE_TEST
#include <pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#endif

namespace wmb {

struct DriverDetect {
    char     m[4];   // 3-letter manufacturer code
    uint8_t  ver;    // 0xFF = wildcard
    uint8_t  typ;    // 0xFF = wildcard
};

struct LookupMapEntry {
    const char* name;
    uint64_t    value;
    uint8_t     test;  // 0 = Set, 1 = NotSet, 2 = Equal
};

struct LookupDef {
    const char* name;
    uint8_t     map_type;  // 0 = BitToString, 1 = IndexToString
    uint64_t    mask;
    const char* default_msg;
    const LookupMapEntry* maps;
    uint8_t     num_maps;
};

// Field attributes (match upstream print properties)
#define FATTR_HIDE               0x0001
#define FATTR_STATUS             0x0002
#define FATTR_INCLUDE_TPL_STATUS 0x0004
#define FATTR_DEPRECATED         0x0008
#define FATTR_INJECT_INTO_STATUS 0x0010

struct FieldRule {
    const char* name;          // may contain {storage_counter-8counter} templates
    Quantity    quantity;
    const char* display_unit;  // nullptr = quantity default; else unit lcname ("date","ppm",...)
    uint16_t    attrs;
    uint8_t     vif_scaling;   // 0 = Auto, 1 = None
    uint8_t     signedness;    // 0 = Default, 1 = Signed, 2 = Unsigned
    double      force_scale;   // 0 = unused; else value = raw * force_scale

    // match rule (ignored when calculate != nullptr)
    const char* difvifkey;     // exact key match, or nullptr
    VifRange    vif_range;     // VifRange::None when matching by difvifkey
    int8_t      mtype;         // -1 = any
    int16_t     storage_from;  // -1 = any
    int16_t     storage_to;
    int16_t     tariff_from;
    int16_t     tariff_to;
    int16_t     subunit_from;  // -1 = any
    int16_t     subunit_to;
    uint16_t    comb_must_have;// 0 = no requirement; else record must have this combinable
    int32_t     comb_raw_eq;   // -1 = don't care; else record combinable set must equal this
    bool        comb_synthetic;// compact-profile synthetic records

    const char* calculate;     // formula, or nullptr for a matched field
    const LookupDef* lookup;   // optional text mapping
};

struct DriverDef {
    const char* name;
    uint8_t     meter_type;    // index into meter_type_name table
    const char* default_fields;
    const DriverDetect* detects;
    uint8_t     num_detects;
    const FieldRule* fields;
    uint8_t     num_fields;
    uint8_t     needs_payload_decoder; // 1 = has ixml/match_entire_payload fields
    const LookupDef* tpl_status_lookup;
};

extern const DriverDef DRIVERS[];
extern const size_t DRIVERS_LEN;

const char* meter_type_name(uint8_t idx);

// Find the best driver for a telegram MVT (version/type may be wildcarded).
const DriverDef* find_driver(const char mfct[4], uint8_t version, uint8_t type);

// Find a driver by name ("iperl"), nullptr if unknown.
const DriverDef* find_driver_by_name(const char* name);

} // namespace wmb
