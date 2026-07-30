// wM-Buster ADV — Main Firmware Entrypoint
// GPL-3.0
#ifndef NATIVE_TEST

#include <Arduino.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>
#include <M5Cardputer.h>
#include <M5Unified.h>

#include "config.h"
#include "core/AppController.h"
#include "storage/config_store.h"
#include "ui/meter_db.h"
#include "ui/ui_input.h"
#include "ui/ui_display.h"
#include "ui/theme.h"
#include "net/web_server.h"
#include "net/mqtt_client.h"
#include "radio_sx1262/wmbus_radio.h"
#include "wmbus_decode/techem_decoders.h"
#include "gnss/gnss_handler.h"

using namespace wmb;

static UIInput g_input;
static char s_az_json[4096];

// Web API wrapper to route to our AppController
static bool analyze_wrapper(const char* hex, char* jout, size_t jmax) {
    return AppController::instance().do_analyze(hex, jout, jmax);
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);
    delay(300);
    
    Serial.println("=== wM-Buster ADV ===");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    SPI.begin(40, 39, 14, 12);
    
    if (!SD.begin(12, SPI, 25000000)) {
        Serial.println("[SD] No card");
    } else {
        Serial.println("[SD] Card OK");
    }

    cs_init();
    cs_load_settings(&g_settings);
    cs_import_keys_from_sd();

    g_input.init();
    
    ThemeManager::instance().setTheme(g_settings.theme_idx);
    get_global_ui_display().init();

    if (g_settings.radio_hardware == 1) {
        Serial.println("[MAIN] Hydra RF active, disabling GNSS (Hardware conflict on GPIO 13)");
    } else {
        init_gnss();
    }
    register_techem_payload_decoders();

    set_analyze_callback(analyze_wrapper);
    init_web_server();
    
    if (g_settings.webui_enabled) {
        Serial.printf("[WEB] AP wM-Buster ADV  192.168.4.1\n");
    }

    if (g_settings.mqtt_host[0]) {
        set_mqtt_server(g_settings.mqtt_host, g_settings.mqtt_port);
        set_mqtt_enabled(true);
    }
    init_mqtt();

    int radio_type = radio_init(g_settings.radio_hardware);
    if (radio_type < 0) {
        Serial.println("[RF] Init failed");
    } else {
        g_settings.radio_hardware = (uint8_t)radio_type;
        bool is_c1t1 = (strcmp(g_settings.radio_mode, "S1") != 0);
        if (!is_c1t1) {
            radio_switch_mode(false); // S1 mode
        } else {
            radio_start_receive(); // C1/T1 mode (default from init)
        }
        Serial.println("[RF] RX started");
    }
}

void loop() {
    static uint32_t last_draw = 0;
    static uint32_t last_stats = 0;
    bool need_draw = false;

    update_web_server();
    update_mqtt();
    if (g_settings.radio_hardware != 1) {
        update_gnss();
        
        // Update global GNSS stats
        double la = 0, lo = 0;
        bool fx = false;
        get_gnss_fix(&la, &lo, &fx);
        g_stats.gnss_lat = la; 
        g_stats.gnss_lon = lo; 
        g_stats.gnss_fix = fx;
        g_stats.gnss_sats = 0; // Not tracking sats currently
    }

    uint32_t now = millis();
    if (now - last_stats >= 1000) { 
        g_stats.uptime_s++; 
        last_stats = now; 
    }

    UIEvent ev = g_input.poll_event();
    if (ev != UIEvent::None) {
        get_global_ui_display().handle_event(ev, get_global_meter_db());
        need_draw = true;
    }

    if (radio_packet_available()) {
        uint8_t rx[290]; 
        int16_t rssi = -110; 
        float snr = 0;
        int l = radio_read_packet(rx, sizeof(rx), &rssi, &snr);
        
        if (l > 0) {
            AppController::instance().process_raw_telegram(rx, (size_t)l, (float)rssi, snr);
            need_draw = true;
        }
        radio_start_receive();
    }

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n'); 
        line.trim();
        if (line.startsWith("--analyze=")) line = line.substring(10);
        
        if (line.length() >= 20) {
            AppController::instance().do_analyze(line.c_str(), s_az_json, sizeof(s_az_json));
            Serial.println(s_az_json);
            need_draw = true;
        }
    }

    bool animating = get_global_ui_display().is_animating();
    if (need_draw || animating || (millis() - last_draw >= 200)) {
        get_global_ui_display().update(get_global_meter_db());
        last_draw = millis();
    }
    
    delay(5);
}

#endif // NATIVE_TEST
