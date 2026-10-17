/**
 * test_capture.cpp — host acceptance for the v2 capture helpers
 *
 * Covers the two pieces of multi-ADC / logic-analyzer logic that are
 * deliberately free of the Pico SDK, so they run on the dev machine:
 *   capture/deinterleave.cpp — channel-tag slot mapping and packing
 *   capture/trigger.cpp      — analog edge trigger + digital bit trigger
 *
 * Convention under test (matches src/capture/logic_capture.cpp):
 * one 32-bit PIO snapshot per sample, shifted right so ISR bit 0 is the
 * FIRST logic pin (GPIO5), and only the low byte is meaningful.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "deinterleave.h"
#include "trigger.h"

static int g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("  ok    %s\n", (name)); } \
    else { printf("  FAIL  %s\n", (name)); ++g_fail; } \
} while (0)

/* ADC FIFO word: bits 15:12 channel tag, bits 11:0 conversion result. */
static uint16_t adc_word(uint8_t channel, uint16_t value)
{
    return (uint16_t)(((uint16_t)channel << 12) | (value & 0x0FFFu));
}

/* ------------------------------------------------------------------ */
static void test_slot_for_channel(void)
{
    CHECK(adc_slot_for_channel(0x00u, 0u) == 0xFFu,
          "no channels enabled → no slot");
    CHECK(adc_slot_for_channel(0x01u, 0u) == 0u,
          "mask 0x01 → ch0 is slot 0");
    CHECK(adc_slot_for_channel(0x01u, 1u) == 0xFFu,
          "mask 0x01 → ch1 not enabled");
    CHECK(adc_slot_for_channel(0x07u, 0u) == 0u &&
          adc_slot_for_channel(0x07u, 1u) == 1u &&
          adc_slot_for_channel(0x07u, 2u) == 2u,
          "mask 0x07 → dense slots 0,1,2");
    CHECK(adc_slot_for_channel(0x07u, 3u) == 0xFFu,
          "mask 0x07 → ch3 not enabled");

    /* slots count ENABLED bits, not channel numbers                 */
    CHECK(adc_slot_for_channel(0x05u, 0u) == 0u &&
          adc_slot_for_channel(0x05u, 2u) == 1u,
          "mask 0x05 → ch0=slot0, ch2=slot1");
    CHECK(adc_slot_for_channel(0x05u, 1u) == 0xFFu,
          "mask 0x05 → ch1 skipped");
    CHECK(adc_slot_for_channel(0xE0u, 5u) == 0u &&
          adc_slot_for_channel(0xE0u, 6u) == 1u &&
          adc_slot_for_channel(0xE0u, 7u) == 2u,
          "mask 0xE0 → ch5,6,7 pack to slots 0,1,2");
}

/* ------------------------------------------------------------------ */
static void test_deinterleave_two_channels(void)
{
    uint16_t dst[8];
    uint16_t stage[8];

    /* Round-robin starts on ch2: tags, not position, decide the slot. */
    for (uint16_t i = 0; i < 4u; ++i) {
        stage[i * 2u + 0u] = adc_word(2u, (uint16_t)(0x2A0u + i));
        stage[i * 2u + 1u] = adc_word(0u, (uint16_t)(0x100u + i));
    }
    memset(dst, 0, sizeof(dst));

    adc_deinterleave(stage, 4u, 2u, 0x05u, dst);

    bool ok = true;
    for (uint16_t i = 0; i < 4u; ++i) {
        if (dst[i * 2u + 0u] != (uint16_t)(0x100u + i)) ok = false; /* ch0 */
        if (dst[i * 2u + 1u] != (uint16_t)(0x2A0u + i)) ok = false; /* ch2 */
    }
    CHECK(ok, "interleaved start order does not change the slots");
    CHECK(dst[0] == 0x100u && dst[1] == 0x2A0u,
          "sample 0 packs as [ch0, ch2]");
}

/* ------------------------------------------------------------------ */
static void test_deinterleave_three_channels(void)
{
    uint16_t dst[9];
    uint16_t stage[9];

    for (uint16_t i = 0; i < 3u; ++i) {
        stage[i * 3u + 0u] = adc_word(0u, (uint16_t)(100u + i));
        stage[i * 3u + 1u] = adc_word(1u, (uint16_t)(200u + i));
        stage[i * 3u + 2u] = adc_word(2u, (uint16_t)(300u + i));
    }
    memset(dst, 0, sizeof(dst));

    adc_deinterleave(stage, 3u, 3u, 0x07u, dst);

    CHECK(dst[0] == 100u && dst[1] == 200u && dst[2] == 300u,
          "3-channel sample 0 dense order");
    CHECK(dst[6] == 102u && dst[7] == 202u && dst[8] == 302u,
          "3-channel last sample lands in the tail");
}

/* ------------------------------------------------------------------ */
static void test_deinterleave_drops_disabled(void)
{
    /* Only ch0 enabled: ch1 words must vanish and ch0 must stay dense. */
    static const uint16_t ch0[4] = { 11u, 33u, 55u, 77u };
    static const uint16_t ch1[4] = { 22u, 44u, 66u, 88u };
    uint16_t stage[8];
    uint16_t dst[4];

    for (uint16_t i = 0; i < 4u; ++i) {
        stage[i * 2u + 0u] = adc_word(0u, ch0[i]);
        stage[i * 2u + 1u] = adc_word(1u, ch1[i]);
    }
    for (uint16_t i = 0; i < 4u; ++i) dst[i] = 0xDEADu;

    adc_deinterleave(stage, 4u, 1u, 0x01u, dst);

    /* channels=1 so only 4 stage words are consumed (2 frames of ch0). */
    CHECK(dst[0] == 11u && dst[1] == 33u,
          "disabled-channel words are skipped");
    CHECK(dst[2] == 0xDEADu && dst[3] == 0xDEADu,
          "unfilled tail is left untouched");
}

/* ------------------------------------------------------------------ */
static void test_deinterleave_slot_bounds_and_guards(void)
{
    uint16_t dst[4];
    uint16_t stage[4] = {
        adc_word(0u, 11u), adc_word(2u, 22u),
        adc_word(0u, 33u), adc_word(2u, 44u),
    };

    for (uint16_t i = 0; i < 4u; ++i) dst[i] = 0xDEADu;
    /* mask 0x05 enables 2 slots but the caller asked for 1 channel   */
    adc_deinterleave(stage, 4u, 1u, 0x05u, dst);
    CHECK(dst[0] == 11u && dst[1] == 33u,
          "slot >= channels is discarded");

    for (uint16_t i = 0; i < 4u; ++i) dst[i] = 0xBEEFu;
    adc_deinterleave(nullptr, 4u, 2u, 0x05u, dst);
    adc_deinterleave(stage, 4u, 2u, 0x05u, nullptr);
    adc_deinterleave(stage, 0u, 2u, 0x05u, dst);
    adc_deinterleave(stage, 4u, 0u, 0x05u, dst);
    adc_deinterleave(stage, 4u, 9u, 0x05u, dst);
    CHECK(dst[0] == 0xBEEFu,
          "null/zero/oversize arguments write nothing");
}

/* ------------------------------------------------------------------ */
static void test_trigger_rising_and_falling(void)
{
    static const uint16_t up[7]   = { 0u, 500u, 1000u, 1500u, 2000u, 2500u, 3000u };
    static const uint16_t down[7] = { 3000u, 2500u, 2000u, 1500u, 1000u, 500u, 0u };

    CHECK(trigger_find(up, 7u, 2048u, SCOPE_EDGE_RISING, 100u) == 5u,
          "rising edge at index 5");
    CHECK(trigger_find(down, 7u, 2048u, SCOPE_EDGE_FALLING, 100u) == 2u,
          "falling edge at index 2");

    /* The ramp only ever rises, so a falling edge never arms nor fires. */
    CHECK(trigger_find(up, 7u, 2048u, SCOPE_EDGE_FALLING, 100u) == SCOPE_TRIGGER_NONE,
          "falling edge on a rising ramp → no trigger");

    static const uint16_t flat[4] = { 100u, 120u, 90u, 110u };
    CHECK(trigger_find(flat, 4u, 2048u, SCOPE_EDGE_RISING, 100u) == SCOPE_TRIGGER_NONE,
          "no crossing → SCOPE_TRIGGER_NONE");
    CHECK(trigger_find(flat, 1u, 2048u, SCOPE_EDGE_RISING, 100u) == SCOPE_TRIGGER_NONE,
          "count < 2 → no trigger");
    CHECK(trigger_find(nullptr, 4u, 2048u, SCOPE_EDGE_RISING, 100u) == SCOPE_TRIGGER_NONE,
          "null buffer → no trigger");
}

/* ------------------------------------------------------------------ */
static void test_trigger_strided(void)
{
    /* 3 interleaved channels; the trigger lives on channel index 1.    */
    static const uint16_t ch1[5] = { 0u, 1000u, 2000u, 3000u, 4000u };
    uint16_t buf[15];

    for (uint16_t i = 0; i < 5u; ++i) {
        buf[i * 3u + 0u] = 10u;      /* ch0 quiet low                 */
        buf[i * 3u + 1u] = ch1[i];   /* ch1 ramps through the level   */
        buf[i * 3u + 2u] = 4000u;    /* ch2 quiet high                */
    }

    CHECK(trigger_find_strided(buf + 1u, 5u, 3u, 2048u,
                               SCOPE_EDGE_RISING, 100u) == 3u,
          "strided rising edge at index 3");
    CHECK(trigger_find_strided(buf + 0u, 5u, 3u, 2048u,
                               SCOPE_EDGE_RISING, 100u) == SCOPE_TRIGGER_NONE,
          "quiet neighbour channel never triggers");
}

/* ------------------------------------------------------------------ */
static void test_trigger_crossings_and_span(void)
{
    /* 0, high, 0, high, 0, high → rising crossings at 1, 3, 5        */
    static const uint16_t wave[6] = { 0u, 3000u, 0u, 3000u, 0u, 3000u };
    static const uint16_t one[2]  = { 0u, 3000u };
    uint32_t span = 12345u;

    const uint16_t n = trigger_crossings(wave, 6u, 2048u,
                                        SCOPE_EDGE_RISING, 100u, &span);
    CHECK(n == 3u, "three rising crossings counted");
    CHECK(span == 4u, "span = last - first crossing index");

    span = 12345u;
    CHECK(trigger_crossings(one, 2u, 2048u, SCOPE_EDGE_RISING, 100u, &span) == 1u,
          "single crossing counted");
    CHECK(span == 0u, "span stays 0 with fewer than two crossings");

    CHECK(trigger_crossings(nullptr, 6u, 2048u, SCOPE_EDGE_RISING, 100u, &span) == 0u,
          "null buffer → 0 crossings");
    CHECK(trigger_crossings(wave, 6u, 2048u, SCOPE_EDGE_RISING, 100u, nullptr) == 3u,
          "null span out-param is tolerated");

    /* strided variant over a 2-channel buffer                        */
    uint16_t buf[12];
    for (uint16_t i = 0; i < 6u; ++i) {
        buf[i * 2u + 0u] = 0u;
        buf[i * 2u + 1u] = wave[i];
    }
    uint32_t sspan = 0u;
    CHECK(trigger_crossings_strided(buf + 1u, 6u, 2u, 2048u,
                                    SCOPE_EDGE_RISING, 100u, &sspan) == 3u &&
          sspan == 4u,
          "strided crossings match the flat case");
}

/* ------------------------------------------------------------------ */
static void test_trigger_hysteresis(void)
{
    /* Recrosses 2048 twice but never drops below level-hysteresis:
     * a 100-code band must count one edge, a zero band must count two. */
    static const uint16_t noisy[6] = { 0u, 2100u, 2000u, 1990u, 2050u, 2400u };

    CHECK(trigger_crossings(noisy, 6u, 2048u, SCOPE_EDGE_RISING, 100u, nullptr) == 1u,
          "hysteresis band suppresses the re-crossing");
    CHECK(trigger_crossings(noisy, 6u, 2048u, SCOPE_EDGE_RISING, 0u, nullptr) == 2u,
          "zero hysteresis counts the re-crossing");

    CHECK(trigger_find(noisy, 6u, 2048u, SCOPE_EDGE_RISING, 100u) == 1u,
          "trigger_find honours hysteresis too");
}

/* ------------------------------------------------------------------ */
/* Digital bit-edge trigger. Bit 0 is the first lane (GPIO5).          */
/* ------------------------------------------------------------------ */
static void test_logic_bit_trigger(void)
{
    /* 0xFF00 is a deliberate leak from pins above the 8 lanes: the
       low-byte masks must make it invisible. */
    static const uint32_t s[8] = {
        0xFF00u | 0x20u, 0xFF00u | 0x20u, 0xFF00u | 0x21u, 0xFF00u | 0x01u,
        0xFF00u | 0x00u, 0xFF00u | 0x80u, 0xFF00u | 0x01u, 0xFF00u | 0x01u,
    };

    CHECK(trigger_logic_find(s, 8u, 0u, SCOPE_EDGE_RISING) == 2u,
          "lane0 rising at index 2");
    CHECK(trigger_logic_find(s, 8u, 0u, SCOPE_EDGE_FALLING) == 4u,
          "lane0 falling at index 4");
    CHECK(trigger_logic_find(s, 8u, 5u, SCOPE_EDGE_FALLING) == 3u,
          "lane5 falling at index 3");
    CHECK(trigger_logic_find(s, 8u, 5u, SCOPE_EDGE_RISING) == SCOPE_TRIGGER_NONE,
          "lane5 never rises");
    CHECK(trigger_logic_find(s, 8u, 7u, SCOPE_EDGE_RISING) == 5u,
          "lane7 rising at index 5");
    CHECK(trigger_logic_find(s, 8u, 1u, SCOPE_EDGE_RISING) == SCOPE_TRIGGER_NONE,
          "idle lane never triggers");
    CHECK(trigger_logic_find(s, 8u, 8u, SCOPE_EDGE_RISING) == SCOPE_TRIGGER_NONE,
          "channel 8 out of range");
    CHECK(trigger_logic_find(nullptr, 8u, 0u, SCOPE_EDGE_RISING) == SCOPE_TRIGGER_NONE,
          "null buffer → no trigger");
    CHECK(trigger_logic_find(s, 1u, 0u, SCOPE_EDGE_RISING) == SCOPE_TRIGGER_NONE,
          "count < 2 → no trigger");

    CHECK(trigger_logic_crossings(s, 8u, 0u) == 2u, "lane0 has 2 rising edges");
    CHECK(trigger_logic_crossings(s, 8u, 5u) == 0u, "lane5 has no rising edge");
    CHECK(trigger_logic_crossings(s, 8u, 7u) == 1u, "lane7 has 1 rising edge");
    CHECK(trigger_logic_crossings(s, 8u, 9u) == 0u, "out-of-range lane → 0");
    CHECK(trigger_logic_crossings(s, 8u, 0u) != 0u,
          "ISR bit 0 is the first lane (GPIO5)");
}

/* ------------------------------------------------------------------ */
int main(void)
{
    printf("capture host tests\n");
    test_slot_for_channel();
    test_deinterleave_two_channels();
    test_deinterleave_three_channels();
    test_deinterleave_drops_disabled();
    test_deinterleave_slot_bounds_and_guards();
    test_trigger_rising_and_falling();
    test_trigger_strided();
    test_trigger_crossings_and_span();
    test_trigger_hysteresis();
    test_logic_bit_trigger();

    if (g_fail == 0) {
        printf("ALL PASS\n");
        return 0;
    }
    printf("FAILURES: %d\n", g_fail);
    return 1;
}
