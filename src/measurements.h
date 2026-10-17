/**
 * measurements.h — Vpp / RMS / frequency post-processing
 */
#ifndef SCOPE_MEASUREMENTS_H
#define SCOPE_MEASUREMENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "scope_config.h"

typedef struct {
    bool     valid;
    uint16_t min_code;
    uint16_t max_code;
    uint16_t vpp_code;      /* max - min, in ADC codes                  */
    float    rms_code;      /* AC RMS in ADC codes (calib maps to V)    */
    float    freq_hz;       /* invalid when < 2 crossings => 0          */
} scope_measurements_t;

/* Compute min/max/Vpp/RMS(+DC mean) and frequency estimate. Frequency
 * uses same-direction crossings (trigger_crossings); invalid => 0.     */
void measurements_compute(const uint16_t *samples, uint16_t count,
                          uint32_t rate, uint16_t trig_level,
                          scope_edge_t edge, scope_measurements_t *out);

#endif /* SCOPE_MEASUREMENTS_H */
