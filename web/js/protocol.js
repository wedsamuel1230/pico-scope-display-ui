/**
 * protocol.js — protocol v2 codec for the browser side
 *
 * MUST stay byte-compatible with src/protocol/frame_codec.cpp,
 * include/message_protocol.h and include/scope_config.h
 * (docs/PROTOCOL_V2.md):
 *   frame = COBS(ver u8 | type u8 | seq u16LE | plen u16LE
 *                payload | crc16LE) 0x00
 *   CRC-16/CCITT-FALSE over unencoded header+payload.
 *
 * v2 wire sizes: HELLO_RSP 14 B, SET_CONFIG 13 B, ACK 9 B,
 * CAPTURE_DATA head 11 B (samples follow).
 */
"use strict";

const ScopeProto = (() => {
  const VERSION = 2;
  const HDR_LEN = 6;

  const MSG = {
    HELLO_REQ: 0x01, HELLO_RSP: 0x02, SET_CONFIG: 0x03, ACK: 0x04,
    CAPTURE_REQ: 0x05, CAPTURE_DATA: 0x06, STOP: 0x07, ERROR: 0x08,
  };
  const ACK = { OK: 0, E_VERSION: 1, E_RANGE: 2, E_BUSY: 3 };
  const FLAGS = {
    OVERFLOW: 0x01, NO_TRIGGER: 0x02, NOT_CALIB: 0x04, TRUNCATED: 0x08,
  };

  /* capture mode: which engine fills the buffer                      */
  const MODE = { SCOPE: 0, LOGIC: 1 };

  /* trigger mode                                                    */
  const TRIG = { AUTO: 0, NORMAL: 1 };

  /* capability bits (mirror of scope_config.h)                       */
  const CAPS = {
    SIM: 1 << 0, TRIG_RISE: 1 << 1, TRIG_FALL: 1 << 2, CALIB: 1 << 3,
    MULTI_ADC: 1 << 4, LOGIC: 1 << 5,
  };

  /* limits (mirror of scope_config.h / board_config.h)               */
  const LIMITS = {
    ADC_CHANNELS: 3,
    LOGIC_CHANNELS: 8,
    LOGIC_PIN_MASK: 0xFF,
    SAMPLE_COUNT: 1024,
    MIN_SAMPLE_COUNT: 64,
    MAX_SAMPLE_COUNT: 2048,
    MIN_LOGIC_SAMPLES: 64,
    MAX_LOGIC_SAMPLES: 8192,
    MIN_RATE_HZ: 1000,
    MAX_RATE_HZ: 500000,
    DEFAULT_RATE_HZ: 100000,
    MIN_LOGIC_RATE_HZ: 1000,
    MAX_LOGIC_RATE_HZ: 10000000,
    DEFAULT_LOGIC_RATE_HZ: 1000000,
    TRIG_CHANNEL_NONE: 0xFF,
    TRIGGER_NONE: 0xFFFF,
  };

  /* ---- CRC-16/CCITT-FALSE ---------------------------------------- */
  function crc16(bytes) {
    let crc = 0xFFFF;
    for (let i = 0; i < bytes.length; i++) {
      crc ^= bytes[i] << 8;
      for (let b = 0; b < 8; b++) {
        crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
        crc &= 0xFFFF;
      }
    }
    return crc;
  }

  /* ---- COBS ------------------------------------------------------- */
  function cobsEncode(src) {
    const dst = new Uint8Array(src.length + Math.ceil(src.length / 254) + 1);
    let out = 0;
    let codeIdx = out++;
    let code = 1;
    for (let i = 0; i < src.length; i++) {
      if (src[i] !== 0) {
        dst[out++] = src[i];
        if (++code === 0xFF && i + 1 < src.length) {
          dst[codeIdx] = code;
          codeIdx = out++;
          code = 1;
        }
      } else {
        dst[codeIdx] = code;
        codeIdx = out++;
        code = 1;
      }
    }
    dst[codeIdx] = code;
    return dst.subarray(0, out);
  }

  function cobsDecode(src) {
    if (src.length === 0) return null;
    let out = 0;
    const dst = new Uint8Array(src.length);
    let i = 0;
    while (i < src.length) {
      const code = src[i++];
      if (code === 0) return null;
      const block = code - 1;
      if (i + block > src.length) return null;
      for (let k = 0; k < block; k++) dst[out++] = src[i++];
      if (code < 0xFF && i < src.length) dst[out++] = 0;
    }
    return dst.subarray(0, out);
  }

  /* ---- one-shot frame encoder (browser -> device) ------------------ */
  function buildFrame(type, seq, payload) {
    payload = payload || new Uint8Array(0);
    const raw = new Uint8Array(HDR_LEN + payload.length + 2);
    raw[0] = VERSION;
    raw[1] = type;
    raw[2] = seq & 0xFF;
    raw[3] = seq >> 8;
    raw[4] = payload.length & 0xFF;
    raw[5] = payload.length >> 8;
    raw.set(payload, HDR_LEN);
    const crc = crc16(raw.subarray(0, HDR_LEN + payload.length));
    raw[HDR_LEN + payload.length] = crc & 0xFF;
    raw[HDR_LEN + payload.length + 1] = crc >> 8;
    const enc = cobsEncode(raw).slice();
    const frame = new Uint8Array(enc.length + 1);
    frame.set(enc, 0);
    frame[enc.length] = 0x00;
    return frame;
  }

  /* message payload builders (all little-endian) --------------------- */
  function u32le(v) { return [v & 0xFF, (v >>> 8) & 0xFF,
                              (v >>> 16) & 0xFF, (v >>> 24) & 0xFF]; }
  function u16le(v) { return [v & 0xFF, (v >>> 8) & 0xFF]; }

  function helloReq() { return buildFrame(MSG.HELLO_REQ, 1, null); }
  function captureReq(seq) { return buildFrame(MSG.CAPTURE_REQ, seq, null); }
  function stop(seq) { return buildFrame(MSG.STOP, seq, null); }

  /**
   * SET_CONFIG — 13 bytes:
   *   rate u32 | count u16 | trigLevel u16 | edge u8 | trigMode u8 |
   *   captureMode u8 | channelMask u8 | triggerChannel u8
   * For LOGIC the rate is the lane rate and channelMask is the lane
   * mask; for SCOPE the rate is the TOTAL ADC rate (per-channel rate is
   * rate / popcount(channelMask)) and channelMask bit n selects ADCn.
   */
  function setConfig(seq, opts) {
    const o = opts || {};
    const p = new Uint8Array(13);
    p.set(u32le(o.rate >>> 0), 0);
    p.set(u16le(o.count & 0xFFFF), 4);
    p.set(u16le((o.triggerLevel || 0) & 0xFFFF), 6);
    p[8] = (o.edge === 1) ? 1 : 0;
    p[9] = (o.triggerMode === TRIG.NORMAL) ? TRIG.NORMAL : TRIG.AUTO;
    p[10] = (o.captureMode === MODE.LOGIC) ? MODE.LOGIC : MODE.SCOPE;
    p[11] = (o.channelMask === undefined ? 0x01 : o.channelMask) & 0xFF;
    p[12] = (o.triggerChannel === undefined
      ? LIMITS.TRIG_CHANNEL_NONE
      : o.triggerChannel) & 0xFF;
    return buildFrame(MSG.SET_CONFIG, seq, p);
  }

  /* ---- incremental frame parser ------------------------------------ */
  function makeParser() {
    let buf = []; // accumulating encoded bytes (excludes delimiter)

    function feedByte(b) {
      if (b === 0x00) {
        if (buf.length === 0) return { event: "in-progress" };
        const enc = Uint8Array.from(buf);
        buf = [];
        const raw = cobsDecode(enc);
        if (!raw || raw.length < HDR_LEN + 2) return { event: "invalid" };
        const plen = raw[4] | (raw[5] << 8);
        const payOff = HDR_LEN;
        const payEnd = HDR_LEN + plen;
        if (payEnd + 2 > raw.length || plen !== raw.length - HDR_LEN - 2)
          return { event: "invalid" };
        const calc = crc16(raw.subarray(0, payEnd));
        const rx = raw[payEnd] | (raw[payEnd + 1] << 8);
        if (calc !== rx) return { event: "invalid" };
        if (raw[0] !== VERSION) return { event: "invalid" };
        return {
          event: "frame",
          type: raw[1],
          seq: raw[2] | (raw[3] << 8),
          payload: raw.subarray(payOff, payEnd),
        };
      }
      // discard-over-limit guard (matches firmware fixed buffer rule)
      if (buf.length > 65536) { buf = []; return { event: "invalid" }; }
      buf.push(b);
      return { event: "in-progress" };
    }

    function feedBytes(bytes) {
      const out = [];
      for (const b of bytes) {
        const ev = feedByte(b);
        if (ev.event !== "in-progress") out.push(ev);
      }
      return out;
    }

    return { feedBytes };
  }

  /**
   * decode CAPTURE_DATA. Branches on the `mode` byte:
   *   SCOPE: count = samples PER CHANNEL, channels = interleaved
   *          analog channels, samples = count*channels u16 codes
   *          (channel-major within each time slot).
   *   LOGIC: count = samples, channels = 8, samples = count u16 values
   *          whose low byte holds the 8 digital lines.
   */
  function parseCaptureData(p) {
    if (p.length < 11) return null;
    const view = new DataView(p.buffer, p.byteOffset, p.byteLength);
    const rate = view.getUint32(0, true);
    const count = view.getUint16(4, true);
    const triggerIndex = view.getUint16(6, true);
    const flags = p[8];
    const channels = p[9];
    const mode = p[10];
    const total = (mode === MODE.LOGIC) ? count : count * channels;
    if (p.length < 11 + total * 2) return null;
    const samples = new Uint16Array(total);
    for (let i = 0; i < total; i++)
      samples[i] = view.getUint16(11 + i * 2, true);
    return { rate, count, triggerIndex, flags, channels, mode, samples };
  }

  function parseHelloRsp(p) {
    if (p.length < 14) return null;
    const view = new DataView(p.buffer, p.byteOffset, p.byteLength);
    return {
      firmwareVersion: view.getUint32(0, true),
      capabilities: view.getUint32(4, true),
      maxSampleCount: view.getUint16(8, true),
      voltageRange: p[10],
      calibration: p[11],
      analogChannels: p[12],
      logicChannels: p[13],
    };
  }

  function parseAck(p) {
    if (p.length < 9) return null;
    const view = new DataView(p.buffer, p.byteOffset, p.byteLength);
    return {
      requestSeq: view.getUint16(0, true),
      status: p[2],
      appliedRate: view.getUint32(3, true),
      appliedCount: view.getUint16(7, true),
    };
  }

  /** popcount of an ADC channel mask (bit0 = ADC0). */
  function channelCount(mask) {
    let n = 0;
    for (let b = 0; b < LIMITS.ADC_CHANNELS; b++) if (mask & (1 << b)) n++;
    return n;
  }

  /** per-channel sample rate for a SCOPE capture. */
  function channelRate(totalRate, mask) {
    const n = channelCount(mask);
    return n > 0 ? Math.floor(totalRate / n) : totalRate;
  }

  return {
    VERSION, HDR_LEN, MSG, ACK, FLAGS, MODE, TRIG, CAPS, LIMITS,
    crc16, cobsEncode, cobsDecode, buildFrame,
    helloReq, captureReq, setConfig, stop,
    makeParser, parseCaptureData, parseHelloRsp, parseAck,
    channelCount, channelRate,
  };
})();
