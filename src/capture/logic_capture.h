/**
 * logic_capture.h — digital capture engine (PIO + DMA)
 *
 * v2 logic-analyzer mode: SCOPE_LOGIC_CHANNELS digital lines sampled by
 * one PIO state machine into a DMA buffer. Samples are 32-bit pin
 * snapshots; only bits 7:0 are the logic lines (see logic_capture.cpp).
 */
#ifndef SCOPE_LOGIC_CAPTURE_H
#define SCOPE_LOGIC_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "scope_config.h"

typedef enum {
    LOGIC_CAP_OK = 0,
    LOGIC_CAP_TIMEOUT,
    LOGIC_CAP_FIFO_OVERFLOW,
    LOGIC_CAP_NOT_INIT
} logic_capture_status_t;

void logic_capture_init(void);

/** Requested sample rate, Hz (NOMINAL - see docs/REFERENCE_PROJECTS.md). */
void logic_capture_set_rate(uint32_t requested_rate);

/**
 * Arm a capture of `count` samples. `dst` must hold `count` uint32_t
 * words; each word's bits 7:0 are the eight logic lines.
 */
void logic_capture_start(uint32_t *dst, uint16_t count);

logic_capture_status_t logic_capture_wait(void);
void logic_capture_abort(void);

uint32_t logic_capture_actual_rate(void);
uint8_t  logic_capture_channels(void);
bool     logic_capture_ready(void);

#endif /* SCOPE_LOGIC_CAPTURE_H */
