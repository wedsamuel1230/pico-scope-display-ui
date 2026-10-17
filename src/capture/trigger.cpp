/**
 * trigger.cpp — analog edge trigger and digital bit-edge trigger
 *
 * Pure C++: no Pico SDK dependency, so tests/host compiles this file
 * directly and exercises every code path on the development machine.
 */
#include "trigger.h"

#define SCOPE_ADC_FULL_SCALE 0x0FFFu

/* ------------------------------------------------------------------ */
/* Analog edge trigger                                                 */
/* ------------------------------------------------------------------ */

/**
 * Shared implementation. Sample i lives at samples[i * stride]; callers
 * with interleaved buffers pass a pointer to the channel's first sample
 * and stride = number of interleaved channels.
 */
static uint16_t find_impl(const uint16_t *samples, uint16_t count,
                          uint16_t stride, uint16_t level,
                          scope_edge_t edge, uint8_t hysteresis)
{
    if (samples == nullptr || count < 2u || stride == 0u)
        return SCOPE_TRIGGER_NONE;

    const uint16_t low = (level > hysteresis) ? (uint16_t)(level - hysteresis) : 0u;
    const uint16_t high = (level < (uint16_t)(SCOPE_ADC_FULL_SCALE - hysteresis))
                              ? (uint16_t)(level + hysteresis)
                              : SCOPE_ADC_FULL_SCALE;

    /* Arm only if the trace starts on the far side of the edge, so the
     * first crossing is a real arm-and-fire, not a mid-trace hit. */
    const uint16_t first = samples[0];
    bool armed = (edge == SCOPE_EDGE_RISING) ? (first <= low) : (first >= high);

    for (uint16_t i = 1u; i < count; ++i) {
        const uint16_t cur = samples[(uint32_t)i * stride];

        if (edge == SCOPE_EDGE_RISING) {
            if (armed && cur >= level) return i;
            if (cur >= level) armed = false;
            else if (cur <= low) armed = true;
        } else {
            if (armed && cur <= level) return i;
            if (cur <= level) armed = false;
            else if (cur >= high) armed = true;
        }
    }
    return SCOPE_TRIGGER_NONE;
}

uint16_t trigger_find(const uint16_t *samples, uint16_t count, uint16_t level,
                      scope_edge_t edge, uint8_t hysteresis)
{
    return find_impl(samples, count, 1u, level, edge, hysteresis);
}

uint16_t trigger_find_strided(const uint16_t *samples, uint16_t count,
                              uint16_t stride, uint16_t level,
                              scope_edge_t edge, uint8_t hysteresis)
{
    return find_impl(samples, count, stride, level, edge, hysteresis);
}

static uint16_t crossings_impl(const uint16_t *samples, uint16_t count,
                               uint16_t stride, uint16_t level,
                               scope_edge_t edge, uint8_t hysteresis,
                               uint32_t *span_samples)
{
    if (span_samples != nullptr) *span_samples = 0u;
    if (samples == nullptr || count < 2u || stride == 0u) return 0u;

    const uint16_t low = (level > hysteresis) ? (uint16_t)(level - hysteresis) : 0u;
    const uint16_t high = (level < (uint16_t)(SCOPE_ADC_FULL_SCALE - hysteresis))
                              ? (uint16_t)(level + hysteresis)
                              : SCOPE_ADC_FULL_SCALE;

    const uint16_t first = samples[0];
    bool armed = (edge == SCOPE_EDGE_RISING) ? (first <= low) : (first >= high);

    uint16_t crossings = 0;
    uint16_t idx_first = 0;
    uint16_t idx_last = 0;

    for (uint16_t i = 1u; i < count; ++i) {
        const uint16_t cur = samples[(uint32_t)i * stride];
        bool hit = false;

        if (edge == SCOPE_EDGE_RISING) {
            if (armed && cur >= level) hit = true;
            if (cur >= level) armed = false;
            else if (cur <= low) armed = true;
        } else {
            if (armed && cur <= level) hit = true;
            if (cur <= level) armed = false;
            else if (cur >= high) armed = true;
        }

        if (hit) {
            if (crossings == 0u) idx_first = i;
            idx_last = i;
            ++crossings;
        }
    }

    if (span_samples != nullptr && crossings >= 2u)
        *span_samples = (uint32_t)idx_last - (uint32_t)idx_first;

    return crossings;
}

uint16_t trigger_crossings(const uint16_t *samples, uint16_t count,
                           uint16_t level, scope_edge_t edge,
                           uint8_t hysteresis, uint32_t *span_samples)
{
    return crossings_impl(samples, count, 1u, level, edge, hysteresis, span_samples);
}

uint16_t trigger_crossings_strided(const uint16_t *samples, uint16_t count,
                                   uint16_t stride, uint16_t level,
                                   scope_edge_t edge, uint8_t hysteresis,
                                   uint32_t *span_samples)
{
    return crossings_impl(samples, count, stride, level, edge, hysteresis,
                          span_samples);
}

/* ------------------------------------------------------------------ */
/* Digital bit-edge trigger (logic analyzer)                           */
/* ------------------------------------------------------------------ */

static inline bool logic_bit(const uint32_t *samples, uint16_t index, uint32_t mask)
{
    return (samples[index] & mask) != 0u;
}

uint16_t trigger_logic_find(const uint32_t *samples, uint16_t count,
                            uint8_t channel, scope_edge_t edge)
{
    if (samples == nullptr || count < 2u || channel >= 8u)
        return SCOPE_TRIGGER_NONE;

    const uint32_t mask = (1u << channel);
    bool prev = logic_bit(samples, 0u, mask);

    for (uint16_t i = 1u; i < count; ++i) {
        const bool cur = logic_bit(samples, i, mask);
        if (edge == SCOPE_EDGE_RISING) {
            if (!prev && cur) return i;
        } else {
            if (prev && !cur) return i;
        }
        prev = cur;
    }
    return SCOPE_TRIGGER_NONE;
}

uint16_t trigger_logic_crossings(const uint32_t *samples, uint16_t count,
                                 uint8_t channel)
{
    if (samples == nullptr || count < 2u || channel >= 8u) return 0u;

    const uint32_t mask = (1u << channel);
    bool prev = logic_bit(samples, 0u, mask);
    uint16_t crossings = 0;

    for (uint16_t i = 1u; i < count; ++i) {
        const bool cur = logic_bit(samples, i, mask);
        if (!prev && cur) ++crossings;
        prev = cur;
    }
    return crossings;
}
