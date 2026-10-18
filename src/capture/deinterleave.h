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
 * An RP2040 ADC FIFO word carries NO channel tag: bits 11:0 are the 12-bit
 * conversion result, bit 15 is the optional error flag and bits 14:12 are
 * reserved (hardware/regs/adc.h defines only FIFO_VAL and FIFO_ERR). Channel
 * identity therefore comes from the sample POSITION: the engine selects the
 * lowest enabled channel and enables round-robin over `mask`, so conversions
 * step through the enabled channels in ascending bit order and sample `i`
 * belongs to dense slot `i % channels`.
 *
 * `stage` holds count * channels raw FIFO words.
 * Output is interleaved: dst[sample * channels + slot].
 *
 * Nothing is written when the arguments are inconsistent (null pointers,
 * zero/oversized counts, or `channels` not equal to the number of bits set
 * in `mask`), so a misconfigured capture cannot publish misaligned data.
 */
void adc_deinterleave(const uint16_t *stage, uint16_t count, uint8_t channels,
                      uint8_t mask, uint16_t *dst);

#endif /* SCOPE_DEINTERLEAVE_H */
