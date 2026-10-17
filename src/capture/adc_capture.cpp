/**
 * adc_capture.cpp — single-shot ADC+DMA capture (RP2040 native SDK)
 *
 * Pipeline per docs/IMPLEMENTATION_GUIDE.md:
 *   adc_init() →
 *   gpio26 init input0 →
 *   set FIFO / DREQ / clock divider →
 *   clear ADC FIFO →
 *   DMA channel: 16-bit transfer, fixed-read (ADC FIFO), RAM increment →
 *   start DMA then ADC →
 *   wait with timeout →
 *   stop ADC + clear FIFO + report errors (FIFO overflow sticky).
 *
 * No floating point in the capture loop itself; the divider fixup runs
 * once at configuration time.
 */
#include "adc_capture.h"

#include <stdbool.h>
#include <stddef.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/structs/adc.h"
#include "pico/time.h"

#include "board_config.h"

/* RP2040 ADC needs 96 clk_adc cycles per conversion (SAR).             */
#define ADC_CYCLES_PER_SAMPLE   96.0f

static bool     s_inited        = false;
static int      s_dma_ch        = -1;
static uint32_t s_requested     = SCOPE_DEFAULT_RATE_HZ;
static uint32_t s_actual_rate   = 0u;
static uint16_t s_count         = 0u;
static uint16_t *s_dst          = nullptr;

/* ------------------------------------------------------------------ */
void adc_capture_init(void)
{
    if (s_inited)
        return;

    adc_init();
    adc_gpio_init(SCOPE_ADC_GPIO); /* sets pin function to ADC           */
    adc_select_input(SCOPE_ADC_INPUT);

    adc_fifo_setup(true,    /* enable FIFO                               */
                   false,   /* dreq enabled per-capture (in wait)        */
                   1,       /* 1 sample pending is enough for DMA DREQ   */
                   false,   /* errors not routed into FIFO               */
                   false);  /* no byte shift                             */

    if (s_dma_ch < 0)
        s_dma_ch = dma_claim_unused_channel(true);

    s_inited = true;
}

/* ------------------------------------------------------------------ */
void adc_capture_set_rate(uint32_t requested_rate)
{
    if (requested_rate == 0u)
        return;
    s_requested = requested_rate;
}

static void configure_clock(void)
{
    const float f_adc = (float)clock_get_hz(clk_adc); /* 48 MHz nominal */

    float period = f_adc / (float)s_requested;    /* cycles per sample  */
    if (period < ADC_CYCLES_PER_SAMPLE)           /* hardware ceiling   */
        period = ADC_CYCLES_PER_SAMPLE;

    adc_set_clkdiv(period - 1.0f); /* SDK counts (1 + clkdiv) cycles     */

    s_actual_rate = (uint32_t)(f_adc / period);
}

/* ------------------------------------------------------------------ */
void adc_capture_start(uint16_t *dst, uint16_t count)
{
    if (!s_inited || dst == nullptr || count == 0u) {
        s_count = 0u;
        return;
    }
    if (count > SCOPE_MAX_SAMPLE_COUNT)
        count = (uint16_t)SCOPE_MAX_SAMPLE_COUNT;

    s_dst   = dst;
    s_count = count;

    adc_capture_abort();                    /* kills prior in-flight    */
    adc_fifo_drain();                       /* clear stale samples      */
    adc_select_input(SCOPE_ADC_INPUT);      /* defensive re-select      */
    configure_clock();

    /* DMA: fixed read from ADC FIFO, incrementing write to RAM.        */
    dma_channel_config cfg = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(s_dma_ch,
                          &cfg,
                          dst,                       /* write addr       */
                          &adc_hw->fifo,             /* read addr        */
                          count,                     /* transfer count   */
                          false);                    /* not started yet  */
}

/* ------------------------------------------------------------------ */
adc_capture_status_t adc_capture_wait(void)
{
    if (!s_inited || s_count == 0u || s_dst == nullptr)
        return ADC_CAP_NOT_INIT;

    /* start DMA, then start ADC free-running; DREQ paces transfers     */
    dma_channel_start(s_dma_ch);
    adc_fifo_setup(true, true, 1, false, false);
    adc_run(true);

    absolute_time_t deadline =
        make_timeout_time_ms(SCOPE_CAPTURE_TIMEOUT_MS);

    while (dma_channel_is_busy(s_dma_ch)) {
        if (time_reached(deadline)) {
            adc_capture_abort();
            return ADC_CAP_TIMEOUT;
        }
        tight_loop_contents();
    }

    /* stop ADC, drain FIFO, check sticky overflow                      */
    adc_run(false);
    adc_fifo_drain();

    if (adc_hw->fcs & ADC_FIFO_FCS_OVER_BITS) {
        hw_set_bits(&adc_hw->fcs, ADC_FIFO_FCS_OVER_BITS); /* clear W1C */
        return ADC_CAP_FIFO_OVERFLOW;
    }
    return ADC_CAP_OK;
}

/* ------------------------------------------------------------------ */
void adc_capture_abort(void)
{
    if (s_dma_ch >= 0 && dma_channel_is_busy(s_dma_ch))
        dma_channel_abort(s_dma_ch);
    adc_run(false);
    adc_fifo_drain();
    s_count = 0u;
}

uint32_t adc_capture_actual_rate(void)
{
    return s_actual_rate;
}
