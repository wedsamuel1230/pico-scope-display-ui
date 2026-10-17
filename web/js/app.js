/**
 * app.js — UI wiring for the Pico Scope (protocol v2)
 *
 * Single page, no modules (GitHub Pages friendly). Talks to either
 * SerialTransport (Web Serial) or DemoTransport through one interface:
 *   transport.writeFrame(bytes) / setCallbacks(onFrame, onClose)
 *
 * v2 adds a SCOPE/LOGIC mode switch, up to 3 simultaneous ADC channels,
 * and a per-mode trigger channel. All configuration is sent as the new
 * 13-byte SET_CONFIG via ScopeProto.setConfig(seq, opts).
 */
(() => {
  "use strict";

  const P = ScopeProto;
  const L = P.LIMITS;
  const el = (id) => document.getElementById(id);

  /* Analog channel labels. Kept local: the codec (protocol.js) is a pure
     wire layer and deliberately exports no presentation strings.        */
  const CH_LABELS = ["CH1", "CH2", "CH3"];

  /* ---- DOM --------------------------------------------------------- */
  const canvas    = el("scope-canvas");
  const $status   = el("status");
  const $srLive   = el("sr-measurements");
  const $hdrMode  = el("hdr-mode");
  const $hdrBadge = el("hdr-badges");
  const $capsLine = el("caps-line");

  const buttons = {
    demo:       el("btn-demo"),
    connect:    el("btn-connect"),
    run:        el("btn-run"),
    stop:       el("btn-stop"),
    disconnect: el("btn-disconnect"),
    apply:      el("btn-apply"),
  };

  const ctrls = {
    modeScope:   el("mode-scope"),
    modeLogic:   el("mode-logic"),
    ch:          [el("ch1"), el("ch2"), el("ch3")],
    channelsBox: el("ctl-channels"),
    trigMode:    el("trig-mode"),
    trigEdge:    el("trig-edge"),
    trigChannel: el("trig-channel"),
    rate:        el("cfg-rate"),
    count:       el("cfg-count"),
  };

  const tileLabels = [
    el("tile-label-1"), el("tile-label-2"),
    el("tile-label-3"), el("tile-label-4"),
  ];
  const tiles = {
    a: el("m-freq"), b: el("m-vpp"), c: el("m-rms"), d: el("m-rate"),
  };

  /* ---- state ------------------------------------------------------- */
  let ctx = null;
  let transport = null;
  let connected = false;
  let running = false;
  let seq = 2;
  let pending = new Map();   /* msg type -> resolve(ev) */
  let lastFrame = null;
  let appliedCfg = null;     /* the last configuration the device ACKed */
  let caps = null;           /* HELLO_RSP capabilities */
  let srClock = 0;
  let srLast = "";
  let applyTimer = 0;

  /* ---- small formatters -------------------------------------------- */
  function fmtRate(hz) {
    if (hz >= 1e6) return (hz / 1e6).toFixed(hz >= 1e7 ? 0 : 1) + " MS/s";
    if (hz >= 1e3) return (hz / 1e3).toFixed(hz >= 1e5 ? 0 : 1) + " kS/s";
    return Math.round(hz) + " S/s";
  }

  function fmtFreq(hz) {
    if (hz >= 1e6) return (hz / 1e6).toFixed(2) + " MHz";
    if (hz >= 1e3) return (hz / 1e3).toFixed(2) + " kHz";
    return hz.toFixed(1) + " Hz";
  }

  function fmtTime(sec) {
    if (!Number.isFinite(sec) || sec <= 0) return "—";
    if (sec >= 1) return sec.toFixed(2) + " s";
    if (sec >= 1e-3) return (sec * 1e3).toFixed(2) + " ms";
    if (sec >= 1e-6) return (sec * 1e6).toFixed(2) + " us";
    return (sec * 1e9).toFixed(0) + " ns";
  }

  function setStatus(text, cls) {
    $status.textContent = text;
    $status.className = "status" + (cls ? " " + cls : "");
  }

  /* ---- configuration form ------------------------------------------ */
  function currentMode() {
    return ctrls.modeLogic && ctrls.modeLogic.checked
      ? P.MODE.LOGIC : P.MODE.SCOPE;
  }

  function currentMask() {
    let m = 0;
    ctrls.ch.forEach((cb, i) => { if (cb && cb.checked) m |= (1 << i); });
    return m || 0x01;
  }

  function modeDefaults(mode) {
    return mode === P.MODE.LOGIC
      ? { rate: L.DEFAULT_LOGIC_RATE_HZ, count: L.SAMPLE_COUNT }
      : { rate: L.DEFAULT_RATE_HZ,       count: L.SAMPLE_COUNT };
  }

  function rateLimits(mode) {
    return mode === P.MODE.LOGIC
      ? [L.MIN_LOGIC_RATE_HZ, L.MAX_LOGIC_RATE_HZ]
      : [L.MIN_RATE_HZ, L.MAX_RATE_HZ];
  }

  function countLimits(mode) {
    return mode === P.MODE.LOGIC
      ? [L.MIN_LOGIC_SAMPLES, L.MAX_LOGIC_SAMPLES]
      : [L.MIN_SAMPLE_COUNT, L.MAX_SAMPLE_COUNT];
  }

  function rebuildTriggerOptions(mode, keepValue) {
    const sel = ctrls.trigChannel;
    if (!sel) return;

    const prev = keepValue !== undefined
      ? keepValue : parseInt(sel.value, 10);

    sel.textContent = "";

    const none = document.createElement("option");
    none.value = String(L.TRIG_CHANNEL_NONE);
    none.textContent = "None";
    sel.appendChild(none);

    const names = mode === P.MODE.LOGIC
      ? Array.from({ length: L.LOGIC_CHANNELS }, (_, i) => "D" + i)
      : CH_LABELS.slice(0, L.ADC_CHANNELS);

    names.forEach((name, i) => {
      const o = document.createElement("option");
      o.value = String(i);
      o.textContent = name;
      sel.appendChild(o);
    });

    sel.value = String(Number.isFinite(prev) ? prev : L.TRIG_CHANNEL_NONE);
    if (sel.selectedIndex < 0) sel.value = String(L.TRIG_CHANNEL_NONE);
  }

  /** Mirror of the firmware's mode-aware defaults when nothing is set. */
  function currentConfig() {
    const mode = currentMode();
    const [rLo, rHi] = rateLimits(mode);
    const [cLo, cHi] = countLimits(mode);
    const dflt = modeDefaults(mode);

    let rate = parseInt(ctrls.rate.value, 10);
    let count = parseInt(ctrls.count.value, 10);
    if (!Number.isFinite(rate) || rate <= 0) rate = dflt.rate;
    if (!Number.isFinite(count) || count <= 0) count = dflt.count;
    rate = Math.min(rHi, Math.max(rLo, rate));
    count = Math.min(cHi, Math.max(cLo, count));

    let trig = parseInt(ctrls.trigChannel.value, 10);
    if (!Number.isFinite(trig)) trig = L.TRIG_CHANNEL_NONE;

    return {
      rate,
      count,
      triggerLevel: 2048,
      edge: ctrls.trigEdge.value === "falling" ? 1 : 0,
      triggerMode: ctrls.trigMode.value === "normal"
        ? P.TRIG.NORMAL : P.TRIG.AUTO,
      captureMode: mode,
      channelMask: currentMask(),
      triggerChannel: trig,
    };
  }

  /** Reflect mode/capability constraints in the form itself. */
  function syncControls() {
    const mode = currentMode();
    const logic = mode === P.MODE.LOGIC;

    ctrls.ch.forEach((cb) => { if (cb) cb.disabled = logic; });
    if (ctrls.channelsBox) {
      ctrls.channelsBox.classList.toggle("muted", logic);
      ctrls.channelsBox.setAttribute("aria-disabled", logic ? "true" : "false");
    }

    const [rLo, rHi] = rateLimits(mode);
    const [cLo, cHi] = countLimits(mode);
    ctrls.rate.min = String(rLo);
    ctrls.rate.max = String(rHi);
    ctrls.count.min = String(cLo);
    ctrls.count.max = String(cHi);

    const dflt = modeDefaults(mode);
    if (!ctrls.rate.value) ctrls.rate.value = String(dflt.rate);
    if (!ctrls.count.value) ctrls.count.value = String(dflt.count);

    const prevTrig = appliedCfg && appliedCfg.captureMode === mode
      ? appliedCfg.triggerChannel : L.TRIG_CHANNEL_NONE;
    rebuildTriggerOptions(mode, prevTrig);

    /* capability gating, once HELLO_RSP has been seen */
    if (caps) {
      if (ctrls.modeLogic) {
        ctrls.modeLogic.disabled = !(caps.capabilities & P.CAPS.LOGIC);
      }
      ctrls.ch.forEach((cb, i) => {
        if (!cb) return;
        if (i > 0 && !(caps.capabilities & P.CAPS.MULTI_ADC)) cb.disabled = true;
        if (i >= (caps.analogChannels || L.ADC_CHANNELS)) cb.disabled = true;
      });
    }

    syncHeader();
  }

  function syncHeader() {
    const mode = currentMode();
    const logic = mode === P.MODE.LOGIC;
    $hdrMode.textContent = logic ? "LOGIC" : "SCOPE";
    document.body.dataset.mode = logic ? "logic" : "scope";

    $hdrBadge.textContent = "";
    if (logic) {
      const b = document.createElement("span");
      b.className = "badge logic";
      b.textContent = L.LOGIC_CHANNELS + " LANES";
      $hdrBadge.appendChild(b);
      return;
    }

    const classes = ["ch1", "ch2", "ch3"];
    const mask = currentMask();
    for (let i = 0; i < L.ADC_CHANNELS; i++) {
      if (!(mask & (1 << i))) continue;
      const b = document.createElement("span");
      b.className = "badge " + classes[i];
      b.textContent = CH_LABELS[i];
      $hdrBadge.appendChild(b);
    }
  }

  function setTileLabels(a, b, c, d) {
    const list = [a, b, c, d];
    tileLabels.forEach((node, i) => {
      if (node && list[i] != null) node.textContent = list[i];
    });
  }

  /* ---- protocol plumbing ------------------------------------------- */
  function awaitFrame(type, timeoutMs) {
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        pending.delete(type);
        resolve(null);
      }, timeoutMs);
      pending.set(type, (ev) => {
        clearTimeout(timer);
        pending.delete(type);
        resolve(ev);
      });
    });
  }

  function onEvents(ev) {
    if (ev.event === "disconnected") {
      setStatus("device disconnected", "err");
      teardown();
      syncButtons();
      return;
    }
    if (ev.event === "invalid") {
      setStatus("bad frame ignored", "warn");
      return;
    }
    if (ev.event !== "frame") return;

    const waiter = pending.get(ev.type);
    if (waiter) { waiter(ev); return; }

    if (ev.type === P.MSG.CAPTURE_DATA) {
      onCaptureData(ev);
    } else if (ev.type === P.MSG.ERROR) {
      const detail = new TextDecoder().decode(ev.payload.subarray(3));
      setStatus("device error: " + detail, "err");
    }
  }

  function onCaptureData(ev) {
    const d = P.parseCaptureData(ev.payload);
    if (!d) { setStatus("malformed capture payload", "err"); return; }

    lastFrame = d;
    updateTiles(d);
    render();

    if (running) loop();
  }

  /* ---- measurement tiles ------------------------------------------- */
  function updateTiles(d) {
    if (d.mode === P.MODE.LOGIC) updateLogicTiles(d);
    else updateScopeTiles(d);
  }

  function updateScopeTiles(d) {
    const channels = d.channels || 1;
    const n = Math.min(d.count, Math.floor(d.samples.length / channels));
    if (n < 2) { clearTiles(); return; }

    /* first enabled channel is always slot 0 of the interleaved buffer */
    let min = 4095, max = 0, sum = 0;
    for (let i = 0; i < n; i++) {
      const v = d.samples[i * channels];
      if (v < min) min = v;
      if (v > max) max = v;
      sum += v;
    }
    const mean = sum / n;

    let sq = 0;
    for (let i = 0; i < n; i++) {
      const dv = d.samples[i * channels] - mean;
      sq += dv * dv;
    }
    const rms = Math.sqrt(sq / n);
    const vpp = max - min;

    /* frequency: count upward crossings of mid-scale, per-channel rate */
    let crossings = 0;
    for (let i = 1; i < n; i++) {
      const a = d.samples[(i - 1) * channels];
      const b = d.samples[i * channels];
      if (a < mean && b >= mean) crossings++;
    }
    const freq = crossings * (d.rate / n);

    const calibrated = !(d.flags & P.FLAGS.NOT_CALIB);
    const scale = 3.3 / 4095;

    setTileLabels("FREQ", "VPP", "RMS", "RATE");
    tiles.a.textContent = freq > 0 ? fmtFreq(freq) + " (est)" : "—";
    tiles.b.textContent = calibrated
      ? (vpp * scale).toFixed(3) + " V"
      : vpp + " (raw)";
    tiles.c.textContent = calibrated
      ? (rms * scale).toFixed(3) + " V"
      : Math.round(rms) + " (raw)";
    tiles.d.textContent = fmtRate(d.rate);

    announce(d);
  }

  function updateLogicTiles(d) {
    const n = Math.min(d.count, d.samples.length);
    if (n < 2) { clearTiles(); return; }

    let edges = 0;
    let active = 0;
    for (let lane = 0; lane < L.LOGIC_CHANNELS; lane++) {
      const bit = 1 << lane;
      let prev = (d.samples[0] & bit) !== 0;
      let sawHigh = prev;
      for (let i = 1; i < n; i++) {
        const cur = (d.samples[i] & bit) !== 0;
        if (cur !== prev) edges++;
        if (cur) sawHigh = true;
        prev = cur;
      }
      if (sawHigh) active++;
    }

    const span = d.rate > 0 ? n / d.rate : 0;

    setTileLabels("EDGES", "LANES", "RATE", "SPAN");
    tiles.a.textContent = String(edges);
    tiles.b.textContent = active + " / " + L.LOGIC_CHANNELS;
    tiles.c.textContent = fmtRate(d.rate);
    tiles.d.textContent = fmtTime(span);

    announce(d);
  }

  function clearTiles() {
    Object.values(tiles).forEach((t) => { if (t) t.textContent = "—"; });
  }

  function flagList(d) {
    const out = [];
    if (d.flags & P.FLAGS.OVERFLOW) out.push("overflow");
    if (d.flags & P.FLAGS.NO_TRIGGER) out.push("no trigger");
    if (d.flags & P.FLAGS.TRUNCATED) out.push("truncated");
    if (d.flags & P.FLAGS.NOT_CALIB) out.push("raw codes, uncalibrated");
    return out;
  }

  function announce(d) {
    const now = Date.now();
    if (now - srClock < 1000) return;
    srClock = now;

    const label = d.mode === P.MODE.LOGIC ? "Logic" : "Scope";
    const sentence = label + " capture: " +
      tiles.a.textContent + " " + tileLabels[0].textContent + ", " +
      tiles.b.textContent + " " + tileLabels[1].textContent + ", " +
      tiles.c.textContent + " " + tileLabels[2].textContent + ", " +
      tiles.d.textContent + " " + tileLabels[3].textContent +
      ". " + flagList(d).join(", ") + ".";

    if (sentence !== srLast) {
      srLast = sentence;
      $srLive.textContent = sentence;
    }
  }

  /* ---- rendering ---------------------------------------------------- */
  function render() {
    if (!ctx) return;

    const activeMask = currentMode() === P.MODE.LOGIC
      ? L.LOGIC_PIN_MASK
      : (appliedCfg && appliedCfg.captureMode === P.MODE.SCOPE
          ? appliedCfg.channelMask : currentMask());

    if (lastFrame && lastFrame.samples && lastFrame.samples.length) {
      const nChan = Math.max(1, lastFrame.channels || 1);
      /* The firmware de-interleaves into dense ascending slots, so the
         frame's OWN channel count — not the pending form state — decides
         which slots exist. Trusting the form here indexed past the end
         of the buffer whenever a config change was still in flight.    */
      const frameMask = lastFrame.mode === P.MODE.LOGIC
        ? L.LOGIC_PIN_MASK
        : (nChan >= 3 ? 0x07 : nChan === 2 ? 0x03 : 0x01);

      ScopePlot.drawAll(ctx, {
        mode: lastFrame.mode,
        channelMask: frameMask,
        samples: lastFrame.samples,
        rate: lastFrame.rate,
        count: lastFrame.count,
        channels: lastFrame.channels,
        running,
        flags: lastFrame.flags,
        triggerIndex: lastFrame.triggerIndex,
      });
    } else {
      ScopePlot.drawAll(ctx, {
        mode: currentMode(),
        channelMask: activeMask,
        running,
        message: connected ? "Waiting for data…" : "No signal — press Demo",
      });
    }
  }

  function clearFrame() {
    lastFrame = null;
    clearTiles();
  }

  /* ---- command flow -------------------------------------------------- */
  async function handshake() {
    const waiter = awaitFrame(P.MSG.HELLO_RSP, 3000);
    await transport.writeFrame(P.helloReq());
    const ev = await waiter;
    if (!ev) { setStatus("no HELLO response", "warn"); return null; }

    caps = P.parseHelloRsp(ev.payload);
    if (!caps) { setStatus("malformed HELLO response", "err"); return null; }

    const fw = "v" + ((caps.firmwareVersion >> 16) & 0xFF) + "." +
               ((caps.firmwareVersion >> 8) & 0xFF) + "." +
               (caps.firmwareVersion & 0xFF);

    $capsLine.textContent = "firmware " + fw +
      " · " + caps.analogChannels + " ADC · " +
      caps.logicChannels + " logic";

    return caps;
  }

  async function applyConfig(announceIt) {
    if (!connected || !transport) return null;

    const cfg = currentConfig();
    const mode = cfg.captureMode;

    /* fall back to defaults for the OTHER mode so the device is never
     * asked for a rate that the current UI mode cannot express        */
    const waiter = awaitFrame(P.MSG.ACK, 3000);

    try {
      await transport.writeFrame(P.setConfig(seq, cfg));
    } catch (e) {
      setStatus("send failed: " + e.message, "err");
      return null;
    }

    const ev = await waiter;
    if (!ev) {
      if (announceIt) setStatus("no ACK — config not applied", "warn");
      return null;
    }

    const ack = P.parseAck(ev.payload);
    if (!ack) { setStatus("malformed ACK", "err"); return null; }
    if (ack.status !== P.ACK.OK) {
      setStatus("config rejected (code " + ack.status + ")", "err");
      return ack;
    }

    appliedCfg = cfg;
    ctrls.rate.value = String(cfg.rate);
    ctrls.count.value = String(cfg.count);

    const total = ack.appliedRate;
    const perCh = mode === P.MODE.LOGIC
      ? total : P.channelRate(total, cfg.channelMask);

    if (announceIt) {
      setStatus("applied · " + fmtRate(perCh) +
        (mode === P.MODE.LOGIC ? " per lane" : " per channel") +
        " · " + ack.appliedCount + " samples", "ok");
    }

    syncHeader();
    return ack;
  }

  function applyConfigSoon() {
    clearTimeout(applyTimer);
    applyTimer = setTimeout(() => { applyConfig(false); }, 150);
  }

  function loop() {
    if (!connected || !running || !transport) return;
    transport.writeFrame(P.captureReq(seq)).catch(() => { /* link lost */ });
  }

  /* ---- connection lifecycle ----------------------------------------- */
  function teardown() {
    connected = false;
    running = false;
    transport = null;
    pending.forEach((resolve) => resolve(null));
    pending.clear();
  }

  async function connect(kind) {
    if (connected) return;

    const t = kind === "demo" ? DemoTransport() : SerialTransport();
    if (!t.supported) {
      setStatus("Web Serial unavailable — use Demo or Chrome over HTTPS", "warn");
      return;
    }

    setStatus("connecting…");
    t.setCallbacks(onEvents, () => {
      if (connected) {
        setStatus("link closed", "warn");
        teardown();
        syncButtons();
      }
    });

    try {
      await t.connect();
    } catch (e) {
      setStatus("connect failed: " + e.message, "err");
      return;
    }

    transport = t;
    connected = true;
    setStatus(kind === "demo" ? "demo running" : "connected", "ok");
    syncButtons();

    const c = await handshake();
    if (c) {
      const mode = currentMode();
      if (mode === P.MODE.LOGIC && !(c.capabilities & P.CAPS.LOGIC)) {
        ctrls.modeScope.checked = true;
      }
      syncControls();
      await applyConfig(true);
    }
  }

  async function disconnect() {
    if (!connected) return;
    running = false;

    try { await transport.writeFrame(P.stop(seq)); }
    catch (_) { /* port may already be gone */ }

    try { await transport.close(); }
    catch (_) { /* surface nothing; close() already fired */ }

    teardown();
    clearFrame();
    syncControls();
    render();
    setStatus("disconnected");
    syncButtons();
  }

  function syncButtons() {
    buttons.demo.disabled = connected;
    buttons.connect.disabled = connected;
    buttons.disconnect.disabled = !connected;
    buttons.run.disabled = !connected || running;
    buttons.stop.disabled = !connected || !running;
    if (buttons.apply) buttons.apply.disabled = !connected;
  }

  /* ---- events -------------------------------------------------------- */
  buttons.demo.addEventListener("click", () => connect("demo"));
  buttons.connect.addEventListener("click", () => connect("serial"));
  if (buttons.apply) {
    buttons.apply.addEventListener("click", () => applyConfig(true));
  }

  buttons.run.addEventListener("click", () => {
    if (!connected || running) return;
    running = true;
    syncButtons();
    setStatus("capturing…", "ok");
    loop();
  });

  buttons.stop.addEventListener("click", () => {
    if (!connected || !running) return;
    running = false;
    transport.writeFrame(P.stop(seq)).catch(() => { /* ignore */ });
    setStatus("stopped");
    syncButtons();
    render();
  });

  buttons.disconnect.addEventListener("click", () => { disconnect(); });

  ctrls.modeScope.addEventListener("change", () => {
    if (!ctrls.modeScope.checked) return;
    syncControls();
    clearFrame();
    render();
    applyConfigSoon();
  });

  ctrls.modeLogic.addEventListener("change", () => {
    if (!ctrls.modeLogic.checked) return;
    syncControls();
    clearFrame();
    render();
    applyConfigSoon();
  });

  ctrls.ch.forEach((cb) => {
    if (!cb) return;
    cb.addEventListener("change", () => {
      if (currentMask() === 0) { cb.checked = true; return; }
      syncHeader();
      clearFrame();
      render();
      applyConfigSoon();
    });
  });

  [ctrls.trigMode, ctrls.trigEdge, ctrls.trigChannel,
   ctrls.rate, ctrls.count].forEach((node) => {
    if (!node) return;
    node.addEventListener("change", () => applyConfigSoon());
  });

  window.addEventListener("resize", () => {
    ScopePlot.fitCanvas(canvas);
    render();
  });

  window.addEventListener("load", () => {
    ScopePlot.fitCanvas(canvas);
    ctx = canvas.getContext("2d");

    if (ctrls.rate) ctrls.rate.value = String(L.DEFAULT_RATE_HZ);
    if (ctrls.count) ctrls.count.value = String(L.SAMPLE_COUNT);

    syncControls();
    clearFrame();
    render();
    setStatus("not connected");
    if (buttons.apply) buttons.apply.disabled = true;
    syncButtons();
  });
})();
