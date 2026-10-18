/**
 * main.cpp — Pico Scope Display UI firmware top level (protocol v2)
 *
 * Two capture modes share one sample buffer:
 *   SCOPE  analog, 1..SCOPE_ADC_CHANNELS ADC channels (GPIO26/27/28),
 *          converted round-robin and stored interleaved
 *   LOGIC  SCOPE_LOGIC_CHANNELS digital lanes on GPIO5..12 (PIO + DMA)
 *
 * State machine per docs/IMPLEMENTATION_GUIDE.md:
 *   BOOT -> IDLE -> ARM -> CAPTURE -> PROCESS -> DRAW -> [USB SEND] -> IDLE
 *
 * There is no analog front end, so every amplitude the firmware reports
 * is an ADC code and every capture carries SCOPE_FLAG_NOT_CALIB.
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "board_config.h"
#include "message_protocol.h"
#include "scope_config.h"
#include "scope_types.h"
#include "capture/adc_capture.h"
#include "capture/deinterleave.h"
#include "capture/logic_capture.h"
#include "capture/trigger.h"
#include "display/scope_view.h"
#include "display/st7789.h"
#include "measurements.h"

#if SCOPE_ENABLE_USB_CDC
#include "protocol/usb_link.h"
#endif

/* ------------------------------------------------------------------ */
/* Shared capture buffer                                               */
/* ------------------------------------------------------------------ */
/* SCOPE stores count * channels u16 codes; LOGIC stores count 32-bit
 * PIO snapshots, which is the larger of the two at 32 KiB.            */
static union {
    uint16_t analog[SCOPE_MAX_SAMPLE_COUNT * SCOPE_ADC_CHANNELS];
    uint32_t logic[SCOPE_MAX_LOGIC_SAMPLES];
} s_buf;

static scope_set_config_t s_cfg;
static bool               s_lcd_ok           = false;
static bool               s_usb_run_requested = false;

typedef enum {
    ST_BOOT = 0,
    ST_IDLE,
    ST_ARM,
    ST_CAPTURE,
    ST_PROCESS,
    ST_DRAW,
#if SCOPE_ENABLE_USB_CDC
    ST_USB_SEND,
#endif
} scope_state_t;

static scope_state_t s_state = ST_BOOT;

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */
static void config_defaults(void)
{
    s_cfg.sample_rate     = SCOPE_DEFAULT_RATE_HZ;
    s_cfg.sample_count    = SCOPE_SAMPLE_COUNT;
    s_cfg.trigger_level   = (uint16_t)(SCOPE_ADC_FULL_SCALE / 2u);
    s_cfg.edge            = (uint8_t)SCOPE_EDGE_RISING;
    s_cfg.trigger_mode    = (uint8_t)SCOPE_TRIG_MODE_AUTO;
    s_cfg.capture_mode    = (uint8_t)SCOPE_MODE_SCOPE;
    s_cfg.channel_mask    = 0x01u;
    s_cfg.trigger_channel = SCOPE_TRIG_CHANNEL_NONE;
}

/* Clamp a requested configuration in place and report what was applied.
 * Out-of-range values are clamped (never rejected), so the host always
 * learns the real setting from the ACK.                               */
static uint8_t config_clamp(scope_set_config_t *cfg, uint32_t *applied_rate,
                            uint16_t *applied_count)
{
    if (cfg->capture_mode > (uint8_t)SCOPE_MODE_LOGIC)
        cfg->capture_mode = (uint8_t)SCOPE_MODE_SCOPE;
    if (cfg->trigger_mode > (uint8_t)SCOPE_TRIG_MODE_NORMAL)
        cfg->trigger_mode = (uint8_t)SCOPE_TRIG_MODE_AUTO;
    if (cfg->edge > (uint8_t)SCOPE_EDGE_FALLING)
        cfg->edge = (uint8_t)SCOPE_EDGE_RISING;

    if (cfg->capture_mode == (uint8_t)SCOPE_MODE_LOGIC) {
        if (cfg->sample_rate < SCOPE_MIN_LOGIC_RATE_HZ)
            cfg->sample_rate = SCOPE_MIN_LOGIC_RATE_HZ;
        if (cfg->sample_rate > SCOPE_MAX_LOGIC_RATE_HZ)
            cfg->sample_rate = SCOPE_MAX_LOGIC_RATE_HZ;
        if (cfg->sample_count < SCOPE_MIN_LOGIC_SAMPLES)
            cfg->sample_count = SCOPE_MIN_LOGIC_SAMPLES;
        if (cfg->sample_count > SCOPE_MAX_LOGIC_SAMPLES)
            cfg->sample_count = SCOPE_MAX_LOGIC_SAMPLES;

        cfg->channel_mask  = (uint8_t)SCOPE_LOGIC_PIN_MASK;
        cfg->trigger_level = 0u;

        if (cfg->trigger_channel != SCOPE_TRIG_CHANNEL_NONE &&
            cfg->trigger_channel >= SCOPE_LOGIC_CHANNELS) {
            cfg->trigger_channel = SCOPE_TRIG_CHANNEL_NONE;
        }

        logic_capture_set_rate(cfg->sample_rate);
        *applied_rate = logic_capture_actual_rate();
    } else {
        if (cfg->sample_rate < SCOPE_MIN_RATE_HZ)
            cfg->sample_rate = SCOPE_MIN_RATE_HZ;
        if (cfg->sample_rate > SCOPE_MAX_RATE_HZ)
            cfg->sample_rate = SCOPE_MAX_RATE_HZ;
        if (cfg->sample_count < SCOPE_MIN_SAMPLE_COUNT)
            cfg->sample_count = SCOPE_MIN_SAMPLE_COUNT;
        if (cfg->sample_count > SCOPE_MAX_SAMPLE_COUNT)
            cfg->sample_count = SCOPE_MAX_SAMPLE_COUNT;

        cfg->channel_mask &= (uint8_t)((1u << SCOPE_ADC_CHANNELS) - 1u);
        if (cfg->channel_mask == 0u)
            cfg->channel_mask = 0x01u;

        if (cfg->trigger_channel != SCOPE_TRIG_CHANNEL_NONE) {
            if (cfg->trigger_channel >= SCOPE_ADC_CHANNELS ||
                (cfg->channel_mask & (1u << cfg->trigger_channel)) == 0u) {
                cfg->trigger_channel = SCOPE_TRIG_CHANNEL_NONE;
            }
        }

        adc_capture_set_rate(cfg->sample_rate);
        *applied_rate = adc_capture_actual_rate();
    }

    *applied_count = cfg->sample_count;
    return SCOPE_ACK_OK;
}

/* ------------------------------------------------------------------ */
/* Capture + local render                                              */
/* ------------------------------------------------------------------ */
static uint8_t first_enabled_slot(uint8_t mask)
{
    for (uint8_t ch = 0u; ch < SCOPE_ADC_CHANNELS; ++ch) {
        if (mask & (1u << ch))
            return adc_slot_for_channel(mask, ch);
    }
    return 0u;
}

static void capture_once(uint8_t *flags_out, uint16_t *trigger_out,
                         uint32_t *rate_out, uint8_t *channels_out)
{
    uint8_t  flags = (uint8_t)(SCOPE_FLAG_NONE | SCOPE_FLAG_NOT_CALIB);
    uint16_t trig  = SCOPE_TRIGGER_NONE;
    uint32_t rate  = 0u;
    uint8_t  chans = 0u;

    const scope_edge_t edge = (scope_edge_t)s_cfg.edge;

    if (s_cfg.capture_mode == (uint8_t)SCOPE_MODE_LOGIC) {
        if (logic_capture_ready()) {
            logic_capture_start(s_buf.logic, s_cfg.sample_count);
            const logic_capture_status_t st = logic_capture_wait();
            if (st != LOGIC_CAP_OK)
                flags |= SCOPE_FLAG_OVERFLOW;

            rate  = logic_capture_actual_rate();
            chans = (uint8_t)SCOPE_LOGIC_CHANNELS;

            if (s_cfg.trigger_channel != SCOPE_TRIG_CHANNEL_NONE) {
                trig = trigger_logic_find(s_buf.logic, s_cfg.sample_count,
                                          s_cfg.trigger_channel, edge);
                if (trig == SCOPE_TRIGGER_NONE)
                    flags |= SCOPE_FLAG_NO_TRIGGER;
            }

            if (s_lcd_ok) {
                scope_view_draw_logic(s_buf.logic, s_cfg.sample_count,
                                      (uint8_t)SCOPE_LOGIC_PIN_MASK);
            }
        } else {
            flags |= SCOPE_FLAG_OVERFLOW;
        }
    } else {
        const uint8_t  mask  = s_cfg.channel_mask;
        const uint16_t count = s_cfg.sample_count; /* per channel       */

        adc_capture_start(s_buf.analog, count, mask);
        const adc_capture_status_t st = adc_capture_wait();
        if (st != ADC_CAP_OK)
            flags |= SCOPE_FLAG_OVERFLOW;

        chans = adc_capture_last_channels();
        if (chans == 0u)
            chans = adc_capture_channels_for_mask(mask);
        rate = adc_capture_channel_rate();

        uint8_t trig_slot;
        if (s_cfg.trigger_channel == SCOPE_TRIG_CHANNEL_NONE) {
            trig_slot = first_enabled_slot(mask);
        } else {
            trig_slot = adc_slot_for_channel(mask, s_cfg.trigger_channel);
            if (trig_slot == 0xFFu)
                trig_slot = first_enabled_slot(mask);
        }

        trig = trigger_find_strided(s_buf.analog + trig_slot, count, chans,
                                    s_cfg.trigger_level, edge,
                                    (uint8_t)SCOPE_TRIGGER_HYSTERESIS);
        if (trig == SCOPE_TRIGGER_NONE)
            flags |= SCOPE_FLAG_NO_TRIGGER;

        if (s_lcd_ok) {
            scope_view_draw_scope(s_buf.analog, count, chans, mask);

            scope_measurements_t m[SCOPE_ADC_CHANNELS];
            memset(m, 0, sizeof(m));

            for (uint8_t ch = 0u; ch < SCOPE_ADC_CHANNELS; ++ch) {
                const uint8_t slot = adc_slot_for_channel(mask, ch);
                if (slot == 0xFFu)
                    continue;
                measurements_compute_strided(s_buf.analog + slot, count, chans,
                                             rate, s_cfg.trigger_level, edge,
                                             &m[slot]);
            }

            scope_view_draw_measurements(m, chans, mask, rate);
        }
    }

    if (flags_out)    *flags_out = flags;
    if (trigger_out)  *trigger_out = trig;
    if (rate_out)     *rate_out = rate;
    if (channels_out) *channels_out = chans;
}

/* ------------------------------------------------------------------ */
/* Board bring-up                                                      */
/* ------------------------------------------------------------------ */
static void board_bring_up(void)
{
    stdio_init_all();

    adc_capture_init();
    logic_capture_init();

    gpio_init(SCOPE_LED_GPIO);
    gpio_set_dir(SCOPE_LED_GPIO, GPIO_OUT);
    gpio_put(SCOPE_LED_GPIO, 0);

    const uint btn_pins[SCOPE_BTN_COUNT] = {
        SCOPE_BTN_AUTO_GPIO, SCOPE_BTN_TRIG_GPIO, SCOPE_BTN_MENU_GPIO
    };
    for (uint i = 0u; i < SCOPE_BTN_COUNT; ++i) {
        gpio_init(btn_pins[i]);
        gpio_set_dir(btn_pins[i], GPIO_IN);
        gpio_pull_up(btn_pins[i]);
    }

    s_lcd_ok = st7789_init();
    if (s_lcd_ok) {
        scope_view_init();
        scope_view_draw_chrome((scope_capture_mode_t)s_cfg.capture_mode,
                               s_cfg.channel_mask, false);
    } else {
        /* No panel detected: leave the LED lit so the fault is visible. */
        gpio_put(SCOPE_LED_GPIO, 1);
    }
}

/* ------------------------------------------------------------------ */
/* USB request handling                                                */
/* ------------------------------------------------------------------ */
#if SCOPE_ENABLE_USB_CDC
static void handle_usb(void)
{
    usb_link_request_t req;
    req.event = USB_LINK_NONE;
    req.seq   = 0u;
    memset(&req.cfg, 0, sizeof(req.cfg));

    usb_link_poll(&req);

    switch (req.event) {
    case USB_LINK_HELLO_REQ:
        usb_link_send_hello_rsp();
        break;

    case USB_LINK_SET_CONFIG: {
        scope_set_config_t applied = req.cfg;
        uint32_t applied_rate  = 0u;
        uint16_t applied_count = 0u;

        const uint8_t status =
            config_clamp(&applied, &applied_rate, &applied_count);

        const bool chrome_dirty =
            (applied.capture_mode != s_cfg.capture_mode) ||
            (applied.channel_mask != s_cfg.channel_mask);

        s_cfg = applied;

        if (s_lcd_ok && chrome_dirty) {
            scope_view_draw_chrome((scope_capture_mode_t)s_cfg.capture_mode,
                                   s_cfg.channel_mask, s_usb_run_requested);
        }

        usb_link_send_ack(req.seq, status, applied_rate, applied_count);
        break;
    }

    case USB_LINK_CAPTURE_REQ:
        s_usb_run_requested = true;
        break;

    case USB_LINK_STOP:
        s_usb_run_requested = false;
        adc_capture_abort();
        logic_capture_abort();
        if (s_lcd_ok)
            scope_view_set_running(false);
        break;

    case USB_LINK_NONE:
    default:
        break;
    }
}
#endif

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    config_defaults();
    board_bring_up();

#if SCOPE_ENABLE_USB_CDC
    usb_link_init();
#endif

    s_state = ST_IDLE;

    while (true) {
#if SCOPE_ENABLE_USB_CDC
        handle_usb();
#endif

        uint8_t  flags = SCOPE_FLAG_NONE;
        uint16_t trig  = SCOPE_TRIGGER_NONE;
        uint32_t rate  = 0u;
        uint8_t  chans = 0u;

        if (!s_usb_run_requested) {
            /* Nothing was requested. Hold the last LCD frame instead of
             * repainting the panel every loop, which refreshed it ~20x/s. */
            s_state = ST_IDLE;
            sleep_ms(20);
            continue;
        }

        s_state = ST_ARM;
        if (s_lcd_ok)
            scope_view_set_running(true);

        s_state = ST_CAPTURE;
        capture_once(&flags, &trig, &rate, &chans);

#if SCOPE_ENABLE_USB_CDC
        if (s_usb_run_requested) {
            s_state = ST_USB_SEND;
            if (s_cfg.capture_mode == (uint8_t)SCOPE_MODE_LOGIC) {
                usb_link_send_logic_data(s_buf.logic, s_cfg.sample_count,
                                         trig, flags, rate);
            } else {
                usb_link_send_capture_data(s_buf.analog, s_cfg.sample_count,
                                           trig, flags, rate, chans,
                                           s_cfg.capture_mode);
            }
            s_usb_run_requested = false;
        }
#endif

        gpio_put(SCOPE_LED_GPIO, !gpio_get(SCOPE_LED_GPIO));
        s_state = ST_IDLE;
        sleep_ms(50);
    }
}
