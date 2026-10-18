#include "lvgl_ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "board_config.h"
#include "lvgl_port.h"

/* ---- layout ------------------------------------------------------- */

#define LUI_W        240
#define LUI_H        320

#define LUI_HDR_H    22

#define LUI_WAVE_Y   22
#define LUI_WAVE_W   240
#define LUI_WAVE_H   224

#define LUI_SCALE_Y  246

#define LUI_TILES_Y  270
#define LUI_TILES_H  30
#define LUI_TILE_W   56
#define LUI_TILE_X0  3
#define LUI_TILE_DX  59

#define LUI_KEYS_Y   300
#define LUI_KEY_W    76
#define LUI_KEY_H    18
#define LUI_KEY_X0   3
#define LUI_KEY_DX   79

#define LUI_COLS     8
#define LUI_ROWS     4

#define LUI_TRIG_X   (LUI_WAVE_W / 2)          /* Figma mock 2:31 */

#define LUI_LOGIC_X  18
#define LUI_LOGIC_W  (LUI_WAVE_W - LUI_LOGIC_X)
#define LUI_LANE_H   (LUI_WAVE_H / 8)          /* 28 px exactly      */

/* ---- palette ------------------------------------------------------ */

#define LUI_C_BG     0x08080Au
#define LUI_C_PANEL  0x141418u
#define LUI_C_LINE   0x23232Au
#define LUI_C_GRID   0x1B1B21u
#define LUI_C_INK    0xF5F5F7u
#define LUI_C_SUB    0x8A8A93u
#define LUI_C_CH1    0xFFB020u
#define LUI_C_CH2    0x22D3EEu
#define LUI_C_CH3    0xF472B6u
#define LUI_C_RUN    0x30D158u
#define LUI_C_STOP   0xF87171u
#define LUI_C_TRIG   0xA78BFAu

static const uint32_t S_CH_COLOR[3] = { LUI_C_CH1, LUI_C_CH2, LUI_C_CH3 };

/* ---- widget handles ------------------------------------------------ */

static lv_obj_t *s_scr;
static lv_obj_t *s_mode_lbl;
static lv_obj_t *s_chip[3];
static lv_obj_t *s_chip_lbl[3];
static lv_obj_t *s_lanes_lbl;
static lv_obj_t *s_run;
static lv_obj_t *s_run_lbl;
static lv_obj_t *s_wave;
static lv_obj_t *s_scale_lbl;
static lv_obj_t *s_rate_lbl;
static lv_obj_t *s_tile[4];
static lv_obj_t *s_tile_name[4];
static lv_obj_t *s_tile_val[4];
static lv_obj_t *s_key[3];

/* ---- state -------------------------------------------------------- */

static bool s_ready;
static bool s_logic;
static uint8_t s_mask = 0x01u;
static bool s_running = true;

/* waveform data the draw callback reads.  Analog keeps a min/max per
   column per slot; logic keeps an eight-bit lane mask per column.     */
static uint16_t s_col_min[SCOPE_ADC_CHANNELS][LUI_WAVE_W];
static uint16_t s_col_max[SCOPE_ADC_CHANNELS][LUI_WAVE_W];
static uint8_t  s_col_lanes[LUI_WAVE_W];
static bool     s_slot_on[SCOPE_ADC_CHANNELS];

/* step polyline scratch for one logic lane */
static lv_point_precise_t s_pts[2 * LUI_LOGIC_W + 8];

/* cached label text so a per-frame update only reallocs when it changed */
static char s_cache[8][24];

/* ---- formatters (ported from scope_view.cpp) ----------------------- */

/* "1.0k" / "12.3" / "--" */
static void format_freq(char *out, size_t n, float f)
{
    if (out == nullptr || n == 0u)
        return;
    if (f < 1.0f) {
        snprintf(out, n, "--");
        return;
    }
    if (f >= 1000.0f) {
        const int k  = (int)(f / 1000.0f);
        const int df = (int)((f - (float)k * 1000.0f) / 100.0f);
        snprintf(out, n, "%d.%01dk", k, df);
    } else {
        const int v  = (int)f;
        const int df = (int)((f - (float)v) * 10.0f);
        snprintf(out, n, "%d.%01d", v, df);
    }
}

/* "1.0M" / "100k" / "1000" */
static void format_rate(char *out, size_t n, uint32_t rate)
{
    if (out == nullptr || n == 0u)
        return;
    if (rate >= 1000000u) {
        snprintf(out, n, "%lu.%01luM", (unsigned long)(rate / 1000000u),
                 (unsigned long)((rate % 1000000u) / 100000u));
    } else if (rate >= 1000u) {
        snprintf(out, n, "%lu.%01luK", (unsigned long)(rate / 1000u),
                 (unsigned long)((rate % 1000u) / 100u));
    } else {
        snprintf(out, n, "%lu", (unsigned long)rate);
    }
}

/* ---- small helpers ------------------------------------------------- */

static lv_obj_t *make_panel(lv_obj_t *parent, int32_t x, int32_t y,
                            int32_t w, int32_t h, int32_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(LUI_C_PANEL), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(LUI_C_LINE), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_label_set_text(l, text);
    return l;
}

/* set a label only when the text actually changed, so a streaming
   capture does not churn the LVGL heap twenty times a second        */
static void set_cached(lv_obj_t *lbl, char *cache, const char *text)
{
    if (lbl == nullptr || text == nullptr)
        return;
    if (strncmp(cache, text, 23) == 0)
        return;
    strncpy(cache, text, 23);
    cache[23] = '\0';
    lv_label_set_text(lbl, cache);
}

static int32_t y_for(int32_t code, int32_t oy)
{
    if (code < 0)
        code = 0;
    if (code > (int32_t)SCOPE_ADC_FULL_SCALE)
        code = (int32_t)SCOPE_ADC_FULL_SCALE;
    return oy + (LUI_WAVE_H - 1) -
           code * (LUI_WAVE_H - 1) / (int32_t)SCOPE_ADC_FULL_SCALE;
}

/* ---- the wave raster ---------------------------------------------- */

static void draw_wave_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    if (layer == nullptr)
        return;

    const int32_t ox = 0;             /* the wave object sits at x = 0 */
    const int32_t oy = LUI_WAVE_Y;    /* and at y = LUI_WAVE_Y         */

    lv_draw_line_dsc_t d;

    /* grid / lane separators ---------------------------------------- */
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(LUI_C_GRID);
    d.width = 1;
    d.opa = LV_OPA_COVER;

    if (s_logic) {
        for (int lane = 1; lane < 8; ++lane) {
            const int32_t y = oy + lane * LUI_LANE_H - 1;
            d.p1.x = ox;
            d.p1.y = y;
            d.p2.x = ox + LUI_WAVE_W - 1;
            d.p2.y = y;
            lv_draw_line(layer, &d);
        }
    } else {
        for (int c = 1; c < LUI_COLS; ++c) {
            const int32_t x = ox + (LUI_WAVE_W / LUI_COLS) * c - 1;
            d.p1.x = x;
            d.p1.y = oy;
            d.p2.x = x;
            d.p2.y = oy + LUI_WAVE_H - 1;
            lv_draw_line(layer, &d);
        }
        for (int r = 1; r < LUI_ROWS; ++r) {
            const int32_t y = oy + (LUI_WAVE_H / LUI_ROWS) * r - 1;
            d.p1.x = ox;
            d.p1.y = y;
            d.p2.x = ox + LUI_WAVE_W - 1;
            d.p2.y = y;
            lv_draw_line(layer, &d);
        }
    }

    /* trigger column ------------------------------------------------- */
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(LUI_C_TRIG);
    d.width = 1;
    d.opa = LV_OPA_COVER;
    d.p1.x = ox + LUI_TRIG_X;
    d.p1.y = oy;
    d.p2.x = ox + LUI_TRIG_X;
    d.p2.y = oy + LUI_WAVE_H - 1;
    lv_draw_line(layer, &d);

    /* the waveform --------------------------------------------------- */
    if (s_logic) {
        for (int lane = 0; lane < 8; ++lane) {
            const bool on = (s_mask >> lane) & 1u;
            const int32_t top = oy + lane * LUI_LANE_H;
            const int32_t hi  = top + 7;
            const int32_t lo  = top + LUI_LANE_H - 8;

            lv_draw_line_dsc_t w;
            lv_draw_line_dsc_init(&w);
            w.color = lv_color_hex(on ? LUI_C_CH2 : LUI_C_GRID);
            w.width = 1;
            w.opa = LV_OPA_COVER;

            int n = 0;
            int prev = -1;
            for (int c = 0; c < LUI_LOGIC_W; ++c) {
                const int lvl = (s_col_lanes[c] >> lane) & 1;
                if (prev < 0) {
                    s_pts[n].x = ox + LUI_LOGIC_X + c;
                    s_pts[n].y = (lvl ? hi : lo);
                    ++n;
                    prev = lvl;
                } else if (lvl != prev) {
                    s_pts[n].x = ox + LUI_LOGIC_X + c;
                    s_pts[n].y = (prev ? hi : lo);
                    ++n;
                    s_pts[n].x = ox + LUI_LOGIC_X + c;
                    s_pts[n].y = (lvl ? hi : lo);
                    ++n;
                    prev = lvl;
                }
            }
            if (prev < 0)
                continue;
            s_pts[n].x = ox + LUI_LOGIC_X + LUI_LOGIC_W;
            s_pts[n].y = (prev ? hi : lo);
            ++n;

            w.points = s_pts;
            w.point_cnt = n;
            lv_draw_line(layer, &w);
        }
    } else {
        for (int slot = 0; slot < SCOPE_ADC_CHANNELS; ++slot) {
            if (!s_slot_on[slot])
                continue;

            lv_draw_line_dsc_t w;
            lv_draw_line_dsc_init(&w);
            w.color = lv_color_hex(S_CH_COLOR[slot]);
            w.width = 1;
            w.opa = LV_OPA_COVER;

            for (int c = 0; c < LUI_WAVE_W; ++c) {
                const int32_t x  = ox + c;
                w.p1.x = x;
                w.p1.y = y_for(s_col_max[slot][c], oy);
                w.p2.x = x;
                w.p2.y = y_for(s_col_min[slot][c], oy);
                lv_draw_line(layer, &w);
            }
        }
    }
}

/* ---- construction -------------------------------------------------- */

bool lvgl_ui_init(void)
{
    if (s_ready)
        return true;
    if (!lvgl_port_init())
        return false;

    s_scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_radius(s_scr, 0, 0);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(LUI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    /* header ---------------------------------------------------------- */
    s_mode_lbl = make_label(s_scr, &lv_font_montserrat_12,
                            LUI_C_INK, "SCOPE");
    lv_obj_set_pos(s_mode_lbl, 8, 5);

    for (int i = 0; i < 3; ++i) {
        s_chip[i] = make_panel(s_scr, 82 + i * 34, 5, 30, 12, 2);
        s_chip_lbl[i] = make_label(s_chip[i], &lv_font_montserrat_10,
                                   S_CH_COLOR[i], "CH1");
        lv_obj_align(s_chip_lbl[i], LV_ALIGN_CENTER, 0, 0);
    }

    s_lanes_lbl = make_label(s_scr, &lv_font_montserrat_12,
                             LUI_C_CH2, "D0-7");
    lv_obj_set_pos(s_lanes_lbl, 82, 5);
    lv_obj_add_flag(s_lanes_lbl, LV_OBJ_FLAG_HIDDEN);

    s_run = make_panel(s_scr, 198, 5, 34, 12, 2);
    s_run_lbl = make_label(s_run, &lv_font_montserrat_10,
                           LUI_C_BG, "RUN");
    lv_obj_align(s_run_lbl, LV_ALIGN_CENTER, 0, 0);

    /* wave raster ------------------------------------------------------ */
    s_wave = lv_obj_create(s_scr);
    lv_obj_remove_flag(s_wave, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_wave, 0, LUI_WAVE_Y);
    lv_obj_set_size(s_wave, LUI_WAVE_W, LUI_WAVE_H);
    lv_obj_set_style_pad_all(s_wave, 0, 0);
    lv_obj_set_style_radius(s_wave, 0, 0);
    lv_obj_set_style_border_width(s_wave, 0, 0);
    lv_obj_set_style_bg_color(s_wave, lv_color_hex(LUI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_wave, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_wave, draw_wave_cb, LV_EVENT_DRAW_MAIN, nullptr);

    /* scale row -------------------------------------------------------- */
    s_scale_lbl = make_label(s_scr, &lv_font_montserrat_12,
                             LUI_C_SUB, "C/DIV RAW");
    lv_obj_set_pos(s_scale_lbl, 8, LUI_SCALE_Y + 5);

    s_rate_lbl = make_label(s_scr, &lv_font_montserrat_12,
                            LUI_C_SUB, "--");
    lv_obj_set_pos(s_rate_lbl, 168, LUI_SCALE_Y + 5);

    /* measurement tiles ------------------------------------------------ */
    for (int i = 0; i < 4; ++i) {
        s_tile[i] = make_panel(s_scr, LUI_TILE_X0 + i * LUI_TILE_DX,
                               LUI_TILES_Y, LUI_TILE_W, LUI_TILES_H, 4);
        s_tile_name[i] = make_label(s_tile[i], &lv_font_montserrat_10,
                                    LUI_C_SUB, "----");
        lv_obj_align(s_tile_name[i], LV_ALIGN_TOP_LEFT, 4, 2);
        s_tile_val[i] = make_label(s_tile[i], &lv_font_montserrat_12,
                                   LUI_C_INK, "--");
        lv_obj_align(s_tile_val[i], LV_ALIGN_TOP_LEFT, 4, 13);
    }

    /* softkeys --------------------------------------------------------- */
    static const char *KEYS[3] = { "AUTO", "TRIG", "MENU" };
    for (int i = 0; i < 3; ++i) {
        s_key[i] = make_panel(s_scr, LUI_KEY_X0 + i * LUI_KEY_DX,
                              LUI_KEYS_Y, LUI_KEY_W, LUI_KEY_H, 4);
        lv_obj_t *l = make_label(s_key[i], &lv_font_montserrat_10,
                                 LUI_C_SUB, KEYS[i]);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    }

    lv_screen_load(s_scr);

    memset(s_cache, 0, sizeof(s_cache));
    for (int i = 0; i < SCOPE_ADC_CHANNELS; ++i)
        s_slot_on[i] = false;

    s_ready = true;
    lvgl_ui_set_chrome(SCOPE_MODE_SCOPE, s_mask, true);
    return true;
}

void lvgl_ui_pump(void)
{
    if (!s_ready)
        return;
    (void)lvgl_port_handler();
}

/* ---- chrome -------------------------------------------------------- */

void lvgl_ui_set_chrome(scope_capture_mode_t mode, uint8_t channel_mask,
                        bool running)
{
    if (!s_ready)
        return;

    s_logic = (mode == SCOPE_MODE_LOGIC);
    s_mask = s_logic ? (uint8_t)SCOPE_LOGIC_PIN_MASK : channel_mask;

    lv_label_set_text(s_mode_lbl, s_logic ? "LOGIC" : "SCOPE");

    for (int i = 0; i < 3; ++i) {
        const bool on = !s_logic && ((s_mask >> i) & 1u);
        if (on)
            lv_obj_remove_flag(s_chip[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_chip[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_logic)
        lv_obj_remove_flag(s_lanes_lbl, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_lanes_lbl, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(s_scale_lbl, s_logic ? "LOGIC NOMINAL" : "C/DIV RAW");

    for (int i = 0; i < 4; ++i) {
        if (s_logic) {
            static const char *names[4] = { "LANES", "EDGES", "DUTY", "RATE" };
            lv_label_set_text(s_tile_name[i], names[i]);
        } else {
            static const char *names[4] = { "FREQ", "VPP", "RMS", "RATE" };
            lv_label_set_text(s_tile_name[i], names[i]);
        }
        lv_label_set_text(s_tile_val[i], "--");
        s_cache[4 + i][0] = '\0';
    }

    lvgl_ui_set_running(running);

    /* a mode or mask change invalidates the whole raster */
    memset(s_col_lanes, 0, sizeof(s_col_lanes));
    for (int slot = 0; slot < SCOPE_ADC_CHANNELS; ++slot) {
        for (int c = 0; c < LUI_WAVE_W; ++c) {
            s_col_min[slot][c] = 0;
            s_col_max[slot][c] = 0;
        }
        s_slot_on[slot] = false;
    }
    lv_obj_invalidate(s_wave);
}

void lvgl_ui_set_running(bool running)
{
    if (!s_ready)
        return;
    s_running = running;
    lv_obj_set_style_bg_color(s_run,
                              lv_color_hex(running ? LUI_C_RUN : LUI_C_STOP), 0);
    lv_label_set_text(s_run_lbl, running ? "RUN" : "STOP");
}

/* ---- scope --------------------------------------------------------- */

void lvgl_ui_draw_scope(const uint16_t *samples, uint16_t count,
                        uint8_t channels, uint8_t channel_mask)
{
    if (!s_ready || samples == nullptr || count == 0u)
        return;

    const uint16_t chans = (channels == 0u) ? 1u : channels;

    for (int slot = 0; slot < SCOPE_ADC_CHANNELS; ++slot) {
        s_slot_on[slot] = (slot < (int)chans);
        if (!s_slot_on[slot])
            continue;

        for (int c = 0; c < LUI_WAVE_W; ++c) {
            uint32_t a = (uint32_t)c * count / LUI_WAVE_W;
            uint32_t b = (uint32_t)(c + 1) * count / LUI_WAVE_W;
            if (b <= a)
                b = a + 1u;
            if (b > count)
                b = count;

            uint16_t lo = 0xFFFFu;
            uint16_t hi = 0u;
            for (uint32_t i = a; i < b; ++i) {
                const uint16_t v = samples[i * chans + (uint16_t)slot];
                if (v < lo)
                    lo = v;
                if (v > hi)
                    hi = v;
            }
            if (lo == 0xFFFFu)
                lo = 0u;
            s_col_min[slot][c] = lo;
            s_col_max[slot][c] = hi;
        }
    }

    s_logic = false;
    (void)channel_mask;
    lv_obj_invalidate(s_wave);
}

/* ---- logic --------------------------------------------------------- */

void lvgl_ui_draw_logic(const uint32_t *samples, uint16_t count,
                        uint8_t lane_mask)
{
    if (!s_ready || samples == nullptr || count == 0u)
        return;

    s_logic = true;
    s_mask = lane_mask;

    for (int c = 0; c < LUI_WAVE_W; ++c) {
        uint32_t a = (uint32_t)c * count / LUI_WAVE_W;
        uint32_t b = (uint32_t)(c + 1) * count / LUI_WAVE_W;
        if (b <= a)
            b = a + 1u;
        if (b > count)
            b = count;

        uint8_t lanes = 0u;
        for (uint32_t i = a; i < b; ++i)
            lanes |= (uint8_t)(samples[i] & 0xFFu);
        s_col_lanes[c] = lanes;
    }

    /* tiles: how many lanes are live, how busier they are, and how long
       the average lane spends high.  Measured from the same frame the
       raster is drawn from, so the numbers and the picture agree.     */
    int live = 0;
    uint32_t edges = 0u;
    uint32_t high = 0u;
    uint32_t total = 0u;

    for (int lane = 0; lane < 8; ++lane) {
        if (((lane_mask >> lane) & 1u) == 0u)
            continue;
        ++live;
        const uint32_t bit = 1u << lane;
        bool prev = (samples[0] & bit) != 0u;
        for (uint32_t i = 1; i < count; ++i) {
            const bool now = (samples[i] & bit) != 0u;
            if (now && !prev)
                ++edges;
            prev = now;
        }
        for (uint32_t i = 0; i < count; ++i)
            if (samples[i] & bit)
                ++high;
        total += count;
    }

    char buf[24];

    snprintf(buf, sizeof(buf), "%d", live);
    set_cached(s_tile_val[0], s_cache[4], buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)edges);
    set_cached(s_tile_val[1], s_cache[5], buf);

    if (total > 0u)
        snprintf(buf, sizeof(buf), "%lu%%",
                 (unsigned long)((high * 100u) / total));
    else
        snprintf(buf, sizeof(buf), "--");
    set_cached(s_tile_val[2], s_cache[6], buf);

    lv_obj_invalidate(s_wave);
}

/* ---- scale row and scope tiles ------------------------------------- */

void lvgl_ui_draw_measurements(const scope_measurements_t *per_channel,
                               uint8_t channels, uint8_t channel_mask,
                               uint32_t rate)
{
    if (!s_ready)
        return;

    char buf[24];

    format_rate(buf, sizeof(buf), rate);
    if (rate == 0u)
        snprintf(buf, sizeof(buf), "--");
    set_cached(s_rate_lbl, s_cache[0], buf);

    /* the rate tile is mode independent */
    set_cached(s_tile_val[3], s_cache[7], buf);

    if (s_logic || per_channel == nullptr || channels == 0u)
        return;

    const scope_measurements_t *m = &per_channel[0];

    format_freq(buf, sizeof(buf), m->freq_hz);
    set_cached(s_tile_val[0], s_cache[4], buf);

    if (m->valid) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)m->vpp_code);
        set_cached(s_tile_val[1], s_cache[5], buf);

        snprintf(buf, sizeof(buf), "%d", (int)(m->rms_code + 0.5f));
        set_cached(s_tile_val[2], s_cache[6], buf);
    } else {
        set_cached(s_tile_val[1], s_cache[5], "--");
        set_cached(s_tile_val[2], s_cache[6], "--");
    }
}
