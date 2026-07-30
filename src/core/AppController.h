// wM-Buster ADV — Core Application Controller
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>

struct WMBStats;
namespace wmb {
    struct DecodeResult;
}

class AppController {
public:
    static AppController& instance() {
        static AppController inst;
        return inst;
    }

    void init();
    void update();

    // Process a raw frame from the radio
    void process_raw_telegram(const uint8_t* raw, size_t raw_len, float rssi = -75.f, float snr = 6.f);

    // Web analyze callback (dry-run, no side effects)
    bool do_analyze(const char* hex, char* jout, size_t jmax);

private:
    AppController() = default;

    void log_to_sd(const wmb::DecodeResult& res, float rssi);
    size_t parse_hex(const char* hex, uint8_t* out, size_t max);

    // Isolated buffers to prevent stack overflows
    uint8_t m_t1_buf[290];
    uint8_t m_payload_buf[290];
    
    uint8_t m_az_t1[290];
    uint8_t m_az_pl[290];
};
