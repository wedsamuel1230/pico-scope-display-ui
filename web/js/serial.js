/**
 * serial.js — Web Serial transport + deterministic demo simulator
 *
 * Two interchangeable transports behind one interface:
 *   { writeFrame(u8), onFrame(cb), open(opts), close(), name }
 *  - SerialTransport: Chrome Web Serial (user gesture required, HTTPS).
 *    Reader accumulates across reader.read() boundaries until 0x00 —
 *    partial reads and coalesced frames are handled by protocol.js.
 *  - DemoTransport: no hardware; generates deterministic binary frames
 *    (same protocol v1 codec), per REFERENCE_PROJECTS.md decision to
 *    adopt a UI-dev simulator.
 */
"use strict";

/* ================================================================== */
/* SerialTransport                                                    */
/* ================================================================== */
function SerialTransport() {
  let port = null;
  let reader = null;
  let readAbort = { stop: false };
  let frameCb = null;
  let closeCb = null;

  async function open() {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    readAbort = { stop: false };
    pump();
  }

  async function pump() {
    reader = port.readable.getReader();
    const parser = ScopeProto.makeParser();
    try {
      while (!readAbort.stop) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value) {
          for (const ev of parser.feedBytes(value)) {
            if (ev.event === "frame" && frameCb) frameCb(ev);
            if (ev.event === "invalid" && frameCb)
              frameCb({ event: "invalid" });
          }
        }
      }
    } catch (e) {
      if (frameCb) frameCb({ event: "disconnected" });
    } finally {
      try { reader.releaseLock(); } catch (_) { /* already gone */ }
      reader = null;
    }
  }

  function portDisconnect() {
    if (frameCb) frameCb({ event: "disconnected" });
  }

  return {
    supported: typeof navigator !== "undefined" && "serial" in navigator,
    name: "serial",

    setCallbacks(onFrame, onClose) {
      frameCb = onFrame;
      closeCb = onClose;
      if (port) port.addEventListener("disconnect", portDisconnect);
    },

    async connect() {
      if (!this.supported) throw new Error("Web Serial unsupported");
      if (!port) port = undefined; // ensure fresh
      await open();
      if (port)
        port.addEventListener("disconnect", portDisconnect);
    },

    async writeFrame(bytes) {
      if (!port || !port.writable) throw new Error("not connected");
      const writer = port.writable.getWriter();
      try { await writer.write(bytes); }
      finally { writer.releaseLock(); }
    },

    async close() {
      readAbort.stop = true;
      if (reader) {
        try { await reader.cancel(); } catch (_) { /* closing */ }
      }
      if (port) {
        try { await port.close(); } catch (_) { /* already closed */ }
        port.removeEventListener("disconnect", portDisconnect);
        port = null;
      }
      if (closeCb) closeCb();
    },

    isConnected() { return !!port; },
  };
}

/* ================================================================== */
/* DemoTransport — deterministic scene (seed 42), same binary frames   */
/* ================================================================== */
function DemoTransport() {
  let frameCb = null;
  let closeCb = null;
  let rate = 100000;
  let count = 1024;
  let edge = 0;      /* 0 rising, 1 falling */
  let mode = 0;      /* 0 AUTO, 1 NORMAL    */
  let trigLevel = 2048;
  let running = false;

  /* deterministic PRNG (mulberry32)                                  */
  let seed = 42;
  function rnd() {
    seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  }

  function frame(type, seq, payload) {
    return ScopeProto.buildFrame(type, seq, payload);
  }

  function dispatch(frameBytes) {
    if (!frameCb) return;
    for (const ev of ScopeProto.makeParser().feedBytes(frameBytes)) {
      if (ev.event !== "in-progress") frameCb(ev);
    }
  }

  function respond(ev) {
    if (ev.type === ScopeProto.MSG.SET_CONFIG && ev.payload.length >= 10) {
      const v = new DataView(ev.payload.buffer, ev.payload.byteOffset);
      rate = Math.max(1000, Math.min(500000, v.getUint32(0, true)));
      count = Math.max(64, Math.min(2048, v.getUint16(4, true)));
      trigLevel = v.getUint16(6, true);
      edge = ev.payload[8]; mode = ev.payload[9];
      const ack = new Uint8Array(9);
      const as = new DataView(ack.buffer);
      as.setUint16(0, ev.seq, true);
      ack[2] = ScopeProto.ACK.OK;
      as.setUint32(3, rate, true);
      as.setUint16(7, count, true);
      dispatch(frame(ScopeProto.MSG.ACK, ev.seq, ack));
    } else if (ev.type === ScopeProto.MSG.HELLO_REQ) {
      const rsp = new Uint8Array(12);
      const v = new DataView(rsp.buffer);
      v.setUint32(0, 0x00010000, true);   /* fw v0.1.0                    */
      v.setUint32(4, 0b0111, true);       /* caps: sim+trigs+calib-flag   */
      v.setUint16(8, 2048, true);         /* max samples                  */
      rsp[10] = 0; rsp[11] = 0;
      dispatch(frame(ScopeProto.MSG.HELLO_RSP, ev.seq, rsp));
    } else if (ev.type === ScopeProto.MSG.CAPTURE_REQ) {
      running = true;
      setTimeout(() => tryCapture(), 30);
    } else if (ev.type === ScopeProto.MSG.STOP) {
      running = false;
    }
  }

  function tryCapture() {
    if (!running) return;
    const samples = new Uint16Array(count);
    /* deterministic scene: 1.5 kHz sine + 3 kHz component + noise      */
    const periods = 12.0 + (count % 7) * 0.5;
    for (let i = 0; i < count; i++) {
      const t = i / rate;
      let v = 1400 * Math.sin(2 * Math.PI * (periods * rate / count) * t);
      v += 500 * Math.sin(2 * Math.PI * 4 * (periods) * t / count * count / 1 * (rate / count));
      v += (rnd() - 0.5) * 90;
      samples[i] = Math.max(0, Math.min(4095, Math.round(2048 + v)));
    }

    /* deterministic trigger search mirroring the firmware             */
    let trig = 0xFFFF;
    for (let i = 1; i < count; i++) {
      if (samples[i - 1] < trigLevel && samples[i] >= trigLevel) {
        trig = i; break;
      }
    }
    const flags = trig === 0xFFFF ? ScopeProto.FLAGS.NO_TRIGGER
                                  : ScopeProto.FLAGS.NOT_CALIB;

    const payload = new Uint8Array(9 + count * 2);
    const v = new DataView(payload.buffer);
    v.setUint32(0, rate, true);
    v.setUint16(4, count, true);
    v.setUint16(6, trig, true);
    payload[8] = flags;
    for (let i = 0; i < count; i++) v.setUint16(9 + i * 2, samples[i], true);
    dispatch(frame(ScopeProto.MSG.CAPTURE_DATA, 0, payload));
  }

  return {
    supported: true,
    name: "demo",
    setCallbacks(onFrame, onClose) { frameCb = onFrame; closeCb = onClose; },
    async connect() { /* nothing to open */ },
    async writeFrame(bytes) {
      for (const ev of ScopeProto.makeParser().feedBytes(bytes)) {
        if (ev.event === "frame") respond(ev);
      }
    },
    async close() {
      running = false;
      if (closeCb) closeCb();
    },
    isConnected() { return true; },
  };
}
