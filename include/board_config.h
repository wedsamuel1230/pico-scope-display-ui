/**
 * board_config.h — single source of truth for the rev-a pin map
 *
 * Every pin used by the firmware is declared here once. If a pin moves,
 * change it here and in docs/WIRING_GUIDE.md and hardware/rev-a/README.md.
 */
#ifndef SCOPE_BOARD_CONFIG_H
#define SCOPE_BOARD_CONFIG_H

#include "pico.h"

/* ------------------------------------------------------------------ */
/* Analog inputs (ADC)                                                 */
/* ------------------------------------------------------------------ */
/* Three ADC channels, captured round-robin and interleaved.
 * GPIO26/27/28 = ADC0/ADC1/ADC2. ADC3 (GPIO29) is deliberately NOT
 * used: on a stock Pico it is hard-wired to the VSYS/3 sense divider. */
#define SCOPE_ADC_BASE_GPIO    26u   /* GPIO26 = ADC0 .. GPIO28 = ADC2 */
#define SCOPE_ADC_CHANNELS      3u
#define SCOPE_ADC_MAX_CHANNELS  4u   /* hardware limit of the ADC mux   */
#define SCOPE_ADC_FULL_SCALE 0x0FFFu /* 12-bit converter               */
#define SCOPE_ADC_GPIO         26u   /* legacy alias: first channel     */
#define SCOPE_ADC_INPUT         0u   /* legacy alias: first channel     */
#define SCOPE_ADC_VREF_MV    3300u

/* ------------------------------------------------------------------ */
/* Logic analyzer inputs                                               */
/* ------------------------------------------------------------------ */
/* Eight digital lines on a contiguous GPIO window: the PIO "in pins"
 * source must be contiguous, so GPIO5..12 was chosen. These pins are
 * unused by the LCD, buttons, LED and ADC.                            */
#define SCOPE_LOGIC_BASE_GPIO    5u
#define SCOPE_LOGIC_CHANNELS     8u
#define SCOPE_LOGIC_PIN_MASK  0xFFu  /* low byte of each 32-bit word    */

/* ------------------------------------------------------------------ */
/* Status LED                                                          */
/* ------------------------------------------------------------------ */
#define SCOPE_LED_GPIO 25u

/* ------------------------------------------------------------------ */
/* ST7789 LCD on SPI0                                                  */
/* ------------------------------------------------------------------ */
#define SCOPE_LCD_SPI             spi0
#define SCOPE_LCD_SPI_SCK_GPIO    18u
#define SCOPE_LCD_SPI_MOSI_GPIO   19u /* labelled SDA on many modules    */
#define SCOPE_LCD_SPI_BAUDRATE 31250000u
#define SCOPE_LCD_CS_GPIO         15u
#define SCOPE_LCD_DC_GPIO         16u /* 0 = command, 1 = data           */
#define SCOPE_LCD_RST_GPIO        14u
#define SCOPE_LCD_BL_GPIO         13u
#define SCOPE_LCD_MIRROR           0
#define SCOPE_LCD_WIDTH          240u
#define SCOPE_LCD_HEIGHT         320u

/* ------------------------------------------------------------------ */
/* Front-panel buttons                                                 */
/* ------------------------------------------------------------------ */
#define SCOPE_BTN_AUTO_GPIO 2u
#define SCOPE_BTN_TRIG_GPIO 3u
#define SCOPE_BTN_MENU_GPIO 4u
#define SCOPE_BTN_COUNT     3u

#endif /* SCOPE_BOARD_CONFIG_H */
