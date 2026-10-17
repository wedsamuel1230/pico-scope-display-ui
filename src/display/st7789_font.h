/**
 * st7789_font.h — 5x7 ASCII subset font used by scope_view
 */
#ifndef SCOPE_ST7789_FONT_H
#define SCOPE_ST7789_FONT_H

#include <stdint.h>
#include <stddef.h>

/* Returns pointer to a 5-byte glyph (column-major, LSB=top row) or
 * nullptr for characters outside the supported subset (caller draws
 * nothing or a fallback box).                                          */
const uint8_t *font5x7_glyph(char c);

#endif /* SCOPE_ST7789_FONT_H */
