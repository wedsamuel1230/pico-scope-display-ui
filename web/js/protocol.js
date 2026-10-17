/**
 * protocol.js — protocol v1 codec for the browser side
 *
 * MUST stay byte-compatible with src/protocol/frame_codec.cpp and
 * include/message_protocol.h (docs/PROTOCOL_V1.md):
 *   frame = COBS(ver u8 | type u8 | seq u16LE | plen u16LE
 *                payload | crc16LE) 0x00
 *   CRC-16/CCITT-FALSE over unencoded header+payload.
 */
"use strict";

const ScopeProto = (() => {
  const VERSION = 1;
  const HDR_LEN = 6;

  const MSG = {
    HELLO_REQ: 0x01, HELLO_RSP: 0x02, SET_CONFIG: 0x03, ACK: 0x04,
    CAPTURE_REQ: 0x05, CAPTURE_DATA: 0x06, STOP: 0x07, ERROR: 0x08,
  };
  const ACK = { OK: 0, E_VERSION: 1, E_RANGE: 2, E_BUSY: 3 };
  const FLAGS = {
    OVERFLOW: 0x01, NO_TRIGGER: 0x02, NOT_CALIB: 0x04, TRUNCATED: 0x08,
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
    const enc = cobsEncode(raw);
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

  function setConfig(seq, rate, count, trigLevel, edge, mode) {
    const p = new Uint8Array(10);
    const r = u32le(rate), c = u16le(count), t = u16le(trigLevel);
    p.set(r, 0); p.set(c, 4); p.set(t, 6);
    p[8] = edge; p[9] = mode;
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

  /* decode CAPTURE_DATA payload                                        */
  function parseCaptureData(p) {
    if (p.length < 9) return null;
    const view = new DataView(p.buffer, p.byteOffset, p.byteLength);
    const rate = view.getUint32(0, true);
    const count = view.getUint16(4, true);
    const triggerIndex = view.getUint16(6, true);
    if (p.length < 9 + count * 2) return null;
    const flags = p[8];
    const samples = new Uint16Array(count);
    for (let i = 0; i < count; i++)
      samples[i] = view.getUint16(9 + i * 2, true);
    return { rate, count, triggerIndex, flags, samples };
  }

  function parseHelloRsp(p) {
    if (p.length < 12) return null;
    const view = new DataView(p.buffer, p.byteOffset, p.byteLength);
    return {
      firmwareVersion: view.getUint32(0, true),
      capabilities: view.getUint32(4, true),
      maxSampleCount: view.getUint16(8, true),
      voltageRange: p[10],
      calibration: p[11],
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

  return {
    VERSION, MSG, ACK, FLAGS,
    crc16, cobsEncode, cobsDecode, buildFrame,
    helloReq, captureReq, setConfig, stop,
    makeParser, parseCaptureData, parseHelloRsp, parseAck,
  };
})();
