// CST9217 capacitive touch over I2C.
// Protocol ported from Waveshare's esp_lcd_touch_cst9217 and socquique/capsule-radar.
#include "touch_cst9217.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>

#define CST9217_REG_DATA 0xD000
#define CST9217_ACK      0xAB
#define CST9217_DATA_LEN 10

static bool cst_read_reg(uint16_t reg, uint8_t *data, uint8_t len) {
    Wire.beginTransmission((uint8_t)I2C_ADDR_TOUCH);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(true) != 0) return false;
    delayMicroseconds(500);
    if (Wire.requestFrom((uint8_t)I2C_ADDR_TOUCH, len) < len) return false;
    for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
    return true;
}

bool touch_begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    pinMode(PIN_TP_RST, OUTPUT);
    digitalWrite(PIN_TP_RST, LOW);
    delay(10);
    digitalWrite(PIN_TP_RST, HIGH);
    delay(50);
    pinMode(PIN_TP_INT, INPUT);
    uint8_t d[CST9217_DATA_LEN] = {0};
    if (cst_read_reg(CST9217_REG_DATA, d, CST9217_DATA_LEN) && d[6] == CST9217_ACK)
        Serial.println("[touch] CST9217 ok");
    else
        Serial.println("[touch] CST9217 not yet responding (will retry)");
    return true;
}

bool touch_read(uint16_t *ox, uint16_t *oy) {
    uint8_t d[CST9217_DATA_LEN];
    if (!cst_read_reg(CST9217_REG_DATA, d, CST9217_DATA_LEN)) return false;
    if (d[6] != CST9217_ACK) return false;
    if ((d[5] & 0x7F) == 0) return false;

    uint16_t x = ((uint16_t)d[1] << 4) | (d[3] >> 4);
    uint16_t y = ((uint16_t)d[2] << 4) | (d[3] & 0x0F);

    // Discard sensor-noise readings (raw values way above screen size)
    if (x > SCREEN_W * 3 || y > SCREEN_H * 3) return false;

    // Log every unique coordinate change so ghost touches are visible in the monitor.
    static uint16_t s_last_rx = 0xFFFF, s_last_ry = 0xFFFF;
    if (x != s_last_rx || y != s_last_ry) {
        Serial.printf("[touch] RAW  x=%4d  y=%4d\n", x, y);
        s_last_rx = x; s_last_ry = y;
    }

    if (x > SCREEN_W - 1) x = SCREEN_W - 1;
    if (y > SCREEN_H - 1) y = SCREEN_H - 1;
    if (TP_MIRROR_X) x = (SCREEN_W - 1) - x;
    if (TP_MIRROR_Y) y = (SCREEN_H - 1) - y;

    Serial.printf("[touch] LVGL x=%4d  y=%4d\n", x, y);

    *ox = x;
    *oy = y;
    return true;
}
