// raccoon — Theme manager implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "theme.h"

ThemeManager& ThemeManager::instance() {
    static ThemeManager inst;
    return inst;
}

void ThemeManager::setTheme(uint8_t index) {
    if (index >= Themes::COUNT) index = 0;
    _index = index;
    _theme = Themes::ALL[index];
}

uint16_t ThemeManager::rgb565(uint32_t rgb888) {
    uint8_t r = (rgb888 >> 16) & 0xFF;
    uint8_t g = (rgb888 >>  8) & 0xFF;
    uint8_t b = (rgb888      ) & 0xFF;
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void ThemeManager::drawStatusBar(LovyanGFX& disp, const char* left, const char* right) {
    const Theme& t = *_theme;
    // Status bar: full width, 14 px tall at top
    disp.fillRect(0, 0, LCD_WIDTH, 14, rgb565(t.bgStatus));
    disp.setTextColor(rgb565(t.textSecondary));
    disp.setTextSize(1);
    disp.setCursor(2, 3);
    disp.print(left);
    // Right-align the right string
    int16_t tw = disp.textWidth(right);
    disp.setCursor(LCD_WIDTH - tw - 2, 3);
    disp.print(right);
    // Divider line
    disp.drawFastHLine(0, 13, LCD_WIDTH, rgb565(t.border));
}

void ThemeManager::fillRect(LovyanGFX& disp, int x, int y, int w, int h, uint32_t color) {
    disp.fillRect(x, y, w, h, rgb565(color));
}

void ThemeManager::drawText(LovyanGFX& disp, int x, int y, const char* text,
                              uint32_t color, uint8_t size) {
    disp.setTextColor(rgb565(color));
    disp.setTextSize(size);
    disp.setCursor(x, y);
    disp.print(text);
}
