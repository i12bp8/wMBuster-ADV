// wM-Buster ADV — Ntfy.sh Publisher
// GPL-3.0
#pragma once

#ifndef NATIVE_TEST

namespace wmb {
    
// Publish an alert to Ntfy.sh if configured
void ntfy_publish(const char* meter_id, const char* primary_value, const char* driver_name);

}

#endif // NATIVE_TEST
