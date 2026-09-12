// BLE Sniff screen — shows every unique BLE advertisement seen.
// Designed for 466×466 round AMOLED; all content stays inside the circle.
// Layout constants follow the same safe-zone conventions as ui_detector.cpp.
#include "ui_blesniff.h"
#include "config.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#define MAX_ROWS 20          // cap LVGL object count
#define LIST_PAD_H  50       // left/right padding on list (safe inside circle)
#define HDR_PAD_H  100       // left/right padding on header
#define FTR_PAD_H   60       // left/right padding on footer
#define Y_HDR_TOP   36
#define Y_HDR_H     36
#define Y_SEP       (Y_HDR_TOP + Y_HDR_H)
#define Y_LIST      (Y_SEP + 1)
#define Y_FTR       (SCREEN_H - 90)
#define Y_FTR_H     32
#define LIST_H      (Y_FTR - Y_LIST)

static lv_obj_t      *s_screen    = nullptr;
static lv_obj_t      *s_list      = nullptr;
static lv_obj_t      *s_count_lbl = nullptr;
static lv_obj_t      *s_dot       = nullptr;
static lv_anim_t      s_dot_anim;
static BLESniffBackCb s_on_back   = nullptr;
static int            s_count     = 0;

static lv_color_t rssi_col(int8_t r) {
    if (r >= -65) return lv_color_hex(0x44dd88);
    if (r >= -80) return lv_color_hex(0xffaa00);
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
    lv_anim_set_time(&s_dot_anim, 900);
    lv_anim_set_playback_time(&s_dot_anim, 900);
    lv_anim_set_repeat_count(&s_dot_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_dot_anim);
}

void ui_blesniff_create(BLESniffBackCb on_back) {
    s_on_back = on_back;
    s_count   = 0;

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_NONE);

    // Header
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

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "BLE SNIFF");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00ccaa), 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // LIVE dot
    lv_obj_t *dw = lv_obj_create(hdr);
    lv_obj_remove_style_all(dw);
    lv_obj_set_height(dw, Y_HDR_H);
    lv_obj_set_width(dw, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dw, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(dw, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dw, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dw, 4, 0);
    lv_obj_set_scroll_dir(dw, LV_DIR_NONE);

    s_dot = lv_obj_create(dw);
    lv_obj_set_size(s_dot, 8, 8);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0x00ccaa), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    start_dot_anim();

    lv_obj_t *live = lv_label_create(dw);
    lv_label_set_text(live, "LIVE");
    lv_obj_set_style_text_font(live, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(live, lv_color_hex(0x00ccaa), 0);

    // Separator
    lv_obj_t *sep = lv_obj_create(s_screen);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, LIST_PAD_H, Y_SEP);
    lv_obj_set_size(sep, SCREEN_W - 2 * LIST_PAD_H, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    // Scrollable list
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

    // Empty state
    lv_obj_t *empty = lv_label_create(s_list);
    lv_label_set_text(empty, "Listening for\nBLE advertisements...");
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_top(empty, 50, 0);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);

    // Footer
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
    lv_label_set_text(s_count_lbl, "0 devices");
    lv_obj_set_style_text_font(s_count_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_count_lbl, lv_color_hex(0x444444), 0);

    lv_scr_load(s_screen);
}

void ui_blesniff_destroy() {
    if (s_dot) lv_anim_del(s_dot, dot_anim_cb);
    if (s_screen) { lv_obj_del_async(s_screen); s_screen = nullptr; }
    s_list = s_dot = s_count_lbl = nullptr;
    s_count = 0;
}

void ui_blesniff_add(const SniffEntry &e) {
    if (!s_list) return;

    // Remove empty-state label on first entry
    if (s_count == 0) {
        lv_obj_t *first = lv_obj_get_child(s_list, 0);
        if (first && lv_obj_get_child_cnt(s_list) == 1) lv_obj_del(first);
    }
    s_count++;

    // Delete oldest when cap reached
    if (lv_obj_get_child_cnt(s_list) >= MAX_ROWS) {
        lv_obj_t *oldest = lv_obj_get_child(s_list, -1);
        if (oldest) lv_obj_del(oldest);
    }

    // Row — newest at top
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 34, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_left(row, 6, 0);
    lv_obj_set_style_pad_right(row, 6, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    lv_obj_set_style_pad_bottom(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);
    lv_obj_move_to_index(row, 0);

    // RSSI coloured dot
    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_bg_color(dot, rssi_col(e.rssi), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_scroll_dir(dot, LV_DIR_NONE);

    // MAC label — fixed width, dots if too long (won't be — MACs are always 17 chars)
    lv_obj_t *mac = lv_label_create(row);
    lv_label_set_text(mac, e.mac);
    lv_obj_set_style_text_font(mac, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(mac, lv_color_hex(0x888888), 0);
    lv_obj_set_width(mac, 155);
    lv_label_set_long_mode(mac, LV_LABEL_LONG_DOT);

    // Name (flex grows to fill remaining space, truncated)
    lv_obj_t *name = lv_label_create(row);
    const char *nm = (e.name[0] != '\0') ? e.name : "?";
    lv_label_set_text(name, nm);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0x555555), 0);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    // RSSI value (right-aligned, fixed width)
    char rbuf[8]; snprintf(rbuf, sizeof(rbuf), "%d", (int)e.rssi);
    lv_obj_t *rssi_lbl = lv_label_create(row);
    lv_label_set_text(rssi_lbl, rbuf);
    lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rssi_lbl, rssi_col(e.rssi), 0);
    lv_obj_set_width(rssi_lbl, 38);
    lv_obj_set_style_text_align(rssi_lbl, LV_TEXT_ALIGN_RIGHT, 0);

    // Footer count
    char buf[28]; snprintf(buf, sizeof(buf), "%d device%s", s_count, s_count == 1 ? "" : "s");
    lv_label_set_text(s_count_lbl, buf);
}
