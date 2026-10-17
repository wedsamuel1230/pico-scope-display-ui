/**
 * deinterleave.cpp — channel-tag based de-interleave for multi-ADC capture
 */
#include "deinterleave.h"

uint8_t adc_slot_for_channel(uint8_t mask, uint8_t channel)
{
    uint8_t slot = 0;
    for (uint8_t bit = 0; bit < 8u; ++bit) {
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

    uint16_t written[8] = {0};
    const uint32_t total = (uint32_t)count * (uint32_t)channels;

    for (uint32_t i = 0; i < total; ++i) {
        const uint16_t word = stage[i];
        const uint8_t channel = (uint8_t)((word >> 12) & 0x0Fu);
        const uint8_t slot = adc_slot_for_channel(mask, channel);
        if (slot == 0xFFu || slot >= channels) continue; /* disabled/unknown */

        const uint16_t index = written[slot];
        if (index >= count) continue; /* more frames than expected */
        ++written[slot];
        dst[(uint32_t)index * (uint32_t)channels + (uint32_t)slot] =
            (uint16_t)(word & 0x0FFFu);
    }
}
