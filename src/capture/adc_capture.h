/**
 * adc_capture.h — analog capture engine (ADC + DMA, round-robin)
 *
 * v2 captures 1..SCOPE_ADC_CHANNELS channels in a single pass. The
 * enabled channels are converted round-robin; the interleaved FIFO
 * stream is de-interleaved into a per-channel-interleaved output buffer.
 */
#ifndef SCOPE_ADC_CAPTURE_H
#define SCOPE_ADC_CAPTURE_H

#include <stdint.h>

#include "scope_config.h"

typedef enum {
    ADC_CAP_OK = 0,
    ADC_CAP_TIMEOUT,
    ADC_CAP_FIFO_OVERFLOW,
    ADC_CAP_NOT_INIT
} adc_capture_status_t;

void adc_capture_init(void);

/** Total ADC conversion rate. Per-channel rate = total / channels. */
void adc_capture_set_rate(uint32_t requested_rate);

/** Number of enabled channels in `channel_mask` (bit 0 = ADC0). */
uint8_t adc_capture_channels_for_mask(uint8_t channel_mask);

/**
 * Arm a capture of `count` samples PER enabled channel. `dst` must be
 * able to hold count * enabled_channels uint16_t values (interleaved as
 * dst[sample * channels + slot]).
 */
void adc_capture_start(uint16_t *dst, uint16_t count, uint8_t channel_mask);

adc_capture_status_t adc_capture_wait(void);
void adc_capture_abort(void);

uint32_t adc_capture_actual_rate(void);   /* total ADC rate, Hz   */
uint32_t adc_capture_channel_rate(void);  /* per-channel rate, Hz */
uint8_t  adc_capture_last_channels(void);

#endif /* SCOPE_ADC_CAPTURE_H */
