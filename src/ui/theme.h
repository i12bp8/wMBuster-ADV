#pragma once
// raccoon — UI theme system
// SPDX-License-Identifier: GPL-3.0-only
//
// Original color palette — NOT derived from Bruce firmware.
// All hex values are chosen independently.
// Display is 240×135 px, 16-bit RGB565.

#include <M5GFX.h>

#define LCD_WIDTH  240
#define LCD_HEIGHT 135

// ============================================================
// Theme definition
// ============================================================
struct Theme {
    const char* name;

    // Background layers
    uint32_t bg;           // Main background
    uint32_t bgCard;       // Card / panel background
    uint32_t bgStatus;     // Status bar background

    // Text
    uint32_t textPrimary;
    uint32_t textSecondary;
    uint32_t textMuted;

    // Accent colors
    uint32_t accentPrimary;   // Selected / active state
    uint32_t accentLive;      // "Receiving / live" indicator
    uint32_t accentLoRa;      // LoRa mode indicator

    // Semantic
    uint32_t ok;
    uint32_t warning;
    uint32_t error;
    uint32_t battGood;
    uint32_t battLow;
    uint32_t wifi;
    uint32_t mqtt;

    // Border / dividers
    uint32_t border;
    uint32_t borderFaint;
};

// ============================================================
// Built-in themes
// ============================================================
namespace Themes {

// Theme 0: "Midnight" — pure black + hacker purple
inline constexpr Theme MIDNIGHT = {
    .name           = "Midnight",
    .bg             = 0x000000,   // pure black (OLED feel)
    .bgCard         = 0x0D0D12,
    .bgStatus       = 0x000000,
    .textPrimary    = 0xDDE6F0,
    .textSecondary  = 0x8BA3BF,
    .textMuted      = 0x3A5270,
    .accentPrimary  = 0xBB00FF,   // hacker purple
    .accentLive     = 0x00E5C3,   // teal pulse (signal RX)
    .accentLoRa     = 0x6622FF,   // deep violet for LoRa
    .ok             = 0x2ECC71,
    .warning        = 0xF39C12,
    .error          = 0xE74C3C,
    .battGood       = 0x2ECC71,
    .battLow        = 0xE74C3C,
    .wifi           = 0xBB00FF,
    .mqtt           = 0x00E5C3,
    .border         = 0x220033,
    .borderFaint    = 0x110022,
};

// Theme 1: "Amber" — dark charcoal + warm amber
inline constexpr Theme AMBER = {
    .name           = "Amber",
    .bg             = 0x0D0C0A,
    .bgCard         = 0x1A1810,
    .bgStatus       = 0x080706,
    .textPrimary    = 0xF5E6C8,
    .textSecondary  = 0xB8A070,
    .textMuted      = 0x6B5A30,
    .accentPrimary  = 0xF0A000,
    .accentLive     = 0xFF6B35,
    .accentLoRa     = 0x60C0E0,
    .ok             = 0x80C040,
    .warning        = 0xF0A000,
    .error          = 0xFF4040,
    .battGood       = 0x80C040,
    .battLow        = 0xFF4040,
    .wifi           = 0xF0A000,
    .mqtt           = 0xFF6B35,
    .border         = 0x302A18,
    .borderFaint    = 0x1E1B0E,
};

inline constexpr const Theme* ALL[] = { &MIDNIGHT, &AMBER };
inline constexpr size_t COUNT = 2;

} // namespace Themes

// ============================================================
// ThemeManager singleton
// ============================================================
class ThemeManager {
public:
    static ThemeManager& instance();

    void          setTheme(uint8_t index);
    const Theme&  current() const { return *_theme; }
    uint8_t       currentIndex() const { return _index; }

    // Convert 0xRRGGBB → RGB565 for M5GFX
    static uint16_t rgb565(uint32_t rgb888);

    // Draw helpers for common UI elements
    void drawStatusBar(LovyanGFX& disp, const char* left, const char* right);
    void fillRect(LovyanGFX& disp, int x, int y, int w, int h, uint32_t color);
    void drawText(LovyanGFX& disp, int x, int y, const char* text,
                  uint32_t color, uint8_t size = 1);

private:
    ThemeManager() : _theme(Themes::ALL[0]), _index(0) {}
    ThemeManager(const ThemeManager&) = delete;

    const Theme* _theme;
    uint8_t      _index = 0;
};
