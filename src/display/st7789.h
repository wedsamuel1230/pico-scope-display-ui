/**
 * st7789.h — ST7789 240x320 driver (hardware SPI, window writes)
 *
 * No full framebuffer (150 KiB would not fit alongside capture data):
 * all drawing is window-based RGB565 strips, per IMPLEMENTATION_GUIDE.md.
 */
#ifndef SCOPE_ST7789_H
#define SCOPE_ST7789_H

#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"

/* RGB565 colours used by scope_view                                    */
#define COLOR_BG        0x0000u  /* black                             */
#define COLOR_GRID      0x3186u  /* dim blue-gray                     */
#define COLOR_TRACE     0xAFE0u  /* lime-yellow                       */
#define COLOR_TEXT      0xFFFFu  /* white                             */
#define COLOR_TEXT_DIM  0x8410u  /* gray                              */
#define COLOR_CH1       0xFC00u  /* amber  - scope trace channel 1    */
#define COLOR_CH2       0x07FFu  /* cyan   - scope trace channel 2    */
#define COLOR_CH3       0xF81Fu  /* magenta- scope trace channel 3    */
#define COLOR_RUN       0x07E0u  /* green                             */
#define COLOR_TRIG      0xF800u  /* red                               */

bool st7789_init(void);    /* false = init sequence failed             */

/* Streaming fills: chunk-safe (no full-screen framebuffer).            */
void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color);

/* Draw a vertical strip of pixels (columns of the trace).              */
void st7789_vline(uint16_t x, uint16_t y0, uint16_t y1, uint16_t color);

/* 5x7 ASCII text (subset font, see st7789_font.c)                      */
void st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t color);
void st7789_draw_string(uint16_t x, uint16_t y, const char *s,
                        uint16_t color);

#endif /* SCOPE_ST7789_H */
