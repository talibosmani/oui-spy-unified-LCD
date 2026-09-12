// Detector screen: header bar + scrollable detection list + count footer.
// Designed for the 466×466 round AMOLED — all content stays inside the circle.
#include "ui_detector.h"
#include "config.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t  *s_screen      = nullptr;
static lv_obj_t  *s_list        = nullptr;
static lv_obj_t  *s_dot         = nullptr;
static lv_obj_t  *s_count_lbl   = nullptr;
static lv_anim_t  s_dot_anim;
static DetBackCb  s_on_back      = nullptr;
static int        s_count        = 0;

// ---- helpers ----------------------------------------------------------------

static lv_color_t rssi_color(int8_t rssi) {
    if (rssi >= -65) return lv_color_hex(0x44dd88);
    if (rssi >= -80) return lv_color_hex(0xffaa00);
    return lv_color_hex(0xff4545);
}

static void dot_anim_cb(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void start_dot_anim() {
    lv_anim_init(&s_dot_anim);
    lv_anim_set_var(&s_dot_anim, s_dot);
    lv_anim_set_exec_cb(&s_dot_anim, dot_anim_cb);
    lv_anim_set_values(&s_dot_anim, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_time(&s_dot_anim, 800);
    lv_anim_set_playback_time(&s_dot_anim, 800);
    lv_anim_set_repeat_count(&s_dot_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_dot_anim);
}

// Max rows kept in the list — caps LVGL object count to prevent heap exhaustion
// in debug mode where every BLE device is shown
#define MAX_DETECTION_ROWS 18

// ---- layout constants -------------------------------------------------------
// On a 466×466 circle (r=233, centre=233), safe horizontal extent at row y:
//   x_margin = 233 - sqrt(233²-(|y-233|)²)
// Key positions used below (with 8 px extra safety margin):
//   y=54  (topbar centre)  → margin≈92 → pad 100 each side → content 266 px
//   y=233 (list centre)    → margin≈0  → pad  50 each side → content 366 px
//   y=390 (footer centre)  → margin≈48 → pad  60 each side → content 346 px

#define LIST_PAD_H   50   // left/right padding on the scrollable list
#define HDR_PAD_H   100   // left/right padding on the header bar
#define FTR_PAD_H    60   // left/right padding on the footer bar

// y positions (all relative to screen top = 0)
#define Y_STATUS_BAR   0
#define Y_HDR_TOP     36   // header starts just below the 36 px chrome status bar
#define Y_HDR_H       36
#define Y_SEP         (Y_HDR_TOP + Y_HDR_H)   // 72
#define Y_LIST        (Y_SEP + 1)              // 73
#define Y_FTR         (SCREEN_H - 90)          // 376
#define Y_FTR_H       32
#define LIST_H        (Y_FTR - Y_LIST)         // 303

// ---- public API -------------------------------------------------------------

void ui_detector_create(DetBackCb on_back) {
    s_on_back = on_back;
    s_count   = 0;

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_NONE);

    // ---- Header bar ---------------------------------------------------------
    // Sits between the persistent chrome status bar and the list.
    // Heavy side padding keeps text out of the circle's curved edges.
    lv_obj_t *hdr = lv_obj_create(s_screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_pos(hdr, 0, Y_HDR_TOP);
    lv_obj_set_size(hdr, SCREEN_W, Y_HDR_H);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(hdr, HDR_PAD_H, 0);
    lv_obj_set_style_pad_right(hdr, HDR_PAD_H, 0);
    lv_obj_set_style_pad_top(hdr, 0, 0);
    lv_obj_set_style_pad_bottom(hdr, 0, 0);
    lv_obj_set_scroll_dir(hdr, LV_DIR_NONE);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 8, 0);

    // "DETECTOR" title
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "DETECTOR");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xff4545), 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Scan status: dot + "LIVE"
    lv_obj_t *dot_wrap = lv_obj_create(hdr);
    lv_obj_remove_style_all(dot_wrap);
    lv_obj_set_height(dot_wrap, Y_HDR_H);
    lv_obj_set_width(dot_wrap, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dot_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(dot_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dot_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dot_wrap, 4, 0);
    lv_obj_set_scroll_dir(dot_wrap, LV_DIR_NONE);

    s_dot = lv_obj_create(dot_wrap);
    lv_obj_set_size(s_dot, 8, 8);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0x44dd88), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    start_dot_anim();

    lv_obj_t *live_lbl = lv_label_create(dot_wrap);
    lv_label_set_text(live_lbl, "LIVE");
    lv_obj_set_style_text_font(live_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(live_lbl, lv_color_hex(0x44dd88), 0);

    // Separator line
    lv_obj_t *sep = lv_obj_create(s_screen);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, LIST_PAD_H, Y_SEP);
    lv_obj_set_size(sep, SCREEN_W - 2 * LIST_PAD_H, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    // ---- Detection list -----------------------------------------------------
    // Side-padded so items clear the round edges at the top and bottom of the list.
    s_list = lv_obj_create(s_screen);
    lv_obj_set_pos(s_list, 0, Y_LIST);
    lv_obj_set_size(s_list, SCREEN_W, LIST_H);
    lv_obj_set_style_bg_color(s_list, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_style_pad_left(s_list, LIST_PAD_H, 0);
    lv_obj_set_style_pad_right(s_list, LIST_PAD_H, 0);
    lv_obj_set_style_pad_top(s_list, 0, 0);
    lv_obj_set_style_pad_bottom(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    // Empty state label
    lv_obj_t *empty = lv_label_create(s_list);
    lv_label_set_text(empty, "Scanning for\nsurveillance hardware...");
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_top(empty, 50, 0);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);

    // ---- Footer with detection count ----------------------------------------
    lv_obj_t *footer = lv_obj_create(s_screen);
    lv_obj_remove_style_all(footer);
    lv_obj_set_pos(footer, 0, Y_FTR);
    lv_obj_set_size(footer, SCREEN_W, Y_FTR_H);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(footer, FTR_PAD_H, 0);
    lv_obj_set_style_pad_right(footer, FTR_PAD_H, 0);
    lv_obj_set_style_pad_top(footer, 0, 0);
    lv_obj_set_style_pad_bottom(footer, 0, 0);
    lv_obj_set_scroll_dir(footer, LV_DIR_NONE);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_count_lbl = lv_label_create(footer);
    lv_label_set_text(s_count_lbl, "0 detections");
    lv_obj_set_style_text_font(s_count_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_count_lbl, lv_color_hex(0x444444), 0);

    lv_scr_load(s_screen);
}

void ui_detector_destroy() {
    if (s_dot) lv_anim_del(s_dot, dot_anim_cb);
    if (s_screen) {
        lv_obj_del_async(s_screen);
        s_screen = nullptr;
    }
    s_list = s_dot = s_count_lbl = nullptr;
    s_count = 0;
}

void ui_detector_add(const Detection &d) {
    if (!s_list) return;

    // Remove the empty-state label on first real detection
    if (s_count == 0) {
        lv_obj_t *first = lv_obj_get_child(s_list, 0);
        if (first && lv_obj_get_child_cnt(s_list) == 1)
            lv_obj_del(first);
    }
    s_count++;

    // Cap to MAX_DETECTION_ROWS — delete oldest (last child) to prevent heap exhaustion
    // in debug mode where every BLE device is logged
    uint32_t nrows = lv_obj_get_child_cnt(s_list);
    if (nrows >= MAX_DETECTION_ROWS) {
        lv_obj_t *oldest = lv_obj_get_child(s_list, -1);
        if (oldest) lv_obj_del(oldest);
    }

    // Row (newest at top)
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 44, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_style_pad_right(row, 8, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);
    lv_obj_move_to_index(row, 0);

    // Vendor badge
    lv_obj_t *badge_bg = lv_obj_create(row);
    lv_obj_set_size(badge_bg, 60, 22);
    lv_obj_set_style_bg_color(badge_bg, lv_color_hex(d.badge_color), 0);
    lv_obj_set_style_bg_opa(badge_bg, LV_OPA_20, 0);
    lv_obj_set_style_border_color(badge_bg, lv_color_hex(d.badge_color), 0);
    lv_obj_set_style_border_width(badge_bg, 1, 0);
    lv_obj_set_style_border_opa(badge_bg, LV_OPA_60, 0);
    lv_obj_set_style_radius(badge_bg, 4, 0);
    lv_obj_set_scroll_dir(badge_bg, LV_DIR_NONE);
    lv_obj_t *badge_lbl = lv_label_create(badge_bg);
    lv_label_set_text(badge_lbl, d.vendor);
    lv_obj_set_style_text_font(badge_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(badge_lbl, lv_color_hex(d.badge_color), 0);
    lv_obj_center(badge_lbl);

    // MAC + method (stacked vertically, flex grows to fill)
    lv_obj_t *info = lv_obj_create(row);
    lv_obj_remove_style_all(info);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(info, LV_DIR_NONE);

    lv_obj_t *mac_lbl = lv_label_create(info);
    lv_label_set_text(mac_lbl, d.mac);
    lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(mac_lbl, lv_color_hex(0x999999), 0);

    lv_obj_t *method_lbl = lv_label_create(info);
    lv_label_set_text(method_lbl, d.method);
    lv_obj_set_style_text_font(method_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(method_lbl, lv_color_hex(0x444444), 0);

    // RSSI (right)
    char rssi_buf[8];
    snprintf(rssi_buf, sizeof(rssi_buf), "%ddBm", (int)d.rssi);
    lv_obj_t *rssi_lbl = lv_label_create(row);
    lv_label_set_text(rssi_lbl, rssi_buf);
    lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rssi_lbl, rssi_color(d.rssi), 0);

    // Update count in footer
    char buf[24];
    snprintf(buf, sizeof(buf), "%d detection%s", s_count, s_count == 1 ? "" : "s");
    lv_label_set_text(s_count_lbl, buf);
}

void ui_detector_set_scanning(bool active) {
    if (!s_dot) return;
    if (active)
        start_dot_anim();
    else {
        lv_anim_del(s_dot, dot_anim_cb);
        lv_obj_set_style_opa(s_dot, LV_OPA_50, 0);
    }
}
