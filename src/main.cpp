/**
 * main.cpp — Pico Scope Display UI firmware top level
 *
 * State machine per docs/IMPLEMENTATION_GUIDE.md:
 *   BOOT → IDLE → ARM → CAPTURE → PROCESS → DRAW → [USB SEND] → IDLE
 *
 * Responsibilities:
 *  - board bring-up (ADC, LCD, LED, buttons, optional USB CDC)
 *  - single-shot capture with edge trigger (AUTO fallback)
 *  - measurements (Vpp/RMS/freq) and local ST7789 rendering
 *  - protocol-v1 serving over USB CDC (HELLO / SET_CONFIG / CAPTURE /
 *    STOP / ERROR) without ever mixing printf/CSV onto the same stream.
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "board_config.h"
#include "scope_config.h"
#include "capture/adc_capture.h"
#include "capture/trigger.h"
#include "display/st7789.h"
#include "display/scope_view.h"
#include "measurements.h"
#include "message_protocol.h"

#if SCOPE_ENABLE_USB_CDC
#include "protocol/usb_link.h"
#endif

/* ------------------------------------------------------------------ */
/* Ping-pong capture buffers: 2 x 1024 x u16 = 4 KiB                    */
static uint16_t buf_a[SCOPE_SAMPLE_COUNT];
static uint16_t buf_b[SCOPE_SAMPLE_COUNT];
static uint16_t *buf_live = buf_a;

static bool s_usb_run_requested = false; /* CAPTURE_REQ outstanding      */

typedef enum {
    ST_BOOT,
    ST_IDLE,
    ST_ARM,
    ST_CAPTURE,
    ST_PROCESS,
    ST_DRAW,
#if SCOPE_ENABLE_USB_CDC
    ST_USB_SEND,
#endif
} scope_state_t;

static scope_state_t state = ST_BOOT;

/* ------------------------------------------------------------------ */
static void board_bring_up(void)
{
    adc_capture_init();
    stdio_init_all(); /* debug UART only; NEVER writes to CDC bus       */

    gpio_init(SCOPE_LED_GPIO);
    gpio_set_dir(SCOPE_LED_GPIO, GPIO_OUT);

    /* buttons: active-low with internal pull-ups                       */
    const uint btns[] = { SCOPE_BTN_AUTO_GPIO, SCOPE_BTN_TRIG_GPIO,
                          SCOPE_BTN_MENU_GPIO };
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); ++i) {
        gpio_init(btns[i]);
        gpio_set_dir(btns[i], GPIO_IN);
        gpio_pull_up(btns[i]);
    }

    /* LCD is optional bring-up: failure shouldn't block capture        */
    if (!st7789_init())
        gpio_put(SCOPE_LED_GPIO, 1); /* LED latched on = LCD missing     */
    else
        scope_view_draw_chrome();
}

/* ------------------------------------------------------------------ */
static void apply_config_clamp(scope_set_config_t *cfg, uint32_t *applied_rate,
                               uint16_t *applied_count, uint8_t *status)
{
    *status = SCOPE_ACK_OK;
    if (cfg->sample_rate > SCOPE_MAX_RATE_HZ ||
        cfg->sample_rate < SCOPE_MIN_RATE_HZ) {
        cfg->sample_rate = (cfg->sample_rate > SCOPE_MAX_RATE_HZ)
                               ? SCOPE_MAX_RATE_HZ
                               : SCOPE_MIN_RATE_HZ;
        *status = SCOPE_ACK_OK; /* clamped, not rejected                   */
    }
    if (cfg->sample_count > SCOPE_MAX_SAMPLE_COUNT) {
        cfg->sample_count = SCOPE_MAX_SAMPLE_COUNT;
    }
    if (cfg->sample_count < SCOPE_MIN_SAMPLE_COUNT) {
        cfg->sample_count = SCOPE_MIN_SAMPLE_COUNT;
    }
    *applied_rate  = cfg->sample_rate;
    *applied_count = cfg->sample_count;

    adc_capture_set_rate(cfg->sample_rate);
}

/* run a full capture on `buf` and render locally                        */
static void run_local_capture(void)
{
    adc_capture_start(buf_live, SCOPE_SAMPLE_COUNT);
    const adc_capture_status_t st = adc_capture_wait();

    uint16_t trig = SCOPE_TRIGGER_NONE;
    uint8_t  flags = SCOPE_FLAG_NONE;
    if (st == ADC_CAP_FIFO_OVERFLOW)
        flags |= SCOPE_FLAG_OVERFLOW;
    else if (st == ADC_CAP_TIMEOUT) {
        /* skip processing this round                                   */
        return;
    }

    scope_measurements_t m;
    measurements_compute(buf_live, SCOPE_SAMPLE_COUNT,
                         adc_capture_actual_rate(), 2048,
                         SCOPE_EDGE_RISING, &m);

    scope_view_draw_trace(buf_live, SCOPE_SAMPLE_COUNT, false);
    (void)trig;
    (void)flags;
}

/* ------------------------------------------------------------------ */
int main(void)
{
    board_bring_up();

#if SCOPE_ENABLE_USB_CDC
    usb_link_init();
#endif

    state = ST_IDLE;

    while (true) {
#if SCOPE_ENABLE_USB_CDC
        usb_link_request_t req;
        usb_link_poll(&req);
        switch (req.event) {
        case USB_LINK_HELLO_REQ:
            usb_link_send_hello_rsp();
            break;
        case USB_LINK_SET_CONFIG: {
            uint32_t applied_rate = 0u;
            uint16_t applied_count = 0u;
            uint8_t  status = SCOPE_ACK_OK;
            apply_config_clamp(&req.cfg, &applied_rate, &applied_count,
                               &status);
            usb_link_send_ack(req.seq, status, applied_rate,
                              applied_count);
            break;
        }
        case USB_LINK_CAPTURE_REQ:
            s_usb_run_requested = true;
            break;
        case USB_LINK_STOP:
            s_usb_run_requested = false;
            adc_capture_abort();
            break;
        default:
            break;
        }
#endif

        gpio_put(SCOPE_LED_GPIO, !gpio_get(SCOPE_LED_GPIO));

        /* free-run local display; process one USB capture if pending   */
        if (s_usb_run_requested) {
            /* armed single shot for the browser                        */
            adc_capture_start(buf_live, SCOPE_SAMPLE_COUNT);
            const adc_capture_status_t st = adc_capture_wait();
            s_usb_run_requested = false;

            uint8_t  flags = SCOPE_FLAG_NONE;
            uint16_t trig  = SCOPE_TRIGGER_NONE;
            if (st == ADC_CAP_FIFO_OVERFLOW)
                flags |= SCOPE_FLAG_OVERFLOW;
            if (st != ADC_CAP_TIMEOUT) {
                trig = trigger_find(buf_live, SCOPE_SAMPLE_COUNT,
                                    2048, SCOPE_EDGE_RISING,
                                    SCOPE_TRIGGER_HYSTERESIS);
                if (trig == SCOPE_TRIGGER_NONE)
                    flags |= SCOPE_FLAG_NO_TRIGGER;
                flags |= SCOPE_FLAG_NOT_CALIB;
#if SCOPE_ENABLE_USB_CDC
                usb_link_send_capture_data(buf_live, SCOPE_SAMPLE_COUNT,
                                           trig, flags,
                                           adc_capture_actual_rate());
#endif
            }
        }

        /* local display loop                                           */
        run_local_capture();
        sleep_ms(50);
    }
    return 0;
}
