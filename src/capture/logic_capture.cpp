/**
 * logic_capture.cpp — digital capture engine (PIO + DMA)
 *
 * The PIO program is a single instruction:
 *
 *     .program logic_sampler
 *         in pins, 32
 *
 * With sm_config_set_in_pins(&c, SCOPE_LOGIC_BASE_GPIO) and the IN
 * shift direction set to RIGHT - the same convention the official
 * raspberrypi/pico-examples pio/logic_analyser uses - each 32-bit ISR
 * holds the pin snapshot with the base pin in bit 0. The state machine
 * autopushes every 32 bits, so one RX FIFO word carries exactly one
 * sample and DMA can move them 32 bits at a time.
 *
 * Only bits 7:0 are our eight logic lines. Bits 8..31 come from GPIO13+
 * (LCD pins) because PIO pin indices wrap modulo 32, so every consumer
 * MUST mask with SCOPE_LOGIC_PIN_MASK before using or transmitting a
 * sample.
 */
#include "logic_capture.h"

#include "board_config.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "pico/time.h"

/* One IN instruction per sample and a free PC wrap, so one PIO clock
 * per sample. The reported rate stays NOMINAL: FIFO stall effects are
 * not modelled. */
#define LOGIC_PIO_CYCLES_PER_SAMPLE 1.0f

static PIO      s_pio = nullptr;
static int      s_sm = -1;
static int      s_dma_ch = -1;
static uint     s_offset = 0u;
static uint32_t s_requested = SCOPE_DEFAULT_LOGIC_RATE_HZ;
static uint32_t s_actual_rate = 0u;
static uint16_t s_count = 0u;
static uint32_t *s_dst = nullptr;

static uint16_t s_instr[1];
static pio_program_t s_prog;

void logic_capture_init(void)
{
    if (s_sm >= 0) return;

    s_pio = pio0;
    s_sm = (int)pio_claim_unused_sm(s_pio, false);
    if (s_sm < 0) {
        s_pio = nullptr;
        return;
    }

    s_dma_ch = dma_claim_unused_channel(false);
    if (s_dma_ch < 0) {
        pio_sm_unclaim(s_pio, (uint)s_sm);
        s_sm = -1;
        s_pio = nullptr;
        return;
    }

    s_instr[0] = (uint16_t)pio_encode_in(pio_pins, 32);
    s_prog.instructions = s_instr;
    s_prog.length = 1u;
    s_prog.origin = -1;
    s_offset = pio_add_program(s_pio, &s_prog);

    for (uint i = 0; i < SCOPE_LOGIC_CHANNELS; ++i) {
        pio_gpio_init(s_pio, SCOPE_LOGIC_BASE_GPIO + i);
    }
    pio_sm_set_consecutive_pindirs(s_pio, (uint)s_sm,
                                   SCOPE_LOGIC_BASE_GPIO,
                                   SCOPE_LOGIC_CHANNELS, false);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, s_offset, s_offset);
    sm_config_set_in_pins(&c, SCOPE_LOGIC_BASE_GPIO);
    sm_config_set_in_shift(&c, true /*shift_right*/, true /*autopush*/, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c, LOGIC_PIO_CYCLES_PER_SAMPLE);
    pio_sm_init(s_pio, (uint)s_sm, s_offset, &c);

    s_actual_rate = SCOPE_DEFAULT_LOGIC_RATE_HZ;
}

void logic_capture_set_rate(uint32_t requested_rate)
{
    if (requested_rate == 0u) return;
    s_requested = requested_rate;
}

static void apply_rate(void)
{
    const uint32_t sys = clock_get_hz(clk_sys);
    float div = (float)sys / ((float)s_requested * LOGIC_PIO_CYCLES_PER_SAMPLE);
    if (div < 1.0f) div = 1.0f;

    if (s_sm >= 0) pio_sm_set_clkdiv(s_pio, (uint)s_sm, div);
    s_actual_rate = (uint32_t)((float)sys / (div * LOGIC_PIO_CYCLES_PER_SAMPLE));
}

void logic_capture_start(uint32_t *dst, uint16_t count)
{
    s_count = 0u;
    s_dst = nullptr;

    if (s_sm < 0 || dst == nullptr || count == 0u) return;
    if (count > SCOPE_MAX_LOGIC_SAMPLES) count = SCOPE_MAX_LOGIC_SAMPLES;

    s_dst = dst;
    s_count = count;

    logic_capture_abort();
    apply_rate();

    dma_channel_config cfg = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, pio_get_dreq(s_pio, (uint)s_sm, false));
    dma_channel_configure(s_dma_ch, &cfg, dst, &s_pio->rxf[s_sm], count, false);
}

logic_capture_status_t logic_capture_wait(void)
{
    if (s_sm < 0) return LOGIC_CAP_NOT_INIT;
    if (s_count == 0u || s_dst == nullptr) return LOGIC_CAP_NOT_INIT;

    pio_sm_clear_fifos(s_pio, (uint)s_sm);
    pio_sm_set_enabled(s_pio, (uint)s_sm, true);
    dma_channel_start(s_dma_ch);

    const absolute_time_t deadline = make_timeout_time_ms(SCOPE_CAPTURE_TIMEOUT_MS);
    while (dma_channel_is_busy(s_dma_ch)) {
        if (time_reached(deadline)) {
            logic_capture_abort();
            return LOGIC_CAP_TIMEOUT;
        }
    }

    pio_sm_set_enabled(s_pio, (uint)s_sm, false);
    return LOGIC_CAP_OK;
}

void logic_capture_abort(void)
{
    if (s_sm >= 0) pio_sm_set_enabled(s_pio, (uint)s_sm, false);
    if (s_dma_ch >= 0 && dma_channel_is_busy(s_dma_ch)) {
        dma_channel_abort(s_dma_ch);
    }
    if (s_sm >= 0) pio_sm_clear_fifos(s_pio, (uint)s_sm);
    s_count = 0u;
}

uint32_t logic_capture_actual_rate(void)
{
    return s_actual_rate;
}

uint8_t logic_capture_channels(void)
{
    return (uint8_t)SCOPE_LOGIC_CHANNELS;
}

bool logic_capture_ready(void)
{
    return s_sm >= 0;
}
