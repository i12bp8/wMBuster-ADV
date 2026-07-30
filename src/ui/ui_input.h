// wM-Buster ADV — Keyboard & UI Input Handler
// GPL-3.0
#pragma once

#include <stdint.h>

namespace wmb {

enum class UIEvent {
    None,
    NavUp,
    NavDown,
    NavLeft,
    NavRight,
    Select,
    Back,
    StarToggle,
    MuteToggle,
    ClearFeed,
    SDLogToggle,
    ChargeModeToggle
};

struct KeyState {
    bool up_pressed;
    bool down_pressed;
    bool left_pressed;
    bool right_pressed;
    bool enter_pressed;
    bool back_pressed;
    char last_char;
};

class UIInput {
public:
    UIInput();
    void init();
    
    // Poll keyboard & physical buttons, returns single UIEvent or UIEvent::None
    UIEvent poll_event();

private:
    uint32_t last_poll_ms_;
};

} // namespace wmb
