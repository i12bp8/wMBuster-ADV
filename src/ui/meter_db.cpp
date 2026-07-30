// wM-Buster ADV — Resident Meter Database & Reading History Implementation
// GPL-3.0
#include "meter_db.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

#ifndef NATIVE_TEST
#include <Arduino.h>  // millis()
#endif

namespace wmb {

static MeterDatabase g_meter_db;

MeterDatabase& get_global_meter_db() {
    return g_meter_db;
}

MeterDatabase::MeterDatabase()
    : meter_count_(0), feed_head_(0), feed_count_(0) {
    memset(meters_, 0, sizeof(meters_));
    memset(feed_, 0, sizeof(feed_));
#ifndef NATIVE_TEST
    mutex_ = xSemaphoreCreateMutex();
#endif
}

void MeterDatabase::lock() {
#ifndef NATIVE_TEST
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
#endif
}

void MeterDatabase::unlock() {
#ifndef NATIVE_TEST
    if (mutex_) xSemaphoreGive(mutex_);
#endif
}

// ── Friendly meter-type labels ───────────────────────────────────────────────
const char* get_friendly_meter_type(const char* media, const char* driver) {
    if (!media)  media  = "";
    if (!driver) driver = "";
    // HCA / heat cost allocator — check driver first (more specific)
    if (strstr(driver, "fhkv") || strstr(driver, "vario") ||
        strstr(driver, "caloric") || strstr(driver, "compact5") ||
        strstr(driver, "tsd2") || strstr(driver, "mkradio") ||
        strstr(media,  "heat cost")) return "Heat Allocator";
    // Water sub-types
    if (strstr(media, "warm water") || strstr(media, "hot water"))
        return "Hot Water";
    if (strstr(media, "water") || strstr(driver, "iperl") ||
        strstr(driver, "hydrus") || strstr(driver, "picoflux") ||
        strstr(driver, "multical21") || strstr(driver, "minomess") ||
        strstr(driver, "qwater"))
        return "Water Meter";
    // Heat
    if (strstr(media, "heat") || strstr(media, "heating") ||
        strstr(driver, "kamheat") || strstr(driver, "sharky") ||
        strstr(driver, "ultraheat") || strstr(driver, "calecst") ||
        strstr(driver, "sensostar"))
        return "Heat Meter";
    // Gas
    if (strstr(media, "gas") || strstr(driver, "gwf") || strstr(driver, "gas"))
        return "Gas Meter";
    // Electricity
    if (strstr(media, "electric") || strstr(driver, "amiplus") ||
        strstr(driver, "em24") || strstr(driver, "omni") ||
        strstr(driver, "nemo"))
        return "Electricity";
    // Cooling
    if (strstr(media, "cool")) return "Cooling";
    // Generic
    return "Smart Meter";
}

// Extract the unit suffix from a DIF/VIF field name (e.g. "total_m3" → " m³")
static const char* unit_suffix(const char* field_name) {
    if (!field_name || !*field_name) return "";
    // Check longer suffixes first to avoid partial matches
    if (strstr(field_name, "_m3h"))   return " m³/h";
    if (strstr(field_name, "_m3c"))   return " m³";
    if (strstr(field_name, "_m3"))    return " m³";
    if (strstr(field_name, "_mwh"))   return " MWh";
    if (strstr(field_name, "_kwh"))   return " kWh";
    if (strstr(field_name, "_wh"))    return " Wh";
    if (strstr(field_name, "_hca"))   return " HCA";
    if (strstr(field_name, "_mbar"))  return " mbar";
    if (strstr(field_name, "_bar"))   return " bar";
    if (strstr(field_name, "_kw"))    return " kW";
    if (strstr(field_name, "_mw"))    return " MW";
    if (strstr(field_name, "_w"))     return " W";
    if (strstr(field_name, "_lh"))    return " l/h";
    if (strstr(field_name, "_l"))     return " L";
    if (strstr(field_name, "_mj"))    return " MJ";
    if (strstr(field_name, "_gj"))    return " GJ";
    // Temperature: ends with _c but careful not to match other _c endings
    const char* p = strstr(field_name, "_c");
    if (p && *(p+2) == '\0') return " \xC2\xB0""C";  // UTF-8 °C (won't render on LCD but fine for web)
    if (strstr(field_name, "_k"))     return " K";
    if (strstr(field_name, "_kmh"))   return " km/h";
    if (strstr(field_name, "_hz"))    return " Hz";
    if (strstr(field_name, "_db"))    return " dB";
    if (strstr(field_name, "_dbm"))   return " dBm";
    return "";
}

// Temperature display unit — short ASCII for on-device display
static const char* unit_suffix_ascii(const char* field_name) {
    if (!field_name || !*field_name) return "";
    if (strstr(field_name, "_m3h"))  return " m3/h";
    if (strstr(field_name, "_m3c"))  return " m3";
    if (strstr(field_name, "_m3"))   return " m3";
    if (strstr(field_name, "_mwh"))  return " MWh";
    if (strstr(field_name, "_kwh"))  return " kWh";
    if (strstr(field_name, "_wh"))   return " Wh";
    if (strstr(field_name, "_hca"))  return " HCA";
    if (strstr(field_name, "_mbar")) return " mbar";
    if (strstr(field_name, "_bar"))  return " bar";
    if (strstr(field_name, "_kw"))   return " kW";
    if (strstr(field_name, "_mw"))   return " MW";
    if (strstr(field_name, "_w"))    return " W";
    if (strstr(field_name, "_lh"))   return " l/h";
    if (strstr(field_name, "_l"))    return " L";
    if (strstr(field_name, "_mj"))   return " MJ";
    if (strstr(field_name, "_gj"))   return " GJ";
    const char* p = strstr(field_name, "_c");
    if (p && *(p+2) == '\0') return " *C";
    if (strstr(field_name, "_k"))   return " K";
    return "";
}

void MeterDatabase::extract_primary_summary(const DecodeResult& res,
                                             char* name_buf, size_t nlen,
                                             char* val_buf, size_t vlen) {
    name_buf[0] = '\0';
    val_buf[0] = '\0';

    if (res.num_fields == 0) {
        snprintf(val_buf, vlen, "NO DATA");
        return;
    }

    const OutField* primary = nullptr;
    static const char* priority_keys[] = {
        "total_energy_consumption_kwh", "total_kwh", "total_volume_m3", "total_m3",
        "total_energy_consumption", "total_volume", "total", "forward_energy_m3c",
        "current_consumption_hca", "consumption_hca", "current_temperature_c", "flow_temperature_c",
        "026E", "0215", "426E", "4215"
    };

    for (const char* key : priority_keys) {
        for (int i = 0; i < res.num_fields; ++i) {
            if (!res.fields[i].hidden && strcmp(res.fields[i].name, key) == 0) {
                primary = &res.fields[i];
                break;
            }
        }
        if (primary) break;
    }

    if (!primary) {
        for (int i = 0; i < res.num_fields; ++i) {
            if (!res.fields[i].hidden && strcmp(res.fields[i].name, "status") != 0) {
                primary = &res.fields[i];
                break;
            }
        }
    }

    if (!primary) {
        for (int i = 0; i < res.num_fields; ++i) {
            if (!res.fields[i].hidden) {
                primary = &res.fields[i];
                break;
            }
        }
    }

    if (primary) {
        if (strcmp(primary->name, "status") == 0 ||
            (primary->is_text && strcasecmp(primary->text, "OK") == 0)) {
            name_buf[0] = '\0';
            snprintf(val_buf, vlen, "Active");
        } else {
            snprintf(name_buf, nlen, "%s", primary->name);
            if (primary->is_text) {
                snprintf(val_buf, vlen, "%s", primary->text);
            } else {
                // Include ASCII unit suffix for compact display
                const char* u = unit_suffix_ascii(primary->name);
                snprintf(val_buf, vlen, "%.3f%s", primary->value, u);
            }
        }
    } else {
        name_buf[0] = '\0';
        snprintf(val_buf, vlen, "Active");
    }
}

MeterEntry* MeterDatabase::add_reading(const DecodeResult& res, float rssi, float snr,
                                        double lat, double lon, bool gnss_fix,
                                        bool* is_new_meter, bool* is_starred_update) {
    if (is_new_meter) *is_new_meter = false;
    if (is_starred_update) *is_starred_update = false;

    lock();
    MeterEntry* m = find_meter_by_id(res.id);
    if (!m) {
        if (meter_count_ < MAX_METERS) {
            m = &meters_[meter_count_++];
            if (is_new_meter) *is_new_meter = true;
        } else {
            // LRU eviction: reuse oldest un-starred entry
            size_t oldest_idx = 0;
            uint32_t oldest_seen = UINT32_MAX;
            for (size_t i = 0; i < meter_count_; ++i) {
                if (!meters_[i].is_starred && meters_[i].last_seen_ms < oldest_seen) {
                    oldest_seen = meters_[i].last_seen_ms;
                    oldest_idx = i;
                }
            }
            m = &meters_[oldest_idx];
        }
        memset(m, 0, sizeof(MeterEntry));
        snprintf(m->id, sizeof(m->id), "%s", res.id);
        snprintf(m->mfct, sizeof(m->mfct), "%s", res.mfct);
        snprintf(m->name, sizeof(m->name), "%s:%s",
                 res.driver ? res.driver->name : "unknown", res.id);
        m->is_starred = false;
        m->telegram_count = 0;
    }

    // Update meter fields
    snprintf(m->media, sizeof(m->media), "%s", res.media);
    snprintf(m->driver_name, sizeof(m->driver_name), "%s",
             res.driver ? res.driver->name : "unknown");
    snprintf(m->friendly_type, sizeof(m->friendly_type), "%s",
             get_friendly_meter_type(res.media, res.driver ? res.driver->name : ""));
    m->last_rssi = rssi;
    m->last_snr  = snr;
    m->lat       = lat;
    m->lon       = lon;
    m->gnss_fix  = gnss_fix;
#ifndef NATIVE_TEST
    m->last_seen_ms = (uint32_t)millis();
#else
    m->last_seen_ms = 0;
#endif
    m->telegram_count++;
    last_decode_ = res;

    extract_primary_summary(res, m->primary_field_name, sizeof(m->primary_field_name),
                            m->primary_value_str, sizeof(m->primary_value_str));

    // Populate display_fields (key\nvalue+unit\n pairs for detail view)
    m->display_fields[0] = '\0';
    size_t df_len = 0;
    
    auto add_field = [&](const char* k, const char* v) {
        if (!k || !v || df_len >= sizeof(m->display_fields)) return;
        char line[128];
        snprintf(line, sizeof(line), "%s\n%s\n", k, v[0] ? v : "-");
        size_t ll = strlen(line);
        if (df_len + ll < sizeof(m->display_fields)) {
            strcat(m->display_fields, line);
            df_len += ll;
        }
    };
    
    add_field("ID", res.id);
    add_field("Manufacturer", res.mfct[0] ? res.mfct : "Unknown");
    add_field("Media", res.media);
    add_field("Driver", res.driver ? res.driver->name : "Unknown");

    for (int i = 0; i < res.num_fields; ++i) {
        const OutField* f = &res.fields[i];
        if (f->hidden) continue;
        if (f->is_text) {
            add_field(f->name, f->text);
        } else {
            const char* u = unit_suffix_ascii(f->name);
            char vbuf[32];
            snprintf(vbuf, sizeof(vbuf), "%.3f%s", f->value, u);
            add_field(f->name, vbuf);
        }
    }

    if (m->is_starred && is_starred_update) {
        *is_starred_update = true;
    }

    // Add entry to live feed ring buffer
    feed_head_ = (feed_head_ + 1) % MAX_FEED_ENTRIES;
    FeedEntry* fe = &feed_[feed_head_];
#ifndef NATIVE_TEST
    fe->timestamp_ms = (uint32_t)millis();
#else
    fe->timestamp_ms = 0;
#endif
    snprintf(fe->meter_id,    sizeof(fe->meter_id),    "%s", m->id);
    snprintf(fe->driver_name, sizeof(fe->driver_name), "%s", m->driver_name);
    snprintf(fe->mfct, sizeof(fe->mfct), "%s", m->mfct);

    // Compact summary: just the value + unit (no field name, fits on small display)
    if (m->primary_field_name[0] && strcmp(m->primary_field_name, "status") != 0) {
        snprintf(fe->summary, sizeof(fe->summary), "%s", m->primary_value_str);
    } else {
        snprintf(fe->summary, sizeof(fe->summary), "%s", m->primary_value_str);
    }
    fe->rssi      = rssi;
    fe->is_starred = m->is_starred;

    if (feed_count_ < MAX_FEED_ENTRIES) {
        feed_count_++;
    }

    sort_meters();
    unlock();
    return m;
}

MeterEntry* MeterDatabase::get_meter(size_t index) {
    if (index >= meter_count_) return nullptr;
    return &meters_[index];
}

const MeterEntry* MeterDatabase::get_meter(size_t index) const {
    if (index >= meter_count_) return nullptr;
    return &meters_[index];
}

MeterEntry* MeterDatabase::find_meter_by_id(const char* id) {
    for (size_t i = 0; i < meter_count_; ++i) {
        if (strcmp(meters_[i].id, id) == 0) return &meters_[i];
    }
    return nullptr;
}

const MeterEntry* MeterDatabase::find_meter_by_id(const char* id) const {
    for (size_t i = 0; i < meter_count_; ++i) {
        if (strcmp(meters_[i].id, id) == 0) return &meters_[i];
    }
    return nullptr;
}

bool MeterDatabase::toggle_star(size_t index) {
    lock();
    if (index >= meter_count_) { unlock(); return false; }
    meters_[index].is_starred = !meters_[index].is_starred;
    sort_meters();
    bool starred = meters_[index].is_starred;
    unlock();
    return starred;
}

bool MeterDatabase::toggle_star_by_id(const char* id) {
    lock();
    MeterEntry* m = find_meter_by_id(id);
    if (!m) { unlock(); return false; }
    m->is_starred = !m->is_starred;
    sort_meters();
    bool starred = m->is_starred;
    unlock();
    return starred;
}

const FeedEntry* MeterDatabase::get_feed_entry(size_t index) const {
    if (index >= feed_count_) return nullptr;
    // index 0 = newest (at feed_head_)
    size_t pos = (feed_head_ + MAX_FEED_ENTRIES - index) % MAX_FEED_ENTRIES;
    return &feed_[pos];
}

void MeterDatabase::clear_feed() {
    lock();
    feed_count_ = 0;
    feed_head_  = 0;
    unlock();
}

void MeterDatabase::sort_meters() {
    if (meter_count_ <= 1) return;
    std::sort(meters_, meters_ + meter_count_, [](const MeterEntry& a, const MeterEntry& b) {
        if (a.is_starred != b.is_starred) return a.is_starred > b.is_starred;
        return a.last_seen_ms > b.last_seen_ms;
    });
}

} // namespace wmb
