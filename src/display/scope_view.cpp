/**
 * scope_view.cpp — on-LCD screen composition (protocol v2)
 *
 * Two views share one wave raster:
 *   SCOPE  up to three overlaid analog traces in CH1/CH2/CH3 colours
 *   LOGIC  eight stacked digital lanes D0..D7
 *
 * All text is UPPERCASE because the font atlas only covers 0x20..0x5A.
 */
#include "scope_view.h"

#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "st7789.h"

static scope_capture_mode_t s_mode         = SCOPE_MODE_SCOPE;
static uint8_t              s_channel_mask = 0x01u;
static bool                 s_running      = true;

/* ---- helpers -------------------------------------------------------- */
static uint16_t channel_color(uint8_t slot)
{
    switch (slot) {
    case 0u:  return COLOR_CH1;
    case 1u:  return COLOR_CH2;
    case 2u:  return COLOR_CH3;
    default:  return COLOR_TRACE;
    }
}

static const char *mode_label(void)
{
    return (s_mode == SCOPE_MODE_LOGIC) ? "LOGIC" : "SCOPE";
}

/* "1.0k" / "12.3" / "--"                                              */
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

static void format_rate(char *out, size_t n, uint32_t rate)
{
    if (out == nullptr || n == 0u)
        return;
    if (rate >= 1000000u) {
        snprintf(out, n, "%lu.%01luM", (unsigned long)(rate / 1000000u),
                 (unsigned long)((rate % 1000000u) / 100000u));
    } else if (rate >= 1000u) {
        snprintf(out, n, "%lu.%01luk", (unsigned long)(rate / 1000u),
                 (unsigned long)((rate % 1000u) / 100u));
    } else {
        snprintf(out, n, "%lu", (unsigned long)rate);
    }
}

/* ---- static chrome -------------------------------------------------- */
static void draw_header(void)
{
    st7789_fill_rect(0, SV_HEADER_Y, SCOPE_LCD_WIDTH, SV_HEADER_H, COLOR_BG);
    st7789_draw_string(4, 5, mode_label(), COLOR_TEXT);

    if (s_mode == SCOPE_MODE_LOGIC) {
        st7789_draw_string(74, 5, "D0-7", COLOR_CH2);
    } else {
        uint16_t x = 74;
        for (uint8_t ch = 0; ch < SCOPE_ADC_CHANNELS; ++ch) {
            if ((s_channel_mask & (1u << ch)) == 0u)
                continue;
            char lbl[5];
            snprintf(lbl, sizeof(lbl), "CH%u", (unsigned)(ch + 1u));
            st7789_draw_string(x, 5, lbl, channel_color(ch));
            x = (uint16_t)(x + 34u);
        }
    }

    st7789_draw_string(204, 5, s_running ? "RUN" : "STOP",
                       s_running ? COLOR_RUN : COLOR_TRIG);
}

static void draw_grid_int(void)
{
    /* 8 columns x 6 rows of divisions; drawn once per raster clear.    */
    for (uint16_t d = 0; d <= 6u; ++d) {
        const uint16_t y = (uint16_t)(SV_WAVE_Y + d * SV_DIV_H - 1u);
        if (y < SV_WAVE_Y + SV_WAVE_H)
            st7789_fill_rect(SV_WAVE_X, y, SV_WAVE_W, 1u, COLOR_GRID);
    }
    for (uint16_t d = 0; d <= 8u; ++d) {
        const uint16_t x = (uint16_t)(SV_WAVE_X + d * SV_DIV_W - 1u);
        if (x < SV_WAVE_X + SV_WAVE_W)
            st7789_fill_rect(x, SV_WAVE_Y, 1u, SV_WAVE_H, COLOR_GRID);
    }
}

static void draw_lane_separators(void)
{
    for (uint16_t lane = 1u; lane < SV_LOGIC_LANES; ++lane) {
        const uint16_t y = (uint16_t)(SV_WAVE_Y + lane * SV_LOGIC_LANE_H);
        st7789_fill_rect(SV_WAVE_X, y, SV_WAVE_W, 1u, COLOR_GRID);
    }
}

static void draw_scale_row(uint32_t rate)
{
    st7789_fill_rect(0, SV_SCALE_Y, SCOPE_LCD_WIDTH, SV_SCALE_H, COLOR_BG);

    if (s_mode == SCOPE_MODE_LOGIC) {
        st7789_draw_string(4, (uint16_t)(SV_SCALE_Y + 5u), "LOGIC NOMINAL",
                           COLOR_TEXT_DIM);
    } else {
        /* No AFE / calibration layer: codes per division, not volts.   */
        st7789_draw_string(4, (uint16_t)(SV_SCALE_Y + 5u), "C/DIV RAW",
                           COLOR_TEXT_DIM);
    }

    if (rate > 0u) {
        char buf[12];
        format_rate(buf, sizeof(buf), rate);
        st7789_draw_string(150, (uint16_t)(SV_SCALE_Y + 5u), buf,
                           COLOR_TEXT_DIM);
    }
}

static void draw_tiles(const scope_measurements_t *m, uint8_t channels,
                       uint8_t channel_mask)
{
    st7789_fill_rect(0, SV_TILES_Y, SCOPE_LCD_WIDTH, SV_TILES_H, COLOR_BG);

    if (s_mode == SCOPE_MODE_LOGIC || m == nullptr || channels == 0u)
        return;

    char   line[52];
    size_t len  = 0u;
    uint8_t slot = 0u;

    for (uint8_t ch = 0u; ch < SCOPE_ADC_CHANNELS; ++ch) {
        if ((channel_mask & (1u << ch)) == 0u)
            continue;
        if (slot >= channels)
            break;

        char fb[12];
        format_freq(fb, sizeof(fb), m[slot].freq_hz);

        const int n = snprintf(line + len, sizeof(line) - len, "%sC%u %s",
                               (len == 0u) ? "" : "  ", (unsigned)(ch + 1u),
                               fb);
        if (n <= 0)
            break;
        len += (size_t)n;
        if (len >= sizeof(line) - 14u)
            break;
        ++slot;
    }

    st7789_draw_string(4, (uint16_t)(SV_TILES_Y + 3u), line, COLOR_TEXT);
}

static void draw_softkeys(void)
{
    st7789_fill_rect(0, SV_SOFTKEY_Y, SCOPE_LCD_WIDTH, SV_SOFTKEY_H, COLOR_BG);
    st7789_draw_string(8, (uint16_t)(SV_SOFTKEY_Y + 6u), "AUTO", COLOR_TEXT_DIM);
    st7789_draw_string(104, (uint16_t)(SV_SOFTKEY_Y + 6u), "TRIG", COLOR_TEXT_DIM);
    st7789_draw_string(192, (uint16_t)(SV_SOFTKEY_Y + 6u), "MENU", COLOR_TEXT_DIM);
}

/* ---- public API ----------------------------------------------------- */
void scope_view_init(void)
{
    s_mode         = SCOPE_MODE_SCOPE;
    s_channel_mask = 0x01u;
    s_running      = true;
}

void scope_view_draw_chrome(scope_capture_mode_t mode, uint8_t channel_mask,
                            bool running)
{
    s_mode         = mode;
    s_channel_mask = channel_mask;
    s_running      = running;

    draw_header();

    st7789_fill_rect(SV_WAVE_X, SV_WAVE_Y, SV_WAVE_W, SV_WAVE_H, COLOR_BG);
    if (s_mode == SCOPE_MODE_LOGIC)
        draw_lane_separators();
    else
        draw_grid_int();

    draw_scale_row(0u);
    draw_tiles(nullptr, 0u, 0u);
    draw_softkeys();
}

void scope_view_set_running(bool running)
{
    if (running == s_running)
        return;
    s_running = running;
    st7789_fill_rect(200, SV_HEADER_Y, 40u, SV_HEADER_H, COLOR_BG);
    st7789_draw_string(204, 5, s_running ? "RUN" : "STOP",
                       s_running ? COLOR_RUN : COLOR_TRIG);
}

void scope_view_draw_scope(const uint16_t *samples, uint16_t count,
                           uint8_t channels, uint8_t channel_mask)
{
    if (samples == nullptr || count == 0u || channels == 0u)
        return;

    st7789_fill_rect(SV_WAVE_X, SV_WAVE_Y, SV_WAVE_W, SV_WAVE_H, COLOR_BG);
    draw_grid_int();

    uint16_t col_min[SV_WAVE_W];
    uint16_t col_max[SV_WAVE_W];

    uint8_t slot = 0u;
    for (uint8_t ch = 0u; ch < SCOPE_ADC_CHANNELS; ++ch) {
        if ((channel_mask & (1u << ch)) == 0u)
            continue;
        if (slot >= channels)
            break;

        memset(col_min, 0xFF, sizeof(col_min));
        memset(col_max, 0x00, sizeof(col_max));

        for (uint16_t s = 0u; s < count; ++s) {
            const uint16_t col = (uint16_t)(((uint32_t)s * SV_WAVE_W) / count);
            const uint16_t v   = samples[(uint32_t)s * channels + slot];
            if (v < col_min[col]) col_min[col] = v;
            if (v > col_max[col]) col_max[col] = v;
        }

        const uint16_t color = channel_color(slot);
        for (uint16_t c = 0u; c < SV_WAVE_W; ++c) {
            if (col_min[c] == 0xFFFFu)
                continue;
            const uint16_t y_hi =
                (uint16_t)(SV_WAVE_Y + SV_WAVE_H - 1u -
                           ((uint32_t)col_max[c] * (SV_WAVE_H - 1u)) / 4095u);
            const uint16_t y_lo =
                (uint16_t)(SV_WAVE_Y + SV_WAVE_H - 1u -
                           ((uint32_t)col_min[c] * (SV_WAVE_H - 1u)) / 4095u);
            st7789_vline(c, y_lo, y_hi, color);
        }
        ++slot;
    }
}

void scope_view_draw_logic(const uint32_t *samples, uint16_t count,
                           uint8_t lane_mask)
{
    if (samples == nullptr || count == 0u)
        return;

    st7789_fill_rect(SV_WAVE_X, SV_WAVE_Y, SV_WAVE_W, SV_WAVE_H, COLOR_BG);
    draw_lane_separators();

    const uint16_t x0    = SV_LOGIC_LABEL_W;
    const uint16_t width = (uint16_t)(SV_WAVE_W - SV_LOGIC_LABEL_W);

    for (uint8_t lane = 0u; lane < SV_LOGIC_LANES; ++lane) {
        const uint16_t lane_y = (uint16_t)(SV_WAVE_Y + lane * SV_LOGIC_LANE_H);
        const uint16_t y_hi   = (uint16_t)(lane_y + 2u);
        const uint16_t y_lo   = (uint16_t)(lane_y + SV_LOGIC_LANE_H - 3u);

        char lbl[4];
        snprintf(lbl, sizeof(lbl), "D%u", (unsigned)lane);
        st7789_draw_string(1, (uint16_t)(lane_y + (SV_LOGIC_LANE_H - 7u) / 2u),
                           lbl, ((lane_mask & (1u << lane)) != 0u)
                                    ? COLOR_TEXT_DIM
                                    : COLOR_GRID);

        if ((lane_mask & (1u << lane)) == 0u)
            continue;

        const uint32_t mask   = (1u << lane);
        bool           seen   = false;
        uint16_t       prev_y = y_lo;

        for (uint16_t x = 0u; x < width; ++x) {
            uint16_t s0 = (uint16_t)(((uint32_t)x * count) / width);
            uint16_t s1 = (uint16_t)(((uint32_t)(x + 1u) * count) / width);
            if (s1 <= s0) s1 = (uint16_t)(s0 + 1u);
            if (s1 > count) s1 = count;

            bool high = false;
            for (uint16_t s = s0; s < s1; ++s) {
                if ((samples[s] & mask) != 0u) {
                    high = true;
                    break;
                }
            }

            const uint16_t level_top = high ? y_hi : y_lo;
            const uint16_t px        = (uint16_t)(x0 + x);

            if (!seen) {
                st7789_vline(px, level_top, (uint16_t)(level_top + 1u),
                             COLOR_TRACE);
            } else {
                const uint16_t a = (level_top < prev_y) ? level_top : prev_y;
                const uint16_t b = (level_top < prev_y) ? prev_y : level_top;
                st7789_vline(px, a, (uint16_t)(b + 1u), COLOR_TRACE);
            }

            seen   = true;
            prev_y = level_top;
        }
    }
}

void scope_view_draw_measurements(const scope_measurements_t *per_channel,
                                  uint8_t channels, uint8_t channel_mask,
                                  uint32_t rate)
{
    draw_scale_row(rate);
    draw_tiles(per_channel, channels, channel_mask);
}
