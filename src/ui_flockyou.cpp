// Flock-You screen: passive 802.11 probe + beacon capture.
// Same round-screen layout as ui_detector — 50px side padding on list,
// 100px side padding on header/footer so all text clears the circular bezel.
#include "ui_flockyou.h"
#include "flock_log.h"
#include "audio_alert.h"
#include "config.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <Arduino.h>

static lv_obj_t *s_screen    = nullptr;
static lv_obj_t *s_list      = nullptr;
static lv_obj_t *s_count_lbl = nullptr;
static lv_obj_t *s_ch_lbl    = nullptr;
static FlockBackCb s_on_back = nullptr;
static int       s_count     = 0;

// ── Flock alert overlay ───────────────────────────────────────────────────────
// Sits above the list; hidden until a FLOCK-vendor frame is received.
// State machine: RED (live) → AMBER (out of range, 10s countdown) → hidden.
// x=60, w=346 is safe at all y positions on the 466px round screen.
static lv_obj_t  *s_alert_panel  = nullptr;
static lv_obj_t  *s_alert_big    = nullptr;
static lv_obj_t  *s_alert_sub    = nullptr;
static lv_obj_t  *s_alert_mac    = nullptr;
static lv_obj_t  *s_alert_status = nullptr;
static uint32_t   s_last_flock_ms = 0;
static uint32_t   s_last_sec_shown = 0;
static bool       s_alert_active  = false;

#define ALERT_LIVE_MS   5000u   // stay RED for 5s after last packet
#define ALERT_AMBER_MS 10000u   // then AMBER for 10s, counting down

#define MAX_ROWS       20
#define LIST_PAD_H     50
#define HDR_PAD_H     100
#define FTR_PAD_H      60

#define Y_HDR_TOP  36
#define Y_HDR_H    36
#define Y_SEP      (Y_HDR_TOP + Y_HDR_H)
#define Y_LIST     (Y_SEP + 1)
#define Y_FTR      (SCREEN_H - 90)
#define LIST_H     (Y_FTR - Y_LIST)

static lv_color_t rssi_color(int8_t rssi) {
    if (rssi >= -65) return lv_color_hex(0x44dd88);
    if (rssi >= -80) return lv_color_hex(0xffaa00);
    return lv_color_hex(0xff4545);
}

void ui_flockyou_create(FlockBackCb on_back) {
    s_on_back = on_back;
    s_count   = 0;

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_NONE);

    // ---- Header bar (centred, heavily padded for round screen) ---------------
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
    lv_label_set_text(title, "FLOCK-YOU");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffd700), 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Channel indicator (right side of header)
    lv_obj_t *ch_wrap = lv_obj_create(hdr);
    lv_obj_remove_style_all(ch_wrap);
    lv_obj_set_height(ch_wrap, Y_HDR_H);
    lv_obj_set_width(ch_wrap, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ch_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(ch_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ch_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ch_wrap, 3, 0);
    lv_obj_set_scroll_dir(ch_wrap, LV_DIR_NONE);

    lv_obj_t *dot = lv_obj_create(ch_wrap);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xffd700), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);

    s_ch_lbl = lv_label_create(ch_wrap);
    lv_label_set_text(s_ch_lbl, "CH 1");
    lv_obj_set_style_text_font(s_ch_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ch_lbl, lv_color_hex(0xffd700), 0);

    // Separator line
    lv_obj_t *sep = lv_obj_create(s_screen);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, LIST_PAD_H, Y_SEP);
    lv_obj_set_size(sep, SCREEN_W - 2 * LIST_PAD_H, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    // ---- Detection list ------------------------------------------------------
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
    lv_label_set_text(empty, "Scanning WiFi\nprobe requests & APs...");
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_top(empty, 50, 0);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);

    // ---- Footer --------------------------------------------------------------
    lv_obj_t *footer = lv_obj_create(s_screen);
    lv_obj_remove_style_all(footer);
    lv_obj_set_pos(footer, 0, Y_FTR);
    lv_obj_set_size(footer, SCREEN_W, 32);
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
    lv_label_set_text(s_count_lbl, "0 networks");
    lv_obj_set_style_text_font(s_count_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_count_lbl, lv_color_hex(0x444444), 0);

    // ── Flock detection alert overlay (hidden until first FLOCK packet) ─────────
    // Covers the list area; safe-zone checked: x=60..406 at all y on 466px circle.
    s_alert_panel = lv_obj_create(s_screen);
    lv_obj_set_pos(s_alert_panel, 60, 82);
    lv_obj_set_size(s_alert_panel, 346, 155);
    lv_obj_set_style_bg_color(s_alert_panel, lv_color_hex(0x170404), 0);
    lv_obj_set_style_bg_opa(s_alert_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_alert_panel, lv_color_hex(0xff3333), 0);
    lv_obj_set_style_border_width(s_alert_panel, 2, 0);
    lv_obj_set_style_radius(s_alert_panel, 12, 0);
    lv_obj_set_flex_flow(s_alert_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_alert_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_alert_panel, 10, 0);
    lv_obj_set_style_pad_row(s_alert_panel, 5, 0);
    lv_obj_clear_flag(s_alert_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_alert_panel, LV_DIR_NONE);
    lv_obj_add_flag(s_alert_panel, LV_OBJ_FLAG_HIDDEN);

    s_alert_big = lv_label_create(s_alert_panel);
    lv_label_set_text(s_alert_big, "FLOCK");
    lv_obj_set_style_text_font(s_alert_big, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_alert_big, lv_color_hex(0xff3333), 0);
    lv_obj_set_style_text_letter_space(s_alert_big, 4, 0);
    lv_obj_set_width(s_alert_big, LV_PCT(100));
    lv_obj_set_style_text_align(s_alert_big, LV_TEXT_ALIGN_CENTER, 0);

    s_alert_sub = lv_label_create(s_alert_panel);
    lv_label_set_text(s_alert_sub, "CAMERA DETECTED");
    lv_obj_set_style_text_font(s_alert_sub, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_alert_sub, lv_color_hex(0xcc2222), 0);
    lv_obj_set_style_text_letter_space(s_alert_sub, 2, 0);
    lv_obj_set_width(s_alert_sub, LV_PCT(100));
    lv_obj_set_style_text_align(s_alert_sub, LV_TEXT_ALIGN_CENTER, 0);

    s_alert_mac = lv_label_create(s_alert_panel);
    lv_label_set_text(s_alert_mac, "--:--:--:--:--:--");
    lv_obj_set_style_text_font(s_alert_mac, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_alert_mac, lv_color_hex(0x887766), 0);
    lv_obj_set_style_text_letter_space(s_alert_mac, 1, 0);
    lv_obj_set_width(s_alert_mac, LV_PCT(100));
    lv_obj_set_style_text_align(s_alert_mac, LV_TEXT_ALIGN_CENTER, 0);

    s_alert_status = lv_label_create(s_alert_panel);
    lv_label_set_text(s_alert_status, "LIVE SIGNAL");
    lv_obj_set_style_text_font(s_alert_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_alert_status, lv_color_hex(0xff4545), 0);
    lv_obj_set_style_text_letter_space(s_alert_status, 1, 0);
    lv_obj_set_width(s_alert_status, LV_PCT(100));
    lv_obj_set_style_text_align(s_alert_status, LV_TEXT_ALIGN_CENTER, 0);

    lv_scr_load(s_screen);
}

void ui_flockyou_destroy() {
    if (s_screen) {
        lv_obj_del_async(s_screen);
        s_screen = nullptr;
    }
    s_list = s_count_lbl = s_ch_lbl = nullptr;
    s_alert_panel = s_alert_big = s_alert_sub = s_alert_mac = s_alert_status = nullptr;
    s_alert_active = false;
    s_count = 0;
}

void ui_flockyou_set_channel(uint8_t ch) {
    if (!s_ch_lbl) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "CH %d", ch);
    lv_label_set_text(s_ch_lbl, buf);
}

void ui_flockyou_add(const FlockDetection &d) {
    if (!s_list) return;

    // Trigger alert for any Flock-vendor frame (type badge == "FLOCK")
    if (strncmp(d.type, "FLOCK", 5) == 0 && s_alert_panel) {
        flock_log_update(d);                              // persist to JSON
        audio_alert_request(AlertType::FlockCamera);     // belt+suspenders (scanner also calls this)
        s_last_flock_ms  = millis();
        s_last_sec_shown = 0;
        s_alert_active   = true;
        // Red state
        lv_obj_set_style_bg_color(s_alert_panel, lv_color_hex(0x170404), 0);
        lv_obj_set_style_border_color(s_alert_panel, lv_color_hex(0xff3333), 0);
        lv_obj_set_style_text_color(s_alert_big,    lv_color_hex(0xff3333), 0);
        lv_obj_set_style_text_color(s_alert_sub,    lv_color_hex(0xcc2222), 0);
        lv_obj_set_style_text_color(s_alert_status, lv_color_hex(0xff4545), 0);
        lv_label_set_text(s_alert_mac,    d.mac);
        lv_label_set_text(s_alert_status, "LIVE SIGNAL");
        lv_obj_clear_flag(s_alert_panel, LV_OBJ_FLAG_HIDDEN);
    }

    // Remove empty-state label on first real result
    if (s_count == 0) {
        lv_obj_t *first = lv_obj_get_child(s_list, 0);
        if (first && lv_obj_get_child_cnt(s_list) == 1)
            lv_obj_del(first);
    }

    // Cap to MAX_ROWS — delete the oldest (last child) when full
    uint32_t current_rows = lv_obj_get_child_cnt(s_list);
    if (current_rows >= MAX_ROWS) {
        lv_obj_t *oldest = lv_obj_get_child(s_list, -1);
        if (oldest) lv_obj_del(oldest);
    }

    s_count++;

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
    lv_obj_move_to_index(row, 0);  // newest on top

    // Type badge (PROBE / AP)
    lv_obj_t *badge = lv_obj_create(row);
    lv_obj_set_size(badge, 56, 22);
    lv_obj_set_style_bg_color(badge, lv_color_hex(d.badge_color), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(d.badge_color), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_opa(badge, LV_OPA_60, 0);
    lv_obj_set_style_radius(badge, 4, 0);
    lv_obj_set_scroll_dir(badge, LV_DIR_NONE);
    lv_obj_t *badge_lbl = lv_label_create(badge);
    lv_label_set_text(badge_lbl, d.type);
    lv_obj_set_style_text_font(badge_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(badge_lbl, lv_color_hex(d.badge_color), 0);
    lv_obj_center(badge_lbl);

    // SSID + MAC (stacked)
    lv_obj_t *info = lv_obj_create(row);
    lv_obj_remove_style_all(info);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(info, LV_DIR_NONE);

    // SSID (primary)
    lv_obj_t *ssid_lbl = lv_label_create(info);
    lv_label_set_text(ssid_lbl, d.ssid[0] ? d.ssid : d.mac);
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xcccccc), 0);
    lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ssid_lbl, LV_PCT(100));

    // MAC (secondary, dimmer)
    if (d.ssid[0]) {
        lv_obj_t *mac_lbl = lv_label_create(info);
        lv_label_set_text(mac_lbl, d.mac);
        lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(mac_lbl, lv_color_hex(0x444444), 0);
    }

    // RSSI (right)
    char rssi_buf[8];
    snprintf(rssi_buf, sizeof(rssi_buf), "%ddBm", (int)d.rssi);
    lv_obj_t *rssi_lbl = lv_label_create(row);
    lv_label_set_text(rssi_lbl, rssi_buf);
    lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rssi_lbl, rssi_color(d.rssi), 0);

    // Update footer count
    char buf[24];
    snprintf(buf, sizeof(buf), "%d network%s", s_count, s_count == 1 ? "" : "s");
    lv_label_set_text(s_count_lbl, buf);
}

void ui_flockyou_tick() {
    if (!s_alert_active || !s_alert_panel) return;

    const uint32_t elapsed = millis() - s_last_flock_ms;

    if (elapsed < ALERT_LIVE_MS) return;  // still RED — nothing to update

    if (elapsed < ALERT_LIVE_MS + ALERT_AMBER_MS) {
        // AMBER — out of range, countdown
        uint32_t secs_left = (ALERT_LIVE_MS + ALERT_AMBER_MS - elapsed) / 1000 + 1;
        if (secs_left != s_last_sec_shown) {
            s_last_sec_shown = secs_left;
            lv_obj_set_style_bg_color(s_alert_panel,   lv_color_hex(0x160f02), 0);
            lv_obj_set_style_border_color(s_alert_panel, lv_color_hex(0xff9500), 0);
            lv_obj_set_style_text_color(s_alert_big,    lv_color_hex(0xff9500), 0);
            lv_obj_set_style_text_color(s_alert_sub,    lv_color_hex(0xcc7700), 0);
            lv_obj_set_style_text_color(s_alert_status, lv_color_hex(0xff9500), 0);
            char sbuf[32];
            snprintf(sbuf, sizeof(sbuf), "OUT OF RANGE  %lus", (unsigned long)secs_left);
            lv_label_set_text(s_alert_status, sbuf);
        }
        return;
    }

    // Timeout — dismiss
    s_alert_active = false;
    lv_obj_add_flag(s_alert_panel, LV_OBJ_FLAG_HIDDEN);
}
