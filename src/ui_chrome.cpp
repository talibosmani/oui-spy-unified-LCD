// Persistent UI chrome on lv_layer_top():
//   - Top status bar: battery symbol + percentage + CHG indicator
//   - Bottom exit button: circle X, shown on all mode screens, hidden on menu
// Because these live on lv_layer_top() they render above every screen automatically.
#include "ui_chrome.h"
#include "config.h"
#include <lvgl.h>
#include <stdio.h>

static lv_obj_t      *s_bat_sym      = nullptr;
static lv_obj_t      *s_bat_label    = nullptr;
static lv_obj_t      *s_store_label  = nullptr;
static lv_obj_t      *s_exit_btn     = nullptr;
static ChromeExitCb   s_exit_cb      = nullptr;
static bool           s_has_sd       = false;

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

// ---- SD card boot dialog -------------------------------------------------------

struct SdDialogCtx {
    SdPrimaryCb   on_primary;
    SdSecondaryCb on_secondary;
    lv_obj_t     *overlay;
    lv_obj_t     *status;
};
static SdDialogCtx s_sd_ctx;

static void sd_dialog_close() {
    if (s_sd_ctx.overlay) lv_obj_del(s_sd_ctx.overlay);
    s_sd_ctx.overlay = nullptr;
    s_sd_ctx.status  = nullptr;
}

static void sd_dialog_btn_cb(lv_event_t *e) {
    bool primary = (lv_event_get_user_data(e) != nullptr);
    if (!primary) {
        if (s_sd_ctx.on_secondary) s_sd_ctx.on_secondary();
        sd_dialog_close();
        return;
    }

    lv_label_set_text(s_sd_ctx.status, "Working...");
    lv_obj_set_style_text_color(s_sd_ctx.status, lv_color_hex(0xffffff), 0);
    lv_refr_now(nullptr); // paint before the blocking call

    const char *err = s_sd_ctx.on_primary ? s_sd_ctx.on_primary() : nullptr;
    if (!err) { sd_dialog_close(); return; }

    lv_label_set_text(s_sd_ctx.status, err);
    lv_obj_set_style_text_color(s_sd_ctx.status, lv_color_hex(0xff4545), 0);
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
    lv_obj_set_pos(bar, (SCREEN_W - 220) / 2, 5);
    lv_obj_set_size(bar, 220, 26);
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

    // Divider
    lv_obj_t *div = lv_label_create(bar);
    lv_label_set_text(div, "|");
    lv_obj_set_style_text_font(div, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(div, lv_color_hex(0x444444), 0);

    // Storage indicator — display only (dialog shown at boot via ui_chrome_show_sd_dialog)
    s_store_label = lv_label_create(bar);
    lv_label_set_text(s_store_label, "LFS");
    lv_obj_set_style_text_font(s_store_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_store_label, lv_color_hex(0xffaa00), 0);

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

void ui_chrome_update_storage(bool has_sd) {
    if (!s_store_label) return;
    s_has_sd = has_sd;
    if (has_sd) {
        lv_label_set_text(s_store_label, "SD");
        lv_obj_set_style_text_color(s_store_label, lv_color_hex(0x44dd88), 0);
    } else {
        lv_label_set_text(s_store_label, "LFS");
        lv_obj_set_style_text_color(s_store_label, lv_color_hex(0xffaa00), 0); // amber = actionable
    }
}

void ui_chrome_show_sd_dialog(const char *status_text, const char *hint_text,
                              const char *primary,   SdPrimaryCb   on_primary,
                              const char *secondary, SdSecondaryCb on_secondary) {
    s_sd_ctx.on_primary   = on_primary;
    s_sd_ctx.on_secondary = on_secondary;

    // Overlay on lv_layer_top() so it survives screen loads and sits above the menu
    lv_obj_t *ov = lv_obj_create(lv_layer_top());
    s_sd_ctx.overlay = ov;
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE); // absorb touches so the menu underneath is inert

    lv_obj_t *title = lv_label_create(ov);
    lv_label_set_text(title, LV_SYMBOL_SD_CARD "  Storage");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *status = lv_label_create(ov);
    s_sd_ctx.status = status;
    lv_label_set_text(status, status_text);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xffaa00), 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(status, 340);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 112);

    if (hint_text) {
        lv_obj_t *hint = lv_label_create(ov);
        lv_label_set_text(hint, hint_text);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x777777), 0);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(hint, 340);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, -10);
    }

    // Primary (green) — optional
    int secondary_y = -28;
    if (primary) {
        lv_obj_t *btn = lv_btn_create(ov);
        lv_obj_set_size(btn, 260, 56);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -90);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a6e3a), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x24a854), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_add_event_cb(btn, sd_dialog_btn_cb, LV_EVENT_CLICKED, (void*)1);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, primary);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
    } else {
        secondary_y = -70; // centre the lone button in the safe zone
    }

    // Secondary (grey)
    lv_obj_t *btn2 = lv_btn_create(ov);
    lv_obj_set_size(btn2, 260, 48);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_MID, 0, secondary_y);
    lv_obj_set_style_bg_color(btn2, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_bg_color(btn2, lv_color_hex(0x3a3a3a), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn2, 12, 0);
    lv_obj_add_event_cb(btn2, sd_dialog_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl2 = lv_label_create(btn2);
    lv_label_set_text(lbl2, secondary);
    lv_obj_set_style_text_font(lbl2, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl2);
}
