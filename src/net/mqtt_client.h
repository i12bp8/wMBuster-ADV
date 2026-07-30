// wM-Buster ADV — MQTT Publisher & Home Assistant Auto-Discovery
// GPL-3.0
#pragma once

#include <stdint.h>
#include "wmbus_decode/interpreter.h"

namespace wmb {

void init_mqtt();
void update_mqtt();
void publish_mqtt_reading(const DecodeResult& res, float rssi);

bool is_mqtt_enabled();
void set_mqtt_enabled(bool en);
void set_mqtt_server(const char* host, uint16_t port);

} // namespace wmb
