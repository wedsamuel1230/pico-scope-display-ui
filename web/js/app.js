/**
 * app.js — UI wiring: connect / demo / run / stop / status
 *
 * Sequence per protocol v1:
 *   connect -> HELLO_REQ -> (await) HELLO_RSP
 *           -> SET_CONFIG -> (await) ACK
 *           -> CAPTURE_REQ -> (await) CAPTURE_DATA  (repeat while RUN)
 * Errors land in the status line: CRC error, capture timeout,
 * disconnected, unsupported. Demo transport needs no device.
 *
 * `render()` owns the whole instrument face; readings live in the DOM.
 */
"use strict";

(() => {
  const el = (id) => document.getElementById(id);
  const $status = el("status");
  const $srLive = el("sr-measurements");
  const buttons = {
    connect: el("btn-connect"),
    demo: el("btn-demo"),
    run: el("btn-run"),
    stop: el("btn-stop"),
    disconnect: el("btn-disconnect"),
  };
  const canvas = el("scope-canvas");
  let ctx = null;

  let transport = null;          // SerialTransport | DemoTransport
  let connected = false;
  let running = false;
  let seq = 2;
  const pending = new Map();     // type -> resolve(ev)
  let lastFrame = null;          // last parsed capture (for redraw)
  let srClock = 0;               // throttles the screen-reader live region

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
      setStatus("device disconnected", "err");
      syncButtons();
      return;
    }
    if (ev.event === "invalid") {
      setStatus("CRC / frame error — stream damaged", "warn");
      return;
    }
    if (ev.event === "frame") {
      const waiter = pending.get(ev.type);
      if (waiter) { waiter(ev); return; }
      if (ev.type === ScopeProto.MSG.CAPTURE_DATA) onCaptureData(ev);
      else if (ev.type === ScopeProto.MSG.ERROR) {
        setStatus("device error: " + new TextDecoder()
          .decode(ev.payload.subarray(3)), "err");
      }
    }
  }

  /* ---- rendering: one owner for the whole face -------------------- */
  function render() {
    if (!ctx) return;
    const f = lastFrame;
    if (f) {
      ScopePlot.drawAll(ctx, {
        samples: f.samples, rate: f.rate, count: f.count,
        running: running, flags: f.flags, triggerIndex: f.triggerIndex,
      });
    } else {
      ScopePlot.drawAll(ctx, {
        running: running,
        message: connected ? "Waiting for data…" : "No signal — press Demo",
      });
    }
  }

  function clearFrame() { lastFrame = null; }

  function onCaptureData(ev) {
    const d = ScopeProto.parseCaptureData(ev.payload);
    if (!d) return;
    lastFrame = d;
    updateTiles(d);
    render();
    if (running) loop(); // next capture request
  }

  function statusFlags(d) {
    if (d.flags & ScopeProto.FLAGS.OVERFLOW)
      setStatus("ADC FIFO overflow reported", "warn");
    else if (d.flags & ScopeProto.FLAGS.NO_TRIGGER)
      setStatus("no trigger — showing free-run data", "warn");
    else if (running)
      setStatus("capturing…", "ok");
  }

  function updateTiles(d) {
    const cal = !(d.flags & ScopeProto.FLAGS.NOT_CALIB);
    let mn = 4096, mx = 0, sum = 0;
    for (const s of d.samples) {
      if (s < mn) mn = s;
      if (s > mx) mx = s;
      sum += s;
    }
    const vpp = mx - mn;
    const mean = sum / d.samples.length;
    let sumSq = 0;
    for (const s of d.samples) { const x = s - mean; sumSq += x * x; }
    const rms = Math.sqrt(sumSq / d.samples.length);

    el("m-vpp").textContent = cal
      ? (vpp * 3.3 / 4095).toFixed(2) + " V"
      : vpp + " (raw)";
    el("m-rms").textContent = cal
      ? (rms * 3.3 / 4095).toFixed(2) + " V AC"
      : rms.toFixed(0) + " (raw)";

    /* frequency is estimated client-side until firmware reports it     */
    let crossings = 0;
    for (let i = 1; i < d.samples.length; i++) {
      if (d.samples[i - 1] < 2048 && d.samples[i] >= 2048) crossings++;
    }
    const freq = crossings >= 2
      ? crossings * (d.rate / d.samples.length)
      : NaN;
    el("m-freq").textContent = Number.isNaN(freq)
      ? "—"
      : (freq >= 1000 ? (freq / 1000).toFixed(2) + " kHz (est)"
                      : freq.toFixed(1) + " Hz (est)");

    const r = d.rate;
    el("m-rate").textContent =
      r >= 1e6 ? (r / 1e6).toFixed(2) + " MS/s"
      : r >= 1e3 ? (r / 1e3).toFixed(r % 1000 ? 1 : 0) + " kS/s"
      : r + " S/s";

    statusFlags(d);

    /* announce readings to screen readers at most once per second      */
    const now = Date.now();
    if ($srLive && now - srClock > 1000) {
      srClock = now;
      $srLive.textContent =
        `Vpp ${el("m-vpp").textContent}, RMS ${el("m-rms").textContent}, ` +
        `frequency ${el("m-freq").textContent}, rate ${el("m-rate").textContent}`;
    }
  }

  async function handshake() {
    const p = awaitFrame(ScopeProto.MSG.HELLO_RSP, 3000);
    await transport.writeFrame(ScopeProto.helloReq());
    const hello = await p;
    if (!hello) { setStatus("connect timeout — no greeting from device", "err");
                  return false; }
    const rsp = ScopeProto.parseHelloRsp(hello.payload);
    if (!rsp) { setStatus("bad HELLO_RSP", "err"); return false; }
    return true;
  }

  async function applyConfig() {
    seq += 1;
    const p = awaitFrame(ScopeProto.MSG.ACK, 3000);
    await transport.writeFrame(
      ScopeProto.setConfig(seq, 100000, 1024, 2048, 0, 0));
    const ack = await p;
    if (!ack) { setStatus("config timeout", "err"); return false; }
    const a = ScopeProto.parseAck(ack.payload);
    if (!a || a.status !== ScopeProto.ACK.OK) {
      setStatus("config rejected", "err"); return false;
    }
    return true;
  }

  async function loop() {
    if (!running || !connected) return;
    try {
      await transport.writeFrame(ScopeProto.captureReq(seq));
    } catch (e) {
      onEvents({ event: "disconnected" });
    }
  }

  async function teardown() {
    running = false;
    if (transport) {
      try { await transport.close(); } catch (_) { /* best effort */ }
      transport = null;
    }
    connected = false;
    syncButtons();
  }

  async function connect(kind) {
    if (connected) await disconnect();
    clearFrame();
    try {
      if (kind === "demo") {
        transport = DemoTransport();
      } else {
        const t = SerialTransport();
        if (!t.supported) {
          setStatus("Web Serial unsupported — use Chrome over HTTPS, "
                    + "or press Demo", "err");
          return;
        }
        transport = t;
      }
      transport.setCallbacks(onEvents, () => {
        connected = false; running = false; syncButtons();
        setStatus("device disconnected", "err");
      });
      await transport.connect();
      setStatus("connecting…");
      if (!(await handshake())) { await teardown(); return; }
      if (!(await applyConfig())) { await teardown(); return; }
      connected = true;
      setStatus("connected — " + (kind === "demo" ? "Simulator" : "Pico (USB)"),
                "ok");
      syncButtons();
      render();
    } catch (e) {
      setStatus("connect failed: " + e.message + " — try again", "err");
      await teardown();
    }
  }

  async function disconnect() {
    running = false;
    if (transport) {
      try { await transport.writeFrame(ScopeProto.stop(seq)); }
      catch (_) { /* fire-and-forget */ }
    }
    await teardown();
    clearFrame();
    setStatus("not connected");
    render();
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
    running = true;
    syncButtons();
    setStatus("capturing…", "ok");
    loop();
  });
  buttons.stop.addEventListener("click", async () => {
    running = false;
    try { await transport.writeFrame(ScopeProto.stop(seq)); }
    catch (_) { /* ignore */ }
    setStatus("stopped");
    syncButtons();
    render();
  });

  window.addEventListener("resize", () => {
    if (!canvas) return;
    ctx = ScopePlot.fitCanvas(canvas);
    render();                    /* re-render cached frame after resize */
  });

  window.addEventListener("load", () => {
    ctx = ScopePlot.fitCanvas(canvas);
    clearFrame();
    render();
    setStatus("not connected");
    syncButtons();
  });
})();
