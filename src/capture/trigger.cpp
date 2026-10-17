/**
 * trigger.cpp — rising/falling edge search with hysteresis
 *
 * Per docs/IMPLEMENTATION_GUIDE.md: rising edge = "prev < level AND
 * cur >= level" (falling mirrored), with a hysteresis band so noise
 * around the threshold does not produce spurious re-crossings. The
 * signal must leave the band on the far side (below level-hysteresis
 * for rising, above level+hysteresis for falling) before the next
 * crossing counts.
 */
#include "trigger.h"

uint16_t trigger_find(const uint16_t *samples, uint16_t count,
                      uint16_t level, scope_edge_t edge, uint8_t hysteresis)
{
    if (samples == nullptr || count < 2u)
        return SCOPE_TRIGGER_NONE;

    const uint16_t low  = (level > (uint16_t)hysteresis)
                              ? (uint16_t)(level - hysteresis)
                              : (uint16_t)0u;
    const uint16_t high = (level < (uint16_t)(0x0FFFu - hysteresis))
                              ? (uint16_t)(level + hysteresis)
                              : (uint16_t)0x0FFFu;

    /* Start "armed" if the signal begins below the level (for rising
     * searches) / above the level (for falling searches). Hysteresis
     * then governs re-arming; a raw in-band start does not fire.        */
    bool armed = (edge == SCOPE_EDGE_RISING)
                     ? (samples[0] <= low)
                     : (samples[0] >= high);

    for (uint16_t i = 1u; i < count; ++i) {
        const uint16_t cur = samples[i];

        if (edge == SCOPE_EDGE_RISING) {
            if (armed && cur >= level)
                return i; /* first post-band rising crossing           */
            if (cur >= level)
                armed = false; /* missed it — can only re-arm below-low */
            else if (cur <= low)
                armed = true;
        } else {
            if (armed && cur <= level)
                return i;
            if (cur <= level)
                armed = false; /* missed it — can only re-arm above-high */
            else if (cur >= high)
                armed = true;
        }
        /* Staying inside the hysteresis band does not change the armed
         * state, which is exactly the purpose of hysteresis.           */
    }
    return SCOPE_TRIGGER_NONE;
}

uint16_t trigger_crossings(const uint16_t *samples, uint16_t count,
                           uint16_t level, scope_edge_t edge,
                           uint8_t hysteresis, uint32_t *span_samples)
{
    if (span_samples != nullptr)
        *span_samples = 0u;
    if (samples == nullptr || count < 2u)
        return 0u;

    const uint16_t low  = (level > (uint16_t)hysteresis)
                              ? (uint16_t)(level - hysteresis)
                              : (uint16_t)0u;
    const uint16_t high = (level < (uint16_t)(0x0FFFu - hysteresis))
                              ? (uint16_t)(level + hysteresis)
                              : (uint16_t)0x0FFFu;

    bool armed = (edge == SCOPE_EDGE_RISING)
                ? (samples[0] <= low)
                : (samples[0] >= high);

    uint16_t crossings = 0u;
    uint16_t first     = 0u;
    uint16_t last      = 0u;

    for (uint16_t i = 1u; i < count; ++i) {
        const uint16_t cur = samples[i];
        const bool crossed = (edge == SCOPE_EDGE_RISING)
                                 ? (armed && cur >= level)
                                 : (armed && cur <= level);

        if (crossed) {
            if (crossings == 0u)
                first = i;
            last = i;
            ++crossings;
            armed = false;
        } else if (edge == SCOPE_EDGE_RISING) {
            if (cur <= low)
                armed = true;
        } else {
            if (cur >= high)
                armed = true;
        }
    }

    if (crossings >= 2u && span_samples != nullptr)
        *span_samples = (uint32_t)(last - first);

    return crossings;
}
