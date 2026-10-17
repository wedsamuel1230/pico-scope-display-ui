/**
 * scope_view.h — oscilloscope screen composition (240x320 portrait)
 *
 * Layout (PROMPT-240x320-v2.md concept):
 *   y  0..17   header   : 'SCOPE' + CH1(amber) + RUN(green)
 *   y 18..259  graticule: 8 x 6 divisions (30 x 40 px), blue-gray grid
 *   y 260..277 scale row : V/div / us/div
 *   y 278..299 tiles    : FREQ / VPP readouts
 *   y 300..319 soft keys: AUTO / TRIG / MENU
 *
 * Drawing model per IMPLEMENTATION_GUIDE.md: clear waveform area only,
 * fixed grid is NOT redrawn each frame (grid is stamped at init and
 * trace clears only the previous trace columns).
 */
#ifndef SCOPE_SCOPE_VIEW_H
#define SCOPE_SCOPE_VIEW_H

#include <stdint.h>
#include <stdbool.h>
#include "scope_config.h"
#include "measurements.h"

/* --- geometry ------------------------------------------------------- */
#define SV_HEADER_Y       0u
#define SV_HEADER_H       18u
#define SV_WAVE_Y         18u
#define SV_WAVE_H         242u   /* 6 divisions x 40 px + 2             */
#define SV_WAVE_X         0u
#define SV_WAVE_W         240u   /* 8 divisions x 30 px                 */
#define SV_SCALE_Y        261u
#define SV_SCALE_H        17u
#define SV_TILES_Y        279u
#define SV_TILES_H        13u
#define SV_SOFTKEY_Y      300u
#define SV_SOFTKEY_H      19u

#define SV_DIV_W          30u
#define SV_DIV_H          40u

/* Draw whole static chrome (header/grid/scales/soft keys). Once.       */
void scope_view_draw_chrome(void);

/* Clear only the waveform raster area (background, no grid).           */
void scope_view_clear_wave(void);

/* Stamp graticule divisions into the cleared wave area.                */
void scope_view_draw_grid(void);

/* Plot the waveform: per-column min/max downsample (240 columns).
 * Samples map linearly; sweep bottom->top. `grid` = redraw grid too.   */
void scope_view_draw_trace(const uint16_t *samples, uint16_t count,
                           bool redraw_grid);

#endif /* SCOPE_SCOPE_VIEW_H */
