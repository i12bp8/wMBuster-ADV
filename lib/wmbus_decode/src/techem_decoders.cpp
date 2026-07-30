// wM-Buster ADV — Techem Family Payload Decoders Implementation
// GPL-3.0
#include "wmbus_decode/techem_decoders.h"
#include <stdio.h>
#include <string.h>

namespace wmb {

static uint16_t read_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u24_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static void fill_record(DVRecord* r, const char* key, VifRange vr, double val, int storage = 0) {
    memset(r, 0, sizeof(DVRecord));
    snprintf(r->difvifkey, sizeof(r->difvifkey), "%s", key);
    r->vif_range = vr;
    r->mtype = MeasureType::Instantaneous;
    r->storage_nr = storage;
    r->tariff_nr = -1;
    r->subunit_nr = -1;
    r->raw = val;
}

// ---------------------------------------------------------------------------
// 1. mkradio4 (Water meter: warm/cold)
// ---------------------------------------------------------------------------
static size_t decode_mkradio4(const Telegram& t, const uint8_t* payload, size_t len,
                             DVRecord* out, size_t max_out) {
    if (len < 9 || max_out < 2) return 0;
    size_t n = 0;

    // Prev volume (offset 3, 2 bytes): "4215" (storage 1)
    fill_record(&out[n++], "4215", VifRange::Volume, (double)read_u16_le(payload + 3), 1);

    // Curr volume (offset 7, 2 bytes): "0215" (storage 0)
    fill_record(&out[n++], "0215", VifRange::Volume, (double)read_u16_le(payload + 7), 0);

    return n;
}

// ---------------------------------------------------------------------------
// 2. mkradio3 (Water meter: warm/cold)
// ---------------------------------------------------------------------------
static size_t decode_mkradio3(const Telegram& t, const uint8_t* payload, size_t len,
                             DVRecord* out, size_t max_out) {
    if (len < 9 || max_out < 4) return 0;
    size_t n = 0;

    // Prev date raw (offset 1, 2 bytes): "02FD3A"
    fill_record(&out[n++], "02FD3A", VifRange::None, (double)read_u16_le(payload + 1), 0);

    // Prev volume (offset 3, 2 bytes): "4215"
    fill_record(&out[n++], "4215", VifRange::Volume, (double)read_u16_le(payload + 3), 1);

    // Curr date raw (offset 5, 2 bytes): "42FD3A"
    fill_record(&out[n++], "42FD3A", VifRange::None, (double)read_u16_le(payload + 5), 1);

    // Curr volume (offset 7, 2 bytes): "0215"
    fill_record(&out[n++], "0215", VifRange::Volume, (double)read_u16_le(payload + 7), 0);

    return n;
}

// ---------------------------------------------------------------------------
// 3. fhkvdataiii (Heat Cost Allocator)
// ---------------------------------------------------------------------------
static size_t decode_fhkvdataiii(const Telegram& t, const uint8_t* payload, size_t len,
                                DVRecord* out, size_t max_out) {
    if (len < 9 || max_out < 6) return 0;
    size_t n = 0;

    uint8_t hdr = payload[0];
    size_t offset = 1;

    // Prev date raw (offset 1, 2 bytes): "02FD3A"
    fill_record(&out[n++], "02FD3A", VifRange::None, (double)read_u16_le(payload + offset), 0);
    offset += 2;

    // Prev HCA (offset 3, 2 bytes): "426E"
    fill_record(&out[n++], "426E", VifRange::HeatCostAllocation, (double)read_u16_le(payload + offset), 1);
    offset += 2;

    // Curr date raw (offset 5, 2 bytes): "42FD3A"
    fill_record(&out[n++], "42FD3A", VifRange::None, (double)read_u16_le(payload + offset), 1);
    offset += 2;

    // Curr HCA (offset 7, 2 bytes): "026E"
    fill_record(&out[n++], "026E", VifRange::HeatCostAllocation, (double)read_u16_le(payload + offset), 0);
    offset += 2;

    if (hdr == 0x0F) offset++; // skip extra byte for version_94

    if (offset + 4 <= len && n + 2 <= max_out) {
        // Temp Room (2 bytes): "0265"
        fill_record(&out[n++], "0265", VifRange::ExternalTemperature, (double)read_u16_le(payload + offset), 0);
        offset += 2;

        // Temp Radiator (2 bytes): "025D"
        fill_record(&out[n++], "025D", VifRange::ReturnTemperature, (double)read_u16_le(payload + offset), 0);
        offset += 2;
    }

    return n;
}

// ---------------------------------------------------------------------------
// 4. tsd2 (Smoke detector)
// ---------------------------------------------------------------------------
static size_t decode_tsd2(const Telegram& t, const uint8_t* payload, size_t len,
                         DVRecord* out, size_t max_out) {
    if (len < 3 || max_out < 2) return 0;
    size_t n = 0;

    // Status byte (offset 0): "0101"
    fill_record(&out[n++], "0101", VifRange::None, (double)payload[0], 0);

    // Date raw (offset 1, 2 bytes): "02FD3A"
    fill_record(&out[n++], "02FD3A", VifRange::None, (double)read_u16_le(payload + 1), 0);

    return n;
}

// ---------------------------------------------------------------------------
// 5. compact5 (Heat meter)
// ---------------------------------------------------------------------------
static size_t decode_compact5(const Telegram& t, const uint8_t* payload, size_t len,
                             DVRecord* out, size_t max_out) {
    if (len < 13 || max_out < 2) return 0;
    size_t n = 0;

    // Prev raw (offset 6, 3 bytes): "037E"
    fill_record(&out[n++], "037E", VifRange::EnergyWh, (double)read_u24_le(payload + 6), 1);

    // Curr raw (offset 10, 3 bytes): "037F"
    fill_record(&out[n++], "037F", VifRange::EnergyWh, (double)read_u24_le(payload + 10), 0);

    return n;
}

// ---------------------------------------------------------------------------
// 6. vario451 (Heat meter)
// ---------------------------------------------------------------------------
static size_t decode_vario451(const Telegram& t, const uint8_t* payload, size_t len,
                              DVRecord* out, size_t max_out) {
    if (len < 12 || max_out < 2) return 0;
    size_t n = 0;

    // Prev raw (offset 6, 2 bytes): "027E"
    fill_record(&out[n++], "027E", VifRange::EnergyWh, (double)read_u16_le(payload + 6), 1);

    // Curr raw (offset 10, 2 bytes): "027F"
    fill_record(&out[n++], "027F", VifRange::EnergyWh, (double)read_u16_le(payload + 10), 0);

    return n;
}

void register_techem_payload_decoders() {
    register_payload_decoder("mkradio4", decode_mkradio4);
    register_payload_decoder("mkradio3", decode_mkradio3);
    register_payload_decoder("fhkvdataiii", decode_fhkvdataiii);
    register_payload_decoder("fhkvdataiv", decode_fhkvdataiii);
    register_payload_decoder("tsd2", decode_tsd2);
    register_payload_decoder("compact5", decode_compact5);
    register_payload_decoder("vario451", decode_vario451);
    register_payload_decoder("vario411", decode_vario451);
    register_payload_decoder("vario451mid", decode_vario451);
}

} // namespace wmb
