// wM-Buster ADV — Keyboard & UI Input Handler Implementation
// GPL-3.0
#include "ui_input.h"

#ifndef NATIVE_TEST
#include <M5Cardputer.h>
#include <M5Unified.h>
#endif

namespace wmb {

UIInput::UIInput() : last_poll_ms_(0) {}

void UIInput::init() {
    // Initialization moved to main.cpp
}

UIEvent UIInput::poll_event() {
#ifndef NATIVE_TEST
    M5Cardputer.update();
    M5.update();

    // Check Cardputer keyboard state changes
    if (M5Cardputer.Keyboard.isChange()) {
        if (M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

            // Check word characters / key presses
            for (auto c : status.word) {
                if (c == ';' || c == 'w' || c == 'W') return UIEvent::NavUp;
                if (c == '.' || c == 's' || c == 'S') return UIEvent::NavDown;
                if (c == ',' || c == 'a' || c == 'A') return UIEvent::NavLeft;
                if (c == '/' || c == 'd' || c == 'D') return UIEvent::NavRight;
                if (c == '*' || c == '8' || c == 'f' || c == 'F') return UIEvent::StarToggle;
                if (c == 'm' || c == 'M') return UIEvent::MuteToggle;    // [M] audio toggle
                if (c == 'l' || c == 'L') return UIEvent::SDLogToggle;   // [L] SD log toggle
                if (c == 'c' || c == 'C') return UIEvent::ChargeModeToggle; // [C] screen off
                if (c == 'x' || c == 'X') return UIEvent::ClearFeed;     // [X] clear live feed
            }

            if (status.enter) return UIEvent::Select;
            if (status.del)   return UIEvent::Back;
            if (status.tab)   return UIEvent::NavRight;
        }
    }

    // Check M5 Unified BtnA (the main button on top/front of Cardputer)
    if (M5.BtnA.wasPressed()) {
        return UIEvent::Select;
    }
#endif

    return UIEvent::None;
}

} // namespace wmb
