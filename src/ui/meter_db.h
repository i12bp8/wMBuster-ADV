// wM-Buster ADV — Resident Meter Database & Reading History
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>
#ifndef NATIVE_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif
#include "wmbus_decode/interpreter.h"

namespace wmb {

#define MAX_METERS 50
#define MAX_FEED_ENTRIES 50

struct MeterEntry {
    char id[10];             // Meter ID string (e.g. "83010647")
    char name[32];           // Meter name / alias
    char media[32];          // Media type ("water", "heat", etc.)
    char driver_name[32];    // Driver name ("kamheat", etc.)
    char mfct[8];            // Manufacturer code ("TCH", "KAM", etc.)
    char friendly_type[22];  // Human label ("Water Meter", "Heat Allocator", …)
    float last_rssi;
    float last_snr;
    double lat;
    double lon;
    bool gnss_fix;
    uint32_t last_seen_ms;
    uint32_t telegram_count;
    bool is_starred;

    // Primary field summary for list & feed display
    char primary_field_name[32];
    char primary_value_str[32];   // number + unit, e.g. "11895.000 kWh"

    // Formatted decoded fields for detail view (key\nvalue+unit\n pairs)
    char display_fields[768];
};

struct FeedEntry {
    uint32_t timestamp_ms;
    char meter_id[10];
    char driver_name[32];
    char mfct[8];
    char summary[64];
    float rssi;
    bool is_starred;
};

// Returns a user-friendly type label for a given media + driver combination.
// E.g.: ("water", "iperl") → "Water Meter"
//       ("heat cost allocation", "fhkvdataiv") → "Heat Allocator"
const char* get_friendly_meter_type(const char* media, const char* driver);

class MeterDatabase {
public:
    MeterDatabase();

    MeterEntry* add_reading(const DecodeResult& res, float rssi, float snr,
                           double lat, double lon, bool gnss_fix,
                           bool* is_new_meter, bool* is_starred_update);

    void lock();
    void unlock();

    size_t get_meter_count() const { return meter_count_; }
    MeterEntry* get_meter(size_t index);
    const MeterEntry* get_meter(size_t index) const;
    MeterEntry* find_meter_by_id(const char* id);
    const MeterEntry* find_meter_by_id(const char* id) const;

    bool toggle_star(size_t index);
    bool toggle_star_by_id(const char* id);

    size_t get_feed_count() const { return feed_count_; }
    const FeedEntry* get_feed_entry(size_t index) const; // 0 = newest
    void clear_feed();

    const DecodeResult* get_last_decode() const { return &last_decode_; }

private:
    MeterEntry  meters_[MAX_METERS];
    size_t      meter_count_;

    FeedEntry   feed_[MAX_FEED_ENTRIES];
    size_t      feed_head_;
    size_t      feed_count_;

    DecodeResult last_decode_;

#ifndef NATIVE_TEST
    SemaphoreHandle_t mutex_;
#endif

    void extract_primary_summary(const DecodeResult& res,
                                 char* name_buf, size_t nlen,
                                 char* val_buf,  size_t vlen);

    void sort_meters();
};

MeterDatabase& get_global_meter_db();

} // namespace wmb
