/**
 * app.js — UI wiring: connect / demo / run / stop / status
 *
 * Sequence per protocol v1:
 *   connect -> HELLO_REQ -> (await) HELLO_RSP
 *           -> SET_CONFIG -> (await) ACK
 *           -> CAPTURE_REQ -> (await) CAPTURE_DATA  (repeat while RUN)
 * Errors land in the status line: CRC error, capture timeout,
 * disconnected, unsupported. Demo transport needs no device.
 */
"use strict";

(() => {
  const el = (id) => document.getElementById(id);
  const $status = el("status");
  const buttons = {
    connect: el("btn-connect"),
    demo: el("btn-demo"),
    run: el("btn-run"),
    stop: el("btn-stop"),
    disconnect: el("btn-disconnect"),
  };
  const canvas = el("scope-canvas");
  let ctx = null;

  let transport = null; // SerialTransport | DemoTransport
  let connected = false;
  let running = false;
  let seq = 2;
  const pending = new Map(); // type -> resolve(ev)

  function setStatus(text, cls) {
    $status.textContent = text;
    $status.className = "status " + (cls || "");
  }

  function awaitFrame(type, timeoutMs) {
    return new Promise((resolve) => {
      const t = setTimeout(() => {
        pending.delete(type);
        resolve(null);
      }, timeoutMs);
      pending.set(type, (ev) => { clearTimeout(t); pending.delete(type);
                                 resolve(ev); });
    });
  }

  function onEvents(ev) {
    if (ev.event === "disconnected") {
      running = false;
      connected = false;
      setStatus("disconnected", "err");
      syncButtons();
      return;
    }
    if (ev.event === "invalid") {
      setStatus("CRC / frame error — stream marked damaged", "warn");
      return;
    }
    if (ev.event === "frame") {
      const waiter = pending.get(ev.type);
      if (waiter) { waiter(ev); return; }
      if (ev.type === ScopeProto.MSG.CAPTURE_DATA) onCaptureData(ev);
      else if (ev.type === ScopeProto.MSG.ERROR) {
        setStatus("device ERROR: " + new TextDecoder()
          .decode(ev.payload.subarray(3)), "err");
      }
    }
  }

  let lastRate = 0;
  function onCaptureData(ev) {
    const d = ScopeProto.parseCaptureData(ev.payload);
    if (!d) return;
    lastRate = d.rate;
    ScopePlot.drawTrace(ctx, d.samples);
    ScopePlot.drawGrid(ctx);
    updateTiles(d);
    if (running) loop(); // next capture request
  }

  function updateTiles(d) {
    let mn = 4096, mx = 0;
    for (const s of d.samples) { if (s < mn) mn = s; if (s > mx) mx = s; }
    const vpp = mx - mn;
    el("m-vpp").textContent = d.flags & ScopeProto.FLAGS.NOT_CALIB
      ? `${vpp} (raw)` : `${(vpp * 3.3 / 4095).toFixed(2)} V`;
    /* frequency comes from device measurement text in real firmware;   */
    /* demo estimates from crossings for now                            */
    let crossings = 0;
    for (let i = 1; i < d.samples.length; i++) {
      if (d.samples[i - 1] < 2048 && d.samples[i] >= 2048) crossings++;
    }
    const freq = crossings >= 2
      ? crossings * (d.rate / d.samples.length)
      : NaN;
    el("m-freq").textContent = Number.isNaN(freq)
      ? "—" : (freq >= 1000 ? (freq / 1000).toFixed(2) + " kHz"
                            : freq.toFixed(1) + " Hz");
    let sumSq = 0;
    const mean = d.samples.reduce((a, b) => a + b, 0) / d.samples.length;
    for (const s of d.samples) { const x = s - mean; sumSq += x * x; }
    const rms = Math.sqrt(sumSq / d.samples.length);
    el("m-rms").textContent = (d.flags & ScopeProto.FLAGS.NOT_CALIB)
      ? rms.toFixed(0) + " (raw)" : rms.toFixed(2) + " raw";
    el("m-rate").textContent = (d.rate / 1000).toFixed(0) + " kS/s";
    if (d.flags & ScopeProto.FLAGS.OVERFLOW)
      setStatus("ADC FIFO overflow reported", "warn");
  }

  async function handshake() {
    transport.writeFrame(ScopeProto.helloReq());
    const hello = await awaitFrame(ScopeProto.MSG.HELLO_RSP, 3000);
    if (!hello) { setStatus("connect timeout", "err"); return false; }
    const rsp = ScopeProto.parseHelloRsp(hello.payload);
    if (!rsp) { setStatus("bad HELLO_RSP", "err"); return false; }
    return true;
  }

  async function applyConfig() {
    seq += 1;
    transport.writeFrame(
      ScopeProto.setConfig(seq, 100000, 1024, 2048, 0, 0));
    const ack = await awaitFrame(ScopeProto.MSG.ACK, 3000);
    if (!ack) { setStatus("config timeout", "err"); return false; }
    const a = ScopeProto.parseAck(ack.payload);
    if (!a || a.status !== ScopeProto.ACK.OK) {
      setStatus("config rejected", "err"); return false;
    }
    return true;
  }

  async function loop() {
    if (!running || !connected) return;
    transport.writeFrame(ScopeProto.captureReq(seq));
  }

  async function connect(kind) {
    if (connected) { await disconnect(); }
    try {
      transport = kind === "demo"
        ? DemoTransport()
        : (function () {
            const t = SerialTransport();
            if (!t.supported) {
              setStatus("Web Serial unsupported — use Chrome over HTTPS",
                        "err");
              return null;
            }
            return t;
          })();
      if (!transport) return;
      transport.setCallbacks(onEvents,
        () => { connected = false; running = false; syncButtons();
                setStatus("disconnected", "err"); });
      await transport.connect();
      connected = true;
      setStatus(`connected (${transport.name})`, "ok");
      syncButtons();
      if (await handshake() && await applyConfig()) {
        ScopePlot.drawAll(ctx, null, 0, false, null);
      }
    } catch (e) {
      setStatus("connect failed: " + e.message, "err");
      connected = false;
      syncButtons();
    }
  }

  async function disconnect() {
    running = false;
    if (transport) {
      try {
        transport.writeFrame(ScopeProto.stop(seq));
      } catch (_) { /* fire-and-forget */ }
      await transport.close();
    }
    transport = null;
    connected = false;
    setStatus("not connected");
    syncButtons();
  }

  function syncButtons() {
    buttons.connect.disabled = connected;
    buttons.demo.disabled = connected;
    buttons.disconnect.disabled = !connected;
    buttons.run.disabled = !connected || running;
    buttons.stop.disabled = !connected || !running;
  }

  buttons.connect.addEventListener("click", () => connect("serial"));
  buttons.demo.addEventListener("click", () => connect("demo"));
  buttons.disconnect.addEventListener("click", disconnect);
  buttons.run.addEventListener("click", () => {
    if (!connected) return;
    running = true; syncButtons();
    loop();
  });
  buttons.stop.addEventListener("click", () => {
    running = false;
    transport && transport.writeFrame(ScopeProto.stop(seq));
    syncButtons();
  });

  window.addEventListener("resize", () => {
    if (canvas) ctx = ScopePlot.fitCanvas(canvas);
  });

  window.addEventListener("load", () => {
    ctx = ScopePlot.fitCanvas(canvas);
    ScopePlot.drawAll(ctx, null, 0, false, null);
    setStatus("not connected");
    syncButtons();
  });
})();
