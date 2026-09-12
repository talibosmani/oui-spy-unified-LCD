// PCAP mode UI — explicit start/stop capture + download sub-mode (AP file browser).
// Round screen safe-zone layout (466×466 AMOLED).
//
// CAPTURE view:
//   Header: "PCAP" blue + REC dot (pulses only while capturing)
//   Large frame counter + MGMT/CTRL/DATA bars + CH/filename/size
//   [START CAPTURE] / [STOP CAPTURE] toggle (green/red)
//   [DOWNLOAD] button → stops capture if needed, then AP file browser
//
// DOWNLOAD card (overlay, shown while AP is up):
//   SSID / password / IP + [STOP DOWNLOAD]
//
// Storage warning overlay fires when LittleFS > 80% full.
#include "ui_pcap.h"
#include "pcap_capture.h"
#include "pcap_download.h"
#include "config.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <Arduino.h>

#define LIST_PAD_H  50
#define HDR_PAD_H  100
#define FTR_PAD_H   60
#define Y_HDR_TOP   36
#define Y_HDR_H     36
#define Y_SEP       (Y_HDR_TOP + Y_HDR_H)
#define Y_CONTENT   (Y_SEP + 8)

static lv_obj_t  *s_screen      = nullptr;
static lv_obj_t  *s_total_lbl   = nullptr;
static lv_obj_t  *s_mgmt_bar    = nullptr;
static lv_obj_t  *s_ctrl_bar    = nullptr;
static lv_obj_t  *s_data_bar    = nullptr;
static lv_obj_t  *s_mgmt_pct    = nullptr;
static lv_obj_t  *s_ctrl_pct    = nullptr;
static lv_obj_t  *s_data_pct    = nullptr;
static lv_obj_t  *s_ch_lbl      = nullptr;
static lv_obj_t  *s_file_lbl    = nullptr;
static lv_obj_t  *s_size_lbl    = nullptr;
static lv_obj_t  *s_start_btn   = nullptr;
static lv_obj_t  *s_start_lbl   = nullptr;
static lv_obj_t  *s_stop_btn    = nullptr;
static lv_obj_t  *s_stop_lbl    = nullptr;
static lv_obj_t  *s_dl_btn      = nullptr;
static lv_obj_t  *s_dl_lbl      = nullptr;
static lv_obj_t  *s_dot         = nullptr;
static lv_anim_t  s_dot_anim;
static bool       s_capturing   = false;     // true while pcap sniffer is running

// Download info card (shown in DL mode, hidden in capture mode)
static lv_obj_t  *s_dl_card     = nullptr;

// Storage warning overlay
static lv_obj_t  *s_warn_card   = nullptr;

static PcapBackCb s_on_back     = nullptr;
static bool       s_in_dl       = false;  // true = download mode active

// ── Helpers ───────────────────────────────────────────────────────────────────
static void dot_exec(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}
static void start_dot_anim() {
    lv_anim_init(&s_dot_anim);
    lv_anim_set_var(&s_dot_anim, s_dot);
    lv_anim_set_exec_cb(&s_dot_anim, dot_exec);
    lv_anim_set_values(&s_dot_anim, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_time(&s_dot_anim, 600);
    lv_anim_set_playback_time(&s_dot_anim, 600);
    lv_anim_set_repeat_count(&s_dot_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_dot_anim);
}

static lv_obj_t *make_bar_row(lv_obj_t *parent, const char *label, uint32_t color,
                               lv_obj_t **bar_out, lv_obj_t **pct_out) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 28);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_width(lbl, 42);

    // Track background
    lv_obj_t *track = lv_obj_create(row);
    lv_obj_remove_style_all(track);
    lv_obj_set_flex_grow(track, 1);
    lv_obj_set_height(track, 8);
    lv_obj_set_style_bg_color(track, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_scroll_dir(track, LV_DIR_NONE);

    // Fill bar inside track
    lv_obj_t *bar = lv_obj_create(track);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 0, LV_PCT(100));
    lv_obj_set_align(bar, LV_ALIGN_LEFT_MID);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_scroll_dir(bar, LV_DIR_NONE);
    *bar_out = bar;

    lv_obj_t *pct = lv_label_create(row);
    lv_label_set_text(pct, "0%");
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pct, lv_color_hex(color), 0);
    lv_obj_set_width(pct, 36);
    lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
    *pct_out = pct;

    return row;
}

// Update one bar fill. track_w = pixel width of the track object.
static void update_bar(lv_obj_t *bar, lv_obj_t *pct_lbl, uint32_t count, uint32_t total) {
    if (!bar || !pct_lbl) return;
    int p = (total > 0) ? (int)(count * 100 / total) : 0;
    lv_obj_t *track = lv_obj_get_parent(bar);
    if (!track) return;
    int track_w = lv_obj_get_width(track);
    lv_obj_set_width(bar, track_w * p / 100);
    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", p);
    lv_label_set_text(pct_lbl, buf);
}

static void set_capturing(bool on) {
    s_capturing = on;
    if (on) {
        // Capturing: START dimmed, STOP active red, dot pulses
        if (s_start_btn) {
            lv_obj_set_style_bg_color(s_start_btn, lv_color_hex(0x050d08), 0);
            lv_obj_set_style_border_color(s_start_btn, lv_color_hex(0x112211), 0);
        }
        if (s_start_lbl) lv_obj_set_style_text_color(s_start_lbl, lv_color_hex(0x1a3322), 0);
        if (s_stop_btn) {
            lv_obj_set_style_bg_color(s_stop_btn, lv_color_hex(0x200a0a), 0);
            lv_obj_set_style_border_color(s_stop_btn, lv_color_hex(0xff4545), 0);
        }
        if (s_stop_lbl) lv_obj_set_style_text_color(s_stop_lbl, lv_color_hex(0xff4545), 0);
        if (s_dot) start_dot_anim();
    } else {
        // Stopped: START active green, STOP dimmed, dot off
        if (s_start_btn) {
            lv_obj_set_style_bg_color(s_start_btn, lv_color_hex(0x071a0d), 0);
            lv_obj_set_style_border_color(s_start_btn, lv_color_hex(0x44dd88), 0);
        }
        if (s_start_lbl) lv_obj_set_style_text_color(s_start_lbl, lv_color_hex(0x44dd88), 0);
        if (s_stop_btn) {
            lv_obj_set_style_bg_color(s_stop_btn, lv_color_hex(0x0a0505), 0);
            lv_obj_set_style_border_color(s_stop_btn, lv_color_hex(0x331515), 0);
        }
        if (s_stop_lbl) lv_obj_set_style_text_color(s_stop_lbl, lv_color_hex(0x331515), 0);
        if (s_dot) { lv_anim_del(s_dot, dot_exec); lv_obj_set_style_opa(s_dot, LV_OPA_30, 0); }
    }
}

static void on_start_btn(lv_event_t *e) {
    (void)e;
    if (s_capturing) return;
    pcap_start();
    set_capturing(true);
}

static void on_stop_btn(lv_event_t *e) {
    (void)e;
    if (!s_capturing) return;
    pcap_stop();
    set_capturing(false);
}

static void on_dl_btn(lv_event_t *e) {
    (void)e;
    if (s_in_dl) return;

    // Gracefully stop capture first if running
    if (s_capturing) {
        pcap_stop();
        set_capturing(false);
    }

    s_in_dl = true;
    pcap_download_start();

    // Show dl card, hide capture controls
    if (s_start_btn) lv_obj_add_flag(s_start_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_stop_btn)  lv_obj_add_flag(s_stop_btn,  LV_OBJ_FLAG_HIDDEN);
    if (s_dl_btn)    lv_obj_add_flag(s_dl_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_dl_card)   lv_obj_clear_flag(s_dl_card, LV_OBJ_FLAG_HIDDEN);
}

static void on_stop_dl(lv_event_t *e) {
    (void)e;
    if (!s_in_dl) return;
    pcap_download_stop();
    s_in_dl = false;

    // Restore capture controls (stopped state — user decides when to re-capture)
    if (s_start_btn) lv_obj_clear_flag(s_start_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_stop_btn)  lv_obj_clear_flag(s_stop_btn,  LV_OBJ_FLAG_HIDDEN);
    if (s_dl_btn)    lv_obj_clear_flag(s_dl_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_dl_card)   lv_obj_add_flag(s_dl_card, LV_OBJ_FLAG_HIDDEN);
    set_capturing(false); // dot and button already in stopped state
}

static void on_warn_del(lv_event_t *e) {
    (void)e;
    pcap_delete_all();
    if (s_warn_card) lv_obj_add_flag(s_warn_card, LV_OBJ_FLAG_HIDDEN);
}
static void on_warn_dismiss(lv_event_t *e) {
    (void)e;
    if (s_warn_card) lv_obj_add_flag(s_warn_card, LV_OBJ_FLAG_HIDDEN);
}

// ── Screen builder ────────────────────────────────────────────────────────────
void ui_pcap_create(PcapBackCb on_back) {
    s_on_back = on_back;
    s_in_dl   = false;

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_NONE);

    // ── Header ────────────────────────────────────────────────────────────────
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
    lv_label_set_text(title, "PCAP");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x4499ff), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // REC dot
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
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0xff4545), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    start_dot_anim();

    lv_obj_t *rl = lv_label_create(dw);
    lv_label_set_text(rl, "REC");
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rl, lv_color_hex(0xff4545), 0);

    // Separator
    lv_obj_t *sep = lv_obj_create(s_screen);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, LIST_PAD_H, Y_SEP);
    lv_obj_set_size(sep, SCREEN_W - 2 * LIST_PAD_H, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x0d1a2e), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    // ── Content column (capture mode) ─────────────────────────────────────────
    lv_obj_t *col = lv_obj_create(s_screen);
    lv_obj_remove_style_all(col);
    lv_obj_set_pos(col, LIST_PAD_H, Y_CONTENT);
    lv_obj_set_size(col, SCREEN_W - 2 * LIST_PAD_H, SCREEN_H - Y_CONTENT - 10);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(col, LV_DIR_NONE);

    // Large frame count
    s_total_lbl = lv_label_create(col);
    lv_label_set_text(s_total_lbl, "0");
    lv_obj_set_style_text_font(s_total_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_total_lbl, lv_color_hex(0x4499ff), 0);
    lv_obj_set_style_text_align(s_total_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_total_lbl, LV_PCT(100));

    lv_obj_t *frames_lbl = lv_label_create(col);
    lv_label_set_text(frames_lbl, "FRAMES");
    lv_obj_set_style_text_font(frames_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(frames_lbl, lv_color_hex(0x1a3355), 0);
    lv_obj_set_style_text_letter_space(frames_lbl, 2, 0);
    lv_obj_set_style_text_align(frames_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(frames_lbl, LV_PCT(100));

    // Spacer
    lv_obj_t *sp1 = lv_obj_create(col);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_width(sp1, LV_PCT(100));
    lv_obj_set_height(sp1, 0);
    lv_obj_set_style_bg_opa(sp1, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(sp1, LV_DIR_NONE);

    // Bar rows
    make_bar_row(col, "MGMT", 0x4499ff, &s_mgmt_bar, &s_mgmt_pct);
    make_bar_row(col, "CTRL", 0xffaa00, &s_ctrl_bar, &s_ctrl_pct);
    make_bar_row(col, "DATA", 0x44dd88, &s_data_bar, &s_data_pct);

    // Spacer
    lv_obj_t *sp2 = lv_obj_create(col);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_width(sp2, LV_PCT(100));
    lv_obj_set_height(sp2, 0);
    lv_obj_set_style_bg_opa(sp2, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(sp2, LV_DIR_NONE);

    // Channel + file info
    s_ch_lbl = lv_label_create(col);
    lv_label_set_text(s_ch_lbl, "CH --");
    lv_obj_set_style_text_font(s_ch_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ch_lbl, lv_color_hex(0x333355), 0);
    lv_obj_set_style_text_align(s_ch_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_ch_lbl, LV_PCT(100));

    s_file_lbl = lv_label_create(col);
    lv_label_set_text(s_file_lbl, "---");
    lv_obj_set_style_text_font(s_file_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_file_lbl, lv_color_hex(0x1a2a3a), 0);
    lv_obj_set_style_text_align(s_file_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_file_lbl, LV_PCT(100));
    lv_label_set_long_mode(s_file_lbl, LV_LABEL_LONG_DOT);

    s_size_lbl = lv_label_create(col);
    lv_label_set_text(s_size_lbl, "0 B");
    lv_obj_set_style_text_font(s_size_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_size_lbl, lv_color_hex(0x1a2a3a), 0);
    lv_obj_set_style_text_align(s_size_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_size_lbl, LV_PCT(100));

    // START / STOP capture — two side-by-side buttons
    // Column width = SCREEN_W - 2*LIST_PAD_H = 366px; each button = (366-8)/2 = 179px
    lv_obj_t *btn_row = lv_obj_create(col);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), 54);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(btn_row, LV_DIR_NONE);

    // START button (left, green)
    s_start_btn = lv_obj_create(btn_row);
    lv_obj_remove_style_all(s_start_btn);
    lv_obj_set_size(s_start_btn, 179, LV_PCT(100));
    lv_obj_set_style_bg_color(s_start_btn, lv_color_hex(0x071a0d), 0);
    lv_obj_set_style_bg_opa(s_start_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_start_btn, lv_color_hex(0x44dd88), 0);
    lv_obj_set_style_border_width(s_start_btn, 1, 0);
    lv_obj_set_style_radius(s_start_btn, 10, 0);
    lv_obj_add_flag(s_start_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_start_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_start_btn, LV_DIR_NONE);
    lv_obj_add_event_cb(s_start_btn, on_start_btn, LV_EVENT_CLICKED, nullptr);

    s_start_lbl = lv_label_create(s_start_btn);
    lv_label_set_text(s_start_lbl, LV_SYMBOL_PLAY " START");
    lv_obj_set_style_text_font(s_start_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_start_lbl, lv_color_hex(0x44dd88), 0);
    lv_obj_set_style_text_letter_space(s_start_lbl, 1, 0);
    lv_obj_center(s_start_lbl);

    // STOP button (right, dimmed red until capturing)
    s_stop_btn = lv_obj_create(btn_row);
    lv_obj_remove_style_all(s_stop_btn);
    lv_obj_set_size(s_stop_btn, 179, LV_PCT(100));
    lv_obj_set_style_bg_color(s_stop_btn, lv_color_hex(0x0a0505), 0);
    lv_obj_set_style_bg_opa(s_stop_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_stop_btn, lv_color_hex(0x331515), 0);
    lv_obj_set_style_border_width(s_stop_btn, 1, 0);
    lv_obj_set_style_radius(s_stop_btn, 10, 0);
    lv_obj_add_flag(s_stop_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_stop_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_stop_btn, LV_DIR_NONE);
    lv_obj_add_event_cb(s_stop_btn, on_stop_btn, LV_EVENT_CLICKED, nullptr);

    s_stop_lbl = lv_label_create(s_stop_btn);
    lv_label_set_text(s_stop_lbl, LV_SYMBOL_STOP " STOP");
    lv_obj_set_style_text_font(s_stop_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_stop_lbl, lv_color_hex(0x331515), 0);
    lv_obj_set_style_text_letter_space(s_stop_lbl, 1, 0);
    lv_obj_center(s_stop_lbl);

    // DOWNLOAD button at bottom
    s_dl_btn = lv_obj_create(col);
    lv_obj_remove_style_all(s_dl_btn);
    lv_obj_set_size(s_dl_btn, LV_PCT(100), 42);
    lv_obj_set_style_bg_color(s_dl_btn, lv_color_hex(0x0a1a2e), 0);
    lv_obj_set_style_bg_opa(s_dl_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_dl_btn, lv_color_hex(0x4499ff), 0);
    lv_obj_set_style_border_width(s_dl_btn, 1, 0);
    lv_obj_set_style_radius(s_dl_btn, 10, 0);
    lv_obj_add_flag(s_dl_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_dl_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_dl_btn, LV_DIR_NONE);
    lv_obj_add_event_cb(s_dl_btn, on_dl_btn, LV_EVENT_CLICKED, nullptr);

    s_dl_lbl = lv_label_create(s_dl_btn);
    lv_label_set_text(s_dl_lbl, "Download via WIFI");
    lv_obj_set_style_text_font(s_dl_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_dl_lbl, lv_color_hex(0x4499ff), 0);
    lv_obj_set_style_text_letter_space(s_dl_lbl, 1, 0);
    lv_obj_center(s_dl_lbl);

    // ── Download info card (hidden until DL mode) ─────────────────────────────
    // Centered 280×200 card
    s_dl_card = lv_obj_create(s_screen);
    lv_obj_set_pos(s_dl_card, (SCREEN_W - 280) / 2, (SCREEN_H - 210) / 2);
    lv_obj_set_size(s_dl_card, 280, 210);
    lv_obj_set_style_bg_color(s_dl_card, lv_color_hex(0x050d1a), 0);
    lv_obj_set_style_bg_opa(s_dl_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_dl_card, lv_color_hex(0x4499ff), 0);
    lv_obj_set_style_border_width(s_dl_card, 1, 0);
    lv_obj_set_style_radius(s_dl_card, 16, 0);
    lv_obj_set_style_pad_all(s_dl_card, 16, 0);
    lv_obj_set_style_pad_row(s_dl_card, 8, 0);
    lv_obj_set_flex_flow(s_dl_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dl_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_dl_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_dl_card, LV_DIR_NONE);
    lv_obj_add_flag(s_dl_card, LV_OBJ_FLAG_HIDDEN); // hidden initially

    lv_obj_t *dl_title = lv_label_create(s_dl_card);
    lv_label_set_text(dl_title, "DOWNLOAD MODE");
    lv_obj_set_style_text_font(dl_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dl_title, lv_color_hex(0x4499ff), 0);
    lv_obj_set_style_text_letter_space(dl_title, 1, 0);
    lv_obj_set_style_text_align(dl_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(dl_title, LV_PCT(100));

    auto make_info = [](lv_obj_t *parent, const char *k, const char *v) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(row, LV_DIR_NONE);
        lv_obj_t *kl = lv_label_create(row);
        lv_label_set_text(kl, k);
        lv_obj_set_style_text_font(kl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(kl, lv_color_hex(0x334455), 0);
        lv_obj_t *vl = lv_label_create(row);
        lv_label_set_text(vl, v);
        lv_obj_set_style_text_font(vl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(vl, lv_color_hex(0x44dd88), 0);
        lv_label_set_long_mode(vl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(vl, 160);
        lv_obj_set_style_text_align(vl, LV_TEXT_ALIGN_RIGHT, 0);
    };
    make_info(s_dl_card, "SSID",  "Download_WIFI");
    make_info(s_dl_card, "PASS",  "P@ssw0rd@123");
    make_info(s_dl_card, "URL",   "192.168.4.1");

    // Stop download button
    lv_obj_t *stop_btn = lv_obj_create(s_dl_card);
    lv_obj_remove_style_all(stop_btn);
    lv_obj_set_size(stop_btn, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0x200a0a), 0);
    lv_obj_set_style_bg_opa(stop_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(stop_btn, lv_color_hex(0xff4545), 0);
    lv_obj_set_style_border_width(stop_btn, 1, 0);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_flag(stop_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(stop_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(stop_btn, LV_DIR_NONE);
    lv_obj_add_event_cb(stop_btn, on_stop_dl, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *sl = lv_label_create(stop_btn);
    lv_label_set_text(sl, "STOP DOWNLOAD");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(0xff4545), 0);
    lv_obj_center(sl);

    // ── Storage warning overlay ───────────────────────────────────────────────
    s_warn_card = lv_obj_create(s_screen);
    lv_obj_set_pos(s_warn_card, (SCREEN_W - 270) / 2, (SCREEN_H - 180) / 2);
    lv_obj_set_size(s_warn_card, 270, 180);
    lv_obj_set_style_bg_color(s_warn_card, lv_color_hex(0x1a0505), 0);
    lv_obj_set_style_bg_opa(s_warn_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_warn_card, lv_color_hex(0xff4545), 0);
    lv_obj_set_style_border_width(s_warn_card, 1, 0);
    lv_obj_set_style_radius(s_warn_card, 14, 0);
    lv_obj_set_style_pad_all(s_warn_card, 16, 0);
    lv_obj_set_style_pad_row(s_warn_card, 10, 0);
    lv_obj_set_flex_flow(s_warn_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_warn_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_warn_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_warn_card, LV_DIR_NONE);
    lv_obj_add_flag(s_warn_card, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *wt = lv_label_create(s_warn_card);
    lv_label_set_text(wt, "STORAGE FULL");
    lv_obj_set_style_text_font(wt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wt, lv_color_hex(0xff4545), 0);
    lv_obj_set_style_text_align(wt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(wt, LV_PCT(100));

    lv_obj_t *ws = lv_label_create(s_warn_card);
    lv_label_set_text(ws, "Captures use >80%\nof storage.");
    lv_obj_set_style_text_font(ws, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ws, lv_color_hex(0x884444), 0);
    lv_obj_set_style_text_align(ws, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ws, LV_PCT(100));
    lv_label_set_long_mode(ws, LV_LABEL_LONG_WRAP);

    // Delete all button
    lv_obj_t *wb = lv_obj_create(s_warn_card);
    lv_obj_remove_style_all(wb);
    lv_obj_set_size(wb, LV_PCT(100), 36);
    lv_obj_set_style_bg_color(wb, lv_color_hex(0x2a0808), 0);
    lv_obj_set_style_bg_opa(wb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(wb, lv_color_hex(0xff4545), 0);
    lv_obj_set_style_border_width(wb, 1, 0);
    lv_obj_set_style_radius(wb, 6, 0);
    lv_obj_add_flag(wb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(wb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(wb, LV_DIR_NONE);
    lv_obj_add_event_cb(wb, on_warn_del, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *wbl = lv_label_create(wb);
    lv_label_set_text(wbl, "DELETE ALL");
    lv_obj_set_style_text_font(wbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wbl, lv_color_hex(0xff4545), 0);
    lv_obj_center(wbl);

    // Dismiss button
    lv_obj_t *wd = lv_obj_create(s_warn_card);
    lv_obj_remove_style_all(wd);
    lv_obj_set_size(wd, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(wd, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(wd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(wd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(wd, LV_DIR_NONE);
    lv_obj_add_event_cb(wd, on_warn_dismiss, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *wdl = lv_label_create(wd);
    lv_label_set_text(wdl, "Dismiss");
    lv_obj_set_style_text_font(wdl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wdl, lv_color_hex(0x333344), 0);
    lv_obj_center(wdl);

    lv_scr_load(s_screen);
}

void ui_pcap_destroy() {
    if (s_dot) lv_anim_del(s_dot, dot_exec);
    if (s_screen) { lv_obj_del_async(s_screen); s_screen = nullptr; }
    s_total_lbl = s_mgmt_bar = s_ctrl_bar = s_data_bar = nullptr;
    s_mgmt_pct = s_ctrl_pct = s_data_pct = nullptr;
    s_ch_lbl = s_file_lbl = s_size_lbl = nullptr;
    s_start_btn = s_start_lbl = s_stop_btn = s_stop_lbl = nullptr;
    s_dl_btn = s_dl_lbl = s_dot = nullptr;
    s_dl_card = s_warn_card = nullptr;
    s_in_dl = false;
    s_capturing = false;
}

void ui_pcap_update() {
    if (!s_screen) return;
    if (s_in_dl) { pcap_download_tick(); return; }

    PcapStats st;
    pcap_get_stats(&st);

    // Total frames
    if (s_total_lbl) {
        char buf[16];
        if (st.total < 10000)
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)st.total);
        else
            snprintf(buf, sizeof(buf), "%luK", (unsigned long)(st.total / 1000));
        lv_label_set_text(s_total_lbl, buf);
    }

    // Bars
    update_bar(s_mgmt_bar, s_mgmt_pct, st.mgmt, st.total);
    update_bar(s_ctrl_bar, s_ctrl_pct, st.ctrl, st.total);
    update_bar(s_data_bar, s_data_pct, st.data, st.total);

    // Channel
    if (s_ch_lbl) {
        char buf[10]; snprintf(buf, sizeof(buf), "CH %d", (int)st.channel);
        lv_label_set_text(s_ch_lbl, buf);
    }

    // File + size
    if (s_file_lbl) lv_label_set_text(s_file_lbl, st.filename);
    if (s_size_lbl) {
        char buf[20];
        if (st.file_bytes < 1024)
            snprintf(buf, sizeof(buf), "%lu B", (unsigned long)st.file_bytes);
        else if (st.file_bytes < 1024*1024)
            snprintf(buf, sizeof(buf), "%.1f KB", st.file_bytes / 1024.0f);
        else
            snprintf(buf, sizeof(buf), "%.2f MB", st.file_bytes / (1024.0f*1024.0f));
        lv_label_set_text(s_size_lbl, buf);
    }
}

void ui_pcap_show_storage_warning(bool show) {
    if (!s_warn_card) return;
    if (show) lv_obj_clear_flag(s_warn_card, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_warn_card, LV_OBJ_FLAG_HIDDEN);
}
