/**
 * scope_config.h — capture/protocol compile-time limits (v1)
 *
 * Buffers: two ping-pong capture buffers of SCOPE_SAMPLE_COUNT samples
 * (u16 ADC codes each). 1024 samples x 2 B x 2 buffers = 4 KiB, matching
 * docs/IMPLEMENTATION_GUIDE.md (no full framebuffer for the LCD — the
 * 150 KiB full-frame alternative is explicitly forbidden there).
 */
#ifndef SCOPE_CONFIG_H
#define SCOPE_CONFIG_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Capture                                                            */
/* ------------------------------------------------------------------ */
#define SCOPE_SAMPLE_COUNT      1024u            /* samples per capture */
#define SCOPE_MAX_SAMPLE_COUNT  2048u            /* protocol bound      */
#define SCOPE_MIN_SAMPLE_COUNT  64u

/* RP2040 ADC single-shot ceiling is ~500 kS/s/nominal; typical real
 * rates depend on clk_sys/div. Values the SET_CONFIG clamps into.      */
#define SCOPE_MAX_RATE_HZ       500000u
#define SCOPE_MIN_RATE_HZ       1000u
#define SCOPE_DEFAULT_RATE_HZ   100000u

/* Trigger search hysteresis in ADC codes (see src/trigger module).     */
#define SCOPE_TRIGGER_HYSTERESIS 64

/* Capture DMA timeout (ADC done via DMA, but protect against stalls).  */
#define SCOPE_CAPTURE_TIMEOUT_MS 500u

/* ------------------------------------------------------------------ */
/* Protocol v1 (see docs/PROTOCOL_V1.md — DO NOT change without bump)  */
/* ------------------------------------------------------------------ */
#define SCOPE_PROTO_VERSION     1u

/* Frame payload bound. CAPTURE_DATA worst case = 9 B hdr + 2*2048 smpl */
#define SCOPE_PROTO_MAX_PAYLOAD (9u + 2u * SCOPE_MAX_SAMPLE_COUNT)
/* COBS worst case = data + data/254 + 1, plus delimiter byte.          */
#define SCOPE_PROTO_MAX_FRAME                                 \
    (SCOPE_PROTO_MAX_PAYLOAD + SCOPE_PROTO_MAX_PAYLOAD / 254u \
     + 2u)

/* Message type identifiers (little-endian on the wire).               */
#define SCOPE_MSG_HELLO_REQ     0x01u /* browser -> pico, empty          */
#define SCOPE_MSG_HELLO_RSP     0x02u /* pico -> browser                 */
#define SCOPE_MSG_SET_CONFIG    0x03u /* browser -> pico                 */
#define SCOPE_MSG_ACK           0x04u /* pico -> browser                 */
#define SCOPE_MSG_CAPTURE_REQ   0x05u /* browser -> pico, empty          */
#define SCOPE_MSG_CAPTURE_DATA  0x06u /* pico -> browser                 */
#define SCOPE_MSG_STOP          0x07u /* browser -> pico, empty          */
#define SCOPE_MSG_ERROR         0x08u /* either -> other                 */

/* ACK status codes                                                    */
#define SCOPE_ACK_OK            0x00u
#define SCOPE_ACK_E_VERSION     0x01u /* unsupported proto version       */
#define SCOPE_ACK_E_RANGE       0x02u /* config field out of range       */
#define SCOPE_ACK_E_BUSYLE      0x03u /* busy, cannot apply now          */

/* CAPTURE_DATA status flag bits (status u8)                           */
#define SCOPE_FLAG_NONE         0x00u
#define SCOPE_FLAG_OVERFLOW     0x01u /* ADC FIFO overflow              */
#define SCOPE_FLAG_NO_TRIGGER   0x02u /* AUTO mode: no trigger found    */
#define SCOPE_FLAG_NOT_CALIB    0x04u /* calibration not done yet       */
#define SCOPE_FLAG_TRUNCATED    0x08u /* sample buffer short            */

/* ERROR codes                                                         */
#define SCOPE_ERR_NONE          0x00u
#define SCOPE_ERR_BAD_VERSION   0x01u
#define SCOPE_ERR_BAD_TYPE      0x02u
#define SCOPE_ERR_CFG_RANGE     0x03u
#define SCOPE_ERR_BUSY          0x04u
#define SCOPE_ERR_INTERNAL      0x05u

/* Capability flags (HELLO_RSP u32)                                    */
#define SCOPE_CAP_SIM           (1u << 0) /* host simulator present     */
#define SCOPE_CAP_TRIG_RISE     (1u << 1)
#define SCOPE_CAP_TRIG_FALL     (1u << 2)
#define SCOPE_CAP_CALIB         (1u << 3) /* calibration layer exists    */

/* Software version stamped into HELLO_RSP (fw_version u32).            */
#define SCOPE_FW_VERSION        0x00010000u /* v0.1.0 */

/* Firmware build knob: platformio.ini -DSCOPE_ENABLE_USB_CDC=1 builds
 * the TinyUSB CDC link (src/protocol/usb_link.cpp). Disable while the
 * toolchain integration is being validated per docs stage 0.           */
#ifndef SCOPE_ENABLE_USB_CDC
#define SCOPE_ENABLE_USB_CDC    1
#endif

#endif /* SCOPE_CONFIG_H */
