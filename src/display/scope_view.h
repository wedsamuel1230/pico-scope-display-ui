/**
 * scope_view.h — on-LCD screen composition (protocol v2)
 *
 * Layout (240x320 portrait ST7789):
 *   y   0..17  header: mode, channel badges, RUN/STOP
 *   y  18..259 wave raster: SCOPE = overlaid traces, LOGIC = 8 lanes
 *   y 261..277 scale row: C/DIV RAW (no AFE, so codes not volts)
 *   y 279..291 measurement tiles
 *   y 300..318 soft keys
 *
 * The font covers ASCII 0x20..0x5A only, so every label here must be
 * UPPERCASE.
 */
#ifndef SCOPE_SCOPE_VIEW_H
#define SCOPE_SCOPE_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "measurements.h"
#include "scope_config.h"
#include "scope_types.h"

/* ---- geometry ------------------------------------------------------- */
#define SV_HEADER_Y   0u
#define SV_HEADER_H   18u
#define SV_WAVE_Y     18u
#define SV_WAVE_H     242u
#define SV_WAVE_X     0u
#define SV_WAVE_W     240u
#define SV_SCALE_Y    261u
#define SV_SCALE_H    17u
#define SV_TILES_Y    279u
#define SV_TILES_H    13u
#define SV_SOFTKEY_Y  300u
#define SV_SOFTKEY_H  19u
#define SV_DIV_W      30u
#define SV_DIV_H      40u

/* Logic lanes: 8 lanes stacked inside the wave raster.                */
#define SV_LOGIC_LANES    SCOPE_LOGIC_CHANNELS
#define SV_LOGIC_LANE_H   (SV_WAVE_H / SV_LOGIC_LANES) /* 30 px           */
#define SV_LOGIC_LABEL_W  12u                          /* lane label gutter */

/* ---- lifecycle ------------------------------------------------------ */
void scope_view_init(void);

/**
 * Repaint the static chrome and the header. Call on boot and whenever
 * the mode or the enabled channels change (the wave raster is cleared).
 */
void scope_view_draw_chrome(scope_capture_mode_t mode, uint8_t channel_mask,
                            bool running);

/** Repaint only the RUN/STOP indicator. */
void scope_view_set_running(bool running);

/** Clear the wave raster, redraw the grid and overlay all enabled
 * analog traces. `samples` is count * channels interleaved; slot order
 * follows the enabled bits of `channel_mask` (bit 0 = ADC0).           */
void scope_view_draw_scope(const uint16_t *samples, uint16_t count,
                           uint8_t channels, uint8_t channel_mask);

/** Clear the wave raster, draw the 8 lane separators and the lane
 * waveforms. `samples[i]` is a 32-bit PIO pin snapshot; bits 7:0 are
 * the logic lines. `lane_mask` selects which lanes are drawn.         */
void scope_view_draw_logic(const uint32_t *samples, uint16_t count,
                           uint8_t lane_mask);

/** Repaint the scale row and the measurement tiles. `per_channel` holds
 * one entry per slot in the enabled order of `channel_mask`.          */
void scope_view_draw_measurements(const scope_measurements_t *per_channel,
                                  uint8_t channels, uint8_t channel_mask,
                                  uint32_t rate);

#endif /* SCOPE_SCOPE_VIEW_H */
