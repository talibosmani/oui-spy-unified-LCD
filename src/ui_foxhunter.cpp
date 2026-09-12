// Fox Hunter UI — two views on one LVGL screen:
//   SCAN: scrollable list of nearby BLE devices sorted by RSSI.
//   TRACK: large arc RSSI gauge — ideal for the 466×466 round AMOLED.
//
// Round-screen safe zones used throughout:
//   Header (y=36-72)  → HDR_PAD_H=100 left/right
//   List   (y=73-376) → LIST_PAD_H=50 left/right
//   Footer (y=376-408)→ FTR_PAD_H=60 left/right
//   Track arc centred at (233,210), r=130 → fits circle at all y values
#include "ui_foxhunter.h"
#include "config.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

// ─── Layout constants ────────────────────────────────────────────────────────
#define MAX_LIST_ROWS 20
#define LIST_PAD_H    50
#define HDR_PAD_H    100
#define FTR_PAD_H     60
#define Y_HDR_TOP     36
#define Y_HDR_H       36
#define Y_SEP         (Y_HDR_TOP + Y_HDR_H)
#define Y_LIST        (Y_SEP + 1)
#define Y_FTR         (SCREEN_H - 90)
#define Y_FTR_H       32
#define LIST_H        (Y_FTR - Y_LIST)

// Arc geometry: centred at (233, 210), radius 130 px, 270° sweep opening bottom
#define ARC_CX  233
#define ARC_CY  210
#define ARC_R   130
// RSSI gauge: -100 dBm = 0%, -30 dBm = 100%
#define RSSI_MIN  (-100)
#define RSSI_MAX  (-30)

// ─── State ───────────────────────────────────────────────────────────────────
static lv_obj_t *s_screen      = nullptr;
static FoxBackCb s_on_back     = nullptr;
static bool      s_tracking    = false;
static char      s_track_mac[18] = {};

// SCAN view objects
static lv_obj_t *s_scan_panel  = nullptr;
static lv_obj_t *s_list        = nullptr;
static lv_obj_t *s_footer_lbl  = nullptr;
static lv_obj_t *s_dot         = nullptr;
static lv_anim_t s_dot_anim;

// TRACK view objects
static lv_obj_t *s_track_panel = nullptr;
static lv_obj_t *s_arc         = nullptr;
static lv_obj_t *s_rssi_lbl    = nullptr;   // big RSSI number in arc centre
static lv_obj_t *s_name_lbl    = nullptr;   // device name below arc
static lv_obj_t *s_peak_lbl    = nullptr;   // peak RSSI near top of screen
static lv_obj_t *s_mac_lbl     = nullptr;   // MAC below name

// Row MAC slots (for tap-to-track callbacks)
static char s_row_macs[MAX_LIST_ROWS][18];

// Brief cooldown after returning to scan view — prevents the lingering touch
// event from the SCAN LIST button (which sits at the same y-coords as scan rows)
// from immediately firing on_row_tap and bouncing back to track mode.
static bool s_scan_blocked = false;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static lv_color_t rssi_col(int8_t r) {
    if (r >= -65) return lv_color_hex(0x44dd88);
    if (r >= -80) return lv_color_hex(0xffaa00);
    return lv_color_hex(0xff4545);
}

static int rssi_pct(int8_t r) {
    int p = ((int)r - RSSI_MIN) * 100 / (RSSI_MAX - RSSI_MIN);
    if (p < 0) p = 0; if (p > 100) p = 100;
    return p;
}

static void dot_anim_cb(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}
static void start_dot_anim() {
    lv_anim_init(&s_dot_anim);
    lv_anim_set_var(&s_dot_anim, s_dot);
    lv_anim_set_exec_cb(&s_dot_anim, dot_anim_cb);
    lv_anim_set_values(&s_dot_anim, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_time(&s_dot_anim, 700);
    lv_anim_set_playback_time(&s_dot_anim, 700);
    lv_anim_set_repeat_count(&s_dot_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_dot_anim);
}

// ─── Track-row tap callback ───────────────────────────────────────────────────
static void on_row_tap(lv_event_t *ev) {
    if (s_scan_blocked) return;
    const char *mac = (const char *)lv_event_get_user_data(ev);
    if (!mac) return;
    strncpy(s_track_mac, mac, sizeof(s_track_mac) - 1);
    s_tracking = true;

    // Update track panel labels
    if (s_mac_lbl)  lv_label_set_text(s_mac_lbl, mac);
    if (s_name_lbl) lv_label_set_text(s_name_lbl, "");
    if (s_rssi_lbl) lv_label_set_text(s_rssi_lbl, "---");
    if (s_peak_lbl) lv_label_set_text(s_peak_lbl, "PEAK: ---");
    if (s_arc)      lv_arc_set_value(s_arc, 0);

    // Show track panel, hide scan panel
    lv_obj_add_flag(s_scan_panel,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_track_panel, LV_OBJ_FLAG_HIDDEN);
}

static void unblock_scan_cb(lv_timer_t *) { s_scan_blocked = false; }

static void do_back_from_track(void *) {
    s_tracking = false;
    s_track_mac[0] = '\0';
    // Block row taps for 350 ms — the SCAN LIST button sits at the same y
    // coordinates as scan rows; without this guard the touch controller's
    // lingering press immediately fires on_row_tap and bounces back.
    s_scan_blocked = true;
    lv_timer_t *t = lv_timer_create(unblock_scan_cb, 350, nullptr);
    lv_timer_set_repeat_count(t, 1);
    if (s_track_panel) lv_obj_add_flag(s_track_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_scan_panel)  lv_obj_clear_flag(s_scan_panel, LV_OBJ_FLAG_HIDDEN);
}

static void on_back_from_track(lv_event_t *ev) {
    (void)ev;
    lv_async_call(do_back_from_track, nullptr);
}

// ─── SCAN panel builder ───────────────────────────────────────────────────────
static void build_scan_panel() {
    s_scan_panel = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_scan_panel);
    lv_obj_set_pos(s_scan_panel, 0, 0);
    lv_obj_set_size(s_scan_panel, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(s_scan_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(s_scan_panel, LV_DIR_NONE);
    lv_obj_clear_flag(s_scan_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t *hdr = lv_obj_create(s_scan_panel);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_pos(hdr, 0, Y_HDR_TOP);
    lv_obj_set_size(hdr, SCREEN_W, Y_HDR_H);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(hdr, HDR_PAD_H, 0);
    lv_obj_set_style_pad_right(hdr, HDR_PAD_H, 0);
    lv_obj_set_style_pad_top(hdr, 0, 0); lv_obj_set_style_pad_bottom(hdr, 0, 0);
    lv_obj_set_scroll_dir(hdr, LV_DIR_NONE);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 8, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "FOX HUNTER");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xff8c00), 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Scan dot
    lv_obj_t *dw = lv_obj_create(hdr);
    lv_obj_remove_style_all(dw);
    lv_obj_set_height(dw, Y_HDR_H); lv_obj_set_width(dw, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dw, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(dw, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dw, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dw, 4, 0);
    lv_obj_set_scroll_dir(dw, LV_DIR_NONE);

    s_dot = lv_obj_create(dw);
    lv_obj_set_size(s_dot, 8, 8);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0xff8c00), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    start_dot_anim();

    lv_obj_t *live = lv_label_create(dw);
    lv_label_set_text(live, "SCAN");
    lv_obj_set_style_text_font(live, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(live, lv_color_hex(0xff8c00), 0);

    // Separator
    lv_obj_t *sep = lv_obj_create(s_scan_panel);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, LIST_PAD_H, Y_SEP);
    lv_obj_set_size(sep, SCREEN_W - 2 * LIST_PAD_H, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    // List (padded, vertical scroll)
    s_list = lv_obj_create(s_scan_panel);
    lv_obj_set_pos(s_list, 0, Y_LIST);
    lv_obj_set_size(s_list, SCREEN_W, LIST_H);
    lv_obj_set_style_bg_color(s_list, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_style_pad_left(s_list, LIST_PAD_H, 0);
    lv_obj_set_style_pad_right(s_list, LIST_PAD_H, 0);
    lv_obj_set_style_pad_top(s_list, 0, 0); lv_obj_set_style_pad_bottom(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    // Empty-state label
    lv_obj_t *empty = lv_label_create(s_list);
    lv_label_set_text(empty, "Scanning...\nTap a device to track it.");
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_top(empty, 50, 0);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);

    // Footer
    lv_obj_t *footer = lv_obj_create(s_scan_panel);
    lv_obj_remove_style_all(footer);
    lv_obj_set_pos(footer, 0, Y_FTR);
    lv_obj_set_size(footer, SCREEN_W, Y_FTR_H);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(footer, FTR_PAD_H, 0);
    lv_obj_set_style_pad_right(footer, FTR_PAD_H, 0);
    lv_obj_set_style_pad_top(footer, 0, 0); lv_obj_set_style_pad_bottom(footer, 0, 0);
    lv_obj_set_scroll_dir(footer, LV_DIR_NONE);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_footer_lbl = lv_label_create(footer);
    lv_label_set_text(s_footer_lbl, "tap a device to track");
    lv_obj_set_style_text_font(s_footer_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_footer_lbl, lv_color_hex(0x333333), 0);
}

// ─── TRACK panel builder ─────────────────────────────────────────────────────
static void build_track_panel() {
    s_track_panel = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_track_panel);
    lv_obj_set_pos(s_track_panel, 0, 0);
    lv_obj_set_size(s_track_panel, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(s_track_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(s_track_panel, LV_DIR_NONE);
    lv_obj_clear_flag(s_track_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_track_panel, LV_OBJ_FLAG_HIDDEN);  // hidden until lock

    // Back button — inside arc, below dBm label (y=248-290).
    // At y=290 (bottom edge): dist=80, safe_half=√(130²-80²)=√(16900-6400)=√10500≈102px.
    // Button half-width=75px < 102 → fits within the arc circle.
    lv_obj_t *back_btn = lv_obj_create(s_track_panel);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 150, 42);
    lv_obj_set_pos(back_btn, (SCREEN_W - 150) / 2, 248);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x100a00), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0xff8c00), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 21, 0);
    lv_obj_set_scroll_dir(back_btn, LV_DIR_NONE);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(back_btn, 6);
    lv_obj_add_event_cb(back_btn, on_back_from_track, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LIST " SCAN LIST");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xff8c00), 0);
    lv_obj_center(back_lbl);

    // RSSI arc gauge — 270° sweep, opens at bottom (like a signal meter)
    // bg_angle_start=135, bg_angle_end=45 → clockwise sweep through top = 270°
    s_arc = lv_arc_create(s_track_panel);
    lv_obj_set_size(s_arc, ARC_R * 2, ARC_R * 2);
    lv_obj_set_pos(s_arc, ARC_CX - ARC_R, ARC_CY - ARC_R);
    lv_arc_set_bg_angles(s_arc, 135, 45);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 0);
    lv_arc_set_mode(s_arc, LV_ARC_MODE_NORMAL);

    // Background track (grey)
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_arc, LV_OPA_COVER, LV_PART_MAIN);
    // Indicator (orange-green, colour set dynamically)
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0xff4545), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_arc, LV_OPA_COVER, LV_PART_INDICATOR);
    // No knob
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);

    // RSSI value — centred inside the arc
    s_rssi_lbl = lv_label_create(s_track_panel);
    lv_label_set_text(s_rssi_lbl, "---");
    lv_obj_set_style_text_font(s_rssi_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_rssi_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_pos(s_rssi_lbl, 0, ARC_CY - 18);
    lv_obj_set_width(s_rssi_lbl, SCREEN_W);
    lv_obj_set_style_text_align(s_rssi_lbl, LV_TEXT_ALIGN_CENTER, 0);

    // "dBm" sub-label just below RSSI value
    lv_obj_t *unit = lv_label_create(s_track_panel);
    lv_label_set_text(unit, "dBm");
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(0x444444), 0);
    lv_obj_set_pos(unit, 0, ARC_CY + 8);
    lv_obj_set_width(unit, SCREEN_W);
    lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_CENTER, 0);

    // Peak RSSI — top of screen where back button used to be (y=54).
    // At y=54: dist=|54-233|=179, safe_half=√(233²-179²)=√(54289-32041)=√22248≈149px.
    // "PEAK: -100 dBm" text ≈ 80px half-width → safe.
    s_peak_lbl = lv_label_create(s_track_panel);
    lv_label_set_text(s_peak_lbl, "PEAK: ---");
    lv_obj_set_style_text_font(s_peak_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_peak_lbl, lv_color_hex(0x444444), 0);
    lv_obj_set_pos(s_peak_lbl, 0, 54);
    lv_obj_set_width(s_peak_lbl, SCREEN_W);
    lv_obj_set_style_text_align(s_peak_lbl, LV_TEXT_ALIGN_CENTER, 0);

    // Device name — below arc (y=352). At y_mid≈362: safe_half≈189px; content 366/2=183px → ok.
    s_name_lbl = lv_label_create(s_track_panel);
    lv_label_set_text(s_name_lbl, "");
    lv_obj_set_style_text_font(s_name_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_name_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_pos(s_name_lbl, LIST_PAD_H, 352);
    lv_obj_set_width(s_name_lbl, SCREEN_W - 2 * LIST_PAD_H);
    lv_obj_set_style_text_align(s_name_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_name_lbl, LV_LABEL_LONG_DOT);

    // MAC address — below device name (y=382). At y_mid≈392: safe_half≈170px; content 326/2=163px → ok.
    s_mac_lbl = lv_label_create(s_track_panel);
    lv_label_set_text(s_mac_lbl, "");
    lv_obj_set_style_text_font(s_mac_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_mac_lbl, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(s_mac_lbl, FTR_PAD_H + 10, 382);
    lv_obj_set_width(s_mac_lbl, SCREEN_W - 2 * (FTR_PAD_H + 10));
    lv_obj_set_style_text_align(s_mac_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_mac_lbl, LV_LABEL_LONG_DOT);

    // Weak / Strong labels at arc ends
    lv_obj_t *weak_lbl = lv_label_create(s_track_panel);
    lv_label_set_text(weak_lbl, "WEAK");
    lv_obj_set_style_text_font(weak_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(weak_lbl, lv_color_hex(0x333333), 0);
    // Arc starts at 135° = (ARC_CX + ARC_R*cos(135°), ARC_CY - ARC_R*sin(135°))
    // cos(135°)=-0.707, sin(135°)=0.707 → x=233-92=141, y=210-92=118
    // Place label left of the start, adjust slightly
    lv_obj_set_pos(weak_lbl, 60, 300);

    lv_obj_t *strong_lbl = lv_label_create(s_track_panel);
    lv_label_set_text(strong_lbl, "STRONG");
    lv_obj_set_style_text_font(strong_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(strong_lbl, lv_color_hex(0x333333), 0);
    // Arc ends at 45°: x=233+92=325, y=210-92=118 → right side
    lv_obj_set_pos(strong_lbl, 316, 300);
}

// ─── Public API ───────────────────────────────────────────────────────────────
void ui_foxhunter_create(FoxBackCb on_back) {
    s_on_back    = on_back;
    s_tracking   = false;
    s_track_mac[0] = '\0';

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_NONE);

    build_scan_panel();
    build_track_panel();

    lv_scr_load(s_screen);
}

void ui_foxhunter_destroy() {
    if (s_dot) lv_anim_del(s_dot, dot_anim_cb);
    if (s_screen) { lv_obj_del_async(s_screen); s_screen = nullptr; }
    s_scan_panel = s_track_panel = s_list = s_arc = nullptr;
    s_rssi_lbl = s_name_lbl = s_peak_lbl = s_mac_lbl = s_footer_lbl = s_dot = nullptr;
    s_tracking   = false;
}

// ─── Internal: update RSSI arc gauge ─────────────────────────────────────────
static void update_gauge(int8_t rssi, int8_t peak, const char *name) {
    int pct = rssi_pct(rssi);
    lv_color_t col = rssi_col(rssi);

    if (s_arc) {
        lv_arc_set_value(s_arc, pct);
        lv_obj_set_style_arc_color(s_arc, col, LV_PART_INDICATOR);
    }
    if (s_rssi_lbl) {
        char buf[8];
        if (rssi == -128) {
            lv_label_set_text(s_rssi_lbl, "---");
            lv_obj_set_style_text_color(s_rssi_lbl, lv_color_hex(0x333333), 0);
        } else {
            snprintf(buf, sizeof(buf), "%d", (int)rssi);
            lv_label_set_text(s_rssi_lbl, buf);
            lv_obj_set_style_text_color(s_rssi_lbl, col, 0);
        }
    }
    if (s_peak_lbl && peak != -128) {
        char pb[22]; snprintf(pb, sizeof(pb), "PEAK: %d dBm", (int)peak);
        lv_label_set_text(s_peak_lbl, pb);
    }
    if (s_name_lbl && name && name[0])
        lv_label_set_text(s_name_lbl, name);
}

// ─── Public update (scan list OR track gauge depending on mode) ───────────────
void ui_foxhunter_update(const FoxDevice *devs, int count) {
    if (!s_screen) return;

    // TRACK mode: find tracked device and update gauge
    if (s_tracking) {
        for (int i = 0; i < count; i++) {
            if (strncmp(devs[i].mac, s_track_mac, 17) == 0) {
                update_gauge(devs[i].rssi, devs[i].rssi_peak, devs[i].name);
                break;
            }
        }
        return;
    }

    // SCAN mode: rebuild list
    if (!s_list) return;
    lv_obj_clean(s_list);

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, "Scanning...\nTap a device to track it.");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x333333), 0);
        lv_obj_set_style_pad_top(empty, 50, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        return;
    }

    int n = count < MAX_LIST_ROWS ? count : MAX_LIST_ROWS;
    for (int i = 0; i < n; i++) {
        const FoxDevice &d = devs[i];
        strncpy(s_row_macs[i], d.mac, 17);
        s_row_macs[i][17] = '\0';

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 52);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x080808), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x151515), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_hex(0x111111), LV_PART_MAIN);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_left(row, 8, 0);
        lv_obj_set_style_pad_right(row, 8, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_scroll_dir(row, LV_DIR_NONE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_row_tap, LV_EVENT_CLICKED, s_row_macs[i]);

        // RSSI coloured bar (narrow rect scaled by signal strength)
        int bar_w = 4 + rssi_pct(d.rssi) * 20 / 100;  // 4-24 px
        lv_obj_t *bar = lv_obj_create(row);
        lv_obj_set_size(bar, bar_w, 16);
        lv_obj_set_style_bg_color(bar, rssi_col(d.rssi), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_scroll_dir(bar, LV_DIR_NONE);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

        // MAC
        lv_obj_t *mac = lv_label_create(row);
        lv_label_set_text(mac, d.mac);
        lv_obj_set_style_text_font(mac, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(mac, lv_color_hex(0x888888), 0);
        lv_obj_set_width(mac, 128);
        lv_label_set_long_mode(mac, LV_LABEL_LONG_DOT);

        // Name (grows to fill)
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, d.name[0] ? d.name : "?");
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(0x555555), 0);
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

        // dBm value
        char rb[8]; snprintf(rb, sizeof(rb), "%d", (int)d.rssi);
        lv_obj_t *rv = lv_label_create(row);
        lv_label_set_text(rv, rb);
        lv_obj_set_style_text_font(rv, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rv, rssi_col(d.rssi), 0);
        lv_obj_set_width(rv, 38);
        lv_obj_set_style_text_align(rv, LV_TEXT_ALIGN_RIGHT, 0);
    }

    if (s_footer_lbl) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d device%s — tap to track", count, count == 1 ? "" : "s");
        lv_label_set_text(s_footer_lbl, buf);
    }
}

bool ui_foxhunter_is_tracking() { return s_tracking; }
