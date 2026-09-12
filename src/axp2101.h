#pragma once
#include <stdint.h>

struct BatteryStatus {
    uint8_t pct;       // 0-100 %
    bool    charging;  // USB connected and battery not full
    bool    present;   // battery detected
};

bool          axp_begin();
BatteryStatus axp_read();
