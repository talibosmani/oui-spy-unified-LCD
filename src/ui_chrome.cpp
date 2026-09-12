// Persistent UI chrome on lv_layer_top():
//   - Top status bar: battery symbol + percentage + CHG indicator
//   - Bottom exit button: circle X, shown on all mode screens, hidden on menu
// Because these live on lv_layer_top() they render above every screen automatically.
#include "ui_chrome.h"
#include "config.h"
#include <lvgl.h>
#include <stdio.h>

static lv_obj_t    *s_bat_sym   = nullptr;
static lv_obj_t    *s_bat_label = nullptr;
static lv_obj_t    *s_exit_btn  = nullptr;
static ChromeExitCb s_exit_cb   = nullptr;

static const char *bat_symbol(uint8_t pct) {
    if (pct > 75) return LV_SYMBOL_BATTERY_FULL;
    if (pct > 50) return LV_SYMBOL_BATTERY_3;
    if (pct > 25) return LV_SYMBOL_BATTERY_2;
    if (pct > 10) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void on_exit_clicked(lv_event_t *e) {
    (void)e;
    if (s_exit_cb) s_exit_cb();
}

void ui_chrome_begin() {
    lv_obj_t *layer = lv_layer_top();
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_CLICKABLE);

    // ---- Status bar — centred pill to stay within the round-screen safe zone ----
    // At y=5 the circle half-width ≈ 61px; at y=31 ≈ 120px.
    // A 170px-wide pill centred on the screen is safe at all those y values.
    lv_obj_t *bar = lv_obj_create(layer);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, (SCREEN_W - 170) / 2, 5);
    lv_obj_set_size(bar, 170, 26);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_70, 0);
    lv_obj_set_style_radius(bar, 13, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_scroll_dir(bar, LV_DIR_NONE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 5, 0);
    lv_obj_set_style_pad_left(bar, 10, 0);
    lv_obj_set_style_pad_right(bar, 10, 0);

    s_bat_sym = lv_label_create(bar);
    lv_label_set_text(s_bat_sym, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(s_bat_sym, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_bat_sym, lv_color_hex(0x44dd88), 0);

    s_bat_label = lv_label_create(bar);
    lv_label_set_text(s_bat_label, "---%");
    lv_obj_set_style_text_font(s_bat_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_bat_label, lv_color_hex(0x44dd88), 0);

    // ---- Exit button (bottom-center, hidden by default) ----
    // 64×64 circle; bottom margin 10px → top at SCREEN_H-74=392.
    // Safe-zone check at y=456 (bottom edge): half_width≈67px, button radius=32. OK.
    s_exit_btn = lv_obj_create(layer);
    lv_obj_set_size(s_exit_btn, 64, 64);
    lv_obj_set_pos(s_exit_btn, (SCREEN_W - 64) / 2, SCREEN_H - 74);
    lv_obj_set_style_radius(s_exit_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_exit_btn, lv_color_hex(0x141414), 0);
    lv_obj_set_style_bg_color(s_exit_btn, lv_color_hex(0x222222), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(s_exit_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_exit_btn, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(s_exit_btn, 1, 0);
    lv_obj_set_scroll_dir(s_exit_btn, LV_DIR_NONE);
    lv_obj_clear_flag(s_exit_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(s_exit_btn, 8);
    lv_obj_add_event_cb(s_exit_btn, on_exit_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_exit_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *x_lbl = lv_label_create(s_exit_btn);
    lv_label_set_text(x_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(x_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(x_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_center(x_lbl);
}

void ui_chrome_update_battery(uint8_t pct, bool charging) {
    if (!s_bat_label) return;
    uint32_t col = charging ? 0x44aaff
                 : (pct > 60 ? 0x44dd88 : (pct > 25 ? 0xffaa00 : 0xff4545));
    lv_obj_set_style_text_color(s_bat_sym,   lv_color_hex(col), 0);
    lv_obj_set_style_text_color(s_bat_label, lv_color_hex(col), 0);
    lv_label_set_text(s_bat_sym, charging ? LV_SYMBOL_CHARGE : bat_symbol(pct));
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(s_bat_label, buf);
}

void ui_chrome_show_exit(bool show) {
    if (!s_exit_btn) return;
    if (show) lv_obj_clear_flag(s_exit_btn, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_exit_btn, LV_OBJ_FLAG_HIDDEN);
}

void ui_chrome_set_exit_cb(ChromeExitCb cb) { s_exit_cb = cb; }
