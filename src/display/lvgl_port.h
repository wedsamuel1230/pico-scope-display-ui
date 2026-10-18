#ifndef SCOPE_LVGL_PORT_H
#define SCOPE_LVGL_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* Bridges LVGL to the ST7789 panel driver.
 *
 * The panel has no framebuffer of its own and the RP2040 has no room for
 * a full 240x320x2 screen either, so the port renders in partial mode and
 * hands each finished rectangle to st7789_blit(), which does the RGB565
 * byte ordering the panel expects. See st7789.h and lvgl_port.cpp for the
 * memory budget.
 */

/* Brings LVGL up and registers the display. Safe to call twice; returns
   false only when the display object could not be created. */
bool lvgl_port_init(void);

/* Runs one LVGL refresh pass. Returns the milliseconds until LVGL next
   needs attention, so a caller with nothing else to do can sleep that
   long instead of spinning. */
uint32_t lvgl_port_handler(void);

bool lvgl_port_ready(void);

/* The display object, for anything that needs to attach a screen to it. */
lv_display_t *lvgl_port_display(void);

#endif /* SCOPE_LVGL_PORT_H */
