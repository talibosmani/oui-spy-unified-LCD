// CO5300 466x466 AMOLED via Arduino_GFX (QSPI) + LVGL 8.
// Pins verified against Waveshare board definition (socquique/capsule-radar).
// The panel runs off the always-on DC1 rail — no AXP2101 init required to light it up.
// Rotation stripped vs. capsule-radar: oui-spy runs portrait-only.
#include "display.h"
#include "config.h"
#include "touch_cst9217.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

#define LVGL_BUF_LINES 40

static Arduino_DataBus *s_bus  = nullptr;
static Arduino_CO5300  *s_gfx  = nullptr;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static lv_indev_drv_t     s_indev_drv;
static lv_color_t        *s_buf1 = nullptr;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
#if (LV_COLOR_16_SWAP != 0)
    s_gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
#else
    s_gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
#endif
    lv_disp_flush_ready(drv);
}

// CO5300 requires 2-pixel-aligned flush windows; without this, partial updates tear.
static void rounder_cb(lv_disp_drv_t *drv, lv_area_t *area) {
    (void)drv;
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

// Swipe gesture state — written in touch_read_cb (main loop), read in consume_*()
static int16_t s_start_x = -1, s_start_y = -1;
static int16_t s_last_x  = -1, s_last_y  = -1;
static bool    s_pressed  = false;
static bool    s_swipe_from_top = false;
static bool    s_swipe_up       = false;
static bool    s_swipe_left     = false;

// Two-stage phantom rejection for CST9217:
//
// Stage 1 (debounce window): after any release, ignore new presses at the same
//   spot for DEBOUNCE_MS. Phantom UPs extend this window so it stays active as
//   long as phantom cycles keep coming.
//
// Stage 2 (confirmation gate): a press is forwarded to LVGL only after it has
//   been sustained for CONFIRM_MS. Phantoms vanish in < 5 ms, so they are
//   silently cancelled before LVGL sees a DOWN event → no ghost clicks.
//
static uint32_t s_release_ms  = 0;
static int16_t  s_release_x   = -1;
static int16_t  s_release_y   = -1;
static uint32_t s_press_ms    = 0;
static bool     s_confirmed   = false;
#define DEBOUNCE_MS   300
#define DEBOUNCE_RAD   40
#define CONFIRM_MS     25u   // CST9217 phantoms vanish in < 5 ms; 25 ms is safe

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    uint16_t x, y;
    if (touch_read(&x, &y)) {
        if (!s_pressed) {
            const uint32_t now = millis();
            const bool debounced =
                s_release_ms != 0 &&
                (int32_t)(now - s_release_ms) < DEBOUNCE_MS &&
                abs((int)x - s_release_x) < DEBOUNCE_RAD &&
                abs((int)y - s_release_y) < DEBOUNCE_RAD;
            if (debounced) {
                data->state = LV_INDEV_STATE_RELEASED;
                return;
            }
            s_press_ms  = now;
            s_confirmed = false;
            s_start_x   = (int16_t)x;
            s_start_y   = (int16_t)y;
            s_pressed   = true;
        }
        s_last_x = (int16_t)x;
        s_last_y = (int16_t)y;

        if (!s_confirmed && (millis() - s_press_ms) >= CONFIRM_MS) {
            s_confirmed = true;
            Serial.printf("[indev] DOWN  x=%3d  y=%3d\n", x, y);
        }

        if (s_confirmed) {
            data->point.x = (lv_coord_t)x;
            data->point.y = (lv_coord_t)y;
            data->state   = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;   // pending confirmation
        }
    } else {
        if (s_pressed) {
            if (!s_confirmed) {
                // Phantom: lifted before CONFIRM_MS → extend debounce, cancel silently
                s_release_ms = millis();
                s_release_x  = s_last_x;
                s_release_y  = s_last_y;
                Serial.printf("[indev] PHANTOM  x=%3d  y=%3d  held=%lums\n",
                              s_last_x, s_last_y,
                              (unsigned long)(millis() - s_press_ms));
            } else {
                // Real UP — record release + detect swipes
                Serial.printf("[indev] UP    last=(%3d,%3d)  start=(%3d,%3d)\n",
                              s_last_x, s_last_y, s_start_x, s_start_y);
                s_release_ms = millis();
                s_release_x  = s_last_x;
                s_release_y  = s_last_y;
                if (s_start_y >= 0 && s_last_y >= 0) {
                    int dy     = s_last_y - s_start_y;
                    int dx_raw = s_last_x - s_start_x;
                    int dx     = dx_raw < 0 ? -dx_raw : dx_raw;
                    int dy_abs = dy  < 0 ? -dy  : dy;
                    if (dy >= 70 && s_start_y < 50)
                        s_swipe_from_top = true;
                    if (dy <= -70 && (-dy) > dx)
                        s_swipe_up = true;
                    if (dx_raw <= -50 && dx * 10 > dy_abs * 7)
                        s_swipe_left = true;
                }
            }
        }
        s_pressed   = false;
        s_confirmed = false;
        s_start_x   = s_start_y = s_last_x = s_last_y = -1;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

namespace display {

bool begin() {
    Serial.println("[display] init CO5300...");
    s_bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK,
                                  PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
    s_gfx = new Arduino_CO5300(s_bus, PIN_LCD_RST, 0 /*rotation*/,
                               SCREEN_W, SCREEN_H,
                               LCD_COL_OFFSET, LCD_ROW_OFFSET, 0, 0);
    if (!s_gfx->begin(LCD_QSPI_HZ)) {
        Serial.println("[display] FAILED");
        return false;
    }
    s_gfx->fillScreen(RGB565_BLACK);
    s_gfx->setBrightness(BRIGHTNESS_DEFAULT);

    lv_init();

    const size_t buf_px = (size_t)SCREEN_W * LVGL_BUF_LINES;
    s_buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t),
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_buf1)
        s_buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, nullptr, buf_px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res    = SCREEN_W;
    s_disp_drv.ver_res    = SCREEN_H;
    s_disp_drv.flush_cb   = flush_cb;
    s_disp_drv.rounder_cb = rounder_cb;
    s_disp_drv.draw_buf   = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    if (touch_begin()) {
        lv_indev_drv_init(&s_indev_drv);
        s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
        s_indev_drv.read_cb = touch_read_cb;
        lv_indev_drv_register(&s_indev_drv);
        Serial.println("[display] touch registered");
    }

    Serial.printf("[display] ready. PSRAM free: %u KB\n",
                  (unsigned)(ESP.getFreePsram() / 1024));
    return true;
}

void loop()           { lv_timer_handler(); }
void setBrightness(uint8_t v) { if (s_gfx) s_gfx->setBrightness(v); }

bool consume_swipe_from_top() {
    bool v = s_swipe_from_top;
    s_swipe_from_top = false;
    return v;
}

bool consume_swipe_up() {
    bool v = s_swipe_up;
    s_swipe_up = false;
    return v;
}

bool consume_swipe_left() {
    bool v = s_swipe_left;
    s_swipe_left = false;
    return v;
}

} // namespace display
