# Protocol v2 — host ⇄ Pico USB CDC

Protocol **v2** adds multi-channel analog capture (3 ADC channels) and a
logic-analyzer capture mode (8 digital channels) on top of the v1 framing.

- Firmware constants: `include/scope_config.h`
- Wire structs: `include/message_protocol.h`
- Browser codec: `web/js/protocol.js`
- Demo transport: `web/js/serial.js` (byte-compatible with this document)

The single source of truth is `include/scope_config.h`. If a constant moves
there, it moves here too.

`docs/PROTOCOL_V1.md` is kept for history. v2 is **not** backward compatible:
the version byte changed and three payload layouts grew, so a v1 host and a v2
firmware will refuse each other at `HELLO`.

## Contents

1. [Transport and framing](#transport-and-framing)
2. [Frame header](#frame-header)
3. [Version negotiation](#version-negotiation)
4. [Message IDs](#message-ids)
5. [HELLO_RSP — 14 bytes](#hello_rsp--14-bytes)
6. [SET_CONFIG — 13 bytes](#set_config--13-bytes)
7. [ACK — 9 bytes](#ack--9-bytes)
8. [CAPTURE_REQ](#capture_req)
9. [CAPTURE_DATA — 11-byte head](#capture_data--11-byte-head)
10. [STOP and ERROR](#stop-and-error)
11. [Rate semantics](#rate-semantics)
12. [Flags and capabilities](#flags-and-capabilities)
13. [Limits](#limits)
14. [Worked byte examples](#worked-byte-examples)
15. [Changes from v1](#changes-from-v1)

---

## Transport and framing

The link is a raw USB CDC stream (no line discipline, no baud rate). The
firmware **never** writes anything to it except framed packets — debug output
goes to UART only — so the byte stream is always valid for the parser.

Every frame is:

```
COBS( header | payload | crc16 )   0x00
```

| Piece    | Size            | Notes                                     |
|----------|-----------------|-------------------------------------------|
| header   | 6 bytes         | see below                                 |
| payload  | `plen` bytes    | `0`, or exactly the size of the message    |
| crc16    | 2 bytes         | CRC-16/CCITT-FALSE over header+payload     |
| delimiter| 1 byte          | `0x00`, not part of the COBS input         |

Rules:

- **Little-endian** for every multi-byte field.
- **CRC-16/CCITT-FALSE**: poly `0x1021`, init `0xFFFF`, no reflection, no final
  xor. Check: `crc16("123456789") == 0x29B1`, `crc16("") == 0xFFFF`.
- The CRC covers the **unencoded** header and payload, in that order.
- COBS-encoding guarantees no `0x00` inside the encoded span, so a single `0x00`
  byte unambiguously terminates a frame.
- A frame whose CRC fails is discarded; the decoder resets and resyncs on the
  next `0x00`. A decoder that has seen a partial frame keeps buffering until
  either a complete valid frame appears or the buffer overflows.
- `SCOPE_PROTO_MAX_FRAME` is the largest encoded frame the firmware will accept.

## Frame header

6 bytes, `SCOPE_PROTO_HDR_LEN`:

| Offset | Field   | Type | Meaning                              |
|-------:|---------|------|--------------------------------------|
| 0      | version | u8   | protocol version, must be `2`        |
| 1      | type    | u8   | message id (see the table below)     |
| 2..3   | seq     | u16  | host-chosen sequence number          |
| 4..5   | plen    | u16  | payload length in bytes              |

`seq` is echoed back in the ACK for SET_CONFIG and CAPTURE_REQ so the host can
correlate. The firmware does not otherwise care about its value; the browser
codec increments it per request and wraps at 16 bits.

## Version negotiation

1. Host sends `HELLO_REQ` with `version = 2`.
2. Firmware replies `HELLO_RSP` reporting `firmware_version`, `capabilities`,
   `max_sample_count` (per analog channel), `voltage_range`, `calibration`,
   `analog_channels` and `logic_channels`.
3. If the versions disagree, the firmware answers with `ACK` status
   `SCOPE_ACK_E_VERSION` (`0x01`) and/or `ERROR` `SCOPE_ERR_BAD_VERSION`
   (`0x01`), and the host must not proceed.

A v2 firmware advertises `SCOPE_CAP_MULTI_ADC` and `SCOPE_CAP_LOGIC`; a host
that finds them absent knows to fall back to single-channel behavior.

## Message IDs

| ID   | Name         | Direction | Payload                |
|------|--------------|-----------|------------------------|
| 0x01 | HELLO_REQ    | host → fw | `version` u8           |
| 0x02 | HELLO_RSP    | fw → host | 14 bytes (below)       |
| 0x03 | SET_CONFIG   | host → fw | 13 bytes (below)       |
| 0x04 | ACK          | fw → host | 9 bytes (below)        |
| 0x05 | CAPTURE_REQ  | host → fw | none (`plen = 0`)      |
| 0x06 | CAPTURE_DATA | fw → host | 11-byte head + samples |
| 0x07 | STOP         | both      | none (`plen = 0`)      |
| 0x08 | ERROR        | fw → host | `code` u8              |

## HELLO_RSP — 14 bytes

`SCOPE_PROTO_HELLO_RSP_LEN`. Host sends `HELLO_REQ` (payload `02`), firmware
answers:

| Offset | Field            | Type | Meaning                                        |
|-------:|------------------|------|------------------------------------------------|
| 0..3   | firmware_version | u32  | `0x00020000` = v2.0.0                          |
| 4..7   | capabilities     | u32  | capability bitmask (below)                     |
| 8..9   | max_sample_count | u16  | max samples **per analog channel** (`2048`)    |
| 10     | voltage_range    | u8   | `0` = unknown / no AFE                         |
| 11     | calibration      | u8   | `0` = uncalibrated                             |
| 12     | analog_channels  | u8   | `3`                                            |
| 13     | logic_channels   | u8   | `8`                                            |

A v2.0.0 Pico answers with `capabilities = 0x37`
(`SIM | TRIG_RISE | TRIG_FALL | MULTI_ADC | LOGIC`).

`max_sample_count` describes the **analog** buffer depth. Logic captures are
bounded separately by `SCOPE_MAX_LOGIC_SAMPLES` (8192) and are not advertised
here.

## SET_CONFIG — 13 bytes

`SCOPE_PROTO_SET_CONFIG_LEN`. Configures both capture engines; the meaning of
`channel_mask` and `trigger_channel` depends on `capture_mode`.

| Offset | Field           | Type | Meaning (SCOPE)                         | Meaning (LOGIC)                  |
|-------:|-----------------|------|-----------------------------------------|----------------------------------|
| 0..3   | sample_rate     | u32  | **total** ADC conversion rate, Hz       | PIO lane rate, Hz                |
| 4..5   | sample_count    | u16  | samples **per channel**                 | total samples                    |
| 6..7   | trigger_level   | u16  | ADC code `0..4095`                      | ignored (bit edges only)         |
| 8      | edge            | u8   | `0` rising, `1` falling                 | `0` rising, `1` falling          |
| 9      | trigger_mode    | u8   | `0` auto, `1` normal                    | `0` auto, `1` normal             |
| 10     | capture_mode    | u8   | `0` SCOPE                               | `1` LOGIC                        |
| 11     | channel_mask    | u8   | ADC bitmask, bit *n* = ADC*n*           | lane mask, bit *n* = D*n*        |
| 12     | trigger_channel | u8   | ADC index to trigger on                 | lane `0..7`, `0xFF` = none       |

Clamping is **mode-aware** and happens in the firmware (`config_clamp()` in
`src/main.cpp`); the demo mirrors it exactly.

**SCOPE** (`capture_mode = 0`):

- `sample_rate` is clamped to `[1000, 500000]` and is the **total** rate. The
  per-channel rate reported in CAPTURE_DATA is `rate / popcount(channel_mask)`.
- `channel_mask` must have at least one bit set; bits above
  `SCOPE_ADC_CHANNELS - 1` are dropped, so the usable mask is `0x01..0x07`.
- `sample_count` is clamped to `[64, 2048]` per channel.
- `trigger_channel` selects the ADC bit used for the edge search; `0xFF` means
  "first enabled channel".
- `trigger_level` is an **ADC code**, never a voltage. There is no AFE.

**LOGIC** (`capture_mode = 1`):

- `sample_rate` is clamped to `[1000, 10000000]` and is the **lane** rate — all
  eight lanes are sampled simultaneously, so there is no divide-by-N.
- `sample_count` is clamped to `[64, 8192]` and is the total sample count.
- `channel_mask` selects which lanes participate in the displayed capture;
  `0xFF` means all eight.
- `trigger_channel` is `0..7` selecting the bit of the low byte, or `0xFF` for
  no trigger.
- `trigger_level` is ignored: digital triggers are bit edges, not levels.
- Rates are **nominal and uncalibrated**. The PIO clock divisor is derived from
  the system clock and rounded; treat the reported rate as indicative.

## ACK — 9 bytes

Sent after `HELLO_REQ`, `SET_CONFIG` and `CAPTURE_REQ`.

| Offset | Field       | Type | Meaning                                     |
|-------:|-------------|------|---------------------------------------------|
| 0..1   | request_seq | u16  | echoes the `seq` of the request             |
| 2      | status      | u8   | see below                                   |
| 3..6   | applied_rate| u32  | **total** ADC rate (SCOPE) / lane rate (LOGIC) |
| 7..8   | applied_count| u16 | accepted `sample_count`                     |

`applied_rate` / `applied_count` are the values actually in force after
clamping, so a host can update its controls to match.

| Status | Name              | Meaning                                    |
|-------:|-------------------|--------------------------------------------|
| 0x00   | `SCOPE_ACK_OK`    | accepted                                   |
| 0x01   | `SCOPE_ACK_E_VERSION` | protocol version mismatch              |
| 0x02   | `SCOPE_ACK_E_RANGE`   | request out of range after clamping    |
| 0x03   | `SCOPE_ACK_E_BUSY`    | a capture is already in flight         |

> v1 declared this code as the misspelt `SCOPE_ACK_E_BUSYLE`. v2 uses
> `SCOPE_ACK_E_BUSY`.

## CAPTURE_REQ

Empty payload. Arms a single capture. The firmware answers with an `ACK`, then
emits one `CAPTURE_DATA` frame when the buffer is filled (or when the capture
times out, `SCOPE_CAPTURE_TIMEOUT_MS = 500`).

There is no streaming mode: one request, one buffer.

## CAPTURE_DATA — 11-byte head

`SCOPE_PROTO_CAPTURE_HEAD_LEN`, followed by `count` (or `count * channels`)
16-bit samples.

| Offset | Field         | Type | Meaning                                        |
|-------:|---------------|------|------------------------------------------------|
| 0..3   | rate          | u32  | **per-channel** rate (SCOPE) / lane rate (LOGIC) |
| 4..5   | count         | u16  | SCOPE: samples **per channel**. LOGIC: samples |
| 6..7   | trigger_index | u16  | sample index of the trigger, `0xFFFF` = none   |
| 8      | flags         | u8   | flag bitmask (below)                           |
| 9      | channels      | u8   | SCOPE: enabled channel count. LOGIC: `8`       |
| 10     | mode          | u8   | `0` SCOPE, `1` LOGIC                           |

The `mode` byte is what the parser branches on; everything about the payload's
shape follows from it.

### SCOPE payload

`count * channels` samples, interleaved, in **dense ascending slot order**:
slot 0 is the lowest-numbered enabled ADC channel, slot 1 the next enabled one,
and so on (see `adc_slot_for_channel()` in `src/capture/deinterleave.cpp`).

```
[ s0c0, s0c1, s0c2, s1c0, s1c1, s1c2, ... ]   each a u16 ADC code
```

Each value is a raw 12-bit ADC code in `0..4095`. Sample *i* of slot *k* lives
at index `i * channels + k`. Sample *i* sits at `t = i / rate` where `rate` is
the **per-channel** rate from the head.

### LOGIC payload

`count` samples, one `u16` each. The **low byte** holds the eight lines:

```
bit 0 = first logic GPIO (GPIO5)   ...   bit 7 = GPIO12
```

Bits 8–15 are always zero — the firmware masks each 32-bit PIO snapshot with
`SCOPE_LOGIC_PIN_MASK` (`0xFF`) before packing, because the PIO window
continues into GPIO13+ which is used by the LCD. Sample *i* sits at
`t = i / rate`.

## STOP and ERROR

- `STOP` (either direction, empty payload) aborts an in-flight capture.
- `ERROR` carries a single `code` byte:

| Code | Name                    | Meaning                       |
|-----:|-------------------------|-------------------------------|
| 0x00 | `SCOPE_ERR_NONE`         | no error                      |
| 0x01 | `SCOPE_ERR_BAD_VERSION`  | protocol version mismatch     |
| 0x02 | `SCOPE_ERR_BAD_TYPE`     | unknown message id            |
| 0x03 | `SCOPE_ERR_CFG_RANGE`    | configuration out of range    |
| 0x04 | `SCOPE_ERR_BUSY`         | capture already in flight     |
| 0x05 | `SCOPE_ERR_INTERNAL`     | internal failure              |

## Rate semantics

The two rate fields are **deliberately asymmetric** between the two modes. This
is the single most confusing part of the protocol, so it is stated twice.

| Field                | SCOPE mode        | LOGIC mode |
|----------------------|-------------------|------------|
| SET_CONFIG `sample_rate` | **total** ADC rate | lane rate |
| ACK `applied_rate`   | **total** ADC rate | lane rate |
| CAPTURE_DATA `rate`  | **per-channel** rate | lane rate |

Reason: the analog engine converts the enabled channels round-robin, so one
"conversion slot" is shared. A host that wants 100 kS/s *per channel* on three
channels must ask for **300 kS/s total**, and will then see
`CAPTURE_DATA.rate == 100000`. The logic engine has one lane rate because all
eight lanes are captured together.

The browser codec exposes this as `ScopeProto.channelCount(mask)` (popcount) and
`ScopeProto.channelRate(total, mask)`.

## Flags and capabilities

CAPTURE_DATA `flags`:

| Bit | Name                    | Meaning                                     |
|----:|-------------------------|---------------------------------------------|
| 0x01 | `SCOPE_FLAG_OVERFLOW`   | the engine overran; samples were dropped     |
| 0x02 | `SCOPE_FLAG_NO_TRIGGER` | no qualifying edge was found                 |
| 0x04 | `SCOPE_FLAG_NOT_CALIB`  | values are **raw, uncalibrated**             |
| 0x08 | `SCOPE_FLAG_TRUNCATED`  | the buffer was cut short                     |

`SCOPE_FLAG_NOT_CALIB` is **always** set by this firmware: there is no analog
front end and no calibration stage, so all amplitudes are ADC codes. The UI must
render `C/DIV`, never `V/DIV`.

HELLO_RSP `capabilities`:

| Bit    | Name                    | Meaning                            |
|-------:|-------------------------|------------------------------------|
| 1<<0   | `SCOPE_CAP_SIM`         | simulator / demo capable           |
| 1<<1   | `SCOPE_CAP_TRIG_RISE`   | rising-edge trigger                |
| 1<<2   | `SCOPE_CAP_TRIG_FALL`   | falling-edge trigger               |
| 1<<3   | `SCOPE_CAP_CALIB`       | self-calibration supported         |
| 1<<4   | `SCOPE_CAP_MULTI_ADC`   | more than one analog channel       |
| 1<<5   | `SCOPE_CAP_LOGIC`       | logic-analyzer mode                |

## Limits

| Constant                     | Value   | Applies to                       |
|------------------------------|---------|----------------------------------|
| `SCOPE_PROTO_VERSION`        | 2       | frame header `version`           |
| `SCOPE_FW_VERSION`           | 0x00020000 | HELLO_RSP                    |
| `SCOPE_ADC_CHANNELS`         | 3       | usable ADC inputs                |
| `SCOPE_LOGIC_CHANNELS`       | 8       | logic lanes                      |
| `SCOPE_MIN_RATE_HZ`          | 1000    | SCOPE total rate, LOGIC rate     |
| `SCOPE_MAX_RATE_HZ`          | 500000  | SCOPE **total** rate             |
| `SCOPE_DEFAULT_RATE_HZ`      | 100000  | SCOPE total rate default         |
| `SCOPE_MAX_LOGIC_RATE_HZ`    | 10000000| LOGIC lane rate                  |
| `SCOPE_DEFAULT_LOGIC_RATE_HZ`| 1000000 | LOGIC lane rate default          |
| `SCOPE_MIN_SAMPLE_COUNT`     | 64      | SCOPE per channel                |
| `SCOPE_MAX_SAMPLE_COUNT`     | 2048    | SCOPE per channel                |
| `SCOPE_MIN_LOGIC_SAMPLES`    | 64      | LOGIC total                      |
| `SCOPE_MAX_LOGIC_SAMPLES`    | 8192    | LOGIC total                      |
| `SCOPE_TRIGGER_HYSTERESIS`   | 64      | analog edge trigger, ADC codes   |
| `SCOPE_CAPTURE_TIMEOUT_MS`   | 500     | capture arm timeout              |
| `SCOPE_PROTO_MAX_PAYLOAD`    | 16395   | head + 2 B × 8192                |
| `SCOPE_PROTO_MAX_FRAME`      | 16461   | encoded, including overhead      |

## Worked byte examples

All payloads below are shown **before** COBS encoding, little-endian.

**HELLO_REQ** (payload = `02`):

```
header: 02 01 00 00 01 00        version=2 type=0x01 seq=0 plen=1
payload: 02
```

**HELLO_RSP** (14 bytes) from a v2.0.0 Pico:

```
00 00 02 00   firmware_version = 0x00020000
37 00 00 00   capabilities     = 0x37
00 08         max_sample_count = 2048
00            voltage_range    = 0
00            calibration      = 0
03            analog_channels  = 3
08            logic_channels   = 8
```

**SET_CONFIG** — SCOPE, 100 kS/s total, 1024 samples/channel, level 2048,
rising, auto, CH1 only:

```
A0 86 01 00   sample_rate     = 100000
00 04         sample_count    = 1024
00 08         trigger_level   = 2048
00            edge            = rising
00            trigger_mode    = auto
00            capture_mode    = SCOPE
01            channel_mask    = CH1
00            trigger_channel = CH1
```

**SET_CONFIG** — LOGIC, 1 MS/s, 1024 samples, D2 rising, all lanes:

```
40 42 0F 00   sample_rate     = 1000000
00 04         sample_count    = 1024
00 00         trigger_level   = (ignored)
00            edge            = rising
00            trigger_mode    = auto
01            capture_mode    = LOGIC
FF            channel_mask    = all 8 lanes
02            trigger_channel = D2
```

**ACK** for that LOGIC config (seq 5):

```
05 00         request_seq   = 5
00            status        = OK
40 42 0F 00   applied_rate  = 1000000 (lane rate)
00 04         applied_count = 1024
```

**CAPTURE_DATA** head — LOGIC, 1 MS/s lane rate, 1024 samples, triggered at
sample 512, not calibrated:

```
40 42 0F 00   rate          = 1000000
00 04         count         = 1024
00 02         trigger_index = 512
04            flags         = NOT_CALIB
08            channels      = 8
01            mode          = LOGIC
```

**CAPTURE_DATA** head — SCOPE, 3 channels asked for 300 kS/s total (so 100 kS/s
per channel), 1024 samples/channel, free-run:

```
A0 86 01 00   rate          = 100000 (per channel!)
00 04         count         = 1024
FF FF         trigger_index = none
04            flags         = NOT_CALIB
03            channels      = 3
00            mode          = SCOPE
```
followed by 3072 interleaved `u16` codes.

## Changes from v1

| Area              | v1                        | v2                                        |
|-------------------|---------------------------|-------------------------------------------|
| `version`         | 1                         | 2                                          |
| ADC channels      | 1 (GPIO26)                | 3 (GPIO26/27/28)                           |
| Logic channels    | —                         | 8 (GPIO5–12), PIO + DMA                    |
| SET_CONFIG        | smaller payload           | 13 bytes, adds `capture_mode`, `channel_mask`, `trigger_channel` |
| ACK               | smaller payload           | 9 bytes, adds `applied_rate`, `applied_count` |
| CAPTURE_DATA head | 7 bytes                   | 11 bytes, adds `trigger_index`, `channels`, `mode` |
| HELLO_RSP         | smaller payload           | 14 bytes, adds `analog_channels`, `logic_channels` |
| ACK busy code     | `SCOPE_ACK_E_BUSYLE`      | `SCOPE_ACK_E_BUSY`                         |
| Capabilities      | `... | CALIB`              | adds `MULTI_ADC`, `LOGIC`                  |
| Wire structs      | plain                     | `SCOPE_PACKED` so `sizeof()` == bytes on wire |
