// wM-Buster ADV — Core Application Controller
// GPL-3.0
#ifndef NATIVE_TEST

#include "AppController.h"
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "config.h"
#include "../storage/config_store.h"
#include "../ui/meter_db.h"
#include "../ui/ui_display.h"
#include "../net/mqtt_client.h"
#include "../net/ntfy_client.h"

#include "wmbus_phy/telegram.h"
#include "wmbus_phy/coding.h"
#include "wmbus_phy/frame.h"
#include "wmbus_decode/interpreter.h"
#include "wmbus_decode/driver_table.h"

using namespace wmb;

namespace wmb { 
    WMBStats g_stats = {}; 
}

void AppController::init() {
    // Initialization handled largely in main.cpp to preserve explicit ordering,
    // but controller specific setups could go here.
}

void AppController::update() {
    // Background tasks can be routed through here
}

size_t AppController::parse_hex(const char* hex, uint8_t* out, size_t max) {
    size_t n = 0;
    while (*hex && n < max) {
        while (*hex == ' ' || *hex == '\t' || *hex == '\r' || *hex == '\n') hex++;
        if (!*hex) break;
        char hi = *hex++, lo = *hex++; 
        if (!lo) break;
        
        auto hv = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out[n++] = (hv(hi) << 4) | hv(lo);
    }
    return n;
}

void AppController::log_to_sd(const DecodeResult& res, float rssi) {
    if (!get_global_ui_display().is_sd_logging_enabled()) return;
    
    if (!SD.cardType()) { 
        SPI.begin(40, 39, 14, 12); 
        SD.begin(12, SPI, 25000000); 
    }
    
    File f = SD.open("/captures.csv", FILE_APPEND);
    if (!f) return;
    
    if (f.size() == 0) {
        f.println("timestamp_ms,id,driver,media,rssi,lat,lon,json_fields");
    }
    
    f.printf("%lu,%s,%s,%s,%.0f,%.6f,%.6f,\"{",
             millis(), res.id,
             res.driver ? res.driver->name : "unknown",
             res.media, rssi, g_stats.gnss_lat, g_stats.gnss_lon);
             
    for (int i = 0; i < res.num_fields; i++) {
        if (res.fields[i].hidden) continue;
        if (i > 0) f.print(",");
        f.printf("\"\"%s\"\":\"", res.fields[i].name);
        if (res.fields[i].is_text) {
            f.print(res.fields[i].text);
        } else {
            f.printf("%.3f", res.fields[i].value);
        }
        f.print("\"\"");
    }
    f.println("}\"");
    f.close();
}

void AppController::process_raw_telegram(const uint8_t* raw, size_t raw_len, float rssi, float snr) {
    g_stats.radio_rx_total++;
    g_stats.radio_rssi_live = rssi;
    g_stats.radio_snr_live = snr;

    bool has_sync = (raw_len >= 3 && raw[0] == 0x54 && raw[1] == 0xCD);
    uint8_t L = has_sync ? raw[2] : (raw_len > 0 ? raw[0] : 0);
    
    Serial.printf("[RF] %d bytes (L=%d%s) RSSI:%.0fdBm SNR:%.1fdB  [",
                  (int)raw_len, (int)L, has_sync ? " Techem-sync" : "", rssi, snr);
    for (size_t i = 0; i < (raw_len > 8 ? 8 : raw_len); i++) {
        Serial.printf("%02X", raw[i]);
    }
    Serial.println(raw_len > 8 ? "...]" : "]");

    Frame f; 
    bool valid = false;
    bool ok36 = false;
    
    size_t t1l = coding_3outof6_decode(raw, raw_len, m_t1_buf, sizeof(m_t1_buf), &ok36);
    if (ok36 && t1l >= 12) {
        memcpy(f.data, m_t1_buf, t1l); 
        f.len = t1l;
        f.mode = LinkMode::T1; 
        f.rssi = (int16_t)rssi; 
        f.snr = snr;
        if (frame_trim_crc(&f)) valid = true;
    }

    if (!valid) {
        size_t off = has_sync ? 2 : 0;
        size_t cl = raw_len - off; 
        if (cl > sizeof(f.data)) cl = sizeof(f.data);
        memcpy(f.data, raw + off, cl); 
        f.len = cl;
        f.mode = LinkMode::C1; 
        f.rssi = (int16_t)rssi; 
        f.snr = snr;
        if (frame_trim_crc(&f)) valid = true;
    }

    if (!valid) {
        g_stats.radio_rx_bad++;
        int aL = has_sync ? (raw_len > 2 ? (int)raw[2] : -1) : (raw_len > 0 ? (int)raw[0] : -1);
        Serial.printf("[RF] CRC fail (L=%d%s)\n", aL, has_sync ? " Techem-sync" : "");
        return;
    }

    Telegram t;
    if (!telegram_parse(&f, &t)) {
        g_stats.radio_rx_bad++;
        Serial.println("[WMBUS] Header parse failed");
        return;
    }
    
    Serial.printf("[WMBUS] DLL: mfct=%s id=%s ver=0x%02X type=0x%02X CI=0x%02X mode=%s\n",
                  t.mfct_str, t.dll_id_str, t.dll_version, t.dll_type, t.ci,
                  f.mode == LinkMode::T1 ? "T1" : "C1");

    MeterConfig mc; 
    memset(&mc, 0, sizeof(mc));
    bool have_cfg = cs_find_meter(t.dll_id_str, &mc);
    
    const uint8_t* key_ptr = (have_cfg && mc.has_key) ? mc.key : nullptr;
    const DriverDef* forced = nullptr;
    if (have_cfg && mc.driver[0] && strcmp(mc.driver, "auto") != 0) {
        forced = find_driver_by_name(mc.driver);
    }

    size_t plen = t.payload_len;
    if (plen > sizeof(m_payload_buf)) plen = sizeof(m_payload_buf);
    memcpy(m_payload_buf, t.payload, plen);

    static DecodeResult s_res;
    if (!decode_telegram(t, m_payload_buf, plen, key_ptr, forced, &s_res)) {
        g_stats.radio_rx_encrypted++;
        Serial.printf("[WMBUS] No decode: %s %s\n", t.mfct_str, t.dll_id_str);
        return;
    }
    g_stats.radio_rx_good++;

    bool is_new = false, is_starred = false;
    MeterEntry* m = get_global_meter_db().add_reading(
        s_res, rssi, snr,
        g_stats.gnss_lat, g_stats.gnss_lon, g_stats.gnss_fix,
        &is_new, &is_starred);

    if (m && have_cfg && mc.name[0]) {
        snprintf(m->name, sizeof(m->name), "%s", mc.name);
    }

    Serial.printf("[WMBUS] *** %s  ID:%s  %.0fdBm ***\n",
        s_res.driver ? s_res.driver->name : "unknown", s_res.id, rssi);

    log_to_sd(s_res, rssi);
    publish_mqtt_reading(s_res, rssi);

    if (is_starred) {
        ntfy_publish(s_res.id, m->primary_value_str, s_res.driver ? s_res.driver->name : "Unknown");
        get_global_ui_display().trigger_starred_alert();
    } else {
        get_global_ui_display().trigger_normal_alert();
    }

    get_global_ui_display().reset_feed_scroll();
}

bool AppController::do_analyze(const char* hex, char* jout, size_t jmax) {
    uint8_t raw[290];
    size_t rl = parse_hex(hex, raw, sizeof(raw));
    if (!rl) { 
        snprintf(jout, jmax, "{\"error\":\"Invalid hex\"}"); 
        return false; 
    }

    Frame f; 
    bool valid = false; 
    const char* mode = "C1";
    bool ok36 = false;
    
    size_t t1l = coding_3outof6_decode(raw, rl, m_az_t1, sizeof(m_az_t1), &ok36);
    if (ok36 && t1l >= 12) {
        memcpy(f.data, m_az_t1, t1l); 
        f.len = t1l;
        f.mode = LinkMode::T1; 
        f.rssi = -70; 
        f.snr = 8;
        if (frame_trim_crc(&f)) {
            valid = true; 
            mode = "T1";
        }
    }
    
    if (!valid) {
        size_t off = (rl >= 3 && raw[0] == 0x54 && raw[1] == 0xCD) ? 2 : 0;
        size_t cl = rl - off; 
        if (cl > sizeof(f.data)) cl = sizeof(f.data);
        memcpy(f.data, raw + off, cl); 
        f.len = cl;
        f.mode = LinkMode::C1; 
        f.rssi = -70; 
        f.snr = 8;
        if (frame_trim_crc(&f)) valid = true;
    }
    
    if (!valid) { 
        snprintf(jout, jmax, "{\"error\":\"CRC failed\"}"); 
        return false; 
    }

    Telegram t;
    if (!telegram_parse(&f, &t)) { 
        snprintf(jout, jmax, "{\"error\":\"Parse failed\"}"); 
        return false; 
    }

    MeterConfig mc; 
    memset(&mc, 0, sizeof(mc));
    bool have_cfg = cs_find_meter(t.dll_id_str, &mc);
    const uint8_t* key_ptr = (have_cfg && mc.has_key) ? mc.key : nullptr;

    size_t plen = t.payload_len; 
    if (plen > sizeof(m_az_pl)) plen = sizeof(m_az_pl);
    memcpy(m_az_pl, t.payload, plen);

    static DecodeResult s_az_res;
    bool decoded = decode_telegram(t, m_az_pl, plen, key_ptr, nullptr, &s_az_res);
    
    size_t pos = 0;
    pos += snprintf(jout + pos, jmax - pos,
        "{\"success\":%s,\"driver\":\"%s\",\"id\":\"%s\",\"media\":\"%s\","
        "\"mfct\":\"%s\",\"mode\":\"%s\",\"fields\":[",
        decoded ? "true" : "false",
        s_az_res.driver ? s_az_res.driver->name : "unknown",
        s_az_res.id, s_az_res.media, t.mfct_str, mode);
        
    bool first = true;
    for (int i = 0; i < s_az_res.num_fields && pos < jmax - 80; i++) {
        const OutField& of = s_az_res.fields[i];
        if (of.hidden) continue;
        if (!first) jout[pos++] = ','; 
        first = false;
        
        if (of.is_text) {
            pos += snprintf(jout + pos, jmax - pos, "{\"name\":\"%s\",\"val\":\"%s\"}", of.name, of.text);
        } else {
            pos += snprintf(jout + pos, jmax - pos, "{\"name\":\"%s\",\"val\":\"%.3f\"}", of.name, of.value);
        }
    }
    pos += snprintf(jout + pos, jmax - pos, "]}");
    return decoded;
}

#endif // NATIVE_TEST
