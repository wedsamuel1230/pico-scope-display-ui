/**
 * usb_link.h — TinyUSB CDC host<->device link (protocol v2 transport)
 *
 * Handles CDC<byte-stream> ONLY: no printf/CSV may ever share this bus
 * (docs/PROTOCOL_V2.md rule). Build-gated by SCOPE_ENABLE_USB_CDC.
 */
#ifndef SCOPE_USB_LINK_H
#define SCOPE_USB_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "frame_codec.h"
#include "message_protocol.h"
#include "scope_config.h"

typedef enum {
    USB_LINK_NONE = 0,
    USB_LINK_HELLO_REQ,
    USB_LINK_SET_CONFIG,   /* cfg_reply filled (requested, pre-clamp)   */
    USB_LINK_CAPTURE_REQ,
    USB_LINK_STOP,
} usb_link_event_t;

typedef struct {
    usb_link_event_t   event;
    uint16_t           seq;
    scope_set_config_t cfg; /* when event == USB_LINK_SET_CONFIG         */
} usb_link_request_t;

void usb_link_init(void);

/* Pump CDC rx into frame decoder; consume at most one request.        */
void usb_link_poll(usb_link_request_t *out_req);

/* Send helpers (encode + CDC write).                                   */
void usb_link_send_hello_rsp(void);

void usb_link_send_ack(uint16_t request_seq, uint8_t status,
                       uint32_t applied_rate, uint16_t applied_count);

void usb_link_send_error(uint16_t seq, uint8_t code, const char *detail);

/* SCOPE payload: `count` samples PER CHANNEL across `channels` enabled
 * analog channels, emitted interleaved as count * channels u16 codes.  */
void usb_link_send_capture_data(const uint16_t *samples, uint16_t count,
                                uint16_t trigger_index, uint8_t flags,
                                uint32_t rate, uint8_t channels, uint8_t mode);

/* LOGIC payload: `count` samples, one u16 each, low byte = 8 lines.    */
void usb_link_send_logic_data(const uint32_t *samples, uint16_t count,
                              uint16_t trigger_index, uint8_t flags,
                              uint32_t rate);

#endif /* SCOPE_USB_LINK_H */
