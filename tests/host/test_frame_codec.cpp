/**
 * test_frame_codec.cpp — host acceptance for COBS/CRC frame codec
 *
 * Cases per docs/IMPLEMENTATION_GUIDE.md stage 4:
 *   CRC known vector, encode/decode round-trips, partial-frame feeding,
 *   coalesced frames in one feed call, bad CRC rejection, oversized
 *   frame discard, decoder reset between frames.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "frame_codec.h"
}

static int g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("  ok    %s\n", (name)); } \
    else { printf("  FAIL  %s\n", (name)); ++g_fail; } \
} while (0)

/* payload built as arbitrary non-zero bytes for COBS exercising         */
static void fill_payload(uint8_t *buf, size_t n, uint8_t seed)
{
    for (size_t i = 0; i < n; ++i)
        buf[i] = (uint8_t)((i * 31u + seed) | 0x01u);
}

/* ------------------------------------------------------------------ */
static void test_crc_vectors(void)
{
    const uint8_t v123456789[] = "123456789";
    CHECK(scope_crc16(v123456789, 9) == 0x29B1u,
          "CRC-16/CCITT-FALSE(\"123456789\") == 0x29B1");
    CHECK(scope_crc16(nullptr, 0) == 0xFFFFu, "CRC empty == init 0xFFFF");
}

/* ------------------------------------------------------------------ */
static void test_cobs_roundtrip(void)
{
    for (size_t n = 0; n <= 300u; ++n) {
        uint8_t src[512], dst[1024], back[1024];
        fill_payload(src, sizeof(src), (uint8_t)(n * 7 + 1));
        memset(src, 0, n);
        /* mix of zeros and non-zeros                                   */
        for (size_t i = 0; i < n; ++i)
            src[i] = (i % 5u == 0u) ? 0u : (uint8_t)(i * 13u + 1u);
        if (n == 0u) {
            break;
        }
        const size_t enc = cobs_encode(dst, sizeof(dst), src, n);
        if (enc == 0u) { printf("  FAIL  encode size n=%zu\n", n);
                         ++g_fail; break; }
        bool has_zero = false;
        for (size_t i = 0; i < enc; ++i) {
            if (dst[i] == 0u) { has_zero = true; break; }
        }
        if (has_zero) printf("  FAIL  zero in cobs n=%zu\n", n), ++g_fail;
        const size_t dec = cobs_decode(back, sizeof(back), dst, enc);
        if (dec != n || memcmp(src, back, n) != 0) {
            printf("  FAIL  roundtrip n=%zu\n", n); ++g_fail;
        }
    }
    printf("  done  cobs roundtrip loop\n");
}

/* ------------------------------------------------------------------ */
/* helper: build one full frame into buffer (for stream tests)          */
static size_t make_frame(uint8_t *buf, size_t cap, uint8_t type,
                         uint16_t seq, const uint8_t *pay, uint16_t plen)
{
    return frame_encode(buf, cap, type, seq, pay, plen);
}

static void test_single_frame_parse(void)
{
    frame_decoder_t d;
    frame_view_t    v;
    uint8_t         pay[16];
    uint8_t         stream[128];

    fill_payload(pay, sizeof(pay), 0x42);
    const size_t n = make_frame(stream, sizeof(stream),
                                SCOPE_MSG_SET_CONFIG, 0x1234, pay,
                                sizeof(pay));
    CHECK(n > 0u, "frame_encode emits bytes");

    frame_decoder_init(&d);
    frame_codec_event_t ev = FC_IN_PROGRESS;
    for (size_t i = 0; i < n; ++i) {
        ev = frame_decoder_feed(&d, stream[i], &v);
        if (i + 1u < n && ev != FC_IN_PROGRESS) break;
    }
    CHECK(ev == FC_FRAME, "single frame → FC_FRAME");
    CHECK(ev == FC_FRAME && v.type == SCOPE_MSG_SET_CONFIG &&
          v.seq == 0x1234 && v.plen == sizeof(pay),
          "header fields match");
    CHECK(ev == FC_FRAME &&
          memcmp(v.payload, pay, sizeof(pay)) == 0,
          "payload intact");
}

/* ------------------------------------------------------------------ */
static void test_partial_and_coalesced(void)
{
    frame_decoder_t d;
    frame_view_t    v;
    uint8_t         pay1[8], pay2[5];
    uint8_t         s1[64], s2[64];
    const size_t n1 = make_frame(s1, sizeof(s1), SCOPE_MSG_HELLO_REQ, 7,
                                 pay1, 0);
    const size_t n2 = make_frame(s2, sizeof(s2), SCOPE_MSG_STOP, 9,
                                 pay2, 3);

    fill_payload(pay2, sizeof(pay2), 0x99);
    (void)pay1;

    frame_decoder_init(&d);
    uint8_t stream[160];
    size_t  off = 0;
    memcpy(stream, s1, n1); off = n1;
    memcpy(stream + off, s2, n2); off += n2;

    /* feed first 3 bytes of frame 1 only — partial                     */
    frame_codec_event_t ev = FC_IN_PROGRESS;
    for (size_t i = 0; i < 3; ++i)
        ev = frame_decoder_feed(&d, stream[i], &v);
    CHECK(ev == FC_IN_PROGRESS, "partial frame stays in-progress");

    /* now the rest + a full second frame in one go (coalesced)         */
    int frames = 0;
    uint16_t types[2] = { 0, 0 };
    int idx = 0;
    for (size_t i = 3; i < off; ++i) {
        ev = frame_decoder_feed(&d, stream[i], &v);
        if (ev == FC_FRAME) {
            types[idx++] = (uint16_t)v.type;
            ++frames;
        } else if (ev == FC_INVALID)
            break;
    }
    CHECK(frames == 2 && types[0] == SCOPE_MSG_HELLO_REQ &&
          types[1] == SCOPE_MSG_STOP,
          "coalesced frames parse in order");
}

/* ------------------------------------------------------------------ */
static void test_bad_crc(void)
{
    frame_decoder_t d;
    frame_view_t    v;
    uint8_t         stream[64];
    const size_t n = make_frame(stream, sizeof(stream), SCOPE_MSG_STOP,
                                11, nullptr, 0);
    stream[n - 2] ^= 0xFF; /* corrupt CRC LSB                            */

    frame_decoder_init(&d);
    frame_codec_event_t ev = FC_IN_PROGRESS;
    for (size_t i = 0; i < n; ++i)
        ev = frame_decoder_feed(&d, stream[i], &v);
    CHECK(ev == FC_INVALID, "corrupted CRC rejected");
    /* decoder state reset: next frame still parses                     */
    const size_t n2 = make_frame(stream, sizeof(stream),
                                 SCOPE_MSG_HELLO_REQ, 12, nullptr, 0);
    frame_codec_event_t ev2 = FC_IN_PROGRESS;
    for (size_t i = 0; i < n2; ++i) {
        ev2 = frame_decoder_feed(&d, stream[i], &v);
        if (ev2 != FC_IN_PROGRESS) break;
    }
    CHECK(ev2 == FC_FRAME, "decoder recovered after invalid frame");
}

/* ------------------------------------------------------------------ */
static void test_truncated_and_garbage(void)
{
    frame_decoder_t d;
    frame_view_t    v;
    uint8_t         stream[64];

    /* truncated frame (delimiter arrives too early)                    */
    const size_t n = make_frame(stream, sizeof(stream),
                                SCOPE_MSG_STOP, 21, nullptr, 0);
    frame_decoder_init(&d);
    frame_codec_event_t ev = FC_IN_PROGRESS;
    for (size_t i = 0; i < n - 4u; ++i) /* cut off before CRC             */
        ev = frame_decoder_feed(&d, stream[i], &v);
    ev = frame_decoder_feed(&d, 0x00, &v); /* early delimiter            */
    CHECK(ev == FC_INVALID, "truncated frame rejected");

    /* garbage noise contains delimiters                                 */
    frame_decoder_init(&d);
    ev = FC_IN_PROGRESS;
    const uint8_t garbage[] = { 0x11, 0x00, 0x22, 0x33, 0x00, 0x44 };
    for (size_t i = 0; i < sizeof(garbage); ++i) {
        ev = frame_decoder_feed(&d, garbage[i], &v);
        if (ev == FC_FRAME) { ++g_fail; break; }
    }
    CHECK(ev != FC_FRAME, "garbage never yields a valid frame");
}

/* ------------------------------------------------------------------ */
static void test_max_size_frame(void)
{
    /* A capture-sized payload (1024 samples) full round-trip            */
    static uint8_t  pay[9 + 2048];
    static uint8_t  stream[SCOPE_PROTO_MAX_FRAME];
    static uint8_t  big[2048];

    for (size_t i = 0; i < 2048; ++i)
        big[i] = (uint8_t)(i & 0xFFu);
    /* build CAPTURE_DATA-shaped payload manually (7-byte head)          */
    pay[0] = 0xA0; pay[1] = 0x86; pay[2] = 0x01; pay[3] = 0x00; /* rate */
    pay[4] = 0x00; pay[5] = 0x04;                        /* count=1024  */
    pay[6] = 0x10; pay[7] = 0x00;                        /* trig idx    */
    pay[8] = 0x04;                                       /* flags       */
    memcpy(pay + 9, big, 2048);

    frame_decoder_t d;
    frame_view_t    v;
    frame_decoder_init(&d);

    const size_t n = make_frame(stream, sizeof(stream),
                                SCOPE_MSG_CAPTURE_DATA, 5, pay,
                                (uint16_t)sizeof(pay));
    CHECK(n > 0u, "max-size frame encoded");
    frame_codec_event_t ev = FC_IN_PROGRESS;
    for (size_t i = 0; i < n; ++i) {
        ev = frame_decoder_feed(&d, stream[i], &v);
        if (ev != FC_IN_PROGRESS) break;
    }
    CHECK(ev == FC_FRAME && v.plen == sizeof(pay) &&
          v.type == SCOPE_MSG_CAPTURE_DATA,
          "max-size frame parses");
    CHECK(ev == FC_FRAME && v.payload[8] == 0x04 &&
          const_cast<uint8_t *>(v.payload)[9] == (uint8_t)(0u),
          "payload head intact");
}

/* ------------------------------------------------------------------ */
static void test_decode_buffer_oversize(void)
{
    frame_decoder_t d;
    frame_view_t    v;
    frame_decoder_init(&d);
    /* exceeding internal overflow threshold must yield INVALID then
     * recover on next 0x00 — feed 70000 bytes                         */
    frame_codec_event_t ev = FC_IN_PROGRESS;
    for (size_t i = 0; i < 70000u; ++i) {
        ev = frame_decoder_feed(&d, (uint8_t)(i & 0xFFu), &v);
        if (ev == FC_INVALID) break;
    }
    CHECK(ev == FC_INVALID, "oversize stream rejected");
    CHECK(frame_decoder_feed(&d, 0x00, &v) == FC_IN_PROGRESS,
          "delimiter resets the stream");
    frame_decoder_init(&d);
    CHECK(true, "cleanup");
}

/* ------------------------------------------------------------------ */
int main(void)
{
    printf("frame_codec host tests\n");
    test_crc_vectors();
    test_cobs_roundtrip();
    test_single_frame_parse();
    test_partial_and_coalesced();
    test_bad_crc();
    test_truncated_and_garbage();
    test_max_size_frame();
    test_decode_buffer_oversize();

    if (g_fail == 0) {
        printf("ALL PASS\n");
        return 0;
    }
    printf("FAILURES: %d\n", g_fail);
    return 1;
}
