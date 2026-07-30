// wM-Buster ADV — Ntfy.sh Publisher
// GPL-3.0

#include "ntfy_client.h"
#include "../storage/config_store.h"

#ifndef NATIVE_TEST
#include <WiFi.h>
#include <HTTPClient.h>

namespace wmb {

struct NtfyMsg {
    char url[64];
    char id[16];
    char val[64];
    char drv[32];
};

static void ntfy_task(void* param) {
    NtfyMsg* msg = (NtfyMsg*)param;
    
    HTTPClient http;
    http.begin(msg->url);
    
    String title = String("wM-Buster: Meter ") + msg->id;
    http.addHeader("Title", title);
    http.addHeader("Tags", "satellite,warning");
    
    char body[256];
    snprintf(body, sizeof(body), "Driver: %s\nData: %s", msg->drv, msg->val);
    
    int code = http.POST(body);
    if (code > 0) {
        Serial.printf("[Ntfy] Sent notification for %s, response %d\n", msg->id, code);
    } else {
        Serial.printf("[Ntfy] Request failed for %s: %s\n", msg->id, http.errorToString(code).c_str());
    }
    
    http.end();
    delete msg;
    vTaskDelete(NULL);
}

void ntfy_publish(const char* meter_id, const char* primary_value, const char* driver_name) {
    if (g_settings.ntfy_url[0] == '\0') return;
    if (WiFi.status() != WL_CONNECTED) return;
    
    NtfyMsg* msg = new NtfyMsg();
    snprintf(msg->url, sizeof(msg->url), "%s", g_settings.ntfy_url);
    snprintf(msg->id, sizeof(msg->id), "%s", meter_id ? meter_id : "?");
    snprintf(msg->val, sizeof(msg->val), "%s", primary_value ? primary_value : "?");
    snprintf(msg->drv, sizeof(msg->drv), "%s", driver_name ? driver_name : "?");
    
    // Spawn task on Core 0 (Network core) so it doesn't block the UI/Radio on Core 1
    xTaskCreatePinnedToCore(
        ntfy_task,
        "ntfy",
        4096,
        msg,
        1,
        NULL,
        0 
    );
}

} // namespace wmb

#endif // NATIVE_TEST
