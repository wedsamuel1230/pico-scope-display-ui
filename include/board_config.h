/**
 * board_config.h — rev-a pin / peripheral assignment (single source of truth)
 *
 * Pico Scope Display UI (v1). See docs/IMPLEMENTATION_GUIDE.md layers table:
 * this header is the "board_config" layer consumed by adc_capture, st7789
 * and main. Keep every raw pin number here; modules reference the symbolic
 * names so a carrier-board rev (hardware/rev-a) only edits this file.
 *
 * CH1 ADC: RP2040 ADC channel 0 on GPIO26 (doc v1 scope: 1 channel).
 * LCD:     ST7789 240x320 on hardware SPI0, window-based RGB565 writes.
 * Buttons: 3 soft keys (AUTO/TRIG/MENU concept, PROMPT-240x320-v2.md).
 */
#ifndef SCOPE_BOARD_CONFIG_H
#define SCOPE_BOARD_CONFIG_H

#include "pico.h"

/* ------------------------------------------------------------------ */
/* ADC — channel 1 input                                              */
/* ------------------------------------------------------------------ */
#define SCOPE_ADC_GPIO          26u   /* GPIO26 = ADC0 */
#define SCOPE_ADC_INPUT         0u    /* adc_select_input(0) */
#define SCOPE_ADC_VREF_MV       3300u /* nominal 3.3 V (no AFE yet) */

/* ------------------------------------------------------------------ */
/* LED (capture/status heartbeat)                                     */
/* ------------------------------------------------------------------ */
#define SCOPE_LED_GPIO          25u   /* on-board LED on Pico W also ok */

/* ------------------------------------------------------------------ */
/* LCD — ST7789 240x320 on SPI0                                       */
/* ------------------------------------------------------------------ */
#define SCOPE_LCD_SPI           spi0
#define SCOPE_LCD_SPI_SCK_GPIO  18u
#define SCOPE_LCD_SPI_MOSI_GPIO 19u  /* labeled "SDA" on many modules   */
#define SCOPE_LCD_SPI_BAUDRATE  31'250'000u /* ~SPI clk /4 safety first*/
#define SCOPE_LCD_CS_GPIO       15u   /* active low                    */
#define SCOPE_LCD_DC_GPIO       16u   /* 0=command 1=data              */
#define SCOPE_LCD_RST_GPIO      14u   /* active low, pulse >10 ms      */
#define SCOPE_LCD_BL_GPIO       13u   /* backlight, PWM or plain high  */
#define SCOPE_LCD_MIRROR        0     /* MADCTL row/col flip tune      */

/* Scan order used by scope_view: portrait 240(x) x 320(y).           */
#define SCOPE_LCD_WIDTH         240u
#define SCOPE_LCD_HEIGHT        320u

/* ------------------------------------------------------------------ */
/* Soft-key buttons (active low to 3V3 with internal pull-ups)         */
/* ------------------------------------------------------------------ */
#define SCOPE_BTN_AUTO_GPIO     2u
#define SCOPE_BTN_TRIG_GPIO     3u
#define SCOPE_BTN_MENU_GPIO     4u
#define SCOPE_BTN_COUNT         3u

#endif /* SCOPE_BOARD_CONFIG_H */
