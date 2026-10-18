#ifndef SCOPE_LVGL_UI_H
#define SCOPE_LVGL_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "measurements.h"
#include "scope_config.h"
#include "scope_types.h"

/* The 240x320 panel rendered with LVGL instead of the hand-rolled
   scope_view composition.  Same five operations, so the call sites in
   main.cpp swap one for one and the old code stays compiled as a
   fallback until the new screen is proven on hardware.

   Layout (portrait, 240 wide x 320 tall):

     y   0.. 21  header   mode, channel chips, RUN/STOP
     y  22..245  wave     scope traces or eight logic lanes
     y 246..269  scale    C/DIV RAW (or LOGIC NOMINAL) and the rate
     y 270..299  tiles    four measurement tiles
     y 300..319  softkeys AUTO / TRIG / MENU

   The Figma mock drew 8-9 px text in the tiles.  No real LVGL font is
   that small, so montserrat 10 (names) over 12 (values) is used and the
   rows were re-budgeted to 30 px.  Everything a user reads is UPPERCASE,
   which the LVGL fonts render happily and the old bitmap atlas could not
   go below anyway.

   No analog front end exists, so every amplitude is a raw 12-bit ADC
   code and the scale row says C/DIV, never V/DIV.                      */

bool lvgl_ui_init(void);   /* builds the screen; false if LVGL is not up */

void lvgl_ui_pump(void);   /* one refresh pass; call from the idle loop  */

void lvgl_ui_set_chrome(scope_capture_mode_t mode,
                        uint8_t channel_mask, bool running);

void lvgl_ui_set_running(bool running);

void lvgl_ui_draw_scope(const uint16_t *samples, uint16_t count,
                        uint8_t channels, uint8_t channel_mask);

void lvgl_ui_draw_logic(const uint32_t *samples, uint16_t count,
                        uint8_t lane_mask);

void lvgl_ui_draw_measurements(const scope_measurements_t *per_channel,
                               uint8_t channels, uint8_t channel_mask,
                               uint32_t rate);

#endif /* SCOPE_LVGL_UI_H */
