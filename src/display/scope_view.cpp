/**
 * scope_view.cpp — oscilloscope screen composition
 */
#include "scope_view.h"

#include <string.h>

#include "board_config.h"
#include "st7789.h"

/* ---- helpers ------------------------------------------------------- */
static void draw_header(void)
{
    st7789_fill_rect(0, SV_HEADER_Y, SCOPE_LCD_WIDTH, SV_HEADER_H,
                     COLOR_BG);
    st7789_draw_string(4,  5, "SCOPE", COLOR_TEXT);
    st7789_draw_string(96, 5, "CH1",  COLOR_CH1);
    st7789_draw_string(204, 5, "RUN", COLOR_RUN);
}

/* Grid stamping: divisions are thin lines; only drawn at chrome time. */
static void draw_grid_int(void)
{
    /* horizontal division borders at each 40 px step                   */
    for (uint16_t d = 0; d <= 6u; ++d) {
        const uint16_t y =
            (uint16_t)(SV_WAVE_Y + d * SV_DIV_H - 1u);
        if (y < SV_WAVE_Y + SV_WAVE_H)
            st7789_fill_rect(SV_WAVE_X, y, SV_WAVE_W, 1u, COLOR_GRID);
    }
    /* vertical division borders every 30 px                            */
    for (uint16_t d = 0; d <= 8u; ++d) {
        const uint16_t x =
            (uint16_t)(SV_WAVE_X + d * SV_DIV_W - 1u);
        if (x < SV_WAVE_X + SV_WAVE_W)
            st7789_fill_rect(x, SV_WAVE_Y, 1u, SV_WAVE_H, COLOR_GRID);
    }
}

static void draw_scale_row(void)
{
    st7789_fill_rect(0, SV_SCALE_Y, SCOPE_LCD_WIDTH, SV_SCALE_H,
                     COLOR_BG);
    st7789_draw_string(4,   SV_SCALE_Y + 5, "V/DIV  RAW", COLOR_TEXT_DIM);
    st7789_draw_string(160, SV_SCALE_Y + 5, "US/DIV",     COLOR_TEXT_DIM);
}

static void draw_tiles(void)
{
    st7789_fill_rect(0, SV_TILES_Y, SCOPE_LCD_WIDTH, SV_TILES_H,
                     COLOR_BG);
    st7789_draw_string(4,   SV_TILES_Y + 3, "FREQ --", COLOR_TEXT);
    st7789_draw_string(120, SV_TILES_Y + 3, "VPP --",  COLOR_TEXT);
}

static void draw_softkeys(void)
{
    st7789_fill_rect(0, SV_SOFTKEY_Y, SCOPE_LCD_WIDTH, SV_SOFTKEY_H,
                     COLOR_BG);
    st7789_draw_string(8,   SV_SOFTKEY_Y + 6, "AUTO", COLOR_TEXT_DIM);
    st7789_draw_string(104, SV_SOFTKEY_Y + 6, "TRIG", COLOR_TEXT_DIM);
    st7789_draw_string(192, SV_SOFTKEY_Y + 6, "MENU", COLOR_TEXT_DIM);
}

/* ---- public --------------------------------------------------------- */
void scope_view_draw_chrome(void)
{
    draw_header();
    scope_view_clear_wave();
    draw_grid_int();
    draw_scale_row();
    draw_tiles();
    draw_softkeys();
}

void scope_view_clear_wave(void)
{
    st7789_fill_rect(SV_WAVE_X, SV_WAVE_Y, SV_WAVE_W, SV_WAVE_H,
                     COLOR_BG);
    draw_grid_int(); /* clear removes grid; redraw it                   */
}

void scope_view_draw_grid(void)
{
    draw_grid_int();
}

void scope_view_draw_trace(const uint16_t *samples, uint16_t count,
                           bool redraw_grid)
{
    if (samples == nullptr || count == 0u)
        return;

    scope_view_clear_wave();

    /* per-column min/max: map sample index to column, keep extremes.  */
    uint16_t col_min[SV_WAVE_W];
    uint16_t col_max[SV_WAVE_W];
    memset(col_min, 0xFF, sizeof(col_min));

    for (uint16_t s = 0; s < count; ++s) {
        const uint16_t col = (uint16_t)((uint32_t)s * SV_WAVE_W / count);
        const uint16_t v   = samples[s];
        if (v < col_min[col]) col_min[col] = v;
        if (v > col_max[col]) col_max[col] = v;
    }

    /* map code (0..4095) to y within the wave area: bottom = code 0    */
    for (uint16_t c = 0; c < SV_WAVE_W; ++c) {
        if (col_min[c] == 0xFFFFu)
            continue; /* no sample mapped to this column (thin capture) */
        const uint16_t y_hi =
            (uint16_t)(SV_WAVE_Y + SV_WAVE_H - 1u -
                       ((uint32_t)col_max[c] * (SV_WAVE_H - 1u)) / 4095u);
        const uint16_t y_lo =
            (uint16_t)(SV_WAVE_Y + SV_WAVE_H - 1u -
                       ((uint32_t)col_min[c] * (SV_WAVE_H - 1u)) / 4095u);
        st7789_vline(c, y_lo, y_hi, COLOR_TRACE);
    }

    if (redraw_grid)
        draw_grid_int();
}
