/**
 * st7789.cpp — ST7789 240x320 LCD driver over hardware SPI
 *
 * Init sequence: SW reset → sleep out → pixel format 16-bit → MADCTL →
 * porch/inversion → display on (per IMPLEMENTATION_GUIDE.md and the
 * generic ST7789V datasheet).
 *
 * All drawing uses CASET (0x2A) / RASET (0x2B) / RAMWR (0x2C) with
 * RGB565 big-endian pixels; buffers are small rectangles only, never
 * the full 240x320 raster (would be ~153 KiB — forbidden).
 */
#include "st7789.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/structs/spi.h"
#include "pico/time.h"

#include "scope_config.h"
#include "st7789_font.h"

/* ---- panel byte order ---------------------------------------------
   The comment above says big-endian and it means it: the ST7789 takes
   RGB565 high byte first (MIPI DCS), but a uint16_t in RP2040 memory is
   low byte first, and spi_init leaves the peripheral in 8-bit frames so
   the two bytes go out in memory order. Every pixel handed to the panel
   therefore has to be swapped on the way out, which is what lcd_px()
   does at each store site. If a panel ever shows correct colour with the
   swap applied, this is the one line to flip.
   See docs/IMPLEMENTATION_GUIDE.md §"ST7789 ... 顏色次序" and the colour
   bar check in docs/WIRING_GUIDE.md.                                */
#define SCOPE_LCD_RGB565_SWAP 1

static inline uint16_t lcd_px(uint16_t v)
{
#if SCOPE_LCD_RGB565_SWAP
    return (uint16_t)((v >> 8) | (v << 8));
#else
    return v;
#endif
}

/* ---- low level ---------------------------------------------------- */
static inline void cs_select(void)   { gpio_put(SCOPE_LCD_CS_GPIO, 0); }
static inline void cs_deselect(void) { gpio_put(SCOPE_LCD_CS_GPIO, 1); }
static inline void dc_command(void)  { gpio_put(SCOPE_LCD_DC_GPIO, 0); }
static inline void dc_data(void)     { gpio_put(SCOPE_LCD_DC_GPIO, 1); }

static void write_cmd(uint8_t cmd)
{
    dc_command();
    spi_write_blocking(SCOPE_LCD_SPI, &cmd, 1);
    dc_data();
}

static void write_data(const uint8_t *data, size_t len)
{
    dc_data();
    spi_write_blocking(SCOPE_LCD_SPI, data, len);
}

/* ---- init sequence ------------------------------------------------- */
static void lcd_reset(void)
{
    gpio_put(SCOPE_LCD_RST_GPIO, 1);
    sleep_ms(10);
    gpio_put(SCOPE_LCD_RST_GPIO, 0);
    sleep_ms(20);
    gpio_put(SCOPE_LCD_RST_GPIO, 1);
    sleep_ms(120);
}

bool st7789_init(void)
{
    spi_init(SCOPE_LCD_SPI, SCOPE_LCD_SPI_BAUDRATE);
    gpio_set_function(SCOPE_LCD_SPI_SCK_GPIO,  GPIO_FUNC_SPI);
    gpio_set_function(SCOPE_LCD_SPI_MOSI_GPIO, GPIO_FUNC_SPI);

    gpio_init(SCOPE_LCD_CS_GPIO);
    gpio_init(SCOPE_LCD_DC_GPIO);
    gpio_init(SCOPE_LCD_RST_GPIO);
    gpio_init(SCOPE_LCD_BL_GPIO);
    gpio_set_dir(SCOPE_LCD_CS_GPIO,  GPIO_OUT);
    gpio_set_dir(SCOPE_LCD_DC_GPIO,  GPIO_OUT);
    gpio_set_dir(SCOPE_LCD_RST_GPIO, GPIO_OUT);
    gpio_set_dir(SCOPE_LCD_BL_GPIO,  GPIO_OUT);
    cs_deselect();
    gpio_put(SCOPE_LCD_BL_GPIO, 1); /* backlight on                 */

    lcd_reset();

    cs_select();
    write_cmd(0x01); sleep_ms(150);    /* software reset             */
    write_cmd(0x11); sleep_ms(120);    /* sleep out                  */

    const uint8_t pixfmt[1] = { 0x55 };           /* RGB565, 16-bit   */
    write_cmd(0x3A); write_data(pixfmt, 1);

    const uint8_t madctl[1] = { 0x00 }; /* portrait, top-left origin */
    write_cmd(0x36); write_data(madctl, 1);

    /* porch control (front 5, back 60)                                 */
    const uint8_t porch[5] = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
    write_cmd(0xB2); write_data(porch, 5);

    /* gate control                                                     */
    const uint8_t gctrl[1] = { 0x14 };
    write_cmd(0xB7); write_data(gctrl, 1);

    /* VCOM setting                                                     */
    const uint8_t vcom[1] = { 0x37 };
    write_cmd(0xBB); write_data(vcom, 1);

    /* LCM control                                                      */
    const uint8_t lcm[1] = { 0x2C };
    write_cmd(0xC0); write_data(lcm, 1);

    /* VDV / VRH                                                        */
    const uint8_t vdv[1]  = { 0x12 };
    write_cmd(0xC2); write_data(vdv, 1);
    const uint8_t vrh[1]  = { 0x00 };
    write_cmd(0xC3); write_data(vrh, 1);

    /* frame rate 60 Hz                                                 */
    const uint8_t fr[2] = { 0x0F, 0x1B };
    write_cmd(0xC6); write_data(fr, 2);

    /* power control 1                                                  */
    const uint8_t pwctrl1[2] = { 0xA4, 0xA1 };
    write_cmd(0xD0); write_data(pwctrl1, 2);

    /* gamma positive/negative                                          */
    const uint8_t gamma_p[14] = {
        0xD0, 0x04, 0x0D, 0x12, 0x0C, 0x4A, 0x38,
        0x38, 0x40, 0x3F, 0x35, 0x0E, 0x07, 0x11,
    };
    write_cmd(0xE0); write_data(gamma_p, 14);
    const uint8_t gamma_n[14] = {
        0xD0, 0x04, 0x0C, 0x12, 0x0C, 0x46, 0x35,
        0x44, 0x36, 0x38, 0x30, 0x0C, 0x06, 0x10,
    };
    write_cmd(0xE1); write_data(gamma_n, 14);

    write_cmd(0x13); sleep_ms(10);  /* normal display mode             */
    write_cmd(0x21);                /* display inversion on            */
    write_cmd(0x29); sleep_ms(10);  /* display on                      */
    cs_deselect();

    /* sanity fill to verify bus                                        */
    st7789_fill_rect(0, 0, SCOPE_LCD_WIDTH, SCOPE_LCD_HEIGHT, COLOR_BG);
    return true;
}

/* ---- window primitives --------------------------------------------- */
static void set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    const uint16_t x2 = (uint16_t)(x + w - 1u);
    const uint16_t y2 = (uint16_t)(y + h - 1u);
    uint8_t b4[4];

    write_cmd(0x2A); /* CASET */
    b4[0] = (uint8_t)(x >> 8);  b4[1] = (uint8_t)(x);
    b4[2] = (uint8_t)(x2 >> 8); b4[3] = (uint8_t)(x2);
    write_data(b4, 4);

    write_cmd(0x2B); /* RASET */
    b4[0] = (uint8_t)(y >> 8);  b4[1] = (uint8_t)(y);
    b4[2] = (uint8_t)(y2 >> 8); b4[3] = (uint8_t)(y2);
    write_data(b4, 4);

    write_cmd(0x2C); /* RAMWR (data follows)                            */
}

/* Static line buffer: bounded, so RAM stays ~4 KiB like capture.       */
#define FILL_CHUNK_ROWS 8u  /* 240 * 8 * 2 B = 3840 B                  */
static uint16_t fill_buf[SCOPE_LCD_WIDTH * FILL_CHUNK_ROWS];
static uint16_t col_buf[SCOPE_LCD_HEIGHT]; /* for vline: 320*2=640 B   */

void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color)
{
    if (w == 0u || h == 0u ||
        x >= SCOPE_LCD_WIDTH || y >= SCOPE_LCD_HEIGHT)
        return;
    if (x + w > SCOPE_LCD_WIDTH)  w = (uint16_t)(SCOPE_LCD_WIDTH - x);
    if (y + h > SCOPE_LCD_HEIGHT) h = (uint16_t)(SCOPE_LCD_HEIGHT - y);

    cs_select();
    uint16_t y0 = y;
    while (h > 0u) {
        uint16_t rows = (h > FILL_CHUNK_ROWS) ? FILL_CHUNK_ROWS : h;
        set_window(x, y0, w, rows);

        for (uint32_t i = 0; i < (uint32_t)w * rows; ++i)
            fill_buf[i] = lcd_px(color);
        write_data((const uint8_t *)fill_buf,
                   (size_t)w * rows * 2u);
        y0 = (uint16_t)(y0 + rows);
        h  = (uint16_t)(h - rows);
    }
    cs_deselect();
}

void st7789_vline(uint16_t x, uint16_t y0, uint16_t y1, uint16_t color)
{
    if (x >= SCOPE_LCD_WIDTH)
        return;
    if (y0 >= SCOPE_LCD_HEIGHT) y0 = (uint16_t)(SCOPE_LCD_HEIGHT - 1u);
    if (y1 >= SCOPE_LCD_HEIGHT) y1 = (uint16_t)(SCOPE_LCD_HEIGHT - 1u);
    uint16_t len = (uint16_t)(y1 - y0 + 1u);
    if (len == 0u)
        return;

    for (uint16_t i = 0; i < len; ++i)
        col_buf[i] = lcd_px(color);

    cs_select();
    set_window(x, y0, 1u, len);
    write_data((const uint8_t *)col_buf, (size_t)len * 2u);
    cs_deselect();
}

/* ---- text ----------------------------------------------------------- */
void st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t color)
{
    const uint8_t *glyph = font5x7_glyph(c);
    if (glyph == nullptr)
        return;

    uint16_t bits[5][8]; /* column-major pixel buffer                    */
    for (int col = 0; col < 5; ++col) {
        uint8_t bits_col = glyph[col];
        for (int row = 0; row < 8; ++row) {
            bits[col][row] =
                lcd_px(((bits_col >> row) & 1u) ? color : COLOR_BG);
        }
    }

    cs_select();
    for (int col = 0; col < 5; ++col) {
        set_window((uint16_t)(x + col), y, 1u, 7u);
        write_data((const uint8_t *)bits[col], 7u * 2u);
    }
    cs_deselect();
}

void st7789_draw_string(uint16_t x, uint16_t y, const char *s,
                        uint16_t color)
{
    uint16_t cx = x;
    while (*s != '\0') {
        st7789_draw_char(cx, y, *s, color);
        cx   = (uint16_t)(cx + 6u);
        s   += 1;
    }
}

/* ---- bulk blit ----------------------------------------------------
   A framebuffer-based UI (LVGL) renders into its own buffer and needs to
   push whole rectangles. `px` holds w*h native-order RGB565 words with a
   stride of w; the byte swap happens here, in row chunks sized to the
   same fill_buf the solid-fill path uses, so no second buffer is needed
   and the peak cost stays 3840 bytes. The rectangle is clamped to the
   panel like st7789_fill_rect does, and rows past the edge are skipped
   rather than wrapping.                                             */
void st7789_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 const uint16_t *px)
{
    if (w == 0u || h == 0u || px == nullptr)
        return;
    if (x >= SCOPE_LCD_WIDTH || y >= SCOPE_LCD_HEIGHT)
        return;

    uint16_t cw = w;
    uint16_t ch = h;
    if ((uint32_t)x + cw > SCOPE_LCD_WIDTH)
        cw = (uint16_t)(SCOPE_LCD_WIDTH - x);
    if ((uint32_t)y + ch > SCOPE_LCD_HEIGHT)
        ch = (uint16_t)(SCOPE_LCD_HEIGHT - y);

    cs_select();
    uint16_t y0 = y;
    uint32_t row = 0u; /* row index into the source, stride is w */
    while (ch > 0u) {
        uint16_t rows = (ch > FILL_CHUNK_ROWS) ? FILL_CHUNK_ROWS : ch;
        set_window(x, y0, cw, rows);
        uint32_t n = 0u;
        for (uint16_t r = 0u; r < rows; ++r) {
            const uint16_t *src = px + (row + r) * (uint32_t)w;
            for (uint16_t c = 0u; c < cw; ++c)
                fill_buf[n++] = lcd_px(src[c]);
        }
        write_data((const uint8_t *)fill_buf, (size_t)n * 2u);
        row = (uint16_t)(row + rows);
        y0  = (uint16_t)(y0 + rows);
        ch  = (uint16_t)(ch - rows);
    }
    cs_deselect();
}
