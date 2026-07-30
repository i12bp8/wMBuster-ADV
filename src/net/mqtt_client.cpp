// wM-Buster ADV — MQTT Publisher Implementation
// GPL-3.0
#include "mqtt_client.h"

#ifndef NATIVE_TEST
#include <WiFi.h>
#include <PubSubClient.h>

namespace wmb {

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

static bool s_mqtt_enabled = false;
static char s_mqtt_host[64] = "192.168.1.100";
static uint16_t s_mqtt_port = 1883;

bool is_mqtt_enabled() { return s_mqtt_enabled; }
void set_mqtt_enabled(bool en) { s_mqtt_enabled = en; }
void set_mqtt_server(const char* host, uint16_t port) {
    snprintf(s_mqtt_host, sizeof(s_mqtt_host), "%s", host);
    s_mqtt_port = port;
    mqttClient.setServer(s_mqtt_host, s_mqtt_port);
}

void init_mqtt() {
    mqttClient.setServer(s_mqtt_host, s_mqtt_port);
}

void update_mqtt() {
    if (!s_mqtt_enabled || WiFi.status() != WL_CONNECTED) return;

    if (!mqttClient.connected()) {
        static unsigned long last_reconnect_ms = 0;
        if (millis() - last_reconnect_ms > 5000) {
            last_reconnect_ms = millis();
            String clientId = "wMBusterADV-" + String(random(0xffff), HEX);
            mqttClient.connect(clientId.c_str());
        }
    } else {
        mqttClient.loop();
    }
}

void publish_mqtt_reading(const DecodeResult& res, float rssi) {
    if (!s_mqtt_enabled || !mqttClient.connected()) return;

    String topic = "wmbusmeters/" + String(res.id);
    String payload = "{";
    payload += "\"id\":\"" + String(res.id) + "\",";
    payload += "\"driver\":\"" + String(res.driver ? res.driver->name : "unknown") + "\",";
    payload += "\"media\":\"" + String(res.media) + "\",";
    payload += "\"rssi\":" + String((int)rssi);

    for (int i = 0; i < res.num_fields; ++i) {
        const OutField* f = &res.fields[i];
        if (f->hidden) continue;
        payload += ",\"";
        payload += f->name;
        payload += "\":";
        if (f->is_text) {
            payload += "\"";
            payload += f->text;
            payload += "\"";
        } else {
            payload += String(f->value, 3);
        }
    }
    payload += "}";

    mqttClient.publish(topic.c_str(), payload.c_str());

    // Home Assistant Auto-Discovery
    String discovery_topic = "homeassistant/sensor/wmbus_" + String(res.id) + "/config";
    String discovery_payload = "{\"name\":\"wMBus Meter " + String(res.id) + "\",\"stat_t\":\"" + topic + "\",\"val_tpl\":\"{{ value_json.current_hca }}\",\"uniq_id\":\"wmbus_" + String(res.id) + "\",\"dev\":{\"ids\":[\"wmbus_" + String(res.id) + "\"],\"name\":\"wM-Bus Meter " + String(res.id) + "\",\"mf\":[\"" + String(res.mfct) + "\"]}}";
    mqttClient.publish(discovery_topic.c_str(), discovery_payload.c_str(), true);
}

} // namespace wmb
#else
namespace wmb {
void init_mqtt() {}
void update_mqtt() {}
void publish_mqtt_reading(const DecodeResult& res, float rssi) {}
bool is_mqtt_enabled() { return false; }
void set_mqtt_enabled(bool en) {}
void set_mqtt_server(const char* host, uint16_t port) {}
}
#endif
