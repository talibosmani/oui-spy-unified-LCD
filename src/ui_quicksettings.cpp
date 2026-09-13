// Quick-settings card — slides from above screen to center on swipe-down gesture.
// Centered 295px floating card: safe on 466×466 round AMOLED at all positions.
// Controls: VOL slider + MUTE, BRT slider, SOUND [VOICE/TONE], DEBUG toggle.
#include "ui_quicksettings.h"
#include "config.h"
#include "display.h"
#include "audio_alert.h"
#include "debug.h"
#include <lvgl.h>
#include <Arduino.h>

// Card geometry — centered in 466×466 circle.
// At center (y≈78..408) the safe half-width is 233px; card extends 147.5px → plenty of margin.
#define CARD_W   295
#define CARD_H   346
#define CARD_X   ((SCREEN_W - CARD_W) / 2)      // 85
#define CARD_Y   ((SCREEN_H - CARD_H) / 2)      // 83
#define CARD_Y_HIDE  (-(CARD_Y + CARD_H + 10))  // -393

#define VOL_DEFAULT  75
#define BRT_DEFAULT  78

static lv_obj_t *s_panel        = nullptr;
static lv_obj_t *s_vol_sld      = nullptr;
static lv_obj_t *s_vol_lbl      = nullptr;
static lv_obj_t *s_brt_sld      = nullptr;
static lv_obj_t *s_brt_lbl      = nullptr;
static lv_obj_t *s_mute_btn     = nullptr;
static lv_obj_t *s_mute_lbl     = nullptr;
static lv_obj_t *s_voice_btn    = nullptr;
static lv_obj_t *s_tone_btn     = nullptr;
static bool      s_open          = false;
static lv_timer_t *s_idle_tmr    = nullptr;
static uint32_t  s_last_activity = 0;
static lv_obj_t *s_anim_block   = nullptr; // full-screen touch blocker during slide

static void idle_check_cb(lv_timer_t *) {
    if (s_open && millis() - s_last_activity > 45000) qs_close();
}

static void on_anim_done(lv_anim_t *) {
    if (s_anim_block) lv_obj_add_flag(s_anim_block, LV_OBJ_FLAG_HIDDEN);
}

static void anim_y_exec(void *obj, int32_t v) {
    lv_obj_set_y((lv_obj_t *)obj, v);
}

// ── Pill active/inactive style ────────────────────────────────────────────────

static void pill_set_active(lv_obj_t *pill, bool active,
                             uint32_t active_text   = 0xFFD700,
                             uint32_t active_bg     = 0x1A1A30,
                             uint32_t active_border = 0xFFD700) {
    lv_obj_t *lbl = lv_obj_get_child(pill, 0);
    if (active) {
        lv_obj_set_style_bg_color(pill, lv_color_hex(active_bg), 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(pill, lv_color_hex(active_border), 0);
        lv_obj_set_style_border_opa(pill, LV_OPA_50, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(active_text), 0);
    } else {
        lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(pill, lv_color_hex(0x252535), 0);
        lv_obj_set_style_border_opa(pill, LV_OPA_COVER, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0x404050), 0);
    }
}

// ── Slider callbacks ──────────────────────────────────────────────────────────

static void on_mute_clicked(lv_event_t *e) {
    (void)e;
    s_last_activity = millis();
    bool muted = audio_mute_toggle();
    if (muted) {
        if (s_vol_sld) lv_slider_set_value(s_vol_sld, 0, LV_ANIM_ON);
        if (s_vol_lbl) lv_label_set_text(s_vol_lbl, "0%");
        if (s_mute_lbl) lv_label_set_text(s_mute_lbl, "UNMUTE");
        if (s_mute_btn) {
            lv_obj_set_style_bg_color(s_mute_btn, lv_color_hex(0xff4545), 0);
            lv_obj_set_style_bg_opa(s_mute_btn, LV_OPA_COVER, 0);
        }
    } else {
        uint8_t v = audio_get_volume_pct();
        if (s_vol_sld) lv_slider_set_value(s_vol_sld, v, LV_ANIM_ON);
        if (s_vol_lbl) {
            char buf[8]; snprintf(buf, sizeof(buf), "%d%%", (int)v);
            lv_label_set_text(s_vol_lbl, buf);
        }
        if (s_mute_lbl) lv_label_set_text(s_mute_lbl, "MUTE");
        if (s_mute_btn) {
            lv_obj_set_style_bg_color(s_mute_btn, lv_color_hex(0x1a1a1a), 0);
            lv_obj_set_style_bg_opa(s_mute_btn, LV_OPA_COVER, 0);
        }
    }
}

static void on_vol_changed(lv_event_t *e) {
    s_last_activity = millis();
    lv_obj_t *sld = (lv_obj_t *)lv_event_get_target(e);
    int pct = lv_slider_get_value(sld);
    if (audio_is_muted() && pct > 0) {
        audio_mute_toggle();
        if (s_mute_lbl) lv_label_set_text(s_mute_lbl, "MUTE");
        if (s_mute_btn) {
            lv_obj_set_style_bg_color(s_mute_btn, lv_color_hex(0x1a1a1a), 0);
            lv_obj_set_style_bg_opa(s_mute_btn, LV_OPA_COVER, 0);
        }
    }
    audio_set_volume((uint8_t)pct);
    if (s_vol_lbl) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(s_vol_lbl, buf);
    }
}

static void on_brt_changed(lv_event_t *e) {
    s_last_activity = millis();
    lv_obj_t *sld = (lv_obj_t *)lv_event_get_target(e);
    int pct = lv_slider_get_value(sld);
    uint8_t brt = (uint8_t)(20 + ((uint32_t)pct * 235u) / 100u);
    display::setBrightness(brt);
    if (s_brt_lbl) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(s_brt_lbl, buf);
    }
}

// ── Sound mode callbacks ──────────────────────────────────────────────────────

static void on_sound_voice(lv_event_t *e) {
    (void)e;
    s_last_activity = millis();
    if (audio_is_voice_mode()) return;
    audio_set_voice_mode(true);
    pill_set_active(s_voice_btn, true);
    pill_set_active(s_tone_btn, false);
}

static void on_sound_tone(lv_event_t *e) {
    (void)e;
    s_last_activity = millis();
    if (!audio_is_voice_mode()) return;
    audio_set_voice_mode(false);
    pill_set_active(s_voice_btn, false);
    pill_set_active(s_tone_btn, true);
}

// ── Debug toggle ──────────────────────────────────────────────────────────────

static void on_dbg_toggle(lv_event_t *e) {
    s_last_activity = millis();
    g_debug = !g_debug;
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (g_debug) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a1a0a), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x44dd44), 0);
        if (lbl) { lv_label_set_text(lbl, "DBG  ON"); lv_obj_set_style_text_color(lbl, lv_color_hex(0x44dd44), 0); }
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a0a0a), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x222233), 0);
        if (lbl) { lv_label_set_text(lbl, "DBG OFF"); lv_obj_set_style_text_color(lbl, lv_color_hex(0x333344), 0); }
    }
}

// ── Widget builders ───────────────────────────────────────────────────────────

static lv_obj_t *make_row(lv_obj_t *parent, const char *icon, int val,
                           lv_event_cb_t cb, lv_obj_t **val_lbl_out,
                           lv_obj_t **sld_out = nullptr) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 46);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);

    lv_obj_t *ico = lv_label_create(row);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ico, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_width(ico, 34);

    lv_obj_t *sld = lv_slider_create(row);
    lv_obj_set_flex_grow(sld, 1);
    lv_obj_set_height(sld, 6);
    lv_slider_set_range(sld, 0, 100);
    lv_slider_set_value(sld, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sld, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sld, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sld, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sld, lv_color_hex(0xFFD700), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sld, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sld, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sld, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sld, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(sld, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sld, 7, LV_PART_KNOB);
    lv_obj_set_style_border_width(sld, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(sld, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    if (sld_out) *sld_out = sld;

    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", val);
    lv_obj_t *vlbl = lv_label_create(row);
    lv_label_set_text(vlbl, buf);
    lv_obj_set_style_text_font(vlbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(vlbl, lv_color_hex(0x888888), 0);
    lv_obj_set_width(vlbl, 38);
    lv_obj_set_style_text_align(vlbl, LV_TEXT_ALIGN_RIGHT, 0);
    *val_lbl_out = vlbl;
    return row;
}

static void make_sep(lv_obj_t *parent) {
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
}

static lv_obj_t *make_pill(lv_obj_t *parent, const char *text,
                            lv_event_cb_t cb, bool active_init) {
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_height(btn, 24);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(btn, 12, 0);
    lv_obj_set_style_pad_right(btn, 12, 0);
    lv_obj_set_style_pad_top(btn, 2, 0);
    lv_obj_set_style_pad_bottom(btn, 2, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(btn, LV_DIR_NONE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);

    pill_set_active(btn, active_init);
    return btn;
}

static void make_toggle_row(lv_obj_t *parent, const char *label_text,
                             const char *opt1, const char *opt2,
                             lv_event_cb_t cb1, lv_event_cb_t cb2,
                             lv_obj_t **p1, lv_obj_t **p2) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 30);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888898), 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);

    lv_obj_t *pills = lv_obj_create(row);
    lv_obj_remove_style_all(pills);
    lv_obj_set_size(pills, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pills, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(pills, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pills, 5, 0);
    lv_obj_clear_flag(pills, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(pills, LV_DIR_NONE);

    *p1 = make_pill(pills, opt1, cb1, true);
    *p2 = make_pill(pills, opt2, cb2, false);
}

// ── Public API ────────────────────────────────────────────────────────────────

void qs_begin() {
    // Transparent guard strip catches touch events in the swipe-trigger zone
    // so downward swipes never fire mode-screen buttons underneath.
    lv_obj_t *guard = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(guard);
    lv_obj_set_pos(guard, 0, 0);
    lv_obj_set_size(guard, SCREEN_W, 55);
    lv_obj_set_style_bg_opa(guard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(guard, 0, 0);
    lv_obj_add_flag(guard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(guard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(guard, LV_DIR_NONE);

    // ── Full-screen touch blocker (transparent, behind the card) ─────────────
    // Shown only during slide animation so underlying buttons can't be tapped.
    s_anim_block = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_anim_block);
    lv_obj_set_pos(s_anim_block, 0, 0);
    lv_obj_set_size(s_anim_block, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(s_anim_block, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_anim_block, 0, 0);
    lv_obj_add_flag(s_anim_block, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_anim_block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_anim_block, LV_DIR_NONE);

    // ── Centered floating card ────────────────────────────────────────────────
    s_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(s_panel, CARD_X, CARD_Y_HIDE);
    lv_obj_set_size(s_panel, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x0E0E18), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0x1E1E32), 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, 22, 0);
    lv_obj_set_style_pad_left(s_panel, 16, 0);
    lv_obj_set_style_pad_right(s_panel, 16, 0);
    lv_obj_set_style_pad_top(s_panel, 14, 0);
    lv_obj_set_style_pad_bottom(s_panel, 10, 0);
    lv_obj_set_style_pad_row(s_panel, 4, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_panel, LV_DIR_NONE);

    // Title
    lv_obj_t *title = lv_label_create(s_panel);
    lv_label_set_text(title, "QUICK SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    make_sep(s_panel);

    // Volume row
    make_row(s_panel, "VOL", VOL_DEFAULT, on_vol_changed, &s_vol_lbl, &s_vol_sld);

    // Mute button (centred pill below volume slider)
    s_mute_btn = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_mute_btn);
    lv_obj_set_size(s_mute_btn, 90, 28);
    lv_obj_set_style_bg_color(s_mute_btn, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(s_mute_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_mute_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(s_mute_btn, 1, 0);
    lv_obj_set_style_radius(s_mute_btn, 14, 0);
    lv_obj_set_style_pad_all(s_mute_btn, 0, 0);
    lv_obj_add_flag(s_mute_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_mute_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_mute_btn, LV_DIR_NONE);
    lv_obj_add_event_cb(s_mute_btn, on_mute_clicked, LV_EVENT_CLICKED, nullptr);

    s_mute_lbl = lv_label_create(s_mute_btn);
    lv_label_set_text(s_mute_lbl, "MUTE");
    lv_obj_set_style_text_font(s_mute_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_mute_lbl, lv_color_hex(0x888888), 0);
    lv_obj_center(s_mute_lbl);

    // Brightness row
    make_row(s_panel, "BRT", BRT_DEFAULT, on_brt_changed, &s_brt_lbl, &s_brt_sld);

    make_sep(s_panel);

    // SOUND toggle: VOICE (SAM TTS) / TONE (beeps)
    make_toggle_row(s_panel, "SOUND",
                    "VOICE", "TONE",
                    on_sound_voice, on_sound_tone,
                    &s_voice_btn, &s_tone_btn);

    make_sep(s_panel);

    // DEBUG toggle button
    lv_obj_t *dbg_btn = lv_obj_create(s_panel);
    lv_obj_remove_style_all(dbg_btn);
    lv_obj_set_size(dbg_btn, LV_PCT(100), 46);
    lv_obj_set_style_bg_color(dbg_btn, lv_color_hex(g_debug ? 0x0a1a0a : 0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(dbg_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dbg_btn, lv_color_hex(g_debug ? 0x44dd44 : 0x222233), 0);
    lv_obj_set_style_border_width(dbg_btn, 1, 0);
    lv_obj_set_style_radius(dbg_btn, 10, 0);
    lv_obj_add_flag(dbg_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dbg_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(dbg_btn, LV_DIR_NONE);
    lv_obj_add_event_cb(dbg_btn, on_dbg_toggle, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *dbg_lbl = lv_label_create(dbg_btn);
    lv_label_set_text(dbg_lbl, g_debug ? "DBG  ON" : "DBG OFF");
    lv_obj_set_style_text_font(dbg_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dbg_lbl, lv_color_hex(g_debug ? 0x44dd44 : 0x333344), 0);
    lv_obj_set_style_text_letter_space(dbg_lbl, 1, 0);
    lv_obj_center(dbg_lbl);

    // Dismiss hint
    lv_obj_t *hint = lv_label_create(s_panel);
    lv_label_set_text(hint, "\xe2\x86\x91  swipe up to close");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x1E1E30), 0);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    s_open = false;
    s_idle_tmr = lv_timer_create(idle_check_cb, 5000, nullptr);
}

void qs_open() {
    if (!s_panel || s_open) return;
    s_open = true;
    s_last_activity = millis();
    if (s_anim_block) lv_obj_clear_flag(s_anim_block, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, anim_y_exec);
    lv_anim_set_values(&a, CARD_Y_HIDE, CARD_Y);
    lv_anim_set_time(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, on_anim_done);
    lv_anim_start(&a);
}

void qs_close() {
    if (!s_panel || !s_open) return;
    s_open = false;
    if (s_anim_block) lv_obj_clear_flag(s_anim_block, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, anim_y_exec);
    lv_anim_set_values(&a, CARD_Y, CARD_Y_HIDE);
    lv_anim_set_time(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, on_anim_done);
    lv_anim_start(&a);
}

bool qs_is_open() { return s_open; }
