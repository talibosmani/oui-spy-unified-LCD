// Sky Spy screen — lists detected Drone Remote ID (OpenDroneID) beacons.
// Same safe-zone layout as other mode screens on the 466×466 round AMOLED.
#include "ui_skyspy.h"
#include "config.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#define MAX_DRONE_ROWS 12
#define LIST_PAD_H     50
#define HDR_PAD_H     100
#define FTR_PAD_H      60
#define Y_HDR_TOP      36
#define Y_HDR_H        36
#define Y_SEP          (Y_HDR_TOP + Y_HDR_H)
#define Y_LIST         (Y_SEP + 1)
#define Y_FTR          (SCREEN_H - 90)
#define Y_FTR_H        32
#define LIST_H         (Y_FTR - Y_LIST)

static lv_obj_t    *s_screen    = nullptr;
static lv_obj_t    *s_list      = nullptr;
static lv_obj_t    *s_count_lbl = nullptr;
static lv_obj_t    *s_dot       = nullptr;
static lv_anim_t    s_dot_anim;
static SkySpy_BackCb s_on_back  = nullptr;
static int          s_count     = 0;

static lv_color_t rssi_color(int8_t rssi) {
    if (rssi >= -65) return lv_color_hex(0xaa44ff);
    if (rssi >= -80) return lv_color_hex(0x7722cc);
    return lv_color_hex(0x441166);
}

static const char *ua_type_name(uint8_t t) {
    switch (t) {
        case 1: return "FIXED WING";
        case 2: return "ROTORCRAFT";
        case 3: return "GYROPLANE";
        case 4: return "VTOL";
        case 5: return "ORNITHOPTER";
        case 6: return "GLIDER";
        case 7: return "KITE";
        case 8: return "BALLOON";
        case 12: return "ROCKET";
        case 14: return "OBSTACLE";
        default: return "DRONE";
    }
}

static void dot_exec(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}
static void start_dot_anim() {
    lv_anim_init(&s_dot_anim);
    lv_anim_set_var(&s_dot_anim, s_dot);
    lv_anim_set_exec_cb(&s_dot_anim, dot_exec);
    lv_anim_set_values(&s_dot_anim, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_time(&s_dot_anim, 900);
    lv_anim_set_playback_time(&s_dot_anim, 900);
    lv_anim_set_repeat_count(&s_dot_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_dot_anim);
}

void ui_skyspy_create(SkySpy_BackCb on_back) {
    s_on_back = on_back;
    s_count   = 0;

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_NONE);

    // ── Header ──────────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(s_screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_pos(hdr, 0, Y_HDR_TOP);
    lv_obj_set_size(hdr, SCREEN_W, Y_HDR_H);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(hdr, HDR_PAD_H, 0);
    lv_obj_set_style_pad_right(hdr, HDR_PAD_H, 0);
    lv_obj_set_style_pad_top(hdr, 0, 0);
    lv_obj_set_style_pad_bottom(hdr, 0, 0);
    lv_obj_set_style_pad_column(hdr, 8, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(hdr, LV_DIR_NONE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "SKY SPY");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xaa44ff), 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Pulsing dot + "LIVE" label
    lv_obj_t *dw = lv_obj_create(hdr);
    lv_obj_remove_style_all(dw);
    lv_obj_set_size(dw, LV_SIZE_CONTENT, Y_HDR_H);
    lv_obj_set_style_bg_opa(dw, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(dw, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dw, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dw, 4, 0);
    lv_obj_set_scroll_dir(dw, LV_DIR_NONE);

    s_dot = lv_obj_create(dw);
    lv_obj_set_size(s_dot, 8, 8);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0xaa44ff), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    start_dot_anim();

    lv_obj_t *live = lv_label_create(dw);
    lv_label_set_text(live, "LIVE");
    lv_obj_set_style_text_font(live, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(live, lv_color_hex(0xaa44ff), 0);

    // ── Separator ───────────────────────────────────────────────────────────────
    lv_obj_t *sep = lv_obj_create(s_screen);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, LIST_PAD_H, Y_SEP);
    lv_obj_set_size(sep, SCREEN_W - 2 * LIST_PAD_H, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    // ── Scrollable list ─────────────────────────────────────────────────────────
    s_list = lv_obj_create(s_screen);
    lv_obj_set_pos(s_list, 0, Y_LIST);
    lv_obj_set_size(s_list, SCREEN_W, LIST_H);
    lv_obj_set_style_bg_color(s_list, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_style_pad_left(s_list, LIST_PAD_H, 0);
    lv_obj_set_style_pad_right(s_list, LIST_PAD_H, 0);
    lv_obj_set_style_pad_top(s_list, 4, 0);
    lv_obj_set_style_pad_bottom(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    // Empty-state placeholder
    lv_obj_t *empty = lv_label_create(s_list);
    lv_label_set_text(empty, "Scanning BLE\nfor drone Remote ID\n(ASTM F3411)...");
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(0x2a1a44), 0);
    lv_obj_set_style_pad_top(empty, 50, 0);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);

    // ── Footer ──────────────────────────────────────────────────────────────────
    lv_obj_t *footer = lv_obj_create(s_screen);
    lv_obj_remove_style_all(footer);
    lv_obj_set_pos(footer, 0, Y_FTR);
    lv_obj_set_size(footer, SCREEN_W, Y_FTR_H);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(footer, FTR_PAD_H, 0);
    lv_obj_set_style_pad_right(footer, FTR_PAD_H, 0);
    lv_obj_set_style_pad_top(footer, 0, 0);
    lv_obj_set_style_pad_bottom(footer, 0, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(footer, LV_DIR_NONE);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_count_lbl = lv_label_create(footer);
    lv_label_set_text(s_count_lbl, "0 drones");
    lv_obj_set_style_text_font(s_count_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_count_lbl, lv_color_hex(0x333333), 0);

    lv_scr_load(s_screen);
}

void ui_skyspy_destroy() {
    if (s_dot) lv_anim_del(s_dot, dot_exec);
    if (s_screen) { lv_obj_del_async(s_screen); s_screen = nullptr; }
    s_list = s_dot = s_count_lbl = nullptr;
    s_count = 0;
}

void ui_skyspy_add(const DroneEntry &d) {
    if (!s_list) return;

    // Remove empty placeholder on first real entry
    if (s_count == 0 && lv_obj_get_child_cnt(s_list) == 1)
        lv_obj_del(lv_obj_get_child(s_list, 0));
    s_count++;

    // Cap the list — remove the oldest (last child) if over limit
    if ((int)lv_obj_get_child_cnt(s_list) >= MAX_DRONE_ROWS)
        lv_obj_del(lv_obj_get_child(s_list, -1));

    // New row at top
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 56, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x080812), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0x111122), LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_style_pad_right(row, 8, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);
    lv_obj_move_to_index(row, 0);

    // Top sub-row: type badge + RSSI
    lv_obj_t *tr = lv_obj_create(row);
    lv_obj_remove_style_all(tr);
    lv_obj_set_width(tr, LV_PCT(100));
    lv_obj_set_height(tr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(tr, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(tr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(tr, LV_DIR_NONE);

    // UA type badge
    lv_obj_t *badge = lv_obj_create(tr);
    lv_obj_set_height(badge, 22);
    lv_obj_set_width(badge, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(badge, 8, 0);
    lv_obj_set_style_pad_right(badge, 8, 0);
    lv_obj_set_style_pad_top(badge, 2, 0);
    lv_obj_set_style_pad_bottom(badge, 2, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xaa44ff), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(0xaa44ff), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_opa(badge, LV_OPA_60, 0);
    lv_obj_set_style_radius(badge, 4, 0);
    lv_obj_set_scroll_dir(badge, LV_DIR_NONE);
    lv_obj_t *blbl = lv_label_create(badge);
    lv_label_set_text(blbl, ua_type_name(d.ua_type));
    lv_obj_set_style_text_font(blbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(blbl, lv_color_hex(0xaa44ff), 0);
    lv_obj_center(blbl);

    // RSSI
    char rb[10]; snprintf(rb, sizeof(rb), "%ddBm", (int)d.rssi);
    lv_obj_t *rl = lv_label_create(tr);
    lv_label_set_text(rl, rb);
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rl, rssi_color(d.rssi), 0);

    // Drone ID
    lv_obj_t *id_lbl = lv_label_create(row);
    lv_label_set_text(id_lbl, d.id[0] ? d.id : "(no ID)");
    lv_obj_set_style_text_font(id_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(id_lbl, lv_color_hex(0xcccccc), 0);
    lv_obj_set_width(id_lbl, LV_PCT(100));
    lv_label_set_long_mode(id_lbl, LV_LABEL_LONG_DOT);

    // MAC
    lv_obj_t *ml = lv_label_create(row);
    lv_label_set_text(ml, d.mac);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ml, lv_color_hex(0x3a1a6a), 0);
    lv_obj_set_width(ml, LV_PCT(100));
    lv_label_set_long_mode(ml, LV_LABEL_LONG_DOT);

    // Footer count
    char buf[24];
    snprintf(buf, sizeof(buf), "%d drone%s", s_count, s_count == 1 ? "" : "s");
    if (s_count_lbl) lv_label_set_text(s_count_lbl, buf);
}
