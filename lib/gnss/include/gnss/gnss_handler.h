// wM-Buster ADV — GNSS (AT6668 / ATGM336H) Handler Interface
// GPL-3.0
#pragma once

#include <stdint.h>

namespace wmb {

void init_gnss();
void update_gnss();
bool get_gnss_fix(double* lat, double* lon, bool* valid);
uint32_t gnss_satellites();  // 0 if unknown

} // namespace wmb
