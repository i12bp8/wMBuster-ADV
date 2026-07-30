#include "wmbus_phy/difvif.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace wmb {

// ---------------------------------------------------------------------------
// VIF range table — mirrors upstream LIST_OF_VIF_RANGES.
// ---------------------------------------------------------------------------
struct RangeDef {
    VifRange range;
    uint16_t from, to;
    Quantity quantity;
    const char* unit_suffix;
    const char* name;
};

static const RangeDef RANGE_TABLE[] = {
    { VifRange::Volume,                0x10,   0x17,   Quantity::Volume,      "m3",      "Volume" },
    { VifRange::OnTime,                0x20,   0x23,   Quantity::Time,        "h",       "OnTime" },
    { VifRange::OperatingTime,         0x24,   0x27,   Quantity::Time,        "h",       "OperatingTime" },
    { VifRange::VolumeFlow,            0x38,   0x3F,   Quantity::Flow,        "m3h",     "VolumeFlow" },
    { VifRange::FlowTemperature,       0x58,   0x5B,   Quantity::Temperature, "c",       "FlowTemperature" },
    { VifRange::ReturnTemperature,     0x5C,   0x5F,   Quantity::Temperature, "c",       "ReturnTemperature" },
    { VifRange::TemperatureDifference, 0x60,   0x63,   Quantity::Temperature, "c",       "TemperatureDifference" },
    { VifRange::ExternalTemperature,   0x64,   0x67,   Quantity::Temperature, "c",       "ExternalTemperature" },
    { VifRange::Pressure,              0x68,   0x6B,   Quantity::Pressure,    "bar",     "Pressure" },
    { VifRange::HeatCostAllocation,    0x6E,   0x6E,   Quantity::HCA,         "hca",     "HeatCostAllocation" },
    { VifRange::Date,                  0x6C,   0x6C,   Quantity::PointInTime, "date",    "Date" },
    { VifRange::DateTime,              0x6D,   0x6D,   Quantity::PointInTime, "datetime","DateTime" },
    { VifRange::EnergyMJ,              0x08,   0x0F,   Quantity::Energy,      "mj",      "EnergyMJ" },
    { VifRange::EnergyWh,              0x00,   0x07,   Quantity::Energy,      "kwh",     "EnergyWh" },
    { VifRange::PowerW,                0x28,   0x2F,   Quantity::Power,       "kw",      "PowerW" },
    { VifRange::PowerJh,               0x30,   0x37,   Quantity::Power,       "mjh",     "PowerJh" },
    { VifRange::ActualityDuration,     0x74,   0x77,   Quantity::Time,        "h",       "ActualityDuration" },
    { VifRange::FabricationNo,         0x78,   0x78,   Quantity::Text,        "txt",     "FabricationNo" },
    { VifRange::EnhancedIdentification,0x79,   0x79,   Quantity::Text,        "txt",     "EnhancedIdentification" },
    { VifRange::EnergyMWh,             0x7B00, 0x7B01, Quantity::Energy,      "kwh",     "EnergyMWh" },
    { VifRange::EnergyGJ,              0x7B08, 0x7B09, Quantity::Energy,      "mj",      "EnergyGJ" },
    { VifRange::RelativeHumidity,      0x7B1A, 0x7B1B, Quantity::RH,          "rh",      "RelativeHumidity" },
    { VifRange::AccessNumber,          0x7D08, 0x7D08, Quantity::Counter,     "counter", "AccessNumber" },
    { VifRange::Medium,                0x7D09, 0x7D09, Quantity::Text,        "txt",     "Medium" },
    { VifRange::Manufacturer,          0x7D0A, 0x7D0A, Quantity::Text,        "txt",     "Manufacturer" },
    { VifRange::ParameterSet,          0x7D0B, 0x7D0B, Quantity::Text,        "txt",     "ParameterSet" },
    { VifRange::ModelVersion,          0x7D0C, 0x7D0C, Quantity::Text,        "txt",     "ModelVersion" },
    { VifRange::HardwareVersion,       0x7D0D, 0x7D0D, Quantity::Text,        "txt",     "HardwareVersion" },
    { VifRange::FirmwareVersion,       0x7D0E, 0x7D0E, Quantity::Text,        "txt",     "FirmwareVersion" },
    { VifRange::SoftwareVersion,       0x7D0F, 0x7D0F, Quantity::Text,        "txt",     "SoftwareVersion" },
    { VifRange::Location,              0x7D10, 0x7D10, Quantity::Text,        "txt",     "Location" },
    { VifRange::Customer,              0x7D11, 0x7D11, Quantity::Text,        "txt",     "Customer" },
    { VifRange::ErrorFlags,            0x7D17, 0x7D17, Quantity::Text,        "txt",     "ErrorFlags" },
    { VifRange::DigitalOutput,         0x7D1A, 0x7D1A, Quantity::Text,        "txt",     "DigitalOutput" },
    { VifRange::DigitalInput,          0x7D1B, 0x7D1B, Quantity::Text,        "txt",     "DigitalInput" },
    { VifRange::DurationSinceReadout,  0x7D2C, 0x7D2F, Quantity::Time,        "h",       "DurationSinceReadout" },
    { VifRange::DurationOfTariff,      0x7D31, 0x7D33, Quantity::Time,        "h",       "DurationOfTariff" },
    { VifRange::Dimensionless,         0x7D3A, 0x7D3A, Quantity::Counter,     "counter", "Dimensionless" },
    { VifRange::Voltage,               0x7D40, 0x7D4F, Quantity::Voltage,     "v",       "Voltage" },
    { VifRange::Amperage,              0x7D50, 0x7D5F, Quantity::Amperage,    "a",       "Amperage" },
    { VifRange::ResetCounter,          0x7D60, 0x7D60, Quantity::Counter,     "counter", "ResetCounter" },
    { VifRange::CumulationCounter,     0x7D61, 0x7D61, Quantity::Counter,     "counter", "CumulationCounter" },
    { VifRange::SpecialSupplierInformation, 0x7D67, 0x7D67, Quantity::Text,   "txt",     "SpecialSupplierInformation" },
    { VifRange::RemainingBattery,      0x7D74, 0x7D74, Quantity::Time,        "d",       "RemainingBattery" },
};
static const size_t RANGE_TABLE_LEN = sizeof(RANGE_TABLE) / sizeof(RANGE_TABLE[0]);

VifRange vif_classify(uint16_t vif) {
    vif &= 0x7F7F;
    for (size_t i = 0; i < RANGE_TABLE_LEN; ++i) {
        if (vif >= RANGE_TABLE[i].from && vif <= RANGE_TABLE[i].to) return RANGE_TABLE[i].range;
    }
    return VifRange::None;
}

Quantity vifrange_quantity(VifRange r) {
    for (size_t i = 0; i < RANGE_TABLE_LEN; ++i) {
        if (RANGE_TABLE[i].range == r) return RANGE_TABLE[i].quantity;
    }
    return Quantity::Unknown;
}

const char* vifrange_unit_suffix(VifRange r) {
    for (size_t i = 0; i < RANGE_TABLE_LEN; ++i) {
        if (RANGE_TABLE[i].range == r) return RANGE_TABLE[i].unit_suffix;
    }
    return "";
}

const char* vifrange_name(VifRange r) {
    for (size_t i = 0; i < RANGE_TABLE_LEN; ++i) {
        if (RANGE_TABLE[i].range == r) return RANGE_TABLE[i].name;
    }
    if (r == VifRange::AnyVolumeVIF) return "AnyVolumeVIF";
    if (r == VifRange::AnyEnergyVIF) return "AnyEnergyVIF";
    if (r == VifRange::AnyPowerVIF)  return "AnyPowerVIF";
    return "None";
}

const char* quantity_default_suffix(Quantity q) {
    switch (q) {
    case Quantity::Volume: return "m3";
    case Quantity::Energy: return "kwh";
    case Quantity::Reactive_Energy: return "kvarh";
    case Quantity::Apparent_Energy: return "kvah";
    case Quantity::Power: return "kw";
    case Quantity::Flow: return "m3h";
    case Quantity::Temperature: return "c";
    case Quantity::Voltage: return "v";
    case Quantity::Amperage: return "a";
    case Quantity::Pressure: return "bar";
    case Quantity::Time: return "h";
    case Quantity::PointInTime: return "datetime";
    case Quantity::RH: return "rh";
    case Quantity::HCA: return "hca";
    case Quantity::Text: return "";
    case Quantity::Counter: return "counter";
    case Quantity::Dimensionless: return "counter";
    case Quantity::Mass: return "kg";
    case Quantity::Frequency: return "hz";
    case Quantity::Angle: return "deg";
    case Quantity::Unknown: return "";
    }
    return "";
}

const char* vifrange_name_from_str(const char* name, VifRange* out) {
    for (size_t i = 0; i < RANGE_TABLE_LEN; ++i) {
        if (strcmp(RANGE_TABLE[i].name, name) == 0) { *out = RANGE_TABLE[i].range; return name; }
    }
    if (strcmp(name, "AnyVolumeVIF") == 0) { *out = VifRange::AnyVolumeVIF; return name; }
    if (strcmp(name, "AnyEnergyVIF") == 0) { *out = VifRange::AnyEnergyVIF; return name; }
    if (strcmp(name, "AnyPowerVIF") == 0)  { *out = VifRange::AnyPowerVIF;  return name; }
    *out = VifRange::None;
    return nullptr;
}

// ---------------------------------------------------------------------------
// VIF scaling — canonical divisor table (value = raw / scale), mirrors
// upstream vifScale(). Ranges with per-nibble exponents use pow().
// ---------------------------------------------------------------------------
double vif_scale(uint16_t vif) {
    vif &= 0x7F7F;
    switch (vif) {
    // Energy Wh -> kWh
    case 0x00: return 1000000.0; case 0x01: return 100000.0;
    case 0x02: return 10000.0;   case 0x03: return 1000.0;
    case 0x04: return 100.0;     case 0x05: return 10.0;
    case 0x06: return 1.0;       case 0x07: return 0.1;
    // Energy J -> MJ
    case 0x08: return 1000000.0; case 0x09: return 100000.0;
    case 0x0A: return 10000.0;   case 0x0B: return 1000.0;
    case 0x0C: return 100.0;     case 0x0D: return 10.0;
    case 0x0E: return 1.0;       case 0x0F: return 0.1;
    // Volume -> m3
    case 0x10: return 1000000.0; case 0x11: return 100000.0;
    case 0x12: return 10000.0;   case 0x13: return 1000.0;
    case 0x14: return 100.0;     case 0x15: return 10.0;
    case 0x16: return 1.0;       case 0x17: return 0.1;
    // Mass -> kg
    case 0x18: return 1000.0;    case 0x19: return 100.0;
    case 0x1A: return 10.0;      case 0x1B: return 1.0;
    case 0x1C: return 0.1;       case 0x1D: return 0.01;
    case 0x1E: return 0.001;     case 0x1F: return 0.0001;
    // On time -> h
    case 0x20: return 3600.0;    case 0x21: return 60.0;
    case 0x22: return 1.0;       case 0x23: return 1.0 / 24.0;
    // Operating time -> h
    case 0x24: return 3600.0;    case 0x25: return 60.0;
    case 0x26: return 1.0;       case 0x27: return 1.0 / 24.0;
    // Power W -> kW
    case 0x28: return 1000000.0; case 0x29: return 100000.0;
    case 0x2A: return 10000.0;   case 0x2B: return 1000.0;
    case 0x2C: return 100.0;     case 0x2D: return 10.0;
    case 0x2E: return 1.0;       case 0x2F: return 0.1;
    // Power J/h -> MJ/h
    case 0x30: return 1000000.0; case 0x31: return 100000.0;
    case 0x32: return 10000.0;   case 0x33: return 1000.0;
    case 0x34: return 100.0;     case 0x35: return 10.0;
    case 0x36: return 1.0;       case 0x37: return 0.1;
    // Volume flow -> m3/h
    case 0x38: return 1000000.0; case 0x39: return 100000.0;
    case 0x3A: return 10000.0;   case 0x3B: return 1000.0;
    case 0x3C: return 100.0;     case 0x3D: return 10.0;
    case 0x3E: return 1.0;       case 0x3F: return 0.1;
    // Volume flow ext -> m3/h
    case 0x40: return 600000000.0; case 0x41: return 60000000.0;
    case 0x42: return 6000000.0;   case 0x43: return 600000.0;
    case 0x44: return 60000.0;     case 0x45: return 6000.0;
    case 0x46: return 600.0;       case 0x47: return 60.0;
    case 0x48: return 1000000000.0 * 3600; case 0x49: return 100000000.0 * 3600;
    case 0x4A: return 10000000.0 * 3600;   case 0x4B: return 1000000.0 * 3600;
    case 0x4C: return 100000.0 * 3600;     case 0x4D: return 10000.0 * 3600;
    case 0x4E: return 1000.0 * 3600;       case 0x4F: return 100.0 * 3600;
    // Mass flow -> kg/h
    case 0x50: return 1000.0;    case 0x51: return 100.0;
    case 0x52: return 10.0;      case 0x53: return 1.0;
    case 0x54: return 0.1;       case 0x55: return 0.01;
    case 0x56: return 0.001;     case 0x57: return 0.0001;
    // Flow temperature -> C
    case 0x58: return 1000.0;    case 0x59: return 100.0;
    case 0x5A: return 10.0;      case 0x5B: return 1.0;
    // Return temperature -> C
    case 0x5C: return 1000.0;    case 0x5D: return 100.0;
    case 0x5E: return 10.0;      case 0x5F: return 1.0;
    // Temperature difference -> C(K)
    case 0x60: return 1000.0;    case 0x61: return 100.0;
    case 0x62: return 10.0;      case 0x63: return 1.0;
    // External temperature -> C
    case 0x64: return 1000.0;    case 0x65: return 100.0;
    case 0x66: return 10.0;      case 0x67: return 1.0;
    // Pressure -> bar
    case 0x68: return 1000.0;    case 0x69: return 100.0;
    case 0x6A: return 10.0;      case 0x6B: return 1.0;
    // Date / DateTime / HCA: unscaled
    case 0x6C: return 1.0;       case 0x6D: return 1.0;
    case 0x6E: return 1.0;
    // Averaging duration -> h
    case 0x70: return 3600.0;    case 0x71: return 60.0;
    case 0x72: return 1.0;       case 0x73: return 1.0 / 24.0;
    // Actuality duration -> h
    case 0x74: return 3600.0;    case 0x75: return 60.0;
    case 0x76: return 1.0;       case 0x77: return 1.0 / 24.0;
    case 0x7C: return 1.0; // variable length VIF
    // Energy MWh / GJ, per-nibble exponent
    case 0x7B00: case 0x7B01: { double e = (vif & 0x1) + 2; return pow(10.0, -e); }
    case 0x7B08: case 0x7B09: { double e = (vif & 0x1) + 2; return pow(10.0, -e); }
    // Relative humidity -> %
    case 0x7B1A: return 10.0;    case 0x7B1B: return 1.0;
    case 0x7D08: return 1.0; // access number
    // Duration of tariff -> h
    case 0x7D31: return 60.0;    case 0x7D32: return 1.0;
    case 0x7D33: return 1.0 / 24.0;
    // Duration since readout -> h
    case 0x7D2C: return 3600.0;  case 0x7D2D: return 60.0;
    case 0x7D2E: return 1.0;     case 0x7D2F: return 1.0 / 24.0;
    case 0x7D3A: return 1.0; // dimensionless
    case 0x7D61: return 1.0; // cumulation counter
    case 0x7D74: return 1.0; // remaining battery (days)
    default: break;
    }
    if (vif >= 0x7D40 && vif <= 0x7D4F) { // Voltage -> V
        double e = (vif & 0xF) - 9; return pow(10.0, -e);
    }
    if (vif >= 0x7D50 && vif <= 0x7D5F) { // Amperage -> A
        double e = (vif & 0xF) - 12; return pow(10.0, -e);
    }
    return -1.0;
}

// ---------------------------------------------------------------------------
// Data extraction
// ---------------------------------------------------------------------------
static int dif_data_len(uint8_t dif) {
    switch (dif & 0x0F) {
    case 0x0: return 0;
    case 0x1: return 1;
    case 0x2: return 2;
    case 0x3: return 3;
    case 0x4: return 4;
    case 0x5: return 4;
    case 0x6: return 6;
    case 0x7: return 8;
    case 0x8: return 0;
    case 0x9: return 1;
    case 0xA: return 2;
    case 0xB: return 3;
    case 0xC: return 4;
    case 0xD: return -1; // variable length
    case 0xE: return 6;
    case 0xF: return -2; // special
    }
    return -2;
}

// Two's-complement signed little-endian integer.
static double extract_int(const uint8_t* d, int len) {
    uint64_t raw = 0;
    for (int i = len - 1; i >= 0; --i) raw = (raw << 8) | d[i];
    int bits = len * 8;
    if (bits < 64 && (raw & (1ULL << (bits - 1)))) {
        int64_t sraw = (int64_t)(raw | (~0ULL << bits));
        return (double)sraw;
    }
    return (double)(int64_t)raw;
}

// Packed BCD, little-endian; 0xF in the top digit means negative.
// Returns true if all nibbles were F (=> invalid/NaN).
static bool extract_bcd(const uint8_t* d, int len, double* out) {
    uint64_t raw = 0;
    uint64_t mult = 1;
    bool all_f = true;
    for (int i = 0; i < len; ++i) {
        int lo = d[i] & 0x0F, hi = d[i] >> 4;
        if (lo != 0xF || hi != 0xF) all_f = false;
        bool neg_hi = (i == len - 1) && (hi == 0xF);
        raw += (uint64_t)lo * mult; mult *= 10;
        if (!neg_hi) { raw += (uint64_t)hi * mult; }
        mult *= 10;
    }
    if (all_f) return true;
    bool neg = (d[len - 1] >> 4) == 0xF;
    *out = neg ? -(double)raw : (double)raw;
    return false;
}

static bool likely_ascii(const uint8_t* d, int len) {
    for (int i = 0; i < len; ++i) {
        if (d[i] < 0x20 || d[i] > 0x7E) return false;
    }
    return len > 0;
}

// Text rendering: reversed byte order, as ascii if printable else hex.
static void extract_text(const uint8_t* d, int len, char* out, size_t out_cap) {
    if (len <= 0 || out_cap == 0) { if (out_cap) out[0] = 0; return; }
    if (likely_ascii(d, len)) {
        size_t n = 0;
        for (int i = len - 1; i >= 0 && n + 1 < out_cap; --i) out[n++] = (char)d[i];
        out[n] = 0;
    } else {
        size_t n = 0;
        for (int i = len - 1; i >= 0 && n + 2 < out_cap; --i) {
            snprintf(out + n, out_cap - n, "%02X", d[i]);
            n += 2;
        }
        out[n < out_cap ? n : out_cap - 1] = 0;
    }
}

// ---------------------------------------------------------------------------
// The DV walker
// ---------------------------------------------------------------------------
size_t difvif_parse(const uint8_t* payload, size_t len,
                    DVRecord* out, size_t max_out,
                    const uint8_t** mfct_data, size_t* mfct_len) {
    if (mfct_data) *mfct_data = nullptr;
    if (mfct_len) *mfct_len = 0;

    size_t n_out = 0;
    size_t pos = 0;

    while (pos < len && n_out < max_out) {
        uint8_t dif = payload[pos];

        if (dif == 0x0F) { // manufacturer-specific data follows
            if (mfct_data) *mfct_data = payload + pos + 1;
            if (mfct_len) *mfct_len = len - pos - 1;
            break;
        }
        if (dif == 0x1F) break;      // more records in next datagram
        if (dif == 0x2F) { pos++; continue; } // padding

        int dlen = dif_data_len(dif);
        if (dlen == -2) break;

        DVRecord* r = &out[n_out];
        memset(r, 0, sizeof(*r));
        r->vif_range = VifRange::None;
        r->dif_type = dif & 0x0F;
        r->mtype = (MeasureType)((dif >> 4) & 0x03);

        char* key = r->difvifkey;
        size_t keylen = 0;
        keylen += snprintf(key + keylen, sizeof(r->difvifkey) - keylen, "%02X", dif);

        // DIFEs
        int storage_nr = (dif & 0x40) >> 6;
        int tariff = 0, subunit = 0, difenr = 0;
        uint8_t byte = dif;
        pos++;
        while (byte & 0x80) {
            if (pos >= len) goto done;
            uint8_t dife = payload[pos++];
            keylen += snprintf(key + keylen, sizeof(r->difvifkey) - keylen, "%02X", dife);
            subunit |= ((dife & 0x40) >> 6) << difenr;
            tariff |= ((dife & 0x30) >> 4) << (difenr * 2);
            storage_nr |= (dife & 0x0F) << (1 + difenr * 4);
            difenr++;
            byte = dife;
            if (difenr > 10) goto done;
        }
        r->storage_nr = storage_nr;
        r->tariff_nr = tariff;
        r->subunit_nr = subunit;

        // VIF
        if (pos >= len) goto done;
        uint8_t vif = payload[pos++];
        keylen += snprintf(key + keylen, sizeof(r->difvifkey) - keylen, "%02X", vif);

        bool ext_vif = (vif == 0xFB || vif == 0xFD || vif == 0xEF || vif == 0xFF);
        uint16_t full_vif = vif & 0x7F;
        if (ext_vif) full_vif <<= 8;

        // Variable-length VIF string (0x7C/0xFC): length byte + ascii.
        if (vif == 0x7C || vif == 0xFC) {
            if (pos >= len) goto done;
            uint8_t vlen = payload[pos++];
            keylen += snprintf(key + keylen, sizeof(r->difvifkey) - keylen, "%02X", vlen);
            for (uint8_t i = 0; i < vlen && pos < len; ++i) {
                keylen += snprintf(key + keylen, sizeof(r->difvifkey) - keylen, "%02X", payload[pos++]);
            }
        }

        // VIFEs
        bool first_vife_after_ext = ext_vif;
        bool comb_ext_pending = false;   // 0x7C/0x7F combinable extension marker
        uint16_t comb_ext_hi = 0;
        uint8_t vbyte = vif;
        while (vbyte & 0x80) {
            if (pos >= len) goto done;
            uint8_t vife = payload[pos++];
            keylen += snprintf(key + keylen, sizeof(r->difvifkey) - keylen, "%02X", vife);
            if (first_vife_after_ext) {
                full_vif |= (vife & 0x7F);
                first_vife_after_ext = false;
            } else if (comb_ext_pending) {
                // completes a 16-bit combinable code: (marker&0x7f)<<8 | vife&0x7f
                if (r->num_combinables < WMBUS_MAX_COMBINABLES) {
                    r->combinables[r->num_combinables++] = (uint16_t)(comb_ext_hi | (vife & 0x7F));
                }
                comb_ext_pending = false;
            } else {
                uint8_t c = vife & 0x7F;
                if (c == 0x7C || c == 0x7F) {
                    comb_ext_pending = true;
                    comb_ext_hi = (uint16_t)c << 8;
                } else if (r->num_combinables < WMBUS_MAX_COMBINABLES) {
                    r->combinables[r->num_combinables++] = c;
                }
            }
            vbyte = vife;
        }
        (void)keylen;

        r->vif = full_vif;
        r->vif_range = vif_classify(full_vif);

        // Data
        if (dlen == -1) { // variable length
            if (pos >= len) goto done;
            dlen = payload[pos];
            r->data = payload + pos + 1;
            r->data_len = dlen;
            if (pos + 1 + (size_t)dlen > len) goto done;
            r->is_text = true;
            extract_text(r->data, dlen, r->text, sizeof(r->text));
            pos += 1 + dlen;
        } else {
            if (pos + (size_t)dlen > len) goto done;
            r->data = payload + pos;
            r->data_len = dlen;
            switch (r->dif_type) {
            case 0x1: case 0x2: case 0x3: case 0x4: case 0x6: case 0x7:
                r->raw = extract_int(r->data, dlen);
                r->raw_valid = true;
                break;
            case 0x5: { // 32-bit float, little-endian
                uint32_t u = (uint32_t)r->data[0] | ((uint32_t)r->data[1] << 8) |
                             ((uint32_t)r->data[2] << 16) | ((uint32_t)r->data[3] << 24);
                float f;
                memcpy(&f, &u, 4);
                r->raw = (double)f;
                r->raw_valid = true;
                break;
            }
            case 0x9: case 0xA: case 0xB: case 0xC: case 0xE: {
                double v = 0;
                if (!extract_bcd(r->data, dlen, &v)) {
                    r->raw = v;
                    r->raw_valid = true;
                }
                break;
            }
            default:
                break;
            }
            // Text-ish rendering for Text ranges / fabrication numbers.
            if (dlen > 0 && dlen <= 12 &&
                (vifrange_quantity(r->vif_range) == Quantity::Text)) {
                r->is_text = true;
                extract_text(r->data, dlen, r->text, sizeof(r->text));
            }
            pos += dlen;
        }
        
        // printf("Parsed record: dif=%02X dif_type=%d vif=%04X vif_range=%d dlen=%d mtype=%d raw=%f\n", dif, r->dif_type, full_vif, (int)r->vif_range, dlen, (int)r->mtype, r->raw);

        n_out++;
    }

done:
    return n_out;
}

// ---------------------------------------------------------------------------
// Date / datetime decoders (EN 13757-3 types G and F)
// ---------------------------------------------------------------------------
bool decode_type_g(uint16_t v, char out[11]) {
    int day = v & 0x1F;
    int month = (v >> 8) & 0x0F;
    int year = 2000 + (((v >> 5) & 0x07) | ((v >> 9) & 0x78));
    if (day == 0 || month == 0 || month > 12 || day > 31) return false;
    snprintf(out, 11, "%04d-%02d-%02d", year, month, day);
    return true;
}

bool decode_type_f(uint32_t v, char out[17]) {
    int minute = v & 0x3F;
    int hour = (v >> 8) & 0x1F;
    int day = (v >> 16) & 0x1F;
    int month = (v >> 24) & 0x0F;
    // Year bits are in the DATE sub-field (bytes 2-3 of the 4-byte CP32):
    //   year1 = bits 21..23 (byte 2, bits 5-7)
    //   year2 = bits 28..31 (byte 3, bits 4-7) * 8
    int year = 2000 + (((v >> 21) & 0x07) | (((v >> 28) & 0x0F) << 3));
    if (day == 0 || month == 0 || month > 12 || day > 31 || hour > 23 || minute > 59) return false;
    snprintf(out, 17, "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return true;
}

// Civil date helpers (same algorithms as the formula engine).
static long tg_days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

double type_g_to_days(uint16_t v) {
    int day = v & 0x1F;
    int month = (v >> 8) & 0x0F;
    int year = 2000 + (((v >> 5) & 0x07) | ((v >> 9) & 0x78));
    if (day == 0 || month == 0 || month > 12 || day > 31) return (double)NAN;
    return (double)tg_days_from_civil(year, (unsigned)month, (unsigned)day);
}

double type_f_to_days(uint32_t v) {
    int minute = v & 0x3F;
    int hour = (v >> 8) & 0x1F;
    int day = (v >> 16) & 0x1F;
    int month = (v >> 24) & 0x0F;
    int year = 2000 + (((v >> 21) & 0x07) | (((v >> 28) & 0x0F) << 3));
    if (day == 0 || month == 0 || month > 12 || day > 31 || hour > 23 || minute > 59) return (double)NAN;
    double days = (double)tg_days_from_civil(year, (unsigned)month, (unsigned)day);
    return days + (hour * 60 + minute) / 1440.0;
}

} // namespace wmb
