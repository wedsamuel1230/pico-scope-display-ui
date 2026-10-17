/**
 * measurements.cpp — Vpp / RMS / frequency post-processing
 *
 * Statistics run AFTER the capture completes, so floating point is fine
 * here (docs forbid float only in the capture/DMA loop itself).
 * Frequency is estimated from same-direction threshold crossings
 * (trigger_crossings): with fewer than 2 valid crossings the frequency
 * is invalid and the UI shows "—".
 */
#include "measurements.h"

#include <math.h>

#include "trigger.h"

void measurements_compute(const uint16_t *samples, uint16_t count,
                          uint32_t rate, uint16_t trig_level,
                          scope_edge_t edge, scope_measurements_t *out)
{
    if (out == nullptr)
        return;
    out->valid = false;

    if (samples == nullptr || count < 16u) {
        out->min_code = 0u;
        out->max_code = 0u;
        out->vpp_code = 0u;
        out->rms_code = 0.0f;
        out->freq_hz  = 0.0f;
        return;
    }

    uint16_t min_code = 0x0FFFu;
    uint16_t max_code = 0x0000u;
    uint64_t sum     = 0u;

    for (uint16_t i = 0u; i < count; ++i) {
        const uint16_t v = samples[i];
        if (v < min_code) min_code = v;
        if (v > max_code) max_code = v;
        sum += (uint64_t)v;
    }

    /* AC RMS about the mean: sqrt(mean((x-mean)^2)).                   */
    const float mean_f = (float)((double)sum / (double)count);
    double      sum_sq = 0.0;
    for (uint16_t i = 0u; i < count; ++i) {
        const double d = (double)samples[i] - (double)mean_f;
        sum_sq += d * d;
    }

    const uint16_t ncross = trigger_crossings(samples, count, trig_level,
                                              edge,
                                              (uint8_t)SCOPE_TRIGGER_HYSTERESIS,
                                              nullptr);
    uint32_t span = 0u;
    if (ncross >= 2u) {
        trigger_crossings(samples, count, trig_level, edge,
                          (uint8_t)SCOPE_TRIGGER_HYSTERESIS, &span);
    }

    out->min_code = min_code;
    out->max_code = max_code;
    out->vpp_code = (uint16_t)(max_code - min_code);
    out->rms_code = (float)sqrt(sum_sq / (double)count);
    out->valid    = true;

    out->freq_hz = 0.0f;
    if (ncross >= 2u && span != 0u && rate > 0u) {
        /* period = span_samples / (crossings - 1) / rate               */
        out->freq_hz = (float)rate * (float)(ncross - 1u) / (float)span;
    }
}
