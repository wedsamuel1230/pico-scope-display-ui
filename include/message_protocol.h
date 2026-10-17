/**
 * message_protocol.h — wire-level payload layouts for protocol v1
 *
 * Shared by firmware (src/protocol/) and mirrored on the browser in
 * web/js/protocol.js. Both sides ~MUST~ keep field order/width in sync
 * with docs/PROTOCOL_V1.md. All multi-byte fields are little-endian.
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection) runs
 * over the UNENCODED header+payload.
 *
 * Frame:  COBS( hdr: version u8 | type u8 | seq u16 | plen u16
 *               payload: plen bytes
 *               crc16 ) 0x00
 */
#ifndef SCOPE_MESSAGE_PROTOCOL_H
#define SCOPE_MESSAGE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define SCOPE_PROTO_HDR_LEN 6u /* version+type+seq+plen */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- frame header (6 B) ------------------------------------------- */
typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint16_t seq;
    uint16_t plen;
} scope_msg_header_t;

/* ---- HELLO_RSP payload (12 B) ------------------------------------- */
typedef struct {
    uint32_t firmware_version;
    uint32_t capabilities;
    uint16_t max_sample_count;
    uint8_t  voltage_range;   /* 0 = raw codes (no AFE / calibration)   */
    uint8_t  calibration;     /* 0 = not calibrated                     */
} scope_hello_rsp_t;

/* ---- SET_CONFIG payload (10 B) ------------------------------------ */
typedef struct {
    uint32_t sample_rate;    /* requested, clamped by firmware          */
    uint16_t sample_count;   /* requested, clamped                      */
    uint16_t trigger_level;  /* raw ADC code                            */
    uint8_t  edge;           /* 0 = rising, 1 = falling                 */
    uint8_t  mode;           /* 0 = AUTO, 1 = NORMAL                    */
} scope_set_config_t;

/* ---- ACK payload (9 B) -------------------------------------------- */
typedef struct {
    uint16_t request_seq;
    uint8_t  status;           /* SCOPE_ACK_*                           */
    uint32_t applied_rate;     /* actual applied sample rate            */
    uint16_t applied_count;
} scope_ack_t;

/* ---- CAPTURE_DATA payload (9 + 2*count B) -------------------------- */
typedef struct {
    uint32_t rate;          /* ACTUAL applied sample rate               */
    uint16_t count;         /* number of samples in this frame          */
    uint16_t trigger_index; /* 0xFFFF = none (AUTO/no trigger)          */
    uint8_t  flags;         /* SCOPE_FLAG_* bits                        */
    /* samples u16[] follow in the payload                                */
} scope_capture_data_t;

/* ---- ERROR payload (3 + strlen(detail)) --------------------------- */
typedef struct {
    uint16_t seq;
    uint8_t  code;    /* SCOPE_ERR_*                                 */
    const char *detail; /* ASCII, sender copies WITHOUT terminator   */
} scope_error_t;

/* CRC-16/CCITT-FALSE over the raw header+payload bytes                */
uint16_t scope_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* SCOPE_MESSAGE_PROTOCOL_H */
