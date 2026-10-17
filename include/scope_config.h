/**
 * scope_config.h — firmware limits and USB CDC protocol constants (v2)
 *
 * v2 adds multi-channel analog capture (3 ADC channels) and a
 * logic-analyzer capture mode (8 digital channels). Everything below is
 * shared by the firmware and by web/js/protocol.js; keep them in step.
 */
#ifndef SCOPE_CONFIG_H
#define SCOPE_CONFIG_H

/* ------------------------------------------------------------------ */
/* Capture depth                                                       */
/* ------------------------------------------------------------------ */
#define SCOPE_SAMPLE_COUNT       1024u /* default, per analog channel */
#define SCOPE_MAX_SAMPLE_COUNT   2048u /* per analog channel          */
#define SCOPE_MIN_SAMPLE_COUNT     64u

#define SCOPE_MAX_LOGIC_SAMPLES  8192u /* logic samples per capture   */
#define SCOPE_MIN_LOGIC_SAMPLES    64u

/* ------------------------------------------------------------------ */
/* Sample rates                                                        */
/* ------------------------------------------------------------------ */
/* Analog: the TOTAL ADC conversion rate. Because the enabled channels
 * are converted round-robin, the per-channel rate is the total divided
 * by the number of enabled channels.                                  */
#define SCOPE_MAX_RATE_HZ        500000u
#define SCOPE_MIN_RATE_HZ          1000u
#define SCOPE_DEFAULT_RATE_HZ    100000u

/* Logic: the PIO sample rate. NOMINAL and uncalibrated — see
 * docs/REFERENCE_PROJECTS.md on trusting claimed sample rates.         */
#define SCOPE_MAX_LOGIC_RATE_HZ 10000000u
#define SCOPE_MIN_LOGIC_RATE_HZ     1000u
#define SCOPE_DEFAULT_LOGIC_RATE_HZ 1000000u

#define SCOPE_TRIGGER_HYSTERESIS     64
#define SCOPE_CAPTURE_TIMEOUT_MS    500u

/* ------------------------------------------------------------------ */
/* Protocol                                                            */
/* ------------------------------------------------------------------ */
#define SCOPE_PROTO_VERSION 2u
#define SCOPE_FW_VERSION    0x00020000u

#define SCOPE_PROTO_HDR_LEN           6u
#define SCOPE_PROTO_SET_CONFIG_LEN   13u
#define SCOPE_PROTO_CAPTURE_HEAD_LEN 11u
#define SCOPE_PROTO_HELLO_RSP_LEN    14u

/* Largest decoded payload: a full logic capture (head + 2 B/sample).   */
#define SCOPE_PROTO_MAX_PAYLOAD \
    (SCOPE_PROTO_CAPTURE_HEAD_LEN + 2u * SCOPE_MAX_LOGIC_SAMPLES)
#define SCOPE_PROTO_MAX_FRAME \
    (SCOPE_PROTO_MAX_PAYLOAD + SCOPE_PROTO_MAX_PAYLOAD / 254u + 2u)

/* Message ids */
#define SCOPE_MSG_HELLO_REQ    0x01u
#define SCOPE_MSG_HELLO_RSP    0x02u
#define SCOPE_MSG_SET_CONFIG   0x03u
#define SCOPE_MSG_ACK          0x04u
#define SCOPE_MSG_CAPTURE_REQ  0x05u
#define SCOPE_MSG_CAPTURE_DATA 0x06u
#define SCOPE_MSG_STOP         0x07u
#define SCOPE_MSG_ERROR        0x08u

/* ACK status codes */
#define SCOPE_ACK_OK        0x00u
#define SCOPE_ACK_E_VERSION 0x01u
#define SCOPE_ACK_E_RANGE   0x02u
#define SCOPE_ACK_E_BUSY    0x03u

/* Capture flags */
#define SCOPE_FLAG_NONE       0x00u
#define SCOPE_FLAG_OVERFLOW   0x01u
#define SCOPE_FLAG_NO_TRIGGER 0x02u
#define SCOPE_FLAG_NOT_CALIB  0x04u
#define SCOPE_FLAG_TRUNCATED  0x08u

/* Error codes */
#define SCOPE_ERR_NONE        0x00u
#define SCOPE_ERR_BAD_VERSION 0x01u
#define SCOPE_ERR_BAD_TYPE    0x02u
#define SCOPE_ERR_CFG_RANGE   0x03u
#define SCOPE_ERR_BUSY        0x04u
#define SCOPE_ERR_INTERNAL    0x05u

/* Capability bits */
#define SCOPE_CAP_SIM       (1u << 0)
#define SCOPE_CAP_TRIG_RISE (1u << 1)
#define SCOPE_CAP_TRIG_FALL (1u << 2)
#define SCOPE_CAP_CALIB     (1u << 3)
#define SCOPE_CAP_MULTI_ADC (1u << 4)
#define SCOPE_CAP_LOGIC     (1u << 5)

#ifndef SCOPE_ENABLE_USB_CDC
#define SCOPE_ENABLE_USB_CDC 1
#endif

#endif /* SCOPE_CONFIG_H */
