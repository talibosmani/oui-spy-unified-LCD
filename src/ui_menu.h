#pragma once
#include <stdint.h>

enum class AppMode : uint8_t {
    None     = 0,
    Detector = 1,
    Foxhunter= 2,
    FlockYou = 3,
    PCAP     = 4,
    SkySpy   = 5,
    BLESniff = 6,
};

// Callback fired when user taps a mode button.
using MenuSelectCb = void(*)(AppMode mode);

void ui_menu_create(MenuSelectCb on_select);
void ui_menu_destroy();
