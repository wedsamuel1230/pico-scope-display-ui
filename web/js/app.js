/**
 * app.js — UI wiring for the Pico Scope (protocol v2)
 *
 * Single page, no modules (GitHub Pages friendly). Talks to either
 * SerialTransport (Web Serial) or DemoTransport through one interface:
 *   transport.writeFrame(bytes) / setCallbacks(onFrame, onClose)
 *
 * v2 adds a SCOPE/LOGIC mode switch, up to 3 simultaneous ADC channels,
 * a per-mode trigger channel and a settable trigger level. All
 * configuration is sent as the new 13-byte SET_CONFIG via
 * ScopeProto.setConfig(seq, opts).
 *
 * The dashboard card always drives plot.js's landscape "wide" face: that
 * layout paints background + graticule + traces only, so the header, the
 * time axis and the flag row are rendered here in HTML.
 *
 * Product rule: there is no analog front end on this board, so amplitudes
 * are ALWAYS raw 12-bit ADC codes and logic rates are nominal. Nothing in
 * this file may name volts.
 */
(() => {
  "use strict";

  const P = ScopeProto;
  const L = P.LIMITS;
  const el = (id) => document.getElementById(id);

  /* Analog channel labels. Kept local: the codec (protocol.js) is a pure
     wire layer and deliberately exports no presentation strings.        */
  const CH_LABELS = ["CH1", "CH2", "CH3"];

  /* plot.js layout + scale facts (mirror of LAYOUTS.wide).              */
  const LAYOUT = "wide";
  const GRID_COLS = 12;      /* time divisions across the wide face      */
  const GRID_ROWS = 4;       /* vertical divisions = codes per division  */
  const FULL_SCALE = 4095;   /* 12-bit ADC full scale, in codes          */

  const THEME_KEY = "pico-scope-theme";
  const DEFAULT_TRIG_LEVEL = 2048;

  /* Auto set: a host-side measurement loop, because the firmware has no
     autoscale routine — the AUTO softkey on the LCD is only a label.
     Each pass grabs a frame, measures it, rewrites the timebase and the
     trigger level, and waits for the ACK before judging the result.     */
  const AUTOSET_PASSES  = 3;     /* measurement passes before stopping   */
  const AUTOSET_PERIODS = 2.5;   /* cycles we want across the window     */
  const AUTOSET_TIMEOUT = 1500;  /* ms to wait for one frame             */
  const AUTOSET_FLAT    = 64;    /* codes of swing worth edge-triggering */

  /* ---- DOM --------------------------------------------------------- */
  const canvas      = el("scope-canvas");
  const $status     = el("status");
  const $srLive     = el("sr-measurements");
  const $hdrMode    = el("hdr-mode");
  const $hdrBadge   = el("hdr-badges");
  const $capsLine   = el("caps-line");
  const $panelState = el("panel-state");
  const $plotMeta   = el("plot-meta");
  const $plotTdiv   = el("plot-tdiv");
  const $plotWindow = el("plot-window");
  const $plotRun    = el("plot-run");
  const $plotRunTxt = el("plot-run-text");

  const buttons = {
    demo:       el("btn-demo"),
    connect:    el("btn-connect"),
    run:        el("btn-run"),
    stop:       el("btn-stop"),
    disconnect: el("btn-disconnect"),
    apply:      el("btn-apply"),
    theme:      el("btn-theme"),
    autoSet:    el("btn-autoset"),
    cursors:    el("btn-cursors"),
    csv:        el("btn-csv"),
    copy:       el("btn-copy"),
  };

  const ctrls = {
    modeScope:    el("mode-scope"),
    modeLogic:    el("mode-logic"),
    ch:           [el("ch1"), el("ch2"), el("ch3")],
    channelsBox:  el("ctl-channels"),
    trigMode:     el("trig-mode"),
    trigEdge:     el("trig-edge"),
    trigChannel:  el("trig-channel"),
    trigLevel:    el("cfg-trig-level"),
    trigLevelOut: el("trig-level-out"),
    trigLevelCell: el("cell-trig-level"),
    rate:         el("cfg-rate"),
    count:        el("cfg-count"),
  };

  const cursorOut = {
    root:  el("plot-cursors"),
    a:     el("cursor-a"),
    delta: el("cursor-delta"),
    b:     el("cursor-b"),
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
  let fittedW = 0;           /* clientWidth the backing store suits      */
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
  let autoSetBusy = false;

  /* Resolve the stored theme before the first paint so a light-theme
     reader never sees a dark flash. */
  applyTheme(storedTheme(), false);

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

  /* Same unit style as plot.js fmtTime() so the HTML axis row and the
     on-canvas scale row can never disagree.                              */
  function fmtTime(sec) {
    if (!Number.isFinite(sec) || sec <= 0) return "—";
    if (sec >= 1) return sec.toFixed(2) + " s";
    if (sec >= 1e-3) return (sec * 1e3).toFixed(2) + " ms";
    if (sec >= 1e-6) return (sec * 1e6).toFixed(1) + " µs";
    return (sec * 1e9).toFixed(0) + " ns";
  }

  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

  function setStatus(text, cls) {
    if (!$status) return;
    $status.textContent = text;
    $status.className = "status" + (cls ? " " + cls : "");
  }

  /* ---- theme -------------------------------------------------------- */
  function currentTheme() {
    return document.documentElement.dataset.theme === "light"
      ? "light" : "dark";
  }

  function storedTheme() {
    try {
      const t = localStorage.getItem(THEME_KEY);
      if (t === "light" || t === "dark") return t;
    } catch (_) { /* storage blocked (private mode) — stay on the default */ }
    return "dark";
  }

  /** Flip [data-theme], persist it and push the palette into plot.js.  */
  function applyTheme(name, persist) {
    const theme = name === "light" ? "light" : "dark";
    document.documentElement.dataset.theme = theme;

    if (persist) {
      try { localStorage.setItem(THEME_KEY, theme); }
      catch (_) { /* nothing to do — the theme still applies this session */ }
    }

    ScopePlot.setTheme(theme);

    if (buttons.theme) {
      const next = theme === "dark" ? "light" : "dark";
      buttons.theme.setAttribute("aria-label", "Switch to " + next + " theme");
      buttons.theme.title = "Switch to " + next + " theme";
    }
    return theme;
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

  function currentTriggerLevel() {
    const raw = ctrls.trigLevel ? parseInt(ctrls.trigLevel.value, 10) : NaN;
    return clamp(Number.isFinite(raw) ? raw : DEFAULT_TRIG_LEVEL,
                 0, FULL_SCALE);
  }

  /** Keep the readout (and the slider's filled portion) in step.       */
  function syncTriggerLevelOut() {
    const v = currentTriggerLevel();
    if (ctrls.trigLevelOut) ctrls.trigLevelOut.textContent = String(v);
    if (ctrls.trigLevel) {
      ctrls.trigLevel.style.setProperty(
        "--fill", ((v / FULL_SCALE) * 100).toFixed(2) + "%");
    }
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

    let rate = parseInt(ctrls.rate ? ctrls.rate.value : "", 10);
    let count = parseInt(ctrls.count ? ctrls.count.value : "", 10);
    if (!Number.isFinite(rate) || rate <= 0) rate = dflt.rate;
    if (!Number.isFinite(count) || count <= 0) count = dflt.count;
    rate = clamp(rate, rLo, rHi);
    count = clamp(count, cLo, cHi);

    let trig = parseInt(ctrls.trigChannel ? ctrls.trigChannel.value : "", 10);
    if (!Number.isFinite(trig)) trig = L.TRIG_CHANNEL_NONE;

    return {
      rate,
      count,
      /* the slider is the only source of the trigger level; the firmware
         zeroes it for LOGIC captures, so it is always safe to send     */
      triggerLevel: currentTriggerLevel(),
      edge: ctrls.trigEdge && ctrls.trigEdge.value === "falling" ? 1 : 0,
      triggerMode: ctrls.trigMode && ctrls.trigMode.value === "normal"
        ? P.TRIG.NORMAL : P.TRIG.AUTO,
      captureMode: mode,
      channelMask: currentMask(),
      triggerChannel: trig,
    };
  }

  /**
   * Reflect mode/capability constraints in the form itself.
   * `applyModeDefaults` is set when the operator switches mode: each mode
   * carries its own timebase/depth, and seeding them is what stops LOGIC
   * from inheriting SCOPE's 100 kS/s.
   */
  function syncControls(applyModeDefaults) {
    /* Home and Docs carry no capture form — nothing to synchronise there */
    if (!ctrls.rate || !ctrls.count) { syncHeader(); return; }

    const mode = currentMode();
    const logic = mode === P.MODE.LOGIC;

    ctrls.ch.forEach((cb) => { if (cb) cb.disabled = logic; });
    if (ctrls.channelsBox) {
      ctrls.channelsBox.classList.toggle("muted", logic);
      ctrls.channelsBox.setAttribute("aria-disabled", logic ? "true" : "false");
    }

    /* LOGIC triggers on lane edges; the level stays readable but is
       flagged as ignored rather than silently pretending to matter.    */
    if (ctrls.trigLevelCell) {
      ctrls.trigLevelCell.classList.toggle("is-inactive", logic);
    }
    if (ctrls.trigLevel) {
      ctrls.trigLevel.setAttribute(
        "aria-describedby",
        logic ? "trig-level-note trig-level-na" : "trig-level-note");
    }

    const [rLo, rHi] = rateLimits(mode);
    const [cLo, cHi] = countLimits(mode);
    ctrls.rate.min = String(rLo);
    ctrls.rate.max = String(rHi);
    ctrls.count.min = String(cLo);
    ctrls.count.max = String(cHi);

    const dflt = modeDefaults(mode);
    const rateVal = parseInt(ctrls.rate.value, 10);
    const countVal = parseInt(ctrls.count.value, 10);
    if (applyModeDefaults || !Number.isFinite(rateVal)) {
      ctrls.rate.value = String(dflt.rate);
    }
    if (applyModeDefaults || !Number.isFinite(countVal)) {
      ctrls.count.value = String(dflt.count);
    }

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
    if (!$hdrMode) return;
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

    /* every analog chip stays on the card; the masked-off ones dim so the
       capture mask reads at a glance instead of vanishing               */
    const classes = ["ch1", "ch2", "ch3"];
    const mask = currentMask();
    for (let i = 0; i < L.ADC_CHANNELS; i++) {
      const b = document.createElement("span");
      b.className = "badge " + classes[i] + ((mask & (1 << i)) ? "" : " is-off");
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

  /** Colour the readout cells by meaning, not by position.             */
  function setTileTones(list) {
    [tiles.a, tiles.b, tiles.c, tiles.d].forEach((node, i) => {
      if (!node || !list[i]) return;
      const cell = node.closest(".cell");
      if (cell) cell.dataset.tone = list[i];
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

  /* ---- export -------------------------------------------------------
     One row per sample, raw ADC codes, nothing rescaled on the way out
     so the file matches what the plot is drawing.                     */

  /** Which analog channels a frame's columns are. The wire carries a
      COUNT, not a mask, so the applied configuration is the only honest
      source; fall back to the count when nothing has been applied.     */
  function frameSlotLabels(d) {
    const mask = (appliedCfg && appliedCfg.channelMask) ||
      (d.channels >= 3 ? 0x07 : d.channels === 2 ? 0x03 : 0x01);
    return ScopePlot.slotsForMask(mask).map((slot) => CH_LABELS[slot]);
  }

  function frameToCSV(d) {
    const lines = [];
    const dt = d.rate > 0 ? 1 / d.rate : 0;

    if (d.mode === P.MODE.LOGIC) {
      const head = ["t_ms"];
      for (let lane = 0; lane < L.LOGIC_CHANNELS; lane++) head.push("D" + lane);
      lines.push(head.join(","));
      const n = Math.min(d.count, d.samples.length);
      for (let i = 0; i < n; i++) {
        const row = [(i * dt * 1000).toFixed(6)];
        for (let lane = 0; lane < L.LOGIC_CHANNELS; lane++) {
          row.push((d.samples[i] >> lane) & 1);
        }
        lines.push(row.join(","));
      }
      return lines.join("\r\n") + "\r\n";
    }

    const channels = Math.max(1, d.channels || 1);
    const labels = frameSlotLabels(d).slice(0, channels);
    while (labels.length < channels) labels.push("CH" + (labels.length + 1));
    lines.push(["t_ms"].concat(labels).join(","));

    const n = Math.min(d.count, Math.floor(d.samples.length / channels));
    for (let i = 0; i < n; i++) {
      const row = [(i * dt * 1000).toFixed(6)];
      for (let c = 0; c < channels; c++) row.push(d.samples[i * channels + c]);
      lines.push(row.join(","));
    }
    return lines.join("\r\n") + "\r\n";
  }

  function syncExportButtons() {
    const has = !!lastFrame;
    if (buttons.csv) buttons.csv.disabled = !has;
    if (buttons.copy) buttons.copy.disabled = !has;
  }

  function downloadCSV() {
    if (!lastFrame) { setStatus("nothing to export yet", "warn"); return; }
    const csv = frameToCSV(lastFrame);
    const blob = new Blob([csv], { type: "text/csv;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "pico-scope-" +
      new Date().toISOString().replace(/[:.]/g, "-") + ".csv";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 0);
    setStatus("exported " + lastFrame.count + " samples as CSV", "ok");
  }

  function copyCodes() {
    if (!lastFrame) { setStatus("nothing to export yet", "warn"); return; }
    const csv = frameToCSV(lastFrame);
    if (!navigator.clipboard || !navigator.clipboard.writeText) {
      setStatus("clipboard unavailable — use Download CSV", "warn");
      return;
    }
    navigator.clipboard.writeText(csv).then(
      () => setStatus("codes copied to the clipboard", "ok"),
      () => setStatus("clipboard blocked — use Download CSV", "warn"));
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
    if (!canvas) return;
    if (d.mode === P.MODE.LOGIC) updateLogicTiles(d);
    else updateScopeTiles(d);
  }

  function updateScopeTiles(d) {
    const channels = d.channels || 1;
    const n = Math.min(d.count, Math.floor(d.samples.length / channels));
    if (n < 2) { clearTiles(); return; }

    /* first enabled channel is always slot 0 of the interleaved buffer */
    const nChan = Math.max(1, d.channels || 1);
    const frameMask = nChan >= 3 ? 0x07 : nChan === 2 ? 0x03 : 0x01;
    const first = CH_LABELS[ScopePlot.slotsForMask(frameMask)[0]] || CH_LABELS[0];

    let min = FULL_SCALE, max = 0, sum = 0;
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

    /* Amplitudes stay in raw ADC codes. The firmware always sets
       SCOPE_FLAG_NOT_CALIB and there is no analog front end, so there is
       no honest way to name a voltage here.                          */
    setTileTones(["amber", "ink", "ink", "green"]);
    setTileLabels(first + " · FREQ (EST)", first + " · VPP",
                  first + " · RMS", "RATE");
    tiles.a.textContent = freq > 0 ? fmtFreq(freq) : "—";
    tiles.b.textContent = vpp + " CODES";
    tiles.c.textContent = Math.round(rms) + " CODES";
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

    /* logic lane rates come from an uncalibrated divider, so they are
       labelled nominal rather than presented as exact                */
    setTileTones(["amber", "ink", "green", "ink"]);
    setTileLabels("EDGES", "LANES ACTIVE", "RATE (NOMINAL)", "SPAN");
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
  /* plot.js requires this exact order before a paint:
       setLayout -> fitCanvas -> setTheme -> drawAll
     setLayout() rewrites W/H, so the backing store has to be re-derived
     after it and after any resize or theme change.                      */
  function fitCanvasNow() {
    if (!canvas) { ctx = null; return; }
    ctx = ScopePlot.fitCanvas(canvas);
    fittedW = canvas.clientWidth || ScopePlot.W;
  }

  function paint(opts) {
    if (!canvas) return;
    ScopePlot.setLayout(LAYOUT);

    const cssW = canvas.clientWidth || ScopePlot.W;
    if (!ctx || cssW !== fittedW) fitCanvasNow();

    ScopePlot.setTheme(currentTheme());
    ScopePlot.drawAll(ctx, opts);
  }

  function drawOpts() {
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

      return {
        mode: lastFrame.mode,
        channelMask: frameMask,
        samples: lastFrame.samples,
        rate: lastFrame.rate,
        count: lastFrame.count,
        channels: lastFrame.channels,
        running,
        flags: lastFrame.flags,
        triggerIndex: lastFrame.triggerIndex,
        triggerLevel: currentTriggerLevel(),
        cursors: cursorOn ? [cursorA, cursorB] : null,
      };
    }

    return {
      mode: currentMode(),
      channelMask: activeMask,
      running,
      message: connected ? "Waiting for data…" : "No signal — press Run the demo",
    };
  }

  function render() {
    if (!canvas) return;
    syncExportButtons();
    paint(drawOpts());
    syncCardChrome();
  }

  /** Header meta, time axis and RUN lamp — the chrome the wide layout
      deliberately leaves to the page instead of the canvas.            */
  function syncCardChrome() {
    if ($plotRun) {
      $plotRun.classList.toggle("is-run", !!running);
      if ($plotRunTxt) $plotRunTxt.textContent = running ? "RUN" : "STOP";
    }

    const d = lastFrame;
    if (!d) {
      if ($plotMeta) {
        $plotMeta.textContent = connected
          ? "waiting for the first frame…"
          : "no capture yet";
      }
      if ($plotTdiv) $plotTdiv.textContent = "T/DIV —";
      if ($plotWindow) $plotWindow.textContent = "— WINDOW";
      return;
    }

    const logic = d.mode === P.MODE.LOGIC;
    const rate = d.rate || 0;
    const count = d.count || 0;
    const channels = logic ? L.LOGIC_CHANNELS : Math.max(1, d.channels || 1);

    const parts = [
      fmtRate(rate) + (logic ? " nominal" : ""),
      count + " SAMPLES",
      channels + (logic ? " LANES" : " CH"),
    ];
    /* the vertical scale is always in codes: 4095 over GRID_ROWS */
    if (!logic) parts.push(Math.round(FULL_SCALE / GRID_ROWS) + " C/DIV");
    if ($plotMeta) $plotMeta.textContent = parts.join("  ·  ");

    const window = rate > 0 ? count / rate : 0;
    if ($plotTdiv) $plotTdiv.textContent = "T/DIV " + fmtTime(window / GRID_COLS);
    if ($plotWindow) $plotWindow.textContent = fmtTime(window) + " WINDOW";
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

    if ($capsLine) {
      $capsLine.textContent = "firmware " + fw +
        " · " + caps.analogChannels + " ADC · " +
        caps.logicChannels + " logic";
    }

    return caps;
  }

  async function applyConfig(announceIt) {
    if (!connected || !transport) return null;

    const cfg = currentConfig();
    const mode = cfg.captureMode;

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
    if (ctrls.rate) ctrls.rate.value = String(cfg.rate);
    if (ctrls.count) ctrls.count.value = String(cfg.count);

    const total = ack.appliedRate;
    const perCh = mode === P.MODE.LOGIC
      ? total : P.channelRate(total, cfg.channelMask);

    if (announceIt) {
      setStatus("applied · " + fmtRate(perCh) +
        (mode === P.MODE.LOGIC ? " per lane (nominal)" : " per channel") +
        " · " + ack.appliedCount + " samples · trigger level " +
        cfg.triggerLevel + " code" + (cfg.triggerLevel === 1 ? "" : "s"), "ok");
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
      setStatus("Web Serial unavailable — use the demo or Chrome over HTTPS", "warn");
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
      let forced = false;
      if (mode === P.MODE.LOGIC && !(c.capabilities & P.CAPS.LOGIC)) {
        ctrls.modeScope.checked = true;
        forced = true;
      }
      syncControls(forced);
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
    if (buttons.demo) buttons.demo.disabled = connected;
    if (buttons.connect) buttons.connect.disabled = connected;
    if (buttons.disconnect) buttons.disconnect.disabled = !connected;
    if (buttons.run) buttons.run.disabled = !connected || running;
    if (buttons.stop) buttons.stop.disabled = !connected || !running;
    if (buttons.apply) buttons.apply.disabled = !connected;
    if (buttons.autoSet) buttons.autoSet.disabled = !connected || autoSetBusy;

    if ($panelState) {
      $panelState.textContent = connected ? "CONNECTED" : "DISCONNECTED";
      $panelState.classList.toggle("is-on", connected);
    }
  }

  /* ---- auto set ------------------------------------------------------ */
  /* Nothing below is firmware-specific: it is a plain loop over
     CAPTURE_REQ / SET_CONFIG, so it behaves the same on the simulator and
     on the board, and the level it picks is drawn back onto the trace as
     a dashed rule (plot.js drawLevelLine).                              */

  /* Per-slot min/max/mean and mean-crossing count of the interleaved
     buffer. Slot order is the firmware's dense ascending order, so slot
     i is the i-th ENABLED channel, not channel i.                       */
  function slotStats(frame) {
    const channels = Math.max(1, frame.channels || 1);
    const n = Math.min(frame.count || 0,
                       Math.floor(frame.samples.length / channels));
    if (n < 4) return null;

    const chans = ScopePlot.slotsForMask(
      channels >= 3 ? 0x07 : channels === 2 ? 0x03 : 0x01);
    const stats = [];
    for (let slot = 0; slot < channels; ++slot) {
      let min = FULL_SCALE;
      let max = 0;
      let sum = 0;
      for (let i = 0; i < n; ++i) {
        const v = frame.samples[i * channels + slot];
        if (v < min) min = v;
        if (v > max) max = v;
        sum += v;
      }
      const mean = sum / n;
      let crossings = 0;
      for (let i = 1; i < n; ++i) {
        const a = frame.samples[(i - 1) * channels + slot];
        const b = frame.samples[i * channels + slot];
        if (a < mean && b >= mean) ++crossings;
      }
      stats.push({
        channel: chans[slot] === undefined ? slot : chans[slot],
        min: min, max: max, mean: mean,
        vpp: max - min,
        crossings: crossings,
      });
    }
    return { channels: channels, n: n, stats: stats };
  }

  /* Turn one measurement into the next SET_CONFIG. `settled` means the
     frame already looks right, so the caller can stop early.           */
  function autoSetPlan(frame) {
    const m = slotStats(frame);
    if (!m) return null;

    /* Trigger on the lane with the most swing: a flat lane has no edge. */
    let best = 0;
    for (let slot = 1; slot < m.channels; ++slot) {
      if (m.stats[slot].vpp > m.stats[best].vpp) best = slot;
    }
    const s = m.stats[best];

    /* Timebase: put AUTOSET_PERIODS cycles across the window. With no
       measurable period, zoom out a step so the next pass sees more.   */
    const window = frame.rate > 0 ? frame.count / frame.rate : 0;
    let rate;
    if (window > 0 && s.crossings >= 1 && s.crossings <= m.n / 8) {
      const period = window / s.crossings;
      rate = frame.count / (period * AUTOSET_PERIODS);
    } else {
      rate = frame.rate / 4;
    }
    rate = Math.round(clamp(rate, L.MIN_RATE_HZ, L.MAX_RATE_HZ));

    /* Level: mid-swing, where the edge is steepest. The edge is the one
       leaving the average, so a sawtooth latches on its fast ramp.     */
    const mid = (s.min + s.max) / 2;
    const level = Math.round(clamp(mid, 0, FULL_SCALE));

    /* Edge-trigger only a real signal; a near-flat lane stays in AUTO so
       the board free-runs instead of waiting for a crossing.           */
    const live = s.vpp >= AUTOSET_FLAT;
    const plan = {
      rate: rate,
      count: frame.count,
      triggerChannel: s.channel,
      triggerLevel: level,
      edge: s.mean <= mid ? 0 : 1,
      triggerMode: live ? P.TRIG.NORMAL : P.TRIG.AUTO,
    };

    const rateNow = parseInt(ctrls.rate.value, 10);
    plan.settled = Number.isFinite(rateNow) &&
      Math.abs(rate - rateNow) <= Math.max(1, rateNow * 0.1) &&
      Math.abs(level - currentTriggerLevel()) <= 8;
    return plan;
  }

  /* Write the plan into the form, so the controls stay the single source
     of truth and the user can see (and tweak) what auto set decided.   */
  function writeAutoSet(plan) {
    if (ctrls.rate) ctrls.rate.value = String(plan.rate);
    if (ctrls.count) ctrls.count.value = String(plan.count);
    if (ctrls.trigChannel) {
      ctrls.trigChannel.value = String(plan.triggerChannel);
    }
    if (ctrls.trigLevel) ctrls.trigLevel.value = String(plan.triggerLevel);
    if (ctrls.trigEdge) {
      ctrls.trigEdge.value = plan.edge === 1 ? "falling" : "rising";
    }
    if (ctrls.trigMode) {
      ctrls.trigMode.value =
        plan.triggerMode === P.TRIG.NORMAL ? "normal" : "auto";
    }
    syncTriggerLevelOut();
  }

  /* One-shot capture: claim the frame ourselves so onCaptureData does not
     draw it a second time, and resolve null if the board never answers. */
  function grabFrame(timeoutMs) {
    const waiter = awaitFrame(P.MSG.CAPTURE_DATA, timeoutMs || AUTOSET_TIMEOUT);
    transport.writeFrame(P.captureReq(seq)).catch(() => { /* link lost */ });
    return waiter;
  }

  async function autoSet() {
    if (!connected || !transport || autoSetBusy) return;
    if (currentMode() !== P.MODE.SCOPE) {
      setStatus("auto set works on analog captures — switch to SCOPE", "warn");
      return;
    }

    autoSetBusy = true;
    const wasRunning = running;
    running = false;              /* stop the stream: this pass owns the link */
    syncButtons();
    setStatus("auto set · measuring…", "ok");

    let plan = null;
    let ack = null;
    try {
      for (let pass = 0; pass < AUTOSET_PASSES; ++pass) {
        const ev = await grabFrame();
        if (!ev) {
          setStatus("auto set · no frame came back", "err");
          return;
        }

        const d = P.parseCaptureData(ev.payload);
        if (!d || d.mode === P.MODE.LOGIC || !d.samples || !d.samples.length) {
          setStatus("auto set · the frame could not be measured", "err");
          return;
        }

        lastFrame = d;
        updateTiles(d);
        render();

        plan = autoSetPlan(d);
        if (!plan) {
          setStatus("auto set · frame too short to measure", "err");
          return;
        }
        if (plan.settled) break;

        writeAutoSet(plan);
        ack = await applyConfig(false);
        if (!ack || ack.status !== P.ACK.OK) {
          setStatus("auto set · the board refused the new settings", "err");
          return;
        }
      }
    } finally {
      autoSetBusy = false;
      if (wasRunning) {
        running = true;
        if (transport) loop();
      }
      syncButtons();
      render();
    }

    if (plan) {
      const total = ack ? ack.appliedRate : plan.rate;
      setStatus("auto set · " + fmtRate(P.channelRate(total, currentMask())) +
                " per channel · trigger level " + plan.triggerLevel + " codes" +
                (plan.triggerMode === P.TRIG.NORMAL
                  ? "" : " · free-run, the lane looks flat"), "ok");
    }
  }

  /* ---- events -------------------------------------------------------- */
  if (buttons.autoSet) buttons.autoSet.addEventListener("click", () => { autoSet(); });
  /* ---- plot pointer: drag the level rule, drag the cursors ----------- */
  /* Both gestures live on the canvas and both write back into the form,
     so the form stays the single source of truth: a drag still leaves
     through currentConfig() -> SET_CONFIG like any other change.        */

  let cursorOn = false;
  let cursorA = null;
  let cursorB = null;
  let dragging = null;      /* "level" | "a" | "b" | null */

  function canvasPoint(ev) {
    const rect = canvas.getBoundingClientRect();
    if (!rect.width || !rect.height) return null;
    return {
      x: (ev.clientX - rect.left) * (canvas.width / rect.width),
      y: (ev.clientY - rect.top) * (canvas.height / rect.height),
    };
  }

  /* The rule is one pixel; a fingertip is not. */
  function levelBand() { return Math.max(10, canvas.height * 0.04); }

  function nearLevel(y) {
    if (currentMode() !== P.MODE.SCOPE) return false;
    return Math.abs(y - ScopePlot.yAtCodes(currentTriggerLevel())) <= levelBand();
  }

  /* The trace amplitude under a cursor, and its time relative to the
     trigger. Slot 0 is the first ENABLED channel — the same one the
     measurement tiles describe.                                        */
  function cursorInfo(x) {
    if (!lastFrame || !lastFrame.samples || !lastFrame.samples.length) return null;
    const channels = Math.max(1, lastFrame.channels || 1);
    const n = Math.min(lastFrame.count || 0,
                       Math.floor(lastFrame.samples.length / channels));
    if (n < 2) return null;
    const w = canvas.width || ScopePlot.W;
    const win = lastFrame.rate > 0 ? (lastFrame.count || n) / lastFrame.rate : 0;
    const i = ScopePlot.sampleAtX(x, n, lastFrame.triggerIndex);
    return {
      t: win > 0 ? ((x - ScopePlot.TRIG_COL) / w) * win : 0,
      code: lastFrame.samples[i * channels],
    };
  }

  function syncCursorReadout() {
    if (!cursorOut.root) return;
    const one = (x) => {
      const info = x == null ? null : cursorInfo(x);
      if (!info) return "—";
      return (info.t < 0 ? "-" : "+") + fmtTime(Math.abs(info.t)) +
             " · " + info.code + " codes";
    };
    cursorOut.a.textContent = "A " + one(cursorA);
    cursorOut.b.textContent = "B " + one(cursorB);

    let delta = "Δt —";
    if (cursorA != null && cursorB != null) {
      const ia = cursorInfo(cursorA);
      const ib = cursorInfo(cursorB);
      if (ia && ib) {
        const dt = Math.abs(ib.t - ia.t);
        const inv = dt > 0 ? " · " + fmtFreq(1 / dt) : "";
        delta = "Δt " + fmtTime(dt) + " · Δc " +
                Math.abs(ib.code - ia.code) + inv;
      }
    }
    cursorOut.delta.textContent = delta;
    cursorOut.root.classList.toggle("is-live", cursorA != null && cursorB != null);
  }

  function setCursor(kind, x) {
    const w = canvas.width || ScopePlot.W;
    const cx = Math.max(0, Math.min(w, x));
    if (kind === "a") cursorA = cx; else cursorB = cx;
    syncCursorReadout();
    render();
  }

  function onCanvasDown(ev) {
    const pt = canvasPoint(ev);
    if (!pt) return;

    if (nearLevel(pt.y)) {
      dragging = "level";
      canvas.classList.add("is-level");
    } else if (cursorOn) {
      /* grab the closer cursor; with both unset, drop A first */
      const gapA = cursorA == null ? Infinity : Math.abs(cursorA - pt.x);
      const gapB = cursorB == null ? Infinity : Math.abs(cursorB - pt.x);
      dragging = gapA <= gapB ? "a" : "b";
      setCursor(dragging, pt.x);
    } else {
      return;
    }

    if (canvas.setPointerCapture) {
      try { canvas.setPointerCapture(ev.pointerId); } catch (e) { /* ignore */ }
    }
    ev.preventDefault();
  }

  function onCanvasMove(ev) {
    const pt = canvasPoint(ev);
    if (!pt) return;
    if (!dragging) {
      canvas.classList.toggle("is-level", nearLevel(pt.y));
      return;
    }
    if (dragging === "level") {
      if (ctrls.trigLevel) {
        ctrls.trigLevel.value = String(ScopePlot.codesAtY(pt.y));
      }
      syncTriggerLevelOut();
      render();
      return;
    }
    setCursor(dragging, pt.x);
  }

  function onCanvasUp(ev) {
    if (!dragging) return;
    const wasLevel = dragging === "level";
    dragging = null;
    if (canvas.releasePointerCapture) {
      try { canvas.releasePointerCapture(ev.pointerId); } catch (e) { /* ignore */ }
    }
    canvas.classList.remove("is-level");
    /* one SET_CONFIG per gesture, not one per pixel */
    if (wasLevel) applyConfigSoon();
    render();
  }

  if (buttons.cursors) {
    buttons.cursors.addEventListener("click", () => {
      cursorOn = !cursorOn;
      buttons.cursors.setAttribute("aria-pressed", cursorOn ? "true" : "false");
      buttons.cursors.classList.toggle("pill--accent", cursorOn);
      canvas.classList.toggle("is-cursors", cursorOn);
      if (cursorOn && cursorA == null) {
        const w = canvas.width || ScopePlot.W;
        cursorA = Math.round(w / 3);
        cursorB = Math.round(w * 2 / 3);
      }
      syncCursorReadout();
      render();
    });
  }

  if (canvas) {
    canvas.addEventListener("pointerdown", onCanvasDown);
    canvas.addEventListener("pointermove", onCanvasMove);
    canvas.addEventListener("pointerup", onCanvasUp);
    canvas.addEventListener("pointercancel", onCanvasUp);
    canvas.addEventListener("pointerleave", () => {
      if (!dragging) canvas.classList.remove("is-level");
    });
  }

  if (buttons.demo) buttons.demo.addEventListener("click", () => connect("demo"));
  if (buttons.connect) buttons.connect.addEventListener("click", () => connect("serial"));
  if (buttons.apply) {
    buttons.apply.addEventListener("click", () => applyConfig(true));
  }

  if (buttons.run) buttons.run.addEventListener("click", () => {
    if (!connected || running) return;
    running = true;
    syncButtons();
    setStatus("capturing…", "ok");
    render();
    loop();
  });

  if (buttons.stop) buttons.stop.addEventListener("click", () => {
    if (!connected || !running) return;
    running = false;
    transport.writeFrame(P.stop(seq)).catch(() => { /* ignore */ });
    setStatus("stopped");
    syncButtons();
    render();
  });

  if (buttons.disconnect) {
    buttons.disconnect.addEventListener("click", () => { disconnect(); });
  }

  if (buttons.csv) buttons.csv.addEventListener("click", downloadCSV);
  if (buttons.copy) buttons.copy.addEventListener("click", copyCodes);

  if (buttons.theme) {
    buttons.theme.addEventListener("click", () => {
      const next = currentTheme() === "dark" ? "light" : "dark";
      applyTheme(next, true);
      /* the palette changed: re-derive the backing store, then repaint */
      fittedW = 0;
      render();
    });
  }

  function onModeChange() {
    /* each mode owns its own timebase and depth — seed them on switch */
    syncControls(true);
    clearFrame();
    render();
    applyConfigSoon();
  }

  if (ctrls.modeScope) ctrls.modeScope.addEventListener("change", () => {
    if (!ctrls.modeScope.checked) return;
    onModeChange();
  });

  if (ctrls.modeLogic) ctrls.modeLogic.addEventListener("change", () => {
    if (!ctrls.modeLogic.checked) return;
    onModeChange();
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

  /* the slider redraws its readout on every pixel, but only writes to the
     board when the gesture ends — the 150 ms debounce then collapses
     repeated changes into one SET_CONFIG                              */
  if (ctrls.trigLevel) {
    ctrls.trigLevel.addEventListener("input", syncTriggerLevelOut);
  }

  [ctrls.trigMode, ctrls.trigEdge, ctrls.trigChannel,
   ctrls.rate, ctrls.count, ctrls.trigLevel].forEach((node) => {
    if (!node) return;
    node.addEventListener("change", () => applyConfigSoon());
  });

  window.addEventListener("resize", () => {
    fittedW = 0;
    render();
  });

  if (typeof ResizeObserver !== "undefined") {
    /* the card can change width without a window resize (fonts, zoom) */
    const ro = new ResizeObserver(() => {
      const w = canvas.clientWidth;
      if (w && w !== fittedW) {
        fittedW = 0;
        render();
      }
    });
    if (canvas) ro.observe(canvas);
  }

  window.addEventListener("load", () => {
    applyTheme(storedTheme(), false);
    syncTriggerLevelOut();

    if (ctrls.rate) ctrls.rate.value = String(L.DEFAULT_RATE_HZ);
    if (ctrls.count) ctrls.count.value = String(L.SAMPLE_COUNT);

    syncControls();
    clearFrame();
    render();
    setStatus("not connected");
    if (buttons.apply) buttons.apply.disabled = true;
    syncButtons();

    /* Home carries the plot card but none of the controls: run the demo so
       the landing page shows a live trace instead of an empty grid       */
    if (canvas && !ctrls.rate && !connected) {
      connect("demo").then(() => {
        if (!connected || running) return;
        running = true;
        syncButtons();
        render();
        loop();
      });
    }
  });
})();
