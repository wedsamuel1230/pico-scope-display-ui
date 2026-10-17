/**
 * adc_capture.cpp — analog capture engine (ADC + DMA, round-robin)
 *
 * One DMA channel streams the ADC FIFO into a staging buffer while the
 * enabled channels are converted round-robin. Each FIFO word carries
 * its channel in bits 15:12, so adc_deinterleave() maps the stream back
 * onto dense per-channel slots without assuming a round-robin start
 * order.
 */
#include "adc_capture.h"

#include <string.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "board_config.h"
#include "deinterleave.h"

/* The RP2040 ADC needs at least 96 clocks per conversion. */
#define ADC_CYCLES_PER_SAMPLE 96.0f

static bool      s_inited = false;
static int       s_dma_ch = -1;
static uint32_t  s_requested = SCOPE_DEFAULT_RATE_HZ;
static uint32_t  s_actual_rate = 0;
static uint16_t  s_count = 0;
static uint8_t   s_channels = 1;
static uint8_t   s_mask = 0x01u;
static uint16_t *s_dst = nullptr;

/* Raw round-robin words: bits 11:0 result, bits 15:12 channel. */
static uint16_t s_stage[SCOPE_MAX_SAMPLE_COUNT * SCOPE_ADC_CHANNELS];

static uint8_t clamp_mask(uint8_t mask)
{
    return (uint8_t)(mask & (uint8_t)((1u << SCOPE_ADC_CHANNELS) - 1u));
}

uint8_t adc_capture_channels_for_mask(uint8_t channel_mask)
{
    const uint8_t mask = clamp_mask(channel_mask);
    uint8_t n = 0;
    for (uint8_t bit = 0; bit < SCOPE_ADC_CHANNELS; ++bit) {
        if (mask & (uint8_t)(1u << bit)) ++n;
    }
    return n;
}

void adc_capture_init(void)
{
    if (s_inited) return;

    adc_init();
    for (uint8_t ch = 0; ch < SCOPE_ADC_CHANNELS; ++ch) {
        adc_gpio_init(SCOPE_ADC_BASE_GPIO + ch);
    }
    adc_select_input(SCOPE_ADC_INPUT);
    adc_set_round_robin(0u);
    /* byte_shift = false keeps the full 16-bit FIFO word (result +
     * channel tag), which the de-interleaver relies on.                 */
    adc_fifo_setup(true, false, 1, false, false);

    if (s_dma_ch < 0) {
        s_dma_ch = dma_claim_unused_channel(false);
    }

    s_inited = (s_dma_ch >= 0);
    if (s_inited) {
        s_actual_rate = SCOPE_DEFAULT_RATE_HZ;
    }
}

void adc_capture_set_rate(uint32_t requested_rate)
{
    if (requested_rate == 0u) return;
    s_requested = requested_rate;
}

static void configure_clock(void)
{
    const uint32_t f_adc = clock_get_hz(clk_adc);
    const uint32_t target = (s_requested != 0u) ? s_requested : SCOPE_DEFAULT_RATE_HZ;

    float period = (float)f_adc / (float)target;
    if (period < ADC_CYCLES_PER_SAMPLE) period = ADC_CYCLES_PER_SAMPLE;

    adc_set_clkdiv(period - 1.0f);
    s_actual_rate = (uint32_t)((float)f_adc / period);
}

void adc_capture_start(uint16_t *dst, uint16_t count, uint8_t channel_mask)
{
    s_count = 0;
    s_dst = nullptr;

    if (!s_inited || dst == nullptr || count == 0u) return;

    const uint8_t mask = clamp_mask(channel_mask);
    const uint8_t channels = adc_capture_channels_for_mask(mask);
    if (channels == 0u) return;

    if (count > SCOPE_MAX_SAMPLE_COUNT) count = SCOPE_MAX_SAMPLE_COUNT;

    s_dst = dst;
    s_count = count;
    s_channels = channels;
    s_mask = mask;

    adc_capture_abort();
    adc_fifo_drain();

    /* Round-robin over the enabled channels; start at the lowest one. */
    uint32_t round_robin = 0u;
    uint8_t first = 0u;
    bool have_first = false;
    for (uint8_t bit = 0; bit < SCOPE_ADC_CHANNELS; ++bit) {
        if (mask & (uint8_t)(1u << bit)) {
            round_robin |= (1u << bit);
            if (!have_first) { first = bit; have_first = true; }
        }
    }
    adc_set_round_robin(round_robin);
    adc_select_input(first);

    configure_clock();

    const uint32_t total = (uint32_t)count * (uint32_t)channels;

    dma_channel_config cfg = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, DREQ_ADC);
    dma_channel_configure(s_dma_ch, &cfg, s_stage, &adc_hw->fifo, total, false);
}

adc_capture_status_t adc_capture_wait(void)
{
    if (!s_inited) return ADC_CAP_NOT_INIT;
    if (s_count == 0u || s_dst == nullptr) return ADC_CAP_NOT_INIT;

    dma_channel_start(s_dma_ch);
    adc_fifo_setup(true, true, 1, false, false);
    adc_run(true);

    const absolute_time_t deadline = make_timeout_time_ms(SCOPE_CAPTURE_TIMEOUT_MS);
    while (dma_channel_is_busy(s_dma_ch)) {
        if (time_reached(deadline)) {
            adc_capture_abort();
            return ADC_CAP_TIMEOUT;
        }
    }

    adc_run(false);
    adc_fifo_drain();

    const bool overflow = (adc_hw->fcs & ADC_FCS_OVER_BITS) != 0u;
    if (overflow) adc_hw->fcs = ADC_FCS_OVER_BITS; /* write 1 to clear */

    adc_deinterleave(s_stage, s_count, s_channels, s_mask, s_dst);

    return overflow ? ADC_CAP_FIFO_OVERFLOW : ADC_CAP_OK;
}

void adc_capture_abort(void)
{
    if (s_dma_ch >= 0 && dma_channel_is_busy(s_dma_ch)) {
        dma_channel_abort(s_dma_ch);
    }
    adc_run(false);
    adc_fifo_drain();
    s_count = 0;
}

uint32_t adc_capture_actual_rate(void)
{
    return s_actual_rate;
}

uint32_t adc_capture_channel_rate(void)
{
    if (s_channels == 0u) return s_actual_rate;
    return s_actual_rate / (uint32_t)s_channels;
}

uint8_t adc_capture_last_channels(void)
{
    return s_channels;
}
