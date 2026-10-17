/**
 * trigger.h — rising/falling edge search with hysteresis
 */
#ifndef SCOPE_TRIGGER_H
#define SCOPE_TRIGGER_H

#include <stdint.h>

#define SCOPE_TRIGGER_NONE 0xFFFFu

typedef enum {
    SCOPE_EDGE_RISING  = 0,
    SCOPE_EDGE_FALLING = 1,
} scope_edge_t;

/* Search the buffer for the first edge of `edge` polarity crossing
 * `level`, requiring the signal to move beyond level-hysteresis on the
 * far side first (per IMPLEMENTATION_GUIDE.md "hysteresis 64").
 * Returns index or SCOPE_TRIGGER_NONE.                                   */
uint16_t trigger_find(const uint16_t *samples, uint16_t count,
                      uint16_t level, scope_edge_t edge, uint8_t hysteresis);

/* Count valid same-direction crossings (for frequency estimation).
 * Returns number of full cycles found (>=2 needed for a good estimate),
 * and if non_zero_out is given, stores the time span (in samples)
 * between the first and last same-direction crossing.                    */
uint16_t trigger_crossings(const uint16_t *samples, uint16_t count,
                           uint16_t level, scope_edge_t edge,
                           uint8_t hysteresis, uint32_t *span_samples);

#endif /* SCOPE_TRIGGER_H */
