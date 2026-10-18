#include "lvgl_port.h"

#include "board_config.h"
#include "st7789.h"

#include "pico/stdlib.h"
#include "pico/time.h"

/* ---- memory budget ------------------------------------------------
   Two allocations matter here.

   The draw buffer below is done in partial mode: LVGL renders one band at
   a time and calls the flush callback, so the peak cost is the band, not
   the screen. 40 rows of 240 is 19200 bytes, which is four refreshes per
   full repaint — few enough that a scrolling text row still looks smooth.

   LVGL's object heap is separate and lives in include/lv_conf.h
   (LV_MEM_SIZE). It is kept at 32 KiB because the capture path already
   owns most of the RP2040 SRAM; a single screen of labels, a chart and a
   softkey row fits comfortably inside that.
*/
#define LVGL_PORT_DRAW_ROWS 40u

static uint16_t s_draw_buf[SCOPE_LCD_WIDTH * LVGL_PORT_DRAW_ROWS];
static lv_display_t *s_disp = nullptr;
static bool s_ready = false;

/* LVGL keeps its own millisecond clock; pico/time gives us one for free
   and it survives sleep, unlike a counter incremented in the main loop. */
static uint32_t lvgl_port_tick(void)
{
    return (uint32_t)to_ms_since_boot(get_absolute_time());
}

/* LVGL hands us native-order RGB565, which is what st7789_blit expects:
   the driver owns the panel byte ordering, not the widget layer. */
static void lvgl_port_flush(lv_display_t *disp, const lv_area_t *area,
                            uint8_t *px_map)
{
    const uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
    const uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

    st7789_blit((uint16_t)area->x1, (uint16_t)area->y1, w, h,
                (const uint16_t *)px_map);

    lv_display_flush_ready(disp);
}

bool lvgl_port_init(void)
{
    if (s_ready)
        return true;

    lv_init();
    lv_tick_set_cb(lvgl_port_tick);

    s_disp = lv_display_create(SCOPE_LCD_WIDTH, SCOPE_LCD_HEIGHT);
    if (s_disp == nullptr)
        return false;

    lv_display_set_default(s_disp);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, lvgl_port_flush);
    lv_display_set_buffers(s_disp, s_draw_buf, nullptr, sizeof(s_draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    s_ready = true;
    return true;
}

uint32_t lvgl_port_handler(void)
{
    if (!s_ready)
        return 100u;
    return lv_timer_handler();
}

bool lvgl_port_ready(void)
{
    return s_ready;
}

lv_display_t *lvgl_port_display(void)
{
    return s_disp;
}
