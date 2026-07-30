// wM-Buster ADV — GNSS Handler
// AT6668/ATGM336H — factory default baud is 9600, NOT 115200.
// GPL-3.0
#include "gnss/gnss_handler.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <TinyGPS++.h>
#include "config.h"

namespace wmb {

static TinyGPSPlus      gps;
static HardwareSerial   gnssSerial(2);  // UART2

void init_gnss() {
    // AT6668 / ATGM336H powers up at 9600 baud by default.
    // G15 = ESP32 RX (module TX), G13 = ESP32 TX (module RX).
    gnssSerial.begin(9600, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
    Serial.println("[GPS] UART2 started — 9600 baud  RX=G15  TX=G13");
}

void update_gnss() {
    while (gnssSerial.available())
        gps.encode((char)gnssSerial.read());

    // g_stats.gnss_sats repurposed as NMEA-active flag:
    //   0 = no bytes ever received  →  "No signal from module"
    //   1 = NMEA bytes received     →  "Searching (needs open sky)"
    // config.h declares  namespace wmb { extern WMBStats g_stats; }
    // so g_stats is in scope here directly — no extra extern needed.
    if (gps.charsProcessed() > 10 && g_stats.gnss_sats == 0)
        g_stats.gnss_sats = 1;

    // Periodic debug log every 10 s
    static uint32_t last_dbg = 0;
    if (millis() - last_dbg >= 10000) {
        last_dbg = millis();
        Serial.printf("[GPS] chars=%lu  sentences=%lu  fix=%s",
            gps.charsProcessed(),
            gps.sentencesWithFix(),
            gps.location.isValid() ? "YES" : "NO");
        if (gps.location.isValid()) {
            Serial.printf("  lat=%.6f  lon=%.6f  age=%lums",
                gps.location.lat(), gps.location.lng(),
                (unsigned long)gps.location.age());
        }
        Serial.println();

        if (gps.charsProcessed() < 10) {
            Serial.println("[GPS] *** NO DATA from module — check wiring, baud, or antenna ***");
        } else if (!gps.location.isValid() && gps.charsProcessed() > 100) {
            Serial.println("[GPS] Receiving NMEA, waiting for satellite fix (needs open sky).");
        }
    }
}

bool get_gnss_fix(double* lat, double* lon, bool* valid) {
    if (gps.location.isValid() && gps.location.age() < 5000) {
        if (lat)   *lat   = gps.location.lat();
        if (lon)   *lon   = gps.location.lng();
        if (valid) *valid = true;
        return true;
    }
    if (lat)   *lat   = 0.0;
    if (lon)   *lon   = 0.0;
    if (valid) *valid = false;
    return false;
}

uint32_t gnss_satellites() {
    return gps.satellites.isValid() ? (uint32_t)gps.satellites.value() : 0;
}

} // namespace wmb

#else
namespace wmb {
void    init_gnss()  {}
void    update_gnss() {}
bool    get_gnss_fix(double* la, double* lo, bool* v) {
    if(la)*la=0; if(lo)*lo=0; if(v)*v=false; return false;
}
uint32_t gnss_satellites() { return 0; }
}
#endif
