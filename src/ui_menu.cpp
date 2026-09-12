// 2x3 mode-selector grid on the 466x466 round AMOLED.
// Each button now carries a centered geometric icon drawn via LVGL custom draw callback.
#include "ui_menu.h"
#include "config.h"
#include <lvgl.h>

static lv_obj_t    *s_screen    = nullptr;
static MenuSelectCb s_on_select  = nullptr;
static AppMode      s_pending    = AppMode::None;

// ─── Async screen switch (avoids deleting active screen inside an event) ─────

static void async_switch(void *) {
    if (s_on_select && s_pending != AppMode::None) {
        AppMode m = s_pending;
        s_pending = AppMode::None;
        s_on_select(m);
    }
}

// ─── Mode table ──────────────────────────────────────────────────────────────

struct ModeEntry {
    AppMode     mode;
    const char *name;
    const char *sub;
    uint32_t    color;
};

static constexpr ModeEntry MODES[] = {
    { AppMode::Detector,  "Detector",   "BLE surveillance", 0xff4545 },
    { AppMode::Foxhunter, "Foxhunter",  "RSSI tracker",     0xff8c00 },
    { AppMode::FlockYou,  "Flock-You",  "WiFi passive",     0xffd700 },
    { AppMode::PCAP,      "PCAP",       "Packet capture",   0x4499ff },
    { AppMode::SkySpy,    "Sky Spy",    "Drone Remote ID",  0xaa44ff },
    { AppMode::BLESniff,  "BLE Sniff",  "BLE passive",      0x00ccaa },
};

// ─── Icon draw callback ───────────────────────────────────────────────────────
// Draws directly with LVGL draw APIs on LV_EVENT_DRAW_MAIN_END.
// user_data: icon index (0-5) packed as (uintptr_t).

struct IconCtx { int idx; uint32_t color; };
static IconCtx s_icon_ctx[6];   // lifetime = screen lifetime

static void icon_draw_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN_END) return;

    lv_obj_t      *obj      = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    const IconCtx *ctx      = (const IconCtx *)lv_event_get_user_data(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_coord_t cx = (a.x1 + a.x2) / 2;
    lv_coord_t cy = (a.y1 + a.y2) / 2;
    lv_point_t cpt = {cx, cy};
    lv_color_t col = lv_color_hex(ctx->color);

    // ── Shared draw descriptors ────────────────────────────────────────────

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = col;
    arc.opa   = LV_OPA_COVER;
    arc.rounded = 1;

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color       = col;
    line.opa         = LV_OPA_COVER;
    line.round_start = 1;
    line.round_end   = 1;
    line.width       = 2;

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color    = col;
    rect.bg_opa      = LV_OPA_COVER;
    rect.border_width = 0;

    lv_point_t p0, p1;

    switch (ctx->idx) {

    // ── 0: Detector — concentric circles (bullseye) ───────────────────────
    case 0: {
        arc.width = 2; arc.opa = LV_OPA_40;
        lv_draw_arc(draw_ctx, &arc, &cpt, 16, 0, 360);
        arc.opa = LV_OPA_70;
        lv_draw_arc(draw_ctx, &arc, &cpt, 11, 0, 360);
        arc.opa = LV_OPA_COVER;
        lv_draw_arc(draw_ctx, &arc, &cpt, 6, 0, 360);
        lv_area_t da = {cx-3, cy-3, cx+3, cy+3};
        rect.radius = LV_RADIUS_CIRCLE; rect.bg_opa = LV_OPA_COVER;
        lv_draw_rect(draw_ctx, &rect, &da);
        break;
    }

    // ── 1: Foxhunter — crosshair ──────────────────────────────────────────
    case 1: {
        arc.width = 2; arc.opa = LV_OPA_COVER;
        lv_draw_arc(draw_ctx, &arc, &cpt, 10, 0, 360);
        lv_area_t da = {cx-3, cy-3, cx+3, cy+3};
        rect.radius = LV_RADIUS_CIRCLE; rect.bg_opa = LV_OPA_COVER;
        lv_draw_rect(draw_ctx, &rect, &da);
        // tick marks
        p0 = {cx, cy-14}; p1 = {cx, cy-12}; lv_draw_line(draw_ctx, &line, &p0, &p1);
        p0 = {cx, cy+12}; p1 = {cx, cy+14}; lv_draw_line(draw_ctx, &line, &p0, &p1);
        p0 = {cx-14, cy}; p1 = {cx-12, cy}; lv_draw_line(draw_ctx, &line, &p0, &p1);
        p0 = {cx+12, cy}; p1 = {cx+14, cy}; lv_draw_line(draw_ctx, &line, &p0, &p1);
        break;
    }

    // ── 2: Flock-You — WiFi fan arcs ─────────────────────────────────────
    case 2: {
        lv_point_t fan = {cx, cy + 6};
        arc.width = 2; arc.opa = LV_OPA_COVER;
        lv_draw_arc(draw_ctx, &arc, &fan, 6,  215, 325);
        arc.opa   = 140;   // ~55%
        lv_draw_arc(draw_ctx, &arc, &fan, 11, 210, 330);
        arc.width = 1; arc.opa = 72;   // ~28%
        lv_draw_arc(draw_ctx, &arc, &fan, 16, 205, 335);
        lv_area_t da = {fan.x-3, fan.y+4, fan.x+3, fan.y+9};
        rect.radius = LV_RADIUS_CIRCLE; rect.bg_opa = LV_OPA_COVER;
        lv_draw_rect(draw_ctx, &rect, &da);
        break;
    }

    // ── 3: PCAP — stacked data bars ──────────────────────────────────────
    case 3: {
        struct { int w; uint8_t opa; } bars[4] = {{20,255},{15,178},{18,128},{11,64}};
        rect.radius = 2;
        for (int b = 0; b < 4; ++b) {
            rect.bg_opa = bars[b].opa;
            lv_area_t ba = {cx - bars[b].w/2, cy - 10 + b*7,
                            cx + bars[b].w/2,  cy - 10 + b*7 + 4};
            lv_draw_rect(draw_ctx, &rect, &ba);
        }
        break;
    }

    // ── 4: Sky Spy — drone X + prop circles ──────────────────────────────
    case 4: {
        const int arm = 9;
        p0 = {cx, cy};
        p1 = {(lv_coord_t)(cx-arm), (lv_coord_t)(cy-arm)}; lv_draw_line(draw_ctx, &line, &p0, &p1);
        p1 = {(lv_coord_t)(cx+arm), (lv_coord_t)(cy-arm)}; lv_draw_line(draw_ctx, &line, &p0, &p1);
        p1 = {(lv_coord_t)(cx-arm), (lv_coord_t)(cy+arm)}; lv_draw_line(draw_ctx, &line, &p0, &p1);
        p1 = {(lv_coord_t)(cx+arm), (lv_coord_t)(cy+arm)}; lv_draw_line(draw_ctx, &line, &p0, &p1);
        arc.width = 2; arc.opa = LV_OPA_COVER;
        lv_point_t corners[4] = {
            {(lv_coord_t)(cx-arm), (lv_coord_t)(cy-arm)},
            {(lv_coord_t)(cx+arm), (lv_coord_t)(cy-arm)},
            {(lv_coord_t)(cx-arm), (lv_coord_t)(cy+arm)},
            {(lv_coord_t)(cx+arm), (lv_coord_t)(cy+arm)},
        };
        for (int c = 0; c < 4; ++c)
            lv_draw_arc(draw_ctx, &arc, &corners[c], 4, 0, 360);
        lv_area_t da = {cx-3, cy-3, cx+3, cy+3};
        rect.radius = LV_RADIUS_CIRCLE; rect.bg_opa = LV_OPA_COVER;
        lv_draw_rect(draw_ctx, &rect, &da);
        break;
    }

    // ── 5: BLE Sniff — waveform polyline ─────────────────────────────────
    case 5: {
        static const int8_t WX[] = {-14,-10,-7,-4,-1, 2, 5, 8,11,14};
        static const int8_t WY[] = {  0,  0,-8, 8,-7, 6,-5, 4, 0, 0};
        for (int s = 0; s < 9; ++s) {
            p0 = {(lv_coord_t)(cx + WX[s]),   (lv_coord_t)(cy + WY[s])};
            p1 = {(lv_coord_t)(cx + WX[s+1]), (lv_coord_t)(cy + WY[s+1])};
            lv_draw_line(draw_ctx, &line, &p0, &p1);
        }
        break;
    }

    default: break;
    }
}

// ─── Event callbacks ─────────────────────────────────────────────────────────

static void btn_tap_cb(lv_event_t *e) {
    s_pending = *static_cast<const AppMode *>(lv_event_get_user_data(e));
    lv_async_call(async_switch, nullptr);
}

// ─── Public API ─────────────────────────────────────────────────────────────

void ui_menu_create(MenuSelectCb on_select) {
    s_on_select = on_select;

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_NONE);

    // "OUI SPY" wordmark
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "OUI Unified Watcher");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x44dd88), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    // Size to content so shadow blooms from all 4 sides (full-screen width clips left/right shadow)
    lv_obj_set_size(title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(title, 14, 0);
    lv_obj_set_style_pad_ver(title, 6, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);
    // Faint green pill bg — required for LVGL to composite the shadow
    lv_obj_set_style_bg_color(title, lv_color_hex(0x44dd88), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_10, 0);
    lv_obj_set_style_radius(title, 10, 0);
    // Wide neon glow
    lv_obj_set_style_shadow_color(title, lv_color_hex(0x44dd88), 0);
    lv_obj_set_style_shadow_width(title, 39, 0);
    lv_obj_set_style_shadow_spread(title, 7, 0);
    lv_obj_set_style_shadow_opa(title, LV_OPA_60, 0);
    lv_obj_set_style_shadow_ofs_x(title, 0, 0);
    lv_obj_set_style_shadow_ofs_y(title, 0, 0);

    // 2×3 grid: 314×314 at (76,76), buttons 152×98, gap 10
    lv_obj_t *grid = lv_obj_create(s_screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_pos(grid, 76, 110);
    lv_obj_set_size(grid, 314, 284);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_scroll_dir(grid, LV_DIR_NONE);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);

    static AppMode mode_ids[6];

    for (int i = 0; i < 6; ++i) {
        mode_ids[i] = MODES[i].mode;

        lv_obj_t *btn = lv_obj_create(grid);
        lv_obj_set_size(btn, 152, 88);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0e0e0e), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a1a1a), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 23, 0);
        lv_obj_set_style_pad_all(btn, 10, 0);
        // Center all children horizontally
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(btn, LV_DIR_NONE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_ext_click_area(btn, 20);
        lv_obj_add_event_cb(btn, btn_tap_cb, LV_EVENT_CLICKED, &mode_ids[i]);

        // ── Icon container (28×28, transparent, draws via callback) ─────
        lv_obj_t *ico = lv_obj_create(btn);
        lv_obj_remove_style_all(ico);
        lv_obj_set_size(ico, 28, 28);
        lv_obj_set_style_bg_opa(ico, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ico, 0, 0);
        lv_obj_set_scroll_dir(ico, LV_DIR_NONE);
        lv_obj_clear_flag(ico, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        s_icon_ctx[i] = { i, MODES[i].color };
        lv_obj_add_event_cb(ico, icon_draw_cb, LV_EVENT_DRAW_MAIN_END, &s_icon_ctx[i]);

        // ── Mode name ────────────────────────────────────────────────────
        lv_obj_t *name_lbl = lv_label_create(btn);
        lv_label_set_text(name_lbl, MODES[i].name);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xdddddd), 0);
        lv_obj_set_style_pad_top(name_lbl, 4, 0);

        // ── Sub-label ────────────────────────────────────────────────────
        lv_obj_t *sub_lbl = lv_label_create(btn);
        lv_label_set_text(sub_lbl, MODES[i].sub);
        lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x3a3a3a), 0);
        lv_obj_set_style_pad_top(sub_lbl, 1, 0);
    }

    lv_scr_load(s_screen);
}

void ui_menu_destroy() {
    if (s_screen) {
        lv_obj_del_async(s_screen);
        s_screen = nullptr;
    }
}
