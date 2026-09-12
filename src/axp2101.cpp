// AXP2101 PMIC driver — battery SOC and charge status.
// Wire must already be initialised before calling axp_begin().
#include "axp2101.h"
#include "config.h"
#include <Wire.h>
#include <Arduino.h>

static uint8_t read_reg(uint8_t reg) {
    Wire.beginTransmission(I2C_ADDR_PMIC);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0xFF;
    Wire.requestFrom(I2C_ADDR_PMIC, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

bool axp_begin() {
    Wire.beginTransmission(I2C_ADDR_PMIC);
    bool ok = (Wire.endTransmission() == 0);
    Serial.printf("[axp] AXP2101 %s at 0x%02X\n", ok ? "ok" : "not found", I2C_ADDR_PMIC);
    return ok;
}

BatteryStatus axp_read() {
    BatteryStatus s{};
    uint8_t stat0 = read_reg(0x00);   // STATUS0: bit5=VBUS_GOOD, bit3=BAT_PRESENT
    s.pct     = read_reg(0xA4);       // BATSOC: 0-100 %
    s.present  = (stat0 != 0xFF) && ((stat0 >> 3) & 1);
    // Charging = USB present AND battery not full
    bool vbus  = (stat0 >> 5) & 1;
    s.charging = vbus && s.pct < 100;
    if (s.pct > 100) s.pct = 100;
    return s;
}
