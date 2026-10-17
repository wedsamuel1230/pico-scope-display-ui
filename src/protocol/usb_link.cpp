/**
 * usb_link.cpp — TinyUSB CDC transport for protocol v1 frames
 *
 * The CDC interface carries ONLY protocol frames. Firmware must never
 * printf/CSV onto the same byte stream (PROTOCOL_V1.md hard rule).
 */
#include "usb_link.h"

#include <string.h>

#include "bsp/board.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "scope_config.h"

static frame_decoder_t s_decoder;

void usb_link_init(void)
{
    tusb_init();
    frame_decoder_init(&s_decoder);
}

/* ---- helpers -------------------------------------------------------- */
static void send_frame(uint8_t type, uint16_t seq,
                       const uint8_t *payload, uint16_t plen)
{
    uint8_t buf[SCOPE_PROTO_MAX_FRAME];
    const size_t n = frame_encode(buf, sizeof(buf), type, seq,
                                  payload, plen);
    if (n == 0u)
        return;
    tud_task();
    /* write the whole encoded frame in <= CDC buffer chunks            */
    size_t off = 0;
    while (off < n) {
        const uint32_t chunk = tud_cdc_n_write(0, buf + off,
                                               (uint32_t)(n - off));
        if (chunk == 0u) {
            tud_task(); /* wait for space                                */
            continue;
        }
        off += chunk;
    }
    tud_cdc_n_write_flush(0);
}

/* ---- responses per docs/PROTOCOL_V1.md ------------------------------ */
void usb_link_send_hello_rsp(void)
{
    scope_hello_rsp_t rsp;
    rsp.firmware_version = SCOPE_FW_VERSION;
    rsp.capabilities     = SCOPE_CAP_TRIG_RISE | SCOPE_CAP_TRIG_FALL |
                           SCOPE_CAP_SIM;
    rsp.max_sample_count = SCOPE_MAX_SAMPLE_COUNT;
    rsp.voltage_range    = 0u; /* raw ADC codes (no AFE yet)             */
    rsp.calibration      = 0u; /* calibration not applied                */
    send_frame(SCOPE_MSG_HELLO_RSP, 0u,
               (const uint8_t *)&rsp, (uint16_t)sizeof(rsp));
}

void usb_link_send_ack(uint16_t request_seq, uint8_t status,
                       uint32_t applied_rate, uint16_t applied_count)
{
    scope_ack_t ack;
    ack.request_seq   = request_seq;
    ack.status        = status;
    ack.applied_rate  = applied_rate;
    ack.applied_count = applied_count;
    send_frame(SCOPE_MSG_ACK, request_seq,
               (const uint8_t *)&ack, (uint16_t)sizeof(ack));
}

void usb_link_send_capture_data(const uint16_t *samples, uint16_t count,
                                uint16_t trigger_index, uint8_t flags,
                                uint32_t rate)
{
    static uint8_t payload[SCOPE_PROTO_MAX_PAYLOAD];

    /* Serialize explicitly — struct layout could add padding, and the
     * wire layout is FixED little-endian per PROTOCOL_V1.md.            */
    size_t off = 0u;
    payload[off++] = (uint8_t)(rate & 0xFFu);
    payload[off++] = (uint8_t)((rate >> 8) & 0xFFu);
    payload[off++] = (uint8_t)((rate >> 16) & 0xFFu);
    payload[off++] = (uint8_t)((rate >> 24) & 0xFFu);
    payload[off++] = (uint8_t)(count & 0xFFu);
    payload[off++] = (uint8_t)(count >> 8);
    payload[off++] = (uint8_t)(trigger_index & 0xFFu);
    payload[off++] = (uint8_t)(trigger_index >> 8);
    payload[off++] = flags;
    /* off == 9 == CAPTURE_DATA wire head (rate+count+trig+flags)       */

    if (count > 0u && samples != nullptr)
        memcpy(payload + off, samples, (size_t)count * 2u);

    send_frame(SCOPE_MSG_CAPTURE_DATA, 0u, payload,
               (uint16_t)(off + (size_t)count * 2u));
}

void usb_link_send_error(uint16_t seq, uint8_t code, const char *detail)
{
    uint8_t payload[SCOPE_PROTO_MAX_PAYLOAD];
    size_t  off  = 0u;

    payload[off++] = (uint8_t)(seq & 0xFFu);
    payload[off++] = (uint8_t)(seq >> 8);
    payload[off++] = code;
    if (detail != nullptr) {
        const size_t dlen = strlen(detail);
        const size_t room = sizeof(payload) - off;
        const size_t n    = (dlen < room) ? dlen : room;
        memcpy(payload + off, detail, n);
        off += n;
    }
    send_frame(SCOPE_MSG_ERROR, 0u, payload, (uint16_t)off);
}

/* ---- rx pump -------------------------------------------------------- */
void usb_link_poll(usb_link_request_t *out_req)
{
    out_req->event = USB_LINK_NONE;

    tud_task();
    if (!tud_cdc_n_connected(0))
        return;

    uint8_t b;
    while (tud_cdc_n_available(0) > 0 &&
           out_req->event == USB_LINK_NONE) {
        const uint32_t got = tud_cdc_n_read(0, &b, 1u);
        if (got == 0u)
            break;

        frame_view_t view;
        const frame_codec_event_t ev = frame_decoder_feed(&s_decoder, b,
                                                          &view);
        if (ev == FC_FRAME) {
            out_req->seq = view.seq;
            if (view.type == SCOPE_MSG_HELLO_REQ) {
                out_req->event = USB_LINK_HELLO_REQ;
            } else if (view.type == SCOPE_MSG_SET_CONFIG &&
                       view.plen >= sizeof(scope_set_config_t)) {
                memcpy(&out_req->cfg, view.payload, sizeof(out_req->cfg));
                out_req->event = USB_LINK_SET_CONFIG;
            } else if (view.type == SCOPE_MSG_CAPTURE_REQ) {
                out_req->event = USB_LINK_CAPTURE_REQ;
            } else if (view.type == SCOPE_MSG_STOP) {
                out_req->event = USB_LINK_STOP;
            } else {
                /* unknown type / bad payload: ERROR frame, keep alive */
                usb_link_send_error(view.seq, SCOPE_ERR_BAD_TYPE,
                                    "unsupported type");
            }
        } else if (ev == FC_INVALID) {
            usb_link_send_error(0u, SCOPE_ERR_INTERNAL, "frame error");
        }
    }
}
