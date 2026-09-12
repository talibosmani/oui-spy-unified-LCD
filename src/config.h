#pragma once

// ---------- Display (CO5300 QSPI) ----------
// All GPIO values verified against Waveshare board definition and socquique/capsule-radar.
#define SCREEN_W         466
#define SCREEN_H         466
#define LCD_COL_OFFSET   6       // CO5300 x-gap
#define LCD_ROW_OFFSET   0
#define LCD_QSPI_HZ      80000000
#define BRIGHTNESS_DEFAULT 200   // 0-255

#define PIN_LCD_CS   12
#define PIN_LCD_RST  39
#define PIN_LCD_SCLK 38
#define PIN_LCD_D0   4
#define PIN_LCD_D1   5
#define PIN_LCD_D2   6
#define PIN_LCD_D3   7

// ---------- Touch (CST9217 I2C) ----------
#define PIN_TP_INT   11
#define PIN_TP_RST   40
#define TP_MIRROR_X  true
#define TP_MIRROR_Y  true

// ---------- Shared I2C bus ----------
#define PIN_I2C_SDA  15
#define PIN_I2C_SCL  14

// ---------- I2C device addresses ----------
#define I2C_ADDR_TOUCH 0x5A
#define I2C_ADDR_PMIC  0x34

// ---------- Buttons ----------
#define PIN_BOOT_BUTTON 0    // hold >2s to return to mode selector

// ---------- Audio (ES8311 via I2S) ----------
// PIN_I2S_MCLK: verify against board schematic — GPIO3 is not used by anything else
#define PIN_I2S_MCLK  42
#define PIN_I2S_BCLK  9
#define PIN_I2S_LRCLK 45
#define PIN_I2S_DOUT  8
#define PIN_I2S_DIN   10
#define PIN_AUDIO_PA  46

// ---------- BLE detector ----------
#define DET_QUEUE_DEPTH    32   // max detections queued between BLE task and LVGL task
#define DET_DEBOUNCE_MS  8000   // suppress re-alerting the same MAC within this window
#define BLE_SCAN_INTERVAL  100  // ms
#define BLE_SCAN_WINDOW     99  // ms (near-continuous passive scan)
