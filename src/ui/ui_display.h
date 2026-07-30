// wM-Buster ADV — Display
// GPL-3.0
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "meter_db.h"
#include "ui_input.h"
#include <cmath>

namespace wmb {

enum class UIScreen { MainMenu, Home, Detail, Status, Settings, WebUI, ChargeMode };

class UIDisplay {
public:
    UIDisplay();
    void init();
    void update(const MeterDatabase& db);
    void handle_event(UIEvent ev, MeterDatabase& db);
    void trigger_starred_alert();
    void trigger_normal_alert();
    void reset_feed_scroll() {}   // no-op — home screen is always up-to-date
    
    bool is_animating() const { return (cur_ == UIScreen::MainMenu) && (std::abs(menu_selected_ - menu_offset_) > 0.01f); }

    bool is_audio_enabled()      const;
    bool is_sd_logging_enabled() const;
    void set_sd_logging_enabled(bool v);
    bool is_charge_mode()        const;
    void toggle_charge_mode();

private:
    UIScreen cur_;
    size_t   top_;          // index of the top card on Home
    size_t   det_scroll_;   // field scroll in Detail
    uint32_t flash_ms_;
    uint32_t last_interaction_ms_;
    uint32_t last_draw_ms_;
    uint8_t  brightness_ = 100;
    
    // For settings menu
    int settings_sel_ = 0;

    void draw_main_menu(const MeterDatabase& db);
    void draw_home(const MeterDatabase& db);
    void draw_detail(const MeterDatabase& db);
    void draw_status(const MeterDatabase& db);
    void draw_settings();
    void draw_webui();
    void draw_charge();
    
    int   menu_selected_ = 0;
    float menu_offset_ = 0.0f;
    void* menu_sprite_ = nullptr; // Forward declared as void* to avoid M5GFX in header
};

UIDisplay& get_global_ui_display();

} // namespace wmb
