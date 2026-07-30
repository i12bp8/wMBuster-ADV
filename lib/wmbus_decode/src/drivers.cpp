// Driver lookup: MVT-based auto-detection + by-name search.
// GPL-3.0
#include "wmbus_decode/driver_table.h"
#include <string.h>

namespace wmb {

const DriverDef* find_driver(const char mfct[4], uint8_t version, uint8_t type) {
    // Pass 1: Exact match (Manufacturer + Version + Type)
    for (size_t i = 0; i < DRIVERS_LEN; ++i) {
        const DriverDef* d = &DRIVERS[i];
        for (uint8_t j = 0; j < d->num_detects; ++j) {
            const DriverDetect* det = &d->detects[j];
            if (strncmp(det->m, mfct, 3) != 0) continue;
            if (det->ver != 0xFF && det->ver != version) continue;
            if (det->typ != 0xFF && det->typ != type) continue;
            return d;
        }
    }

    // Pass 2: Loose match (Manufacturer + Type). Very common for minor firmware bumps.
    for (size_t i = 0; i < DRIVERS_LEN; ++i) {
        const DriverDef* d = &DRIVERS[i];
        for (uint8_t j = 0; j < d->num_detects; ++j) {
            const DriverDetect* det = &d->detects[j];
            if (strncmp(det->m, mfct, 3) != 0) continue;
            if (det->typ != 0xFF && det->typ != type) continue;
            return d; // Found a fallback driver of the same type!
        }
    }

    // Pass 3: Desperation match (Manufacturer only). Tries its best.
    for (size_t i = 0; i < DRIVERS_LEN; ++i) {
        const DriverDef* d = &DRIVERS[i];
        for (uint8_t j = 0; j < d->num_detects; ++j) {
            const DriverDetect* det = &d->detects[j];
            if (strncmp(det->m, mfct, 3) == 0) {
                return d;
            }
        }
    }

    return nullptr;
}

const DriverDef* find_driver_by_name(const char* name) {
    for (size_t i = 0; i < DRIVERS_LEN; ++i) {
        if (strcmp(DRIVERS[i].name, name) == 0) return &DRIVERS[i];
    }
    return nullptr;
}

} // namespace wmb
