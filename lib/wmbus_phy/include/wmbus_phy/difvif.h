// DIF/VIF (data information field / value information field) record walker,
// per EN 13757-3. Extracts storage/tariff/subunit addressing, raw values
// (int/BCD/float), VIF classification and canonical-unit scaling.
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace wmb {

enum class MeasureType : uint8_t {
    Instantaneous = 0,
    Maximum = 1,
    Minimum = 2,
    AtError = 3,
};

// Matches wmbusmeters' LIST_OF_VIF_RANGES (same names/order).
enum class VifRange : uint8_t {
    Volume, OnTime, OperatingTime, VolumeFlow, FlowTemperature, ReturnTemperature,
    TemperatureDifference, ExternalTemperature, Pressure, HeatCostAllocation,
    Date, DateTime, EnergyMJ, EnergyWh, PowerW, PowerJh, ActualityDuration,
    FabricationNo, EnhancedIdentification, EnergyMWh, EnergyGJ, RelativeHumidity,
    AccessNumber, Medium, Manufacturer, ParameterSet, ModelVersion, HardwareVersion,
    FirmwareVersion, SoftwareVersion, Location, Customer, ErrorFlags, DigitalOutput,
    DigitalInput, DurationSinceReadout, DurationOfTariff, Dimensionless, Voltage,
    Amperage, ResetCounter, CumulationCounter, SpecialSupplierInformation,
    RemainingBattery,
    // pseudo-ranges used only inside driver match rules:
    AnyVolumeVIF, AnyEnergyVIF, AnyPowerVIF,
    None,
};

// Quantities (unit families). Order matches tools/convert_drivers.py.
enum class Quantity : uint8_t {
    Volume, Energy, Reactive_Energy, Apparent_Energy, Power, Flow, Temperature,
    Voltage, Amperage, Pressure, Time, PointInTime, RH, HCA, Text, Counter,
    Dimensionless, Mass, Frequency, Angle, Unknown,
};

// Default JSON key suffix for a quantity ("m3", "kwh", "c", "counter", ...).
// Empty string for Text (no suffix).
const char* quantity_default_suffix(Quantity q);

#define WMBUS_MAX_COMBINABLES 4
#define WMBUS_MAX_DV_RECORDS 40

struct DVRecord {
    char      difvifkey[26];   // uppercase hex of DIF(+DIFEs)+VIF(+VIFEs)
    uint16_t  vif;             // full VIF: single byte, or (ext&0x7f)<<8 | vife&0x7f
    VifRange  vif_range;
    MeasureType mtype;
    uint8_t   dif_type;        // DIF low nibble
    int       storage_nr;
    int       tariff_nr;
    int       subunit_nr;
    uint16_t  combinables[WMBUS_MAX_COMBINABLES]; // raw combinable VIFE codes
    int       num_combinables;

    const uint8_t* data;       // points into the parsed payload
    int       data_len;

    double    raw;             // unscaled numeric value (int/BCD/float32)
    bool      raw_valid;
    bool      is_text;         // varlen / text-ish content (text[] valid)
    char      text[25];        // reversed/ascii rendering for Text fields
};

// Walk a decrypted application payload and extract all DV records.
// Stops at DIF 0x0F (rest is manufacturer-specific; returned via mfct_*) or 0x1F.
// Skips 0x2F padding bytes. Returns number of records written (<= max_out).
size_t difvif_parse(const uint8_t* payload, size_t len,
                    DVRecord* out, size_t max_out,
                    const uint8_t** mfct_data, size_t* mfct_len);

// Classify a full VIF into a range (None if unknown).
VifRange vif_classify(uint16_t vif);

// Canonical scale divisor for a full VIF: canonical_value = raw / vif_scale().
// Returns 1.0 for unscaled types, -1.0 for unknown.
double vif_scale(uint16_t vif);

// Metadata for a range.
Quantity  vifrange_quantity(VifRange r);
const char* vifrange_unit_suffix(VifRange r);  // "m3", "kwh", "c", "counter", ...
const char* vifrange_name(VifRange r);         // "Volume", ...
const char* vifrange_name_from_str(const char* name, VifRange* out); // parse helper

// Decode Type G date (CP16, VIF 0x6C) and Type F datetime (CP32, VIF 0x6D)
// into "YYYY-MM-DD" / "YYYY-MM-DD HH:MM" strings. Returns false on bad data.
bool decode_type_g(uint16_t v, char out[11]);
bool decode_type_f(uint32_t v, char out[17]);

// Convert Type G / Type F packed values to days since the unix epoch
// (Type F includes the fraction of day). Return NAN when invalid.
double type_g_to_days(uint16_t v);
double type_f_to_days(uint32_t v);

} // namespace wmb
