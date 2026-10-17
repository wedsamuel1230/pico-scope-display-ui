/**
 * adc_capture.h — single-shot ADC+DMA capture
 */
#ifndef SCOPE_ADC_CAPTURE_H
#define SCOPE_ADC_CAPTURE_H

#include <stdint.h>
#include "scope_config.h"

typedef enum {
    ADC_CAP_OK = 0,
    ADC_CAP_TIMEOUT,
    ADC_CAP_FIFO_OVERFLOW,
    ADC_CAP_NOT_INIT,
} adc_capture_status_t;

/* Configure ADC GPIO/DMA channel. Must be called once before start().  */
void        adc_capture_init(void);

/* Set the requested sample rate (used for the next start()).           */
void        adc_capture_set_rate(uint32_t requested_rate);

/* Programme ADC+DMA to capture `count` samples (<= SCOPE_MAX_SAMPLE_COUNT)
 * into `dst`. Reused buffers are fine; no float in the capture loop.
 * The rate applied is the one set by adc_capture_set_rate().            */
void        adc_capture_start(uint16_t *dst, uint16_t count);

/* Block until DMA finishes (or timeout). adc_capture_start() must have
 * been called. Returns ADC_CAP_OK / TIMEOUT / FIFO_OVERFLOW              */
adc_capture_status_t adc_capture_wait(void);

/* Abort any in-flight capture (STOP handling) and clear ADC FIFO.       */
void        adc_capture_abort(void);

/* Report actual applied rate for last completed capture (Hz).           */
uint32_t    adc_capture_actual_rate(void);

#endif /* SCOPE_ADC_CAPTURE_H */
