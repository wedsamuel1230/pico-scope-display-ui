/**
 * deinterleave.cpp — positional de-interleave for the round-robin ADC
 *
 * Pure C++: no Pico SDK dependency, so tests/host compiles this file
 * directly and exercises the mapping on the development machine.
 *
 * The RP2040 ADC FIFO word holds only the conversion result (bits 11:0)
 * plus an optional error flag (bit 15); bits 14:12 are reserved and read
 * back as zero.  There is no channel tag in the stream, so channel identity
 * is derived from the sample POSITION: adc_capture_start() selects the
 * lowest enabled channel and enables round-robin over the mask, which makes
 * the ADC step through the enabled channels in ascending bit order.  Sample
 * i therefore belongs to dense slot (i % channels).
 */
#include "deinterleave.h"

uint8_t adc_slot_for_channel(uint8_t mask, uint8_t channel)
{
    uint8_t slot = 0u;
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        if ((mask & (uint8_t)(1u << bit)) == 0u) continue;
        if (bit == channel) return slot;
        ++slot;
    }
    return 0xFFu;
}

void adc_deinterleave(const uint16_t *stage, uint16_t count, uint8_t channels,
                      uint8_t mask, uint16_t *dst)
{
    if (stage == nullptr || dst == nullptr) return;
    if (count == 0u || channels == 0u || channels > 8u) return;

    /* The positional model is only valid when `channels` really is the
     * number of enabled bits: otherwise the round-robin never lines up. */
    uint8_t enabled = 0u;
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        if (mask & (uint8_t)(1u << bit)) ++enabled;
    }
    if (enabled != channels) return;

    const uint32_t total = (uint32_t)count * (uint32_t)channels;
    for (uint32_t i = 0u; i < total; ++i) {
        const uint8_t slot = (uint8_t)(i % (uint32_t)channels);
        const uint32_t sample = i / (uint32_t)channels;
        dst[sample * (uint32_t)channels + (uint32_t)slot] =
            (uint16_t)(stage[i] & 0x0FFFu);
    }
}
