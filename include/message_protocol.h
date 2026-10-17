/**
 * message_protocol.h — wire payload layouts (protocol v2)
 *
 * The wire structs are packed so their sizeof() equals the exact number
 * of bytes on the wire; usb_link.cpp memcpy()s them straight out of a
 * decoded payload. See docs/PROTOCOL_V2.md for the field-by-field table.
 */
#ifndef SCOPE_MESSAGE_PROTOCOL_H
#define SCOPE_MESSAGE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "scope_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#define SCOPE_PACKED __attribute__((packed))
#else
#define SCOPE_PACKED
#endif

/* Frame header: version u8 | type u8 | seq u16 | plen u16 (6 bytes).   */
typedef struct SCOPE_PACKED {
    uint8_t  version;
    uint8_t  type;
    uint16_t seq;
    uint16_t plen;
} scope_msg_header_t;

/* HELLO_RSP — 14 bytes */
typedef struct SCOPE_PACKED {
    uint32_t firmware_version;
    uint32_t capabilities;
    uint16_t max_sample_count;
    uint8_t  voltage_range;
    uint8_t  calibration;
    uint8_t  analog_channels;
    uint8_t  logic_channels;
} scope_hello_rsp_t;

/* SET_CONFIG — 13 bytes */
typedef struct SCOPE_PACKED {
    uint32_t sample_rate;
    uint16_t sample_count;
    uint16_t trigger_level;
    uint8_t  edge;            /* scope_edge_t                          */
    uint8_t  trigger_mode;    /* scope_trig_mode_t                     */
    uint8_t  capture_mode;    /* scope_capture_mode_t                  */
    uint8_t  channel_mask;    /* SCOPE: ADC bitmask; LOGIC: lane mask   */
    uint8_t  trigger_channel; /* SCOPE: ADC bit; LOGIC: 0..7; 0xFF none */
} scope_set_config_t;

/* ACK — 9 bytes */
typedef struct SCOPE_PACKED {
    uint16_t request_seq;
    uint8_t  status;
    uint32_t applied_rate;
    uint16_t applied_count;
} scope_ack_t;

/* CAPTURE_DATA head — 11 bytes; samples follow.
 *
 * SCOPE mode: `count` samples PER CHANNEL and `channels` enabled analog
 * channels follow interleaved as count * channels u16 ADC codes.
 * LOGIC mode: `count` samples follow, one u16 each, low byte = the 8
 * digital lines (bit 0 = the first logic GPIO).                      */
typedef struct SCOPE_PACKED {
    uint32_t rate;   /* SCOPE: per-channel rate; LOGIC: lane rate      */
    uint16_t count;
    uint16_t trigger_index;
    uint8_t  flags;
    uint8_t  channels;
    uint8_t  mode;   /* scope_capture_mode_t                           */
} scope_capture_data_t;

/* ERROR (host-only convenience struct; not memcpy'd from the wire).   */
typedef struct {
    uint16_t    seq;
    uint8_t     code;
    const char *detail;
} scope_error_t;

uint16_t scope_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SCOPE_MESSAGE_PROTOCOL_H */
