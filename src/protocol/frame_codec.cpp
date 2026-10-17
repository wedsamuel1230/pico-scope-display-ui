/**
 * frame_codec.cpp — protocol v1 frame encode/decode (COBS + CRC16)
 *
 * Decoding contract (docs/PROTOCOL_V1.md "parser safety"):
 *  - accumulate bytes across reader.read() boundaries: the code layer
 *    never assumes whole frames per read;
 *  - 0x00 delimits frames and never appears inside COBS output;
 *  - on bad CRC / bad COBS / oversized plen the decoder reports
 *    FC_INVALID once and waits for the next 0x00 before retrying
 *    (never crashes, never mixes streams).
 */
#include "frame_codec.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection.        */
uint16_t scope_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
size_t cobs_encode(uint8_t *dst, size_t cap, const uint8_t *src, size_t len)
{
    if (dst == nullptr || cap < SCOPE_COBS_MAX_LEN(len))
        return 0u;

    size_t   out      = 0u;
    uint8_t *code_ptr = dst + out++; /* placeholder for this block's code */
    uint8_t  code     = 1u;

    for (size_t i = 0; i < len; ++i) {
        if (src[i] != 0u) {
            dst[out++] = src[i];
            if (++code == 0xFFu) {
                if (i + 1u < len) {
                    *code_ptr = code;
                    code_ptr  = dst + out++;
                    code      = 1u;
                }
                /* trailing full block: loop exit writes final code     */
            }
        } else {
            *code_ptr = code;
            code_ptr  = dst + out++;
            code      = 1u;
        }
    }
    *code_ptr = code;
    return out;
}

/* ------------------------------------------------------------------ */
size_t cobs_decode(uint8_t *dst, size_t cap, const uint8_t *src, size_t len)
{
    if (dst == nullptr || len == 0u)
        return 0u;

    size_t out = 0u;
    size_t i   = 0u;
    while (i < len) {
        const uint8_t code = src[i++];
        if (code == 0u)
            return 0u; /* invalid: COBS blob never contains 0x00        */

        const size_t block = (size_t)(code - 1u);
        if (i + block > len || out + block > cap)
            return 0u;
        memcpy(dst + out, src + i, block);
        i    += block;
        out  += block;

        if (code < 0xFFu && i < len) {
            dst[out++] = 0u; /* code < 255 implies a single zero sep     */
        }
        /* for code == 0xFF, block is 254 and no zero is inserted        */
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* One-shot frame encoding                                             */
size_t frame_encode(uint8_t *buf, size_t cap,
                    uint8_t type, uint16_t seq,
                    const uint8_t *payload, uint16_t plen)
{
    if (buf == nullptr)
        return 0u;
    if (plen > SCOPE_PROTO_MAX_PAYLOAD ||
        (plen > 0u && payload == nullptr))
        return 0u;

    const size_t hdr_pay = SCOPE_PROTO_HDR_LEN + (size_t)plen;
    const size_t need    = SCOPE_COBS_MAX_LEN(hdr_pay) + 1u; /* +delim */
    if (cap < need)
        return 0u;

    uint8_t raw[SCOPE_PROTO_HDR_LEN + SCOPE_PROTO_MAX_PAYLOAD];
    raw[0] = (uint8_t)SCOPE_PROTO_VERSION;
    raw[1] = type;
    raw[2] = (uint8_t)(seq & 0xFFu);
    raw[3] = (uint8_t)(seq >> 8);
    raw[4] = (uint8_t)(plen & 0xFFu);
    raw[5] = (uint8_t)(plen >> 8);
    if (plen > 0u)
        memcpy(raw + SCOPE_PROTO_HDR_LEN, payload, plen);

    const uint16_t crc = scope_crc16(raw, hdr_pay);
    uint8_t crcle[2] = { (uint8_t)(crc & 0xFFu), (uint8_t)(crc >> 8) };

    /* raw-crc concatenate (crc is OUTSIDE the CRC range itself!)       */
    uint8_t stream[SCOPE_PROTO_HDR_LEN + SCOPE_PROTO_MAX_PAYLOAD + 2u];
    memcpy(stream, raw, hdr_pay);
    memcpy(stream + hdr_pay, crcle, 2u);

    const size_t enc = cobs_encode(buf, cap - 1u, stream, hdr_pay + 2u);
    if (enc == 0u)
        return 0u;
    buf[enc] = 0x00u; /* delimiter                                       */
    return enc + 1u;
}

/* ------------------------------------------------------------------ */
void frame_decoder_init(frame_decoder_t *d)
{
    d->used     = 0u;
    d->in_frame = false;
}

/* Little-endian header/CRC readers on the decoded raw buffer.          */
static inline uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

frame_codec_event_t frame_decoder_feed(frame_decoder_t *d,
                                       uint8_t byte,
                                       frame_view_t *out)
{
    const bool is_delim = (byte == 0x00u);

    if (!d->in_frame) {
        if (is_delim)
            return FC_IN_PROGRESS; /* stray delimiter between frames     */
        d->in_frame = true;
        d->used     = 0u;
    }

    if (is_delim) {
        d->in_frame = false;

        /* decode what we have so far (needs >= HDR + CRC bytes)         */
        if (d->used < SCOPE_PROTO_HDR_LEN + 2u) {
            frame_decoder_init(d);
            return FC_INVALID;
        }

        uint8_t raw[SCOPE_PROTO_HDR_LEN + SCOPE_PROTO_MAX_PAYLOAD + 2u];
        const size_t n = cobs_decode(raw, sizeof(raw),
                                     d->buf, d->used);
        if (n < SCOPE_PROTO_HDR_LEN + 2u) {
            frame_decoder_init(d);
            return FC_INVALID;
        }

        const size_t pay_len = (size_t)(n - SCOPE_PROTO_HDR_LEN - 2u);
        const uint16_t crc_rx = le16(raw + n - 2u);
        const uint16_t crc_calc =
            scope_crc16(raw, SCOPE_PROTO_HDR_LEN + pay_len);
        if (crc_rx != crc_calc) {
            frame_decoder_init(d);
            return FC_INVALID;
        }

        frame_view_t v;
        v.version    = raw[0];
        v.type       = raw[1];
        v.seq        = le16(raw + 2);
        v.plen       = le16(raw + 4);
        if (v.version != SCOPE_PROTO_VERSION) {
            frame_decoder_init(d);
            return FC_INVALID;
        }
        if ((size_t)v.plen != pay_len) {
            frame_decoder_init(d);
            return FC_INVALID; /* header plen must match COBS payload    */
        }
        if (out != nullptr) {
            memcpy(out->payload_buf, raw + SCOPE_PROTO_HDR_LEN, pay_len);
            out->version   = v.version;
            out->type      = v.type;
            out->seq       = v.seq;
            out->plen      = v.plen;
            out->payload   = out->payload_buf; /* point at caller buf    */
        }
        return FC_FRAME;
    }

    /* accumulate encoded bytes; oversize → fail once, wait for delim   */
    if (d->used >= sizeof(d->buf)) {
        d->in_frame = false; /* discard until next delimiter            */
        d->used     = 0u;
        return FC_INVALID;
    }
    d->buf[d->used++] = byte;
    return FC_IN_PROGRESS;
}
