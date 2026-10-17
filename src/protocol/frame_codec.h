/**
 * frame_codec.h — protocol v1 frame encode/decode (COBS + CRC16)
 *
 * Pure logic, NO Pico SDK dependency: this file is compiled both into
 * the firmware and into the host test-bench (tests/host/).
 *
 * Frame layout (docs/PROTOCOL_V1.md):
 *   COBS( hdr: ver u8 | type u8 | seq u16 LE | plen u16 LE
 *         payload: plen bytes
 *         crc16 LE over unencoded hdr+payload ) 0x00
 */
#ifndef SCOPE_FRAME_CODEC_H
#define SCOPE_FRAME_CODEC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "message_protocol.h"
#include "scope_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* COBS in-place size helpers: worst-case = n + n/254 + 1.              */
#define SCOPE_COBS_MAX_LEN(n) ((n) + (n) / 254u + 1u)

/* ---- CRC ------------------------------------------------------------ */
uint16_t scope_crc16(const uint8_t *data, size_t len);

/* ---- COBS ----------------------------------------------------------- */
/* Encode `src` (len) into `dst` (cap bytes). Returns bytes written or
 * 0 on insufficient buffer. Output never contains 0x00.                 */
size_t cobs_encode(uint8_t *dst, size_t cap, const uint8_t *src, size_t len);

/* Decode a COBS blob WITHOUT delimiter. Returns payload length or 0
 * on invalid encoding (e.g. embedded zero).                             */
size_t cobs_decode(uint8_t *dst, size_t cap, const uint8_t *src, size_t len);

/* ---- one-shot frame encoding ---------------------------------------- */
/* Build a full frame (COBS + delimiter) for `type`/`seq`/payload.
 * Returns total encoded length (>=1) written to buf, or 0 on overflow.  */
size_t frame_encode(uint8_t *buf, size_t cap,
                    uint8_t type, uint16_t seq,
                    const uint8_t *payload, uint16_t plen);

/* ---- incremental decoding ------------------------------------------- */
typedef enum {
    FC_IN_PROGRESS = 0, /* need more bytes                              */
    FC_FRAME,           /* a valid frame: `out` filled                  */
    FC_INVALID,         /* bad CRC/COBS/too large — state was reset     */
} frame_codec_event_t;

typedef struct {
    uint8_t  buf[SCOPE_PROTO_MAX_FRAME];
    size_t   used;      /* encoded bytes accumulated (no delimiter)     */
    bool     in_frame;  /* true after first byte of a frame             */
} frame_decoder_t;

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint16_t seq;
    uint16_t plen;
    const uint8_t *payload; /* points INTO decoder payload_buf; valid
                               until the next feed() call               */
    uint8_t  payload_buf[SCOPE_PROTO_MAX_PAYLOAD];
} frame_view_t;

void   frame_decoder_init(frame_decoder_t *d);

/* Feed one byte; returns event; payload view valid on FC_FRAME.         */
frame_codec_event_t frame_decoder_feed(frame_decoder_t *d,
                                       uint8_t byte,
                                       frame_view_t *out);

#ifdef __cplusplus
}
#endif
#endif /* SCOPE_FRAME_CODEC_H */
