/**
 * deinterleave.h — pure helpers for the round-robin ADC capture
 *
 * Kept free of the Pico SDK so tests/host can exercise the mapping.
 */
#ifndef SCOPE_DEINTERLEAVE_H
#define SCOPE_DEINTERLEAVE_H

#include <stdint.h>

/**
 * Dense slot index of `channel` within `mask`, counting enabled bits
 * from bit 0. Returns 0xFF when the channel is not enabled.
 */
uint8_t adc_slot_for_channel(uint8_t mask, uint8_t channel);

/**
 * De-interleave a raw round-robin ADC FIFO stage buffer.
 *
 * `stage` holds count * channels 16-bit ADC FIFO words: bits 11:0 are the
 * conversion result and bits 15:12 are the channel number, so the mapping
 * does not rely on a particular round-robin start order.
 * Output is interleaved: dst[sample * channels + slot].
 */
void adc_deinterleave(const uint16_t *stage, uint16_t count, uint8_t channels,
                      uint8_t mask, uint16_t *dst);

#endif /* SCOPE_DEINTERLEAVE_H */
