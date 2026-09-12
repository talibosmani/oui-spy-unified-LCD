// Self-test overlay — swipe-left from menu screen to open.
// Three buttons: TOUCH TEST, ✕ EXIT, AUDIO TEST.
// Slides in from the right edge, floats on lv_layer_top().
#include "ui_selftest.h"
#include "config.h"
#include "audio_alert.h"
#include <lvgl.h>

// Panel geometry — 280×260, centered in 466×466.
// Circle safety at y=103 and y=363: half_w≈197px → panel [93,373] is well inside.
#define PANEL_W   280
#define PANEL_H   260
#define PANEL_X   ((SCREEN_W - PANEL_W) / 2)   // 93
#define PANEL_Y   ((SCREEN_H - PANEL_H) / 2)   // 103
#define PANEL_HIDE_X (SCREEN_W + 20)            // 486 — off-screen right

static lv_obj_t   *s_panel      = nullptr;
static lv_obj_t   *s_touch_lbl  = nullptr;
static lv_obj_t   *s_audio_lbl  = nullptr;
static lv_obj_t   *s_anim_block = nullptr;
static lv_timer_t *s_reset_tmr  = nullptr;
static bool        s_open       = false;

// ── Animation helpers ─────────────────────────────────────────────────────────

static void anim_x_exec(void *obj, int32_t v) {
    lv_obj_set_x((lv_obj_t *)obj, v);
}

static void on_anim_done(lv_anim_t *) {
    if (s_anim_block) lv_obj_add_flag(s_anim_block, LV_OBJ_FLAG_HIDDEN);
}

// ── Touch-test reset timer ────────────────────────────────────────────────────

static void on_touch_reset(lv_timer_t *tmr) {
    lv_timer_del(tmr);
    s_reset_tmr = nullptr;
    if (s_touch_lbl) lv_label_set_text(s_touch_lbl, "TOUCH TEST");
}

// ── Button callbacks ──────────────────────────────────────────────────────────

static void on_touch_clicked(lv_event_t *) {
    if (s_touch_lbl) lv_label_set_text(s_touch_lbl, "TOUCH  OK  *");
    if (s_reset_tmr) lv_timer_del(s_reset_tmr);
    s_reset_tmr = lv_timer_create(on_touch_reset, 1500, nullptr);
    lv_timer_set_repeat_count(s_reset_tmr, 1);
}

static void on_exit_clicked(lv_event_t *) {
    selftest_close();
}

static lv_timer_t *s_audio_reset_tmr = nullptr;

static void on_audio_reset(lv_timer_t *tmr) {
    lv_timer_del(tmr);
    s_audio_reset_tmr = nullptr;
    if (!s_audio_lbl) return;
    if (audio_is_ready())
        lv_label_set_text(s_audio_lbl, "AUDIO TEST");
    else
        lv_label_set_text(s_audio_lbl, "NO AUDIO HW");
    lv_obj_set_style_text_color(s_audio_lbl, lv_color_hex(0xdddddd), 0);
}

static void on_audio_clicked(lv_event_t *) {
    if (!audio_is_ready()) {
        if (s_audio_lbl) {
            lv_label_set_text(s_audio_lbl, "NO AUDIO HW");
            lv_obj_set_style_text_color(s_audio_lbl, lv_color_hex(0xffaa00), 0);
        }
        if (s_audio_reset_tmr) lv_timer_del(s_audio_reset_tmr);
        s_audio_reset_tmr = lv_timer_create(on_audio_reset, 2000, nullptr);
        lv_timer_set_repeat_count(s_audio_reset_tmr, 1);
        return;
    }
    audio_play_test_tone();
    if (s_audio_lbl) {
        lv_label_set_text(s_audio_lbl, "AUDIO  OK  *");
        lv_obj_set_style_text_color(s_audio_lbl, lv_color_hex(0x44dd88), 0);
    }
    if (s_audio_reset_tmr) lv_timer_del(s_audio_reset_tmr);
    s_audio_reset_tmr = lv_timer_create(on_audio_reset, 2000, nullptr);
    lv_timer_set_repeat_count(s_audio_reset_tmr, 1);
}

// ── Widget builder ────────────────────────────────────────────────────────────

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text,
                           lv_event_cb_t cb, uint32_t border_col,
                           lv_obj_t **lbl_out = nullptr) {
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_PCT(100), 58);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x111118), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1e1e28), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(border_col), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(btn, LV_DIR_NONE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xdddddd), 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_obj_center(lbl);

    if (lbl_out) *lbl_out = lbl;
    return btn;
}

// ── Public API ────────────────────────────────────────────────────────────────

void selftest_begin() {
    // Full-screen touch blocker — shown only during slide animation
    s_anim_block = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_anim_block);
    lv_obj_set_pos(s_anim_block, 0, 0);
    lv_obj_set_size(s_anim_block, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(s_anim_block, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_anim_block, 0, 0);
    lv_obj_add_flag(s_anim_block, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_anim_block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_anim_block, LV_DIR_NONE);

    // Floating panel — parented to lv_layer_top() so it overlays everything
    s_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(s_panel, PANEL_HIDE_X, PANEL_Y);
    lv_obj_set_size(s_panel, PANEL_W, PANEL_H);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x0d0d14), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0x1e1e30), 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_radius(s_panel, 22, 0);
    lv_obj_set_style_pad_left(s_panel, 14, 0);
    lv_obj_set_style_pad_right(s_panel, 14, 0);
    lv_obj_set_style_pad_top(s_panel, 14, 0);
    lv_obj_set_style_pad_bottom(s_panel, 14, 0);
    lv_obj_set_style_pad_row(s_panel, 10, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_panel, LV_DIR_NONE);

    // Title
    lv_obj_t *title = lv_label_create(s_panel);
    lv_label_set_text(title, "SELF TEST");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x44dd88), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // TOUCH TEST — green border, label stored for feedback update
    make_btn(s_panel, "TOUCH TEST", on_touch_clicked, 0x44dd88, &s_touch_lbl);

    // EXIT — red border
    make_btn(s_panel, "X  EXIT", on_exit_clicked, 0xff4545);

    // AUDIO TEST — blue border; label stored to reflect HW status on tap
    const char *audio_lbl_text = audio_is_ready() ? "AUDIO TEST" : "NO AUDIO HW";
    make_btn(s_panel, audio_lbl_text, on_audio_clicked, 0x4499ff, &s_audio_lbl);

    s_open = false;
}

void selftest_open() {
    if (!s_panel || s_open) return;
    s_open = true;
    if (s_anim_block) lv_obj_clear_flag(s_anim_block, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, anim_x_exec);
    lv_anim_set_values(&a, PANEL_HIDE_X, PANEL_X);
    lv_anim_set_time(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, on_anim_done);
    lv_anim_start(&a);
}

void selftest_close() {
    if (!s_panel || !s_open) return;
    s_open = false;
    if (s_anim_block) lv_obj_clear_flag(s_anim_block, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, anim_x_exec);
    lv_anim_set_values(&a, PANEL_X, PANEL_HIDE_X);
    lv_anim_set_time(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, on_anim_done);
    lv_anim_start(&a);
}

bool selftest_is_open() { return s_open; }
