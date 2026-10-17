/**
 * serial.js — Web Serial transport + deterministic demo simulator
 *
 * Two interchangeable transports behind one interface:
 *   { writeFrame(u8), onFrame(cb), open(opts), close(), name }
 *  - SerialTransport: Chrome Web Serial (user gesture required, HTTPS).
 *    Reader accumulates across reader.read() boundaries until 0x00 —
 *    partial reads and coalesced frames are handled by protocol.js.
 *  - DemoTransport: no hardware; generates deterministic binary frames
 *    (same protocol v2 codec), per REFERENCE_PROJECTS.md decision to
 *    adopt a UI-dev simulator. Honours both SCOPE and LOGIC modes and
 *    mirrors the firmware's clamping rules exactly.
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
  let hooked = false;

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
    },

    async connect() {
      if (!this.supported) throw new Error("Web Serial unsupported");
      await open();
      /* register the disconnect listener exactly once                */
      if (port && !hooked) {
        port.addEventListener("disconnect", portDisconnect);
        hooked = true;
      }
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
      let err = null;
      if (port) {
        const p = port;
        port = null;
        hooked = false;
        p.removeEventListener("disconnect", portDisconnect);
        try { await p.close(); } catch (e) { err = e; }
      }
      if (closeCb) closeCb();
      if (err) throw err;   /* surface a real port-close failure       */
    },

    isConnected() { return !!port; },
  };
}

/* ================================================================== */
/* DemoTransport — deterministic scene (seed 42), same binary frames   */
/* ================================================================== */
function DemoTransport() {
  const P = ScopeProto;
  const L = P.LIMITS;

  let frameCb = null;
  let closeCb = null;

  /* configuration, mirroring the firmware defaults                    */
  let rate = L.DEFAULT_RATE_HZ;            /* total ADC rate / lane rate */
  let count = L.SAMPLE_COUNT;              /* per channel / per capture  */
  let edge = 0;                            /* 0 rising, 1 falling        */
  let trigMode = P.TRIG.AUTO;
  let captureMode = P.MODE.SCOPE;
  let channelMask = 0x01;
  let triggerChannel = L.TRIG_CHANNEL_NONE;
  let trigLevel = 2048;

  let running = false;
  let lastSeq = 0;

  /* deterministic PRNG (mulberry32)                                   */
  let seed = 42;
  function rnd() {
    seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  }

  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

  function frame(type, seq, payload) {
    return P.buildFrame(type, seq, payload);
  }

  function dispatch(frameBytes) {
    if (!frameCb) return;
    for (const ev of P.makeParser().feedBytes(frameBytes)) {
      if (ev.event !== "in-progress") frameCb(ev);
    }
  }

  /* ---- config handling, same clamping rules as config_clamp() ------ */
  function applyConfig(p) {
    const v = new DataView(p.buffer, p.byteOffset, p.byteLength);
    rate = v.getUint32(0, true);
    count = v.getUint16(4, true);
    trigLevel = v.getUint16(6, true);
    edge = p[8] === 1 ? 1 : 0;
    trigMode = p[9] === P.TRIG.NORMAL ? P.TRIG.NORMAL : P.TRIG.AUTO;
    captureMode = p[10] === P.MODE.LOGIC ? P.MODE.LOGIC : P.MODE.SCOPE;
    channelMask = p[11];
    triggerChannel = p[12];

    let appliedRate;

    if (captureMode === P.MODE.LOGIC) {
      rate = clamp(rate, L.MIN_LOGIC_RATE_HZ, L.MAX_LOGIC_RATE_HZ);
      count = clamp(count, L.MIN_LOGIC_SAMPLES, L.MAX_LOGIC_SAMPLES);
      channelMask = L.LOGIC_PIN_MASK;
      trigLevel = 0;
      if (triggerChannel !== L.TRIG_CHANNEL_NONE &&
          triggerChannel >= L.LOGIC_CHANNELS) {
        triggerChannel = L.TRIG_CHANNEL_NONE;
      }
      appliedRate = rate;
    } else {
      rate = clamp(rate, L.MIN_RATE_HZ, L.MAX_RATE_HZ);
      count = clamp(count, L.MIN_SAMPLE_COUNT, L.MAX_SAMPLE_COUNT);
      channelMask &= (1 << L.ADC_CHANNELS) - 1;
      if (channelMask === 0) channelMask = 0x01;
      if (triggerChannel !== L.TRIG_CHANNEL_NONE &&
          !(channelMask & (1 << triggerChannel))) {
        triggerChannel = L.TRIG_CHANNEL_NONE;
      }
      /* ACK reports the TOTAL ADC rate, like adc_capture_actual_rate() */
      appliedRate = rate;
    }

    return { appliedRate, appliedCount: count };
  }

  function respond(ev) {
    if (ev.type === P.MSG.SET_CONFIG && ev.payload.length >= 13) {
      const applied = applyConfig(ev.payload);
      const ack = new Uint8Array(9);
      const as = new DataView(ack.buffer);
      as.setUint16(0, ev.seq, true);
      ack[2] = P.ACK.OK;
      as.setUint32(3, applied.appliedRate, true);
      as.setUint16(7, applied.appliedCount, true);
      dispatch(frame(P.MSG.ACK, ev.seq, ack));
    } else if (ev.type === P.MSG.HELLO_REQ) {
      const rsp = new Uint8Array(14);
      const v = new DataView(rsp.buffer);
      v.setUint32(0, 0x00020000, true);   /* fw v0.2.0 */
      v.setUint32(4, P.CAPS.SIM | P.CAPS.TRIG_RISE | P.CAPS.TRIG_FALL |
                     P.CAPS.MULTI_ADC | P.CAPS.LOGIC, true);
      v.setUint16(8, L.MAX_SAMPLE_COUNT, true);
      rsp[10] = 0;                        /* voltage range: raw codes */
      rsp[11] = 0;                        /* no calibration layer     */
      rsp[12] = L.ADC_CHANNELS;
      rsp[13] = L.LOGIC_CHANNELS;
      dispatch(frame(P.MSG.HELLO_RSP, ev.seq, rsp));
    } else if (ev.type === P.MSG.CAPTURE_REQ) {
      running = true;
      lastSeq = ev.seq;
      setTimeout(() => tryCapture(), 30);
    } else if (ev.type === P.MSG.STOP) {
      running = false;
    }
  }

  /* ---- deterministic analog scene ---------------------------------- */
  const CH_FREQ = [1000, 2500, 500];
  const CH_AMP  = [1700, 1100, 700];

  function analogScene(slot, t) {
    const f = CH_FREQ[slot % CH_FREQ.length];
    const a = CH_AMP[slot % CH_AMP.length];
    let v = a * Math.sin(2 * Math.PI * f * t + slot * 0.4);
    v += a * 0.14 * Math.sin(2 * Math.PI * 3 * f * t);
    v += (rnd() - 0.5) * 60;
    return clamp(Math.round(2048 + v), 0, 4095);
  }

  /* ---- deterministic logic scene ----------------------------------- */
  /* Lane n toggles every (n+1) samples, so the eight lanes read as an
   * ascending-frequency ladder. Lane 0 is the fastest.                */
  function logicScene(i, lane) {
    return ((Math.floor(i / (lane + 1)) & 1) === 0) ? 1 : 0;
  }

  function captureScope(total, channels) {
    const totalValues = count * channels;
    const samples = new Uint16Array(totalValues);
    const perChRate = P.channelRate(total, channelMask) || total;
    for (let i = 0; i < count; i++) {
      const t = i / perChRate;   /* per-channel timebase */
      for (let slot = 0; slot < channels; slot++) {
        samples[i * channels + slot] = analogScene(slot, t);
      }
    }

    /* trigger search, strided like trigger_find_strided()             */
    let trigSlot = 0;
    if (triggerChannel !== L.TRIG_CHANNEL_NONE) {
      let n = 0;
      for (let b = 0; b < L.ADC_CHANNELS; b++) {
        if (!(channelMask & (1 << b))) continue;
        if (b === triggerChannel) { trigSlot = n; break; }
        n++;
      }
    }

    let trig = L.TRIGGER_NONE;
    for (let i = 1; i < count; i++) {
      const a = samples[(i - 1) * channels + trigSlot];
      const b = samples[i * channels + trigSlot];
      const rising = a < trigLevel && b >= trigLevel;
      const falling = a > trigLevel && b <= trigLevel;
      if (edge === 0 ? rising : falling) { trig = i; break; }
    }

    /* no AFE yet, so the sim always reports codes-not-volts            */
    let flags = P.FLAGS.NOT_CALIB;
    if (trig === L.TRIGGER_NONE && trigMode === P.TRIG.NORMAL) {
      flags |= P.FLAGS.NO_TRIGGER;
    }
    if (trig === L.TRIGGER_NONE) trig = L.TRIGGER_NONE;

    const payload = new Uint8Array(11 + totalValues * 2);
    const v = new DataView(payload.buffer);
    v.setUint32(0, P.channelRate(total, channelMask), true);
    v.setUint16(4, count, true);
    v.setUint16(6, trig, true);
    payload[8] = flags;
    payload[9] = channels;
    payload[10] = P.MODE.SCOPE;
    for (let i = 0; i < totalValues; i++)
      v.setUint16(11 + i * 2, samples[i], true);
    dispatch(frame(P.MSG.CAPTURE_DATA, lastSeq, payload));
  }

  function captureLogic() {
    const samples = new Uint16Array(count);
    for (let i = 0; i < count; i++) {
      let laneBits = 0;
      for (let lane = 0; lane < L.LOGIC_CHANNELS; lane++) {
        if (logicScene(i, lane)) laneBits |= (1 << lane);
      }
      samples[i] = laneBits & L.LOGIC_PIN_MASK;
    }

    let trig = L.TRIGGER_NONE;
    if (triggerChannel !== L.TRIG_CHANNEL_NONE) {
      const mask = 1 << triggerChannel;
      let prev = (samples[0] & mask) !== 0;
      for (let i = 1; i < count; i++) {
        const cur = (samples[i] & mask) !== 0;
        if (edge === 0 ? (!prev && cur) : (prev && !cur)) { trig = i; break; }
        prev = cur;
      }
    }

    let flags = P.FLAGS.NOT_CALIB;
    if (trig === L.TRIGGER_NONE && trigMode === P.TRIG.NORMAL) {
      flags |= P.FLAGS.NO_TRIGGER;
    }

    const payload = new Uint8Array(11 + count * 2);
    const v = new DataView(payload.buffer);
    v.setUint32(0, rate, true);
    v.setUint16(4, count, true);
    v.setUint16(6, trig, true);
    payload[8] = flags;
    payload[9] = L.LOGIC_CHANNELS;
    payload[10] = P.MODE.LOGIC;
    for (let i = 0; i < count; i++)
      v.setUint16(11 + i * 2, samples[i], true);
    dispatch(frame(P.MSG.CAPTURE_DATA, lastSeq, payload));
  }

  function tryCapture() {
    if (!running) return;
    if (captureMode === P.MODE.LOGIC) captureLogic();
    else captureScope(rate, P.channelCount(channelMask));
  }

  return {
    supported: true,
    name: "demo",
    setCallbacks(onFrame, onClose) { frameCb = onFrame; closeCb = onClose; },
    async connect() { /* nothing to open */ },
    async writeFrame(bytes) {
      /* Defer responses so the caller can register its waiter first:  */
      /* a synchronous reply would land before `pending` is populated. */
      for (const ev of P.makeParser().feedBytes(bytes)) {
        if (ev.event === "frame") {
          const frameEv = ev;
          setTimeout(() => respond(frameEv), 0);
        }
      }
    },
    async close() {
      running = false;
      if (closeCb) closeCb();
    },
    isConnected() { return true; },
  };
}
