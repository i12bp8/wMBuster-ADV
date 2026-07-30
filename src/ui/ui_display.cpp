// wM-Buster ADV — Display Renderer
//
// TWO CARDS.  Top card = selected (full brightness, rounded bg).
// Bottom = preview (dimmed, plain bg).
//
// GPL-3.0

#include "ui_display.h"
#include "config.h"
#include "theme.h"
#include "menu_icons.h"
#include "../storage/config_store.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef NATIVE_TEST
#include <WiFi.h>
#endif

#ifndef NATIVE_TEST
#include <M5GFX.h>
#include <M5Unified.h>
static M5Canvas cv(&M5.Lcd);
extern WMBStats g_stats;
#endif

namespace wmb {

static UIDisplay _disp;
UIDisplay& get_global_ui_display() { return _disp; }

inline const Theme& t() { return ThemeManager::instance().current(); }
inline uint16_t trgb(uint32_t c) { return ThemeManager::rgb565(c); }

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint16_t media_col(const char* me, const char* dr) {
    if (!me) me=""; if (!dr) dr="";
    if (strstr(dr,"fhkv")||strstr(dr,"vario")||strstr(dr,"caloric")||
        strstr(dr,"compact5")||strstr(me,"heat cost")) return trgb(t().accentPrimary); // HCA
    if (strstr(me,"water")||strstr(me,"cold"))  return 0x041F; // Blue
    if (strstr(me,"heat")||strstr(me,"warm")||strstr(me,"hot"))  return trgb(t().accentPrimary); // Heat
    if (strstr(me,"gas"))   return trgb(t().ok);
    if (strstr(me,"elec"))  return trgb(t().warning);
    return trgb(t().accentLoRa);
}

// Half-brightness of any RGB565 colour
static uint16_t dim2(uint16_t c) { return (c>>1)&0x7BEF; }

static uint16_t sig_col(float r) { return r>-80?trgb(t().ok):r>-92?trgb(t().warning):trgb(t().error); }

static void strip_z(const char* s, char* d, size_t n) {
    strncpy(d,s,n-1); d[n-1]='\0';
    if (!strchr(d,'.')) return;
    char* e=d+strlen(d)-1;
    while(e>d&&*e=='0') *e--='\0';
    if(*e=='.') *e='\0';
}

static void age_s(uint32_t ts, char* b, size_t l) {
#ifndef NATIVE_TEST
    if(!ts){snprintf(b,l,"---");return;}
    uint32_t s=((uint32_t)millis()-ts)/1000;
    if(s<5)        snprintf(b,l,"now");
    else if(s<60)  snprintf(b,l,"%lus",(unsigned long)s);
    else if(s<3600)snprintf(b,l,"%lum",(unsigned long)(s/60));
    else           snprintf(b,l,"%luh",(unsigned long)(s/3600));
#else
    snprintf(b,l,"---");
#endif
}

static void to_up(const char* src, char* dst, size_t n) {
    size_t i=0;
    for(;src[i]&&i<n-1;i++) dst[i]=(src[i]>='a'&&src[i]<='z')?src[i]-32:src[i];
    dst[i]='\0';
}

#ifndef NATIVE_TEST

static void cp0(const char* txt,int y,uint16_t c){
    int w=strlen(txt)*6;
    cv.setTextFont(0);cv.setTextSize(1);cv.setTextColor(c);
    cv.setCursor((240-w)/2,y);cv.print(txt);
}

// draw_card removed - replaced by list layout

#endif // NATIVE_TEST

// ── UIDisplay ─────────────────────────────────────────────────────────────────

UIDisplay::UIDisplay()
    : cur_(UIScreen::MainMenu), top_(0), det_scroll_(0),
      flash_ms_(0) {}

bool UIDisplay::is_audio_enabled() const { return !g_settings.mute; }
bool UIDisplay::is_sd_logging_enabled() const { return g_settings.sd_logging; }
void UIDisplay::set_sd_logging_enabled(bool v) { 
    g_settings.sd_logging = v; 
    cs_save_settings(g_settings);
}
bool UIDisplay::is_charge_mode() const { return g_settings.charge_mode; }

void UIDisplay::toggle_charge_mode(){
    cur_ = UIScreen::ChargeMode;
    brightness_ = 1;
    M5.Lcd.setBrightness(brightness_);
#ifndef NATIVE_TEST
    last_interaction_ms_ = millis();
    last_draw_ms_ = millis();
#endif
}
void UIDisplay::trigger_starred_alert(){
#ifndef NATIVE_TEST
    if(!g_settings.mute) M5.Speaker.tone(3000,80);
    flash_ms_=(uint32_t)millis()+200;
#endif
}
void UIDisplay::trigger_normal_alert(){
#ifndef NATIVE_TEST
    if(!g_settings.mute) M5.Speaker.tone(1600,18);
#endif
}
void UIDisplay::init(){
#ifndef NATIVE_TEST
    M5.Lcd.begin(); M5.Lcd.setRotation(1);
    cv.setColorDepth(16); cv.createSprite(240,135); cv.setTextWrap(false);
    cv.fillScreen(trgb(t().bg));
    cv.setTextFont(2); cv.setTextSize(2);
    cv.setTextColor(trgb(t().accentPrimary)); cv.setCursor(12,22); cv.print("wM-BUSTER");
    cv.setTextColor(trgb(t().accentLive)); cv.setCursor(12+9*16,22); cv.print(" ADV");
    cv.setTextSize(1); cv.setTextFont(0);
    cv.setTextColor(trgb(t().textMuted)); cv.setCursor(12,66); cv.print("Wireless M-Bus Meter Reader");
    cv.setTextColor(trgb(t().ok)); cv.setCursor(12,84); cv.print("Starting...");
    cv.pushSprite(0,0); delay(700);

    menu_sprite_ = new M5Canvas(&cv);
    ((M5Canvas*)menu_sprite_)->setColorDepth(16);
    ((M5Canvas*)menu_sprite_)->createSprite(64, 64);
#endif
}

void UIDisplay::handle_event(UIEvent ev, MeterDatabase& db) {
    last_interaction_ms_ = millis();
    if (cur_ == UIScreen::ChargeMode) {
        if (ev == UIEvent::Back) {
            cur_ = UIScreen::MainMenu;
            brightness_ = 100;
            M5.Lcd.setBrightness(brightness_);
            last_interaction_ms_ = millis();
        }
        return;
    }

    if (brightness_ == 0) {
        brightness_ = 100;
        M5.Lcd.setBrightness(brightness_);
        return; // Consume the keypress to just wake the screen
    }
    if(ev==UIEvent::None) return;
    const size_t mc=db.get_meter_count();

    switch(ev){
        case UIEvent::NavUp:
            if(cur_==UIScreen::Home){if(top_>0)top_--;}
            else if(cur_==UIScreen::Detail){if(det_scroll_>0)det_scroll_--;}
            else if(cur_==UIScreen::Settings){if(settings_sel_>0)settings_sel_--;}
            break;
        case UIEvent::NavDown:
            if(cur_==UIScreen::Home){if(mc>1&&top_<mc-1)top_++;}
            else if(cur_==UIScreen::Detail) det_scroll_++;
            else if(cur_==UIScreen::Settings){if(settings_sel_<5)settings_sel_++;}
            break;
        case UIEvent::NavLeft:
            if(cur_==UIScreen::MainMenu) { if (menu_selected_ > 0) menu_selected_--; }
            else { cur_=UIScreen::MainMenu; }
            break;
        case UIEvent::NavRight:
            if(cur_==UIScreen::MainMenu) { if (menu_selected_ < 3) menu_selected_++; }
            break;
        case UIEvent::Select:
            if(cur_==UIScreen::MainMenu){
                if(menu_selected_==0) cur_=UIScreen::Home;
                if(menu_selected_==1) cur_=UIScreen::Settings;
                if(menu_selected_==2) cur_=UIScreen::WebUI;
                if(menu_selected_==3) cur_=UIScreen::Status;
            }
            else if(cur_==UIScreen::Home&&mc>0){cur_=UIScreen::Detail;det_scroll_=0;}
            else if(cur_==UIScreen::Settings){
                if(settings_sel_==0){
                    // Rescan Hardware
                    extern int radio_init(int);
                    extern bool radio_switch_mode(bool);
                    extern void radio_start_receive();
                    
                    int rt = radio_init(-1);
                    if (rt >= 0) {
                        g_settings.radio_hardware = (uint8_t)rt;
                        bool is_c1t1 = (strcmp(g_settings.radio_mode, "S1") != 0);
                        if (!is_c1t1) radio_switch_mode(false);
                        else radio_start_receive();
                    }
                }
                if(settings_sel_==1){
                    if (strcmp(g_settings.radio_mode, "C1/T1") == 0 || strcmp(g_settings.radio_mode, "CT") == 0) {
                        strcpy(g_settings.radio_mode, "S1");
                    } else {
                        strcpy(g_settings.radio_mode, "C1/T1");
                    }
                    extern bool radio_switch_mode(bool);
                    radio_switch_mode(strcmp(g_settings.radio_mode, "C1/T1") == 0);
                }
                if(settings_sel_==2){ 
                    g_settings.theme_idx = g_settings.theme_idx == 0 ? 1 : 0; 
                    ThemeManager::instance().setTheme(g_settings.theme_idx);
                }
                if(settings_sel_==3) g_settings.mute = !g_settings.mute;
                if(settings_sel_==4) g_settings.sd_logging = !g_settings.sd_logging;
                if(settings_sel_==5) toggle_charge_mode();
                if(settings_sel_!=5) cs_save_settings(g_settings);
            }
            else if(cur_==UIScreen::WebUI){
                g_settings.webui_enabled = !g_settings.webui_enabled;
                cs_save_settings(g_settings);
                extern void toggle_webui_ap();
                toggle_webui_ap();
            }
            break;
        case UIEvent::Back:
            if (cur_==UIScreen::Home || cur_==UIScreen::Status || cur_==UIScreen::Settings || cur_==UIScreen::WebUI) { cur_ = UIScreen::MainMenu; }
            else if (cur_==UIScreen::Detail) { cur_ = UIScreen::Home; }
            break;
        case UIEvent::StarToggle:
            if(cur_==UIScreen::Home&&mc>0&&top_<mc) db.toggle_star(top_); break;
        case UIEvent::MuteToggle:       
            g_settings.mute = !g_settings.mute; 
            cs_save_settings(g_settings);
            break;
        case UIEvent::SDLogToggle:      
            g_settings.sd_logging = !g_settings.sd_logging; 
            cs_save_settings(g_settings);
            break;
        case UIEvent::ChargeModeToggle: toggle_charge_mode(); break;
        case UIEvent::ClearFeed:        db.clear_feed();      break;
        default: break;
    }
}

void UIDisplay::update(const MeterDatabase& db) {
    uint32_t now = millis();
    float dt = (now - last_draw_ms_) / 1000.0f;
    last_draw_ms_ = now;
    if (dt > 0.1f) dt = 0.1f;

    if (cur_ == UIScreen::ChargeMode) {
        if (brightness_ != 1) {
            brightness_ = 1;
            M5.Lcd.setBrightness(brightness_);
        }
        draw_charge();
        cv.pushSprite(0,0);
        return;
    }

    uint32_t idle_ms = now - last_interaction_ms_;
    if (idle_ms > 60000) {
        if (brightness_ > 0) {
            brightness_ = 0;
            M5.Lcd.setBrightness(brightness_);
        }
        return;
    } else if (idle_ms > 30000) {
        if (brightness_ > 30) {
            brightness_ = 30;
            M5.Lcd.setBrightness(brightness_);
        }
    } else {
        if (brightness_ < 100) {
            brightness_ = 100;
            M5.Lcd.setBrightness(brightness_);
        }
    }

    bool fl=flash_ms_&&(uint32_t)millis()<flash_ms_;
    if(flash_ms_&&!fl) flash_ms_=0;
    cv.fillScreen(fl?trgb(t().border):trgb(t().bg));
    
    if (cur_ == UIScreen::MainMenu) {
        // Smooth carousel animation
        float diff = menu_selected_ - menu_offset_;
        if (abs(diff) > 0.01f) {
            menu_offset_ += diff * 15.0f * dt;
        } else {
            menu_offset_ = menu_selected_;
        }
    } 

    switch(cur_){
        case UIScreen::MainMenu: draw_main_menu(db); break;
        case UIScreen::Home:     draw_home(db);      break;
        case UIScreen::Detail:   draw_detail(db);    break;
        case UIScreen::Status:   draw_status(db);    break;
        case UIScreen::Settings: draw_settings();    break;
        case UIScreen::WebUI:    draw_webui();       break;
        case UIScreen::ChargeMode: break; // Handled above
    }
    cv.pushSprite(0,0);
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN MENU (Carousel)
// ─────────────────────────────────────────────────────────────────────────────
void UIDisplay::draw_main_menu(const MeterDatabase& db){
#ifndef NATIVE_TEST
    ThemeManager::instance().drawStatusBar(cv, "wM-BUSTER", "");
    
    const int NUM_ITEMS = 4;
    const uint8_t* ICONS[NUM_ITEMS] = { icon_signals, icon_config, icon_webui, icon_about };
    const char* LABELS[NUM_ITEMS] = { "Meters", "Settings", "WebUI", "Status" };
    
    int centerY = 135 / 2 + 5;
    int xCenter = 240 / 2;
    int spacing = 80;

    for (int i = 0; i < NUM_ITEMS; i++) {
        float dist = i - menu_offset_;
        float scale = 1.0f - abs(dist) * 0.3f;
        if (scale < 0.2f) scale = 0.2f;
        float alpha = 1.0f - abs(dist) * 0.4f;
        if (alpha < 0) alpha = 0;
        
        int iconSize = 64 * scale;
        int px = xCenter + (dist * spacing) - iconSize / 2;
        int py = centerY - iconSize / 2 - 10;
        
        uint32_t color = (i == menu_selected_) ? ThemeManager::instance().current().accentPrimary : ThemeManager::instance().current().textMuted;
        
        // Draw 64x64 mask scaled directly to cv
        for (int sy = 0; sy < iconSize; sy++) {
            int srcY = (sy * 64) / iconSize;
            for (int sx = 0; sx < iconSize; sx++) {
                int srcX = (sx * 64) / iconSize;
                uint8_t a = ICONS[i][srcY * 64 + srcX];
                a = a * alpha;
                if (a > 128) {
                    cv.drawPixel(px + sx, py + sy, ThemeManager::instance().rgb565(color));
                }
            }
        }
        
        if (alpha > 0.3f) {
            cv.setTextFont(0);
            cv.setTextSize(1);
            cv.setTextColor(ThemeManager::instance().rgb565(color));
            int16_t lw = cv.textWidth(LABELS[i]);
            cv.setCursor(xCenter + (dist * spacing) - lw / 2, py + iconSize + 8);
            cv.print(LABELS[i]);
        }
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// HOME (Meters List)
// ─────────────────────────────────────────────────────────────────────────────
void UIDisplay::draw_home(const MeterDatabase& db){
#ifndef NATIVE_TEST
    const size_t n=db.get_meter_count();

    // ── Status bar (y=0..12) ──────────────────────────────────────────────────
    char left[32];
    if(n==0) snprintf(left, sizeof(left), "Scanning...");
    else snprintf(left, sizeof(left), "%zu meter%s", n, n==1?"":"s");
    
    char right[32];
    int batt=M5.Power.getBatteryLevel(); if(batt<0)batt=0;
    snprintf(right, sizeof(right), "%d%%", batt);
    
    ThemeManager::instance().drawStatusBar(cv, left, right);

    // GPS indicator
    bool gps_rx=(g_stats.gnss_sats>0);
    uint16_t gdc=g_stats.gnss_fix?trgb(t().ok):(gps_rx?trgb(t().warning):trgb(t().error));
    cv.fillCircle(178,6,3,gdc);
    cv.setTextColor(gdc); cv.setCursor(184,3);
    cv.print(g_stats.gnss_fix?"FIX":(gps_rx?"GPS":"GPS!"));

    // ── Empty state ───────────────────────────────────────────────────────────
    if(n==0){
        cv.setTextFont(2); cv.setTextSize(1);
        cv.setTextColor(trgb(t().accentPrimary));
        int tw=strlen("Waiting for meters")*8;
        cv.setCursor((240-tw)/2,42); cv.print("Waiting for meters");
        cv.setTextFont(0);
        cp0("868 MHz · C1+T1 · wM-Bus",62,trgb(t().textMuted));
        char s[48]; snprintf(s,sizeof(s),"RX: %lu    OK: %lu",
            (unsigned long)g_stats.radio_rx_total,
            (unsigned long)g_stats.radio_rx_good);
        cp0(s,78,trgb(t().border));
        cp0("W/S: scroll   Enter: detail   Del: menu",104,trgb(t().border));
        return;
    }

    if(top_>=n) top_=n-1;

    const int ROW_H = 17;
    const int VISIBLE_ROWS = 7;
    const int LIST_Y = 14;

    int start_idx = top_ - (VISIBLE_ROWS / 2);
    if (start_idx > (int)n - VISIBLE_ROWS) start_idx = (int)n - VISIBLE_ROWS;
    if (start_idx < 0) start_idx = 0;

    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int entryIdx = start_idx + row;
        int y = LIST_Y + row * ROW_H;

        if (entryIdx < 0 || entryIdx >= (int)n) {
            cv.fillRect(0, y, 240, ROW_H, trgb(t().bg));
            continue;
        }

        const MeterEntry* e = db.get_meter(entryIdx);
        bool isEven = (row % 2 == 0);
        bool selected = (entryIdx == top_);

        uint32_t rowBg = selected ? t().accentPrimary : (isEven ? t().bg : t().bgCard);
        cv.fillRect(0, y, 240, ROW_H, trgb(rowBg));
        
        cv.drawFastHLine(0, y + ROW_H - 1, 240, trgb(t().borderFaint));

        uint32_t fg = selected ? t().bg : t().textPrimary;
        cv.setTextColor(trgb(fg));

        char label[32];
        snprintf(label, sizeof(label), "%.12s #%.6s", e->friendly_type[0] ? e->friendly_type : "Meter", e->id);
        cv.setTextFont(0);
        cv.setTextSize(1); 
        cv.setCursor(4, y + 5);
        cv.print(label);

        if (e->is_starred) {
            cv.setTextColor(trgb(selected ? t().bg : t().warning));
            cv.print(" *");
        }

        char valStr[24];
        strip_z(e->primary_value_str, valStr, sizeof(valStr));
        if (valStr[0]) {
            cv.setTextColor(trgb(selected ? t().bg : t().accentLive));
            int vw = cv.textWidth(valStr);
            cv.setCursor(205 - vw, y + 5);
            cv.print(valStr);
        }

        int barX = 215;
        int barY = y + 4;
        int bars = 0;
        if (e->last_rssi > -70) bars = 4;
        else if (e->last_rssi > -90) bars = 3;
        else if (e->last_rssi > -105) bars = 2;
        else if (e->last_rssi > -115) bars = 1;

        uint32_t barColor = selected ? t().bg : (e->last_rssi > -105 ? t().ok : t().warning);
        uint32_t bgBarColor = t().borderFaint;
        
        for (int i = 0; i < 4; i++) {
            int h = 3 + i * 2;
            int bx = barX + i * 5;
            int by = barY + (9 - h);
            cv.fillRect(bx, by, 3, h, trgb(i < bars ? barColor : bgBarColor));
        }
    }
    
    cv.fillRect(0, LIST_Y + VISIBLE_ROWS * ROW_H, 240, 135 - (LIST_Y + VISIBLE_ROWS * ROW_H), trgb(t().bg));
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// DETAIL
// ─────────────────────────────────────────────────────────────────────────────
void UIDisplay::draw_detail(const MeterDatabase& db){
#ifndef NATIVE_TEST
    const MeterEntry* m=db.get_meter(top_);
    if(!m){cur_=UIScreen::Home;return;}

    draw_home(db);

    for (int y = 0; y < 135; y += 2) {
        for (int x = 0; x < 240; x += 2) {
            cv.drawPixel(x, y, trgb(t().bg));
        }
    }

    cv.fillRoundRect(12, 10, 240 - 24, 135 - 20, 6, trgb(t().bgCard));
    cv.drawRoundRect(12, 10, 240 - 24, 135 - 20, 6, trgb(t().accentPrimary));
    
    cv.setTextSize(1);
    cv.setClipRect(14, 12, 240 - 28, 135 - 24);

    char buf[sizeof(m->display_fields)];
    snprintf(buf,sizeof(buf),"%s",m->display_fields);
    const char* keys[40]; const char* vals[40]; int fc=0;
    char* tok=strtok(buf,"\n");
    while(tok&&fc<40){keys[fc]=tok;tok=strtok(nullptr,"\n");if(!tok)break;vals[fc]=tok;tok=strtok(nullptr,"\n");fc++;}

    int st=(int)det_scroll_;
    if(st>=fc&&fc>0){st=fc-1;det_scroll_=(size_t)st;}

    auto lbl=[](const char* k)->const char*{
        if(strstr(k,"total_energy")||strstr(k,"total_kwh"))return"Total Energy";
        if(strstr(k,"forward_energy"))return"Forward Energy";
        if(strstr(k,"total_volume")||strstr(k,"total_m3"))return"Total Volume";
        
        if(strstr(k,"prev")&&strstr(k,"hca"))return"Prev. HCA";
        if(strstr(k,"consumption_at_set_date")&&strstr(k,"hca"))return"Prev. HCA";
        if(strstr(k,"consumption_hca")||strstr(k,"current_hca")||strstr(k,"hca"))return"HCA Reading";
        
        if(strstr(k,"flow_temp"))return"Flow Temp";
        if(strstr(k,"return_temp"))return"Return Temp";
        if(strstr(k,"temperature"))return"Temperature";
        if(strstr(k,"volume_flow"))return"Flow Rate";
        if(strstr(k,"power"))return"Power";
        if(strstr(k,"status"))return"Status";
        
        if(strstr(k,"date_time"))return"Date/Time";
        if(strstr(k,"date"))return"Date";
        
        return k;
    };

    int y = 14;
    bool isEven = false;
    for(int i=st;i<fc;i++){
        if (isEven) {
            cv.fillRect(14, y - 2, 240 - 28, 14, trgb(t().bg));
        }
        isEven = !isEven;

        cv.setCursor(18, y);
        cv.setTextColor(trgb(t().accentPrimary));
        
        char keyStr[32];
        snprintf(keyStr, sizeof(keyStr), "%s", lbl(keys[i]));
        if (strlen(keyStr) > 14) { keyStr[12]='.'; keyStr[13]='.'; keyStr[14]='\0'; }
        cv.print(keyStr);
        
        cv.setTextColor(trgb(t().textPrimary));
        cv.setCursor(110, y);
        
        char valStr[32];
        snprintf(valStr, sizeof(valStr), "%s", vals[i]);
        if (strlen(valStr) > 17) { valStr[15]='.'; valStr[16]='.'; valStr[17]='\0'; }
        cv.print(valStr);

        y += 14;
    }

    cv.clearClipRect();

    if(fc > 8){
        const int VIS = 8;
        const int ty=12, th=135-24;
        int th2=VIS*th/fc;if(th2<6)th2=6;
        int ms=fc-VIS;if(ms<1)ms=1;
        int sc=st>ms?ms:st;
        cv.fillRect(222,ty,3,th,trgb(t().bg));
        cv.fillRoundRect(222,ty+sc*(th-th2)/ms,3,th2,1,trgb(t().accentPrimary));
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// STATUS
// ─────────────────────────────────────────────────────────────────────────────
void UIDisplay::draw_status(const MeterDatabase& db){
#ifndef NATIVE_TEST
    ThemeManager::instance().drawStatusBar(cv, "Device Status", "");

    cv.setTextFont(0); cv.setTextSize(1);
    const int LX=6,VX=72,Y0=18,RH=14;

    auto row=[&](int y,uint16_t dc,const char* l,const char* v){
        cv.fillCircle(LX+3,y+5,3,dc);
        cv.setTextColor(trgb(t().textMuted));cv.setCursor(LX+10,y);cv.print(l);
        cv.setTextColor(trgb(t().textPrimary));cv.setCursor(VX,y);cv.print(v);
    };

    char tmp[60];
    row(Y0+RH*0,trgb(t().ok),"Radio","On — 868 MHz C1+T1");

    snprintf(tmp,sizeof(tmp),"%lu received  %lu decoded",
        (unsigned long)g_stats.radio_rx_total,(unsigned long)g_stats.radio_rx_good);
    row(Y0+RH*1,trgb(t().ok),"Signals",tmp);

    snprintf(tmp,sizeof(tmp),"%zu meters found",db.get_meter_count());
    row(Y0+RH*2,db.get_meter_count()>0?trgb(t().ok):trgb(t().warning),"Meters",tmp);

    bool gps_rx=(g_stats.gnss_sats>0);
    if(g_stats.gnss_fix){
        snprintf(tmp,sizeof(tmp),"Fix  %.4fN  %.4fE",
            fabs(g_stats.gnss_lat),fabs(g_stats.gnss_lon));
        row(Y0+RH*3,trgb(t().ok),"GPS",tmp);
    }else if(gps_rx){
        row(Y0+RH*3,trgb(t().warning),"GPS","Searching — open sky needed");
    }else{
        row(Y0+RH*3,trgb(t().error),"GPS","No data from module");
    }

    if (!g_settings.webui_enabled) {
        row(Y0+RH*4,trgb(t().border),"WiFi","Off");
    } else {
        bool has_sta = (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) && WiFi.status() == WL_CONNECTED;
        bool has_ap = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
        if (has_sta && has_ap) {
            snprintf(tmp,sizeof(tmp),"AP & STA (%s)", WiFi.localIP().toString().c_str());
            row(Y0+RH*4,trgb(t().ok),"WiFi",tmp);
        } else if (has_sta) {
            snprintf(tmp,sizeof(tmp),"STA %s", WiFi.localIP().toString().c_str());
            row(Y0+RH*4,trgb(t().ok),"WiFi",tmp);
        } else if (has_ap) {
            snprintf(tmp,sizeof(tmp),"AP 192.168.4.1");
            row(Y0+RH*4,trgb(t().ok),"WiFi",tmp);
        } else {
            row(Y0+RH*4,trgb(t().warning),"WiFi","Connecting STA...");
        }
    }

    uint32_t up=g_stats.uptime_s;
    if(up>=3600)snprintf(tmp,sizeof(tmp),"%uh %02um",up/3600,(up%3600)/60);
    else        snprintf(tmp,sizeof(tmp),"%um %02us",up/60,up%60);
    row(Y0+RH*5,trgb(t().textMuted),"Uptime",tmp);

    row(Y0+RH*6,g_settings.sd_logging?trgb(t().ok):trgb(t().border),"SD Log",g_settings.sd_logging?"On  [L]":"Off  [L]");

    cv.drawFastHLine(0,120,240,trgb(t().border));
    cv.setTextColor(trgb(t().border));
    cv.setCursor(6,124); cv.print("Del: menu  F: star  M: mute  C: screen");
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// SETTINGS
// ─────────────────────────────────────────────────────────────────────────────
void UIDisplay::draw_settings(){
#ifndef NATIVE_TEST
    ThemeManager::instance().drawStatusBar(cv, "Settings", "");

    const int NUM_SETTINGS = 6;
    const char* labels[] = { "RF Module", "Listen Mode", "Theme", "Mute Sound", "SD Logging", "Charge Mode" };
    
    char vals[6][32];
    snprintf(vals[0], 32, "%s", g_settings.radio_hardware == 0 ? "LoRa Cap (Auto)" : "Hydra RF (Auto)");
    snprintf(vals[1], 32, "%s", g_settings.radio_mode);
    snprintf(vals[2], 32, "%s", g_settings.theme_idx == 0 ? "Midnight" : "Amber");
    snprintf(vals[3], 32, "%s", g_settings.mute ? "On" : "Off");
    snprintf(vals[4], 32, "%s", g_settings.sd_logging ? "On" : "Off");
    snprintf(vals[5], 32, "%s", g_settings.charge_mode ? "On" : "Off");

    int y = 16;
    for (int i = 0; i < NUM_SETTINGS; i++) {
        bool sel = (i == settings_sel_);
        uint32_t bg = sel ? t().accentPrimary : t().bg;
        uint32_t fg = sel ? t().bg : t().textPrimary;
        uint32_t vfg = sel ? t().bg : t().accentLive;
        
        cv.fillRect(0, y, 240, 18, trgb(bg));
        cv.drawFastHLine(0, y + 17, 240, trgb(t().borderFaint));
        
        cv.setTextFont(0); cv.setTextSize(1);
        cv.setTextColor(trgb(fg));
        cv.setCursor(12, y + 5);
        cv.print(labels[i]);
        
        cv.setTextColor(trgb(vfg));
        int vw = cv.textWidth(vals[i]);
        cv.setCursor(228 - vw, y + 5);
        cv.print(vals[i]);
        
        y += 18;
    }
    
    cv.setTextColor(trgb(t().border));
    cv.setCursor(12, 120);
    cv.print("Enter: toggle   Del: back");
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// WEBUI
// ─────────────────────────────────────────────────────────────────────────────
void UIDisplay::draw_webui(){
#ifndef NATIVE_TEST
    ThemeManager::instance().drawStatusBar(cv, "WebUI", "");

    bool e = g_settings.webui_enabled;

    cv.setTextFont(0); cv.setTextSize(1);
    
    cv.setTextColor(trgb(e ? t().ok : t().textMuted));
    int tw = cv.textWidth(e ? "WebUI Active" : "WebUI Disabled");
    cv.setCursor((240 - tw) / 2, 24);
    cv.print(e ? "WebUI Active" : "WebUI Disabled");
    
    cv.setTextColor(trgb(t().textPrimary));
    char l1[64] = "";
    char l2[64] = "";
    char l3[64] = "";

    bool has_sta = (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) && WiFi.status() == WL_CONNECTED;
    bool has_ap = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);

    if (e) {
        if (g_settings.webui_mode == 2 && has_sta) { // STA Only
            snprintf(l1, sizeof(l1), "Net: %s", g_settings.wifi_ssid);
            snprintf(l2, sizeof(l2), "IP: %s", WiFi.localIP().toString().c_str());
        } else if (g_settings.webui_mode == 2) {
            snprintf(l1, sizeof(l1), "Connecting to STA...");
            snprintf(l2, sizeof(l2), "Net: %s", g_settings.wifi_ssid);
        } else if (g_settings.webui_mode == 1) { // AP Only
            snprintf(l1, sizeof(l1), "SSID: wM-Buster ADV");
            snprintf(l2, sizeof(l2), "Pass: %s", g_settings.webui_ap_pass);
            snprintf(l3, sizeof(l3), "IP: 192.168.4.1");
        } else { // AP + STA
            snprintf(l1, sizeof(l1), "AP: wM-Buster ADV");
            snprintf(l2, sizeof(l2), "Pass: %s", g_settings.webui_ap_pass);
            if (has_sta) {
                snprintf(l3, sizeof(l3), "STA IP: %s", WiFi.localIP().toString().c_str());
            } else {
                snprintf(l3, sizeof(l3), "AP IP: 192.168.4.1");
            }
        }
    } else {
        snprintf(l1, sizeof(l1), "Press Enter to enable");
    }

    if (l1[0]) { tw = cv.textWidth(l1); cv.setCursor((240 - tw)/2, 44); cv.print(l1); }
    if (l2[0]) { tw = cv.textWidth(l2); cv.setCursor((240 - tw)/2, 60); cv.print(l2); }
    if (l3[0]) { tw = cv.textWidth(l3); cv.setCursor((240 - tw)/2, 76); cv.print(l3); }
    
    // Draw toggle button (simulated as selected)
    cv.fillRoundRect(40, 100, 160, 22, 4, trgb(t().accentPrimary));
    cv.setTextColor(trgb(t().bg));
    const char* btn = e ? "Disable WebUI" : "Enable WebUI";
    tw = cv.textWidth(btn);
    cv.setCursor((240 - tw) / 2, 107);
    cv.print(btn);
#endif
}

void UIDisplay::draw_charge() {
    cv.fillScreen(TFT_BLACK);
    cv.setTextColor(0x4208); // Dark gray
    cv.setTextSize(3);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", M5.Power.getBatteryLevel());
    cv.setCursor((240 - cv.textWidth(buf)) / 2, 55);
    cv.print(buf);
}

} // namespace wmb
