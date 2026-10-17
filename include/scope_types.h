/**
 * scope_types.h — capture/trigger enums shared by firmware and host tests
 *
 * Deliberately free of any Pico SDK dependency so the host test bench
 * (tests/host) can compile the pure logic that uses these types.
 */
#ifndef SCOPE_TYPES_H
#define SCOPE_TYPES_H

#include <stdint.h>

/* Which engine fills the sample buffer. */
typedef enum {
    SCOPE_MODE_SCOPE = 0, /* analog ADC, 1..SCOPE_ADC_CHANNELS traces */
    SCOPE_MODE_LOGIC = 1  /* digital PIO, SCOPE_LOGIC_CHANNELS lanes  */
} scope_capture_mode_t;

/* Trigger edge. */
typedef enum {
    SCOPE_EDGE_RISING  = 0,
    SCOPE_EDGE_FALLING = 1
} scope_edge_t;

/* Trigger mode. */
typedef enum {
    SCOPE_TRIG_MODE_AUTO   = 0, /* free-run when no trigger is found */
    SCOPE_TRIG_MODE_NORMAL = 1  /* hold and report NO_TRIGGER        */
} scope_trig_mode_t;

/* Sentinel returned when no trigger was located. */
#define SCOPE_TRIGGER_NONE 0xFFFFu

/* "No logic trigger source selected". */
#define SCOPE_TRIG_CHANNEL_NONE 0xFFu

#endif /* SCOPE_TYPES_H */
