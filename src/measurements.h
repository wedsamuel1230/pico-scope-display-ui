/**
 * measurements.h — Vpp / RMS / frequency post-processing
 *
 * v2 runs the same statistics per analog channel over the interleaved
 * capture buffer, hence the strided entry point.
 */
#ifndef SCOPE_MEASUREMENTS_H
#define SCOPE_MEASUREMENTS_H

#include <stdbool.h>
#include <stdint.h>

#include "scope_types.h"

typedef struct {
    bool     valid;
    uint16_t min_code;
    uint16_t max_code;
    uint16_t vpp_code;      /* max - min, in ADC codes                  */
    float    rms_code;      /* AC RMS in ADC codes (calib maps to V)    */
    float    freq_hz;       /* invalid when < 2 crossings => 0          */
} scope_measurements_t;

/* Compute min/max/Vpp/RMS(+DC mean) and frequency estimate over a
 * contiguous buffer. Frequency uses same-direction crossings
 * (trigger_crossings); invalid => 0.                                   */
void measurements_compute(const uint16_t *samples, uint16_t count,
                          uint32_t rate, uint16_t trig_level,
                          scope_edge_t edge, scope_measurements_t *out);

/* Interleaved variant: pass a pointer to the channel's first sample and
 * stride = number of interleaved channels (sample i lives at
 * samples[i * stride]).                                                 */
void measurements_compute_strided(const uint16_t *samples, uint16_t count,
                                  uint16_t stride, uint32_t rate,
                                  uint16_t trig_level, scope_edge_t edge,
                                  scope_measurements_t *out);

#endif /* SCOPE_MEASUREMENTS_H */
