// wM-Buster ADV — Persistent Configuration Store Implementation
// GPL-3.0
#include "config_store.h"
#include <string.h>
#include <stdio.h>

#ifndef NATIVE_TEST
#include <Preferences.h>
#include <FS.h>
#include <SD.h>

namespace wmb {

DeviceSettings g_settings;

// NVS namespace used for all keys (max 15 chars).
static const char* NS = "wmb";

// ── Helpers ──────────────────────────────────────────────────────────────────

bool cs_hex_to_key(const char* hex, uint8_t* out) {
    if (!hex) return false;
    size_t n = strlen(hex);
    if (n < 32) return false;
    for (int i = 0; i < 16; i++) {
        char hi = hex[i*2], lo = hex[i*2+1];
        auto hv = [](char c) -> uint8_t {
            if (c>='0'&&c<='9') return c-'0';
            if (c>='a'&&c<='f') return c-'a'+10;
            if (c>='A'&&c<='F') return c-'A'+10;
            return 0;
        };
        out[i] = (hv(hi)<<4)|hv(lo);
    }
    return true;
}

void cs_key_to_hex(const uint8_t* key, char* out) {
    for (int i = 0; i < 16; i++)
        snprintf(out + i*2, 3, "%02X", key[i]);
    out[32] = '\0';
}

// NVS key for a per-meter field.  Stays ≤15 chars.
// Field: 'n'=name  'k'=key  'd'=driver
static void meter_nvskey(char* buf, size_t blen, const char* id, char field) {
    snprintf(buf, blen, "%c_%.8s", field, id);
}

// ── Init ─────────────────────────────────────────────────────────────────────

void cs_init() { /* NVS is always available — nothing to mount */ }

// ── Meter IDs list ────────────────────────────────────────────────────────────
// We keep a comma-separated list "12345678,87654321,..." under key "ids".

static void load_id_list(Preferences& p, char* buf, size_t blen) {
    buf[0] = '\0';
    // isKey() avoids the verbose "NOT_FOUND" log from getString on missing keys
    if (!p.isKey("ids")) return;
    String s = p.getString("ids", "");
    snprintf(buf, blen, "%s", s.c_str());
}

static void save_id_list(Preferences& p, const char* buf) {
    p.putString("ids", buf);
}

// Returns true if id is already in the comma-list, false otherwise.
static bool id_in_list(const char* list, const char* id) {
    const char* p = list;
    while (*p) {
        const char* comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == 8 && strncmp(p, id, 8) == 0) return true;
        p += len + (comma ? 1 : 0);
    }
    return false;
}

// Append id to list if not already there.
static void add_id_to_list(char* list, size_t lbuf, const char* id) {
    if (id_in_list(list, id)) return;
    if (list[0]) strncat(list, ",", lbuf - strlen(list) - 1);
    strncat(list, id, lbuf - strlen(list) - 1);
}

// Remove id from comma-list.
static void remove_id_from_list(char* list, const char* id) {
    char tmp[512] = "";
    const char* p = list;
    bool first = true;
    while (*p) {
        const char* comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (!(len == 8 && strncmp(p, id, 8) == 0)) {
            if (!first) strncat(tmp, ",", sizeof(tmp) - strlen(tmp) - 1);
            strncat(tmp, p, len < sizeof(tmp)-strlen(tmp)-1 ? len : sizeof(tmp)-strlen(tmp)-1);
            first = false;
        }
        p += len + (comma ? 1 : 0);
    }
    strncpy(list, tmp, 511); list[511] = '\0';
}

// ── Meter config CRUD ─────────────────────────────────────────────────────────

bool cs_save_meter(const MeterConfig& mc) {
    if (!mc.id[0]) return false;
    Preferences p;
    p.begin(NS, false);

    // Update ID list
    char ids[512]; load_id_list(p, ids, sizeof(ids));
    add_id_to_list(ids, sizeof(ids), mc.id);
    save_id_list(p, ids);

    // Write fields
    char k[16];
    meter_nvskey(k, sizeof(k), mc.id, 'n');
    p.putString(k, mc.name);

    meter_nvskey(k, sizeof(k), mc.id, 'd');
    p.putString(k, mc.driver);

    if (mc.has_key) {
        char hexbuf[33];
        cs_key_to_hex(mc.key, hexbuf);
        meter_nvskey(k, sizeof(k), mc.id, 'k');
        p.putString(k, hexbuf);
    } else {
        meter_nvskey(k, sizeof(k), mc.id, 'k');
        p.putString(k, "");
    }

    p.end();
    return true;
}

bool cs_delete_meter(const char* id) {
    if (!id || !id[0]) return false;
    Preferences p;
    p.begin(NS, false);

    char ids[512]; load_id_list(p, ids, sizeof(ids));
    remove_id_from_list(ids, id);
    save_id_list(p, ids);

    char k[16];
    meter_nvskey(k, sizeof(k), id, 'n'); p.remove(k);
    meter_nvskey(k, sizeof(k), id, 'd'); p.remove(k);
    meter_nvskey(k, sizeof(k), id, 'k'); p.remove(k);

    p.end();
    return true;
}

bool cs_find_meter(const char* id, MeterConfig* out) {
    if (!id || !id[0] || !out) return false;
    Preferences p;
    p.begin(NS, false);  // read-only

    // Check if this ID is in the list
    char ids[512]; load_id_list(p, ids, sizeof(ids));
    if (!id_in_list(ids, id)) { p.end(); return false; }

    memset(out, 0, sizeof(MeterConfig));
    snprintf(out->id, sizeof(out->id), "%.8s", id);

    char k[16];
    meter_nvskey(k, sizeof(k), id, 'n');
    snprintf(out->name, sizeof(out->name), "%s", p.getString(k, "").c_str());

    meter_nvskey(k, sizeof(k), id, 'd');
    snprintf(out->driver, sizeof(out->driver), "%s", p.getString(k, "auto").c_str());

    meter_nvskey(k, sizeof(k), id, 'k');
    String keyhex = p.getString(k, "");
    if (keyhex.length() >= 32) {
        out->has_key = cs_hex_to_key(keyhex.c_str(), out->key);
    }

    p.end();
    return true;
}

size_t cs_get_all_meters(MeterConfig* out, size_t max_out) {
    if (!out || !max_out) return 0;
    Preferences p;
    p.begin(NS, false);

    char ids[512]; load_id_list(p, ids, sizeof(ids));
    p.end();

    size_t count = 0;
    const char* ptr = ids;
    while (*ptr && count < max_out) {
        const char* comma = strchr(ptr, ',');
        size_t len = comma ? (size_t)(comma - ptr) : strlen(ptr);
        if (len == 8) {
            char id[9]; strncpy(id, ptr, 8); id[8] = '\0';
            if (cs_find_meter(id, &out[count])) count++;
        }
        ptr += len + (comma ? 1 : 0);
    }
    return count;
}

// ── Device settings ───────────────────────────────────────────────────────────

void cs_save_settings(const DeviceSettings& s) {
    Preferences p;
    p.begin(NS, false);
    p.putUChar("s_rhw", s.radio_hardware);
    p.putString("s_mode",  s.radio_mode);
    p.putString("s_mhost", s.mqtt_host);
    p.putUShort("s_mport", s.mqtt_port);
    p.putString("s_musr",  s.mqtt_user);
    p.putString("s_mpwd",  s.mqtt_pass);
    p.putString("s_wssid", s.wifi_ssid);
    p.putString("s_wpass", s.wifi_pass);
    p.putBool("s_hadis", s.ha_discovery);
    
    p.putString("s_wui_p", s.webui_ap_pass);
    p.putBool("s_wui_e", s.webui_enabled);
    p.putUChar("s_wui_m", s.webui_mode);
    p.putString("s_ntfy", s.ntfy_url);
    p.putInt("s_theme", s.theme_idx);
    p.putBool("s_sdl", s.sd_logging);
    p.putBool("s_mute", s.mute);
    p.putBool("s_chg", s.charge_mode);

    p.end();
}

void cs_load_settings(DeviceSettings* s) {
    memset(s, 0, sizeof(DeviceSettings));
    Preferences p;
    p.begin(NS, false);
    s->radio_hardware = p.getUChar("s_rhw", 0);
    snprintf(s->radio_mode, sizeof(s->radio_mode), "%s",
             p.getString("s_mode", "C1/T1").c_str());
    snprintf(s->mqtt_host, sizeof(s->mqtt_host), "%s",
             p.getString("s_mhost", "").c_str());
    s->mqtt_port = p.getUShort("s_mport", 1883);
    snprintf(s->mqtt_user, sizeof(s->mqtt_user), "%s",
             p.getString("s_musr", "").c_str());
    snprintf(s->mqtt_pass, sizeof(s->mqtt_pass), "%s",
             p.getString("s_mpwd", "").c_str());
    snprintf(s->wifi_ssid, sizeof(s->wifi_ssid), "%s",
             p.getString("s_wssid", "").c_str());
    snprintf(s->wifi_pass, sizeof(s->wifi_pass), "%s",
             p.getString("s_wpass", "").c_str());
    s->ha_discovery = p.getBool("s_hadis", false);

    snprintf(s->webui_ap_pass, sizeof(s->webui_ap_pass), "%s",
             p.getString("s_wui_p", "").c_str());
    s->webui_enabled = p.getBool("s_wui_e", false);
    s->webui_mode = p.getUChar("s_wui_m", 0);
    snprintf(s->ntfy_url, sizeof(s->ntfy_url), "%s",
             p.getString("s_ntfy", "").c_str());
    s->theme_idx = p.getInt("s_theme", 0);
    s->sd_logging = p.getBool("s_sdl", false);
    s->mute = p.getBool("s_mute", false);
    s->charge_mode = p.getBool("s_chg", false);

    p.end();

    // Generate random WebUI AP password if empty
    if (s->webui_ap_pass[0] == '\0') {
        const char* charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        for (int i = 0; i < 8; i++) {
            s->webui_ap_pass[i] = charset[esp_random() % 62];
        }
        s->webui_ap_pass[8] = '\0';
        cs_save_settings(*s);
    }
}

void cs_import_keys_from_sd() {
#ifndef NATIVE_TEST
    if (!SD.exists("/keys.txt")) {
        return;
    }
    File f = SD.open("/keys.txt", FILE_READ);
    if (!f) return;
    
    Serial.println("[NVS] Importing keys from /keys.txt");
    int imported = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) continue;
        
        int colon = line.indexOf(':');
        int eq = line.indexOf('=');
        int sep = (colon > 0) ? colon : eq;
        
        if (sep > 0) {
            String id = line.substring(0, sep);
            String key = line.substring(sep + 1);
            id.trim();
            key.trim();
            key.replace(" ", ""); // Remove spaces from key just in case
            
            if (id.length() > 0 && id.length() <= 8 && key.length() == 32) {
                MeterConfig mc;
                if (!cs_find_meter(id.c_str(), &mc)) {
                    memset(&mc, 0, sizeof(mc));
                    snprintf(mc.id, sizeof(mc.id), "%s", id.c_str());
                    snprintf(mc.name, sizeof(mc.name), "Imported");
                }
                
                uint8_t kbuf[16];
                if (cs_hex_to_key(key.c_str(), kbuf)) {
                    memcpy(mc.key, kbuf, 16);
                    mc.has_key = true;
                    if (cs_save_meter(mc)) {
                        imported++;
                    }
                }
            }
        }
    }
    f.close();
    Serial.printf("[NVS] Successfully imported %d keys from SD\n", imported);
#endif
}

} // namespace wmb

#else  // NATIVE_TEST stubs

namespace wmb {
void  cs_init() {}
bool  cs_save_meter(const MeterConfig&) { return false; }
bool  cs_delete_meter(const char*) { return false; }
bool  cs_find_meter(const char*, MeterConfig*) { return false; }
size_t cs_get_all_meters(MeterConfig*, size_t) { return 0; }
void  cs_save_settings(const DeviceSettings&) {}
void  cs_load_settings(DeviceSettings* s) { memset(s, 0, sizeof(*s)); }

bool cs_hex_to_key(const char* hex, uint8_t* out) {
    if (!hex || strlen(hex) < 32) return false;
    for (int i=0;i<16;i++){
        auto hv=[](char c)->uint8_t{
            if(c>='0'&&c<='9')return c-'0';
            if(c>='a'&&c<='f')return c-'a'+10;
            if(c>='A'&&c<='F')return c-'A'+10;
            return 0;};
        out[i]=(hv(hex[i*2])<<4)|hv(hex[i*2+1]);
    }
    return true;
}
void cs_key_to_hex(const uint8_t* k, char* o) {
    for(int i=0;i<16;i++) snprintf(o+i*2,3,"%02X",k[i]);
    o[32]='\0';
}
} // namespace wmb

#endif // NATIVE_TEST
