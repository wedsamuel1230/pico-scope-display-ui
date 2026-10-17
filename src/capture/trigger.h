/**
 * trigger.h — analog edge trigger and digital bit-edge trigger
 *
 * Pure C++ (no Pico SDK), so tests/host compiles this file directly.
 */
#ifndef SCOPE_TRIGGER_H
#define SCOPE_TRIGGER_H

#include <stdint.h>

#include "scope_types.h"

/**
 * Find the first qualifying edge, using hysteresis to avoid re-arming on
 * noise. Returns the sample index, or SCOPE_TRIGGER_NONE.
 */
uint16_t trigger_find(const uint16_t *samples, uint16_t count, uint16_t level,
                      scope_edge_t edge, uint8_t hysteresis);

/**
 * Strided variant for interleaved buffers: the caller passes a pointer to
 * the channel's first sample and stride = the number of interleaved
 * channels, so sample i lives at samples[i * stride].
 */
uint16_t trigger_find_strided(const uint16_t *samples, uint16_t count,
                              uint16_t stride, uint16_t level,
                              scope_edge_t edge, uint8_t hysteresis);

/**
 * Count level crossings and report span = index(last) - index(first),
 * but only when at least two crossings were seen.
 */
uint16_t trigger_crossings(const uint16_t *samples, uint16_t count,
                           uint16_t level, scope_edge_t edge,
                           uint8_t hysteresis, uint32_t *span_samples);

uint16_t trigger_crossings_strided(const uint16_t *samples, uint16_t count,
                                   uint16_t stride, uint16_t level,
                                   scope_edge_t edge, uint8_t hysteresis,
                                   uint32_t *span_samples);

/**
 * Digital bit-edge trigger for logic capture. `channel` is 0..7 and
 * selects a bit of the low byte of each sample.
 */
uint16_t trigger_logic_find(const uint32_t *samples, uint16_t count,
                            uint8_t channel, scope_edge_t edge);

/** Count rising edges on a logic channel (for a frequency estimate). */
uint16_t trigger_logic_crossings(const uint32_t *samples, uint16_t count,
                                 uint8_t channel);

#endif /* SCOPE_TRIGGER_H */
