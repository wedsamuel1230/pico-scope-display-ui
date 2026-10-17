/**
 * usb_link.cpp — TinyUSB CDC transport for protocol v2 frames
 *
 * The CDC interface carries ONLY protocol frames. Firmware must never
 * printf/CSV onto the same byte stream (docs/PROTOCOL_V2.md hard rule).
 */
#include "usb_link.h"

#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "board_config.h"
#include "scope_config.h"
#include "scope_types.h"

static frame_decoder_t s_decoder;

/* One shared staging buffer: a full logic capture is the largest frame
 * the device ever emits (SCOPE_PROTO_MAX_PAYLOAD = 16395 bytes).        */
static uint8_t s_payload[SCOPE_PROTO_MAX_PAYLOAD];

void usb_link_init(void)
{
    tusb_init();
    frame_decoder_init(&s_decoder);
}

/* ---- little-endian field writers ------------------------------------ */
static void put_u16(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)((v >> 8) & 0xFFu);
    dst[2] = (uint8_t)((v >> 16) & 0xFFu);
    dst[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* ---- transport ------------------------------------------------------- */
static void send_frame(uint8_t type, uint16_t seq,
                       const uint8_t *payload, uint16_t plen)
{
    static uint8_t buf[SCOPE_PROTO_MAX_FRAME];

    const size_t n = frame_encode(buf, sizeof(buf), type, seq, payload, plen);
    if (n == 0u)
        return;
    if (!tud_cdc_n_connected(0))
        return;

    size_t   off    = 0u;
    uint32_t stalls = 0u;

    /* Write in whatever chunks the CDC FIFO will take. Bail out rather
     * than spin forever if the host has stopped draining the port.     */
    while (off < n && stalls < 4096u) {
        const uint32_t room = tud_cdc_n_write_available(0);
        if (room == 0u) {
            tud_cdc_n_write_flush(0);
            tud_task();
            ++stalls;
            continue;
        }

        size_t chunk = n - off;
        if (chunk > (size_t)room)
            chunk = (size_t)room;

        const uint32_t wrote = tud_cdc_n_write(0, buf + off, (uint32_t)chunk);
        if (wrote == 0u) {
            tud_task();
            ++stalls;
            continue;
        }

        off += (size_t)wrote;
        tud_cdc_n_write_flush(0);
    }

    tud_cdc_n_write_flush(0);
}

/* ---- outbound messages ----------------------------------------------- */
void usb_link_send_hello_rsp(void)
{
    scope_hello_rsp_t rsp;
    memset(&rsp, 0, sizeof(rsp));

    rsp.firmware_version = SCOPE_FW_VERSION;
    rsp.capabilities     = SCOPE_CAP_SIM | SCOPE_CAP_TRIG_RISE |
                           SCOPE_CAP_TRIG_FALL | SCOPE_CAP_MULTI_ADC |
                           SCOPE_CAP_LOGIC;
    rsp.max_sample_count = SCOPE_MAX_SAMPLE_COUNT;
    rsp.voltage_range    = 0u;  /* no AFE: raw ADC codes are reported   */
    rsp.calibration      = 0u;
    rsp.analog_channels  = SCOPE_ADC_CHANNELS;
    rsp.logic_channels   = SCOPE_LOGIC_CHANNELS;

    send_frame(SCOPE_MSG_HELLO_RSP, 0u, (const uint8_t *)&rsp, sizeof(rsp));
}

void usb_link_send_ack(uint16_t request_seq, uint8_t status,
                       uint32_t applied_rate, uint16_t applied_count)
{
    scope_ack_t ack;
    memset(&ack, 0, sizeof(ack));

    ack.request_seq   = request_seq;
    ack.status        = status;
    ack.applied_rate  = applied_rate;
    ack.applied_count = applied_count;

    send_frame(SCOPE_MSG_ACK, request_seq, (const uint8_t *)&ack,
               sizeof(ack));
}

void usb_link_send_error(uint16_t seq, uint8_t code, const char *detail)
{
    uint8_t  payload[64];
    uint16_t off = 0u;

    put_u16(payload + 0u, seq);
    off = 2u;
    payload[off++] = code;

    if (detail != nullptr) {
        while (*detail != '\0' && off < (uint16_t)(sizeof(payload) - 1u))
            payload[off++] = (uint8_t)*detail++;
    }

    send_frame(SCOPE_MSG_ERROR, seq, payload, off);
}

void usb_link_send_capture_data(const uint16_t *samples, uint16_t count,
                                uint16_t trigger_index, uint8_t flags,
                                uint32_t rate, uint8_t channels, uint8_t mode)
{
    if (samples == nullptr || count == 0u || channels == 0u)
        return;

    const uint32_t values = (uint32_t)count * (uint32_t)channels;
    const uint32_t bytes  = values * 2u;

    if (bytes > (uint32_t)sizeof(s_payload) - SCOPE_PROTO_CAPTURE_HEAD_LEN)
        return;

    put_u32(s_payload + 0u, rate);
    put_u16(s_payload + 4u, count);
    put_u16(s_payload + 6u, trigger_index);
    s_payload[8]  = flags;
    s_payload[9]  = channels;
    s_payload[10] = mode;

    memcpy(s_payload + SCOPE_PROTO_CAPTURE_HEAD_LEN, samples, bytes);

    send_frame(SCOPE_MSG_CAPTURE_DATA, 0u, s_payload,
               (uint16_t)(SCOPE_PROTO_CAPTURE_HEAD_LEN + bytes));
}

void usb_link_send_logic_data(const uint32_t *samples, uint16_t count,
                              uint16_t trigger_index, uint8_t flags,
                              uint32_t rate)
{
    if (samples == nullptr || count == 0u)
        return;

    const uint32_t bytes = (uint32_t)count * 2u;

    if (bytes > (uint32_t)sizeof(s_payload) - SCOPE_PROTO_CAPTURE_HEAD_LEN)
        return;

    put_u32(s_payload + 0u, rate);
    put_u16(s_payload + 4u, count);
    put_u16(s_payload + 6u, trigger_index);
    s_payload[8]  = flags;
    s_payload[9]  = (uint8_t)SCOPE_LOGIC_CHANNELS;
    s_payload[10] = (uint8_t)SCOPE_MODE_LOGIC;

    /* Only the low byte is meaningful: the PIO snapshot is 32 bits wide
     * but bits 8+ come from GPIO13 and up (LCD pins).                  */
    for (uint16_t i = 0u; i < count; ++i) {
        const uint16_t lane =
            (uint16_t)(samples[i] & (uint32_t)SCOPE_LOGIC_PIN_MASK);
        put_u16(s_payload + SCOPE_PROTO_CAPTURE_HEAD_LEN + (uint32_t)i * 2u,
                lane);
    }

    send_frame(SCOPE_MSG_CAPTURE_DATA, 0u, s_payload,
               (uint16_t)(SCOPE_PROTO_CAPTURE_HEAD_LEN + bytes));
}

/* ---- inbound --------------------------------------------------------- */
void usb_link_poll(usb_link_request_t *out_req)
{
    if (out_req == nullptr)
        return;

    out_req->event = USB_LINK_NONE;

    tud_task();

    if (!tud_cdc_n_connected(0))
        return;

    /* Decoded-payload staging (~16 KiB). Stop as soon as one usable
     * request has been produced so the caller can act on it.           */
    frame_view_t view;

    while (tud_cdc_n_available(0) > 0u && out_req->event == USB_LINK_NONE) {
        uint8_t b = 0u;
        if (tud_cdc_n_read(0, &b, 1u) != 1u)
            break;

        const frame_codec_event_t ev =
            frame_decoder_feed(&s_decoder, b, &view);

        if (ev == FC_FRAME) {
            out_req->seq = view.seq;

            switch (view.type) {
            case SCOPE_MSG_HELLO_REQ:
                out_req->event = USB_LINK_HELLO_REQ;
                break;

            case SCOPE_MSG_SET_CONFIG:
                if (view.plen >= (uint16_t)sizeof(scope_set_config_t)) {
                    memcpy((void *)&out_req->cfg, view.payload,
                           sizeof(out_req->cfg));
                    out_req->event = USB_LINK_SET_CONFIG;
                } else {
                    usb_link_send_error(view.seq, SCOPE_ERR_CFG_RANGE,
                                        "short SET_CONFIG");
                }
                break;

            case SCOPE_MSG_CAPTURE_REQ:
                out_req->event = USB_LINK_CAPTURE_REQ;
                break;

            case SCOPE_MSG_STOP:
                out_req->event = USB_LINK_STOP;
                break;

            default:
                usb_link_send_error(view.seq, SCOPE_ERR_BAD_TYPE,
                                    "unsupported type");
                break;
            }
        } else if (ev == FC_INVALID) {
            usb_link_send_error(0u, SCOPE_ERR_INTERNAL, "frame error");
        }
    }
}
