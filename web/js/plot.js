/**
 * plot.js — canvas rendering (mirror of src/display/scope_view)
 *
 * Portrait 240x320 instrument face, two modes:
 *   SCOPE — header, 8x6 graticule, up to 3 overlaid ADC traces
 *           (CH1 amber / CH2 cyan / CH3 magenta), scale + state rows.
 *   LOGIC — header, 8 stacked digital lanes D0..D7, scale + state rows.
 * Traces use per-column min/max downsampling, bottom = code 0.
 * The canvas is the only measurement-free face: readings live in the DOM.
 */
"use strict";

const ScopePlot = (() => {
  /* Geometry is mutable so one renderer serves both faces:
       device — the real 240x320 panel, full on-canvas chrome
       wide   — the desktop plot card, where the chrome lives in HTML  */
  const LAYOUTS = {
    device: {
      W: 240, H: 320, HEADER_H: 18,
      WAVE_Y: 18, WAVE_H: 242, ROW_H: 40,
      SCALE_Y: 262, SCALE_H: 16, STATE_Y: 280, STATE_H: 18,
      COLS: 8, ROWS: 6, chrome: true,
    },
    wide: {
      W: 1144, H: 320, HEADER_H: 0,
      WAVE_Y: 0, WAVE_H: 320, ROW_H: 80,
      SCALE_Y: 320, SCALE_H: 0, STATE_Y: 320, STATE_H: 0,
      COLS: 12, ROWS: 4, chrome: false,
    },
  };

  let W = 240, H = 320;
  let HEADER_H = 18;
  let WAVE_Y = 18, WAVE_H = 242;
  let ROW_H = 40;
  let SCALE_Y = 262, SCALE_H = 16;
  let STATE_Y = 280, STATE_H = 18;
  let GRID_COLS = 8, GRID_ROWS = 6;
  let CHROME = true;
  let TRIG_COL = Math.round(W / 8);

  const LANES = 8;
  let LANE_H = Math.floor(WAVE_H / LANES);     /* 30 device, 40 wide */
  const LANE_LABEL_W = 14;

  function setLayout(name) {
    const L = LAYOUTS[name === "wide" ? "wide" : "device"];
    W = L.W; H = L.H; HEADER_H = L.HEADER_H;
    WAVE_Y = L.WAVE_Y; WAVE_H = L.WAVE_H; ROW_H = L.ROW_H;
    SCALE_Y = L.SCALE_Y; SCALE_H = L.SCALE_H;
    STATE_Y = L.STATE_Y; STATE_H = L.STATE_H;
    GRID_COLS = L.COLS; GRID_ROWS = L.ROWS; CHROME = L.chrome;
    LANE_H = Math.floor(WAVE_H / LANES);
    TRIG_COL = Math.round(W / 8);
    return { W: W, H: H };
  }

  const C = {
    bg: "#000",
    grid: "#26323f",
    text: "#e6edf3",
    dim: "#7d8b99",
    amber: "#ffb000",
    green: "#38d95b",
    err: "#ff5252",
    trace: "#c9e94f",
    cursor: "#a78bfa",
  };

  /* channel colours, matching COLOR_CH1/2/3 in src/display/st7789.h.
     CH1 amber 0xFC00, CH2 cyan 0x07FF, CH3 magenta 0xF81F.            */
  const CH_COLOR = ["#fc8c00", "#00d4ff", "#ff4fd8"];
  const CH_NAME = ["CH1", "CH2", "CH3"];

  /* digital lane colours (web only; the LCD uses one colour for logic)  */
  const LANE_COLOR = ["#ffd23f", "#4fd1ff", "#7ee787", "#ff9f68",
                      "#c792ea", "#f78c6c", "#89ddff", "#ffe082"];

  /* theme palettes: index 0 = dark (matches the LCD face), 1 = light.
     app.js calls ScopePlot.setTheme() from its theme toggle, then
     re-renders. Colours are mutated in place so the closures above
     keep pointing at the same objects.                                */
  const THEMES = [
    {
      bg: "#000000", grid: "#26323f", text: "#e6edf3", dim: "#7d8b99",
      amber: "#ffb000", green: "#38d95b", err: "#ff5252", trace: "#c9e94f",
      cursor: "#a78bfa",
      ch: ["#fc8c00", "#00d4ff", "#ff4fd8"],
      lane: ["#ffd23f", "#4fd1ff", "#7ee787", "#ff9f68",
             "#c792ea", "#f78c6c", "#89ddff", "#ffe082"],
    },
    {
      bg: "#f8f9fb", grid: "#d9e0ea", text: "#1c2128", dim: "#6b7380",
      amber: "#d98c0d", green: "#21a13b", err: "#d1242f", trace: "#3f6212",
      cursor: "#6d28d9",
      ch: ["#c2410c", "#0e7490", "#be185d"],
      lane: ["#b45309", "#0369a1", "#15803d", "#c2410c",
             "#7e22ce", "#be123c", "#0e7490", "#a16207"],
    },
  ];

  function setTheme(name) {
    const t = THEMES[name === "light" ? 1 : 0];
    C.bg = t.bg;
    C.grid = t.grid;
    C.text = t.text;
    C.dim = t.dim;
    C.amber = t.amber;
    C.green = t.green;
    C.err = t.err;
    C.trace = t.trace;
    C.cursor = t.cursor;
    for (let i = 0; i < CH_COLOR.length; i += 1) CH_COLOR[i] = t.ch[i];
    for (let i = 0; i < LANE_COLOR.length; i += 1) LANE_COLOR[i] = t.lane[i];
    return t;
  }

  function fitCanvas(canvas) {
    /* keep the 240:320 ratio exactly; render at an integer device scale
       so 9-10px text lands on whole pixels without smearing.           */
    const dpr = window.devicePixelRatio || 1;
    const cssW = canvas.clientWidth || W;
    const scale = Math.max(1, Math.round(cssW * dpr / W));
    canvas.width = W * scale;
    canvas.height = H * scale;
    const ctx = canvas.getContext("2d");
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    return ctx;
  }

  function clearWave(ctx) {
    ctx.fillStyle = C.bg;
    ctx.fillRect(0, WAVE_Y, W, WAVE_H);
  }

  function drawGrid(ctx) {
    ctx.strokeStyle = C.grid;
    ctx.lineWidth = 0.75;
    ctx.beginPath();
    for (let d = 0; d <= GRID_COLS; d++) {
      const x = d * (W / GRID_COLS) - 0.5;
      if (x >= W) break;
      ctx.moveTo(x, WAVE_Y); ctx.lineTo(x, WAVE_Y + WAVE_H);
    }
    for (let d = 0; d <= GRID_ROWS; d++) {
      const y = WAVE_Y + d * ROW_H - 0.5;
      if (y > WAVE_Y + WAVE_H) break;
      ctx.moveTo(0, y); ctx.lineTo(W, y);
    }
    ctx.stroke();
  }

  /* ---- SCOPE: overlaid analog traces ------------------------------- */
  /* TRIG_COL is recomputed by setLayout(); the trigger sits ~1/8 in.   */

  function drawTrace(ctx, samples, channels, slot, triggerIndex, color) {
    if (!samples || channels < 1) return;
    const n = Math.floor(samples.length / channels);
    if (n < 2) return;

    /* Rotate the column mapping so the trigger sample always lands on a
       fixed column — otherwise every capture starts at a different phase
       and the trace shimmers. Pre-trigger samples fill the lead columns. */
    const hasTrig = triggerIndex != null && triggerIndex !== 0xFFFF;
    const delta = hasTrig
      ? Math.floor(triggerIndex * W / n) - TRIG_COL
      : 0;

    const colMin = new Float64Array(W).fill(Infinity);
    const colMax = new Float64Array(W).fill(-Infinity);
    for (let i = 0; i < n; i++) {
      const c = Math.floor(i * W / n) - delta;
      if (c < 0 || c >= W) continue;
      const v = samples[i * channels + slot];
      if (v < colMin[c]) colMin[c] = v;
      if (v > colMax[c]) colMax[c] = v;
    }

    const yOf = (v) => WAVE_Y + WAVE_H - 1 - v * (WAVE_H - 1) / 4095;

    ctx.strokeStyle = color;
    ctx.lineWidth = 1;

    /* vertical min/max connectors keep glitches inside a column        */
    ctx.beginPath();
    for (let c = 0; c < W; c++) {
      if (!isFinite(colMin[c])) continue;
      ctx.moveTo(c + 0.5, yOf(colMin[c]));
      ctx.lineTo(c + 0.5, yOf(colMax[c]));
    }
    ctx.stroke();

    /* upper and lower envelopes joined across columns, so each trace
       reads as one continuous waveform rather than isolated bars        */
    for (const pick of [colMax, colMin]) {
      ctx.beginPath();
      let started = false;
      for (let c = 0; c < W; c++) {
        if (!isFinite(pick[c])) continue;
        if (started) ctx.lineTo(c + 0.5, yOf(pick[c]));
        else { ctx.moveTo(c + 0.5, yOf(pick[c])); started = true; }
      }
      ctx.stroke();
    }
  }

  function drawTrigMarker(ctx) {
    ctx.fillStyle = C.amber;
    ctx.fillRect(TRIG_COL, WAVE_Y, 1, 6);
  }

  /* Dashed threshold rule for the armed trigger level, in raw ADC codes.
     Auto set writes that level, so drawing it is what makes the button
     legible: you can see the trace cross the exact line it triggered on. */
  function drawLevelLine(ctx, codes) {
    const level = Math.max(0, Math.min(4095, codes));
    const y = Math.round(WAVE_Y + WAVE_H - 1 - level * (WAVE_H - 1) / 4095) + 0.5;
    ctx.save();
    ctx.strokeStyle = C.amber;
    ctx.globalAlpha = 0.8;
    ctx.lineWidth = 1;
    ctx.setLineDash([5, 5]);
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(W, y);
    ctx.stroke();
    ctx.restore();
  }

  /* ---- LOGIC: stacked digital lanes -------------------------------- */
  /* `samples[i] & 0xFF` holds D0 in bit 0 .. D7 in bit 7 — the same
     masking rule the firmware applies before display or transmit.      */
  /* ---- pointer geometry ---------------------------------------------- */
  /* These three exist so the mouse can talk to the trace. They invert and
     re-use drawTrace's own mappings, which is why dragging the level rule
     lands on the same pixel the trace crosses: there is only one formula. */

  /* canvas y -> raw ADC code (inverse of drawTrace's yOf)               */
  function codesAtY(y) {
    if (WAVE_H <= 1) return 0;
    const v = (WAVE_Y + WAVE_H - 1 - y) * 4095 / (WAVE_H - 1);
    return Math.max(0, Math.min(4095, Math.round(v)));
  }

  /* raw ADC code -> canvas y (forward, so app.js can hit-test the rule) */
  function yAtCodes(codes) {
    const v = Math.max(0, Math.min(4095, codes));
    return WAVE_Y + WAVE_H - 1 - v * (WAVE_H - 1) / 4095;
  }

  /* canvas x -> sample index, inverting c = floor(i * W / n) - delta.
     This is what lets a time cursor name the amplitude under it.        */
  function sampleAtX(x, n, triggerIndex) {
    if (n < 2) return 0;
    const hasTrig = triggerIndex != null && triggerIndex !== 0xFFFF;
    const delta = hasTrig ? Math.floor(triggerIndex * W / n) - TRIG_COL : 0;
    const i = Math.round((x + delta) * n / W);
    return Math.max(0, Math.min(n - 1, i));
  }

  /* A is tabbed at the top of the wave area, B at the bottom, so the two
     cursors are told apart without reading the labels off the readout.  */
  function drawCursors(ctx, xs) {
    if (!xs) return;
    ctx.save();
    ctx.lineWidth = 1;
    for (let k = 0; k < xs.length; k += 1) {
      const x = xs[k];
      if (x == null) continue;
      const px = Math.round(x);
      ctx.strokeStyle = C.cursor;
      ctx.globalAlpha = 0.55;
      ctx.setLineDash([3, 4]);
      ctx.beginPath();
      ctx.moveTo(px + 0.5, WAVE_Y);
      ctx.lineTo(px + 0.5, WAVE_Y + WAVE_H);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.globalAlpha = 1;
      ctx.fillStyle = C.cursor;
      ctx.fillRect(px, k === 0 ? WAVE_Y : WAVE_Y + WAVE_H - 6, 2, 6);
    }
    ctx.restore();
  }

  function drawLogic(ctx, samples, count, triggerIndex, running) {
    if (!samples || samples.length === 0) return;
    const n = Math.min(count || samples.length, samples.length);
    if (n < 2) return;

    const hasTrig = triggerIndex != null && triggerIndex !== 0xFFFF;
    const delta = hasTrig
      ? Math.floor(triggerIndex * W / n) - TRIG_COL
      : 0;

    ctx.font = "8px monospace";
    ctx.textBaseline = "middle";

    for (let lane = 0; lane < LANES; lane++) {
      const yTop = WAVE_Y + lane * LANE_H;

      /* lane separator + label */
      ctx.strokeStyle = C.grid;
      ctx.lineWidth = 0.75;
      ctx.beginPath();
      ctx.moveTo(0, yTop + LANE_H - 0.5);
      ctx.lineTo(W, yTop + LANE_H - 0.5);
      ctx.stroke();

      ctx.fillStyle = C.dim;
      ctx.fillText("D" + lane, 1, yTop + LANE_H / 2);

      /* build the level per displayed column                       */
      const mask = 1 << lane;
      const yHi = yTop + 5;
      const yLo = yTop + LANE_H - 6;

      ctx.strokeStyle = LANE_COLOR[lane];
      ctx.lineWidth = 1;
      ctx.beginPath();

      let seen = false;
      let prevY = yLo;
      let prevHigh = false;
      for (let c = 0; c < W; c++) {
        const i = Math.round((c + delta) * n / W);
        const idx = Math.max(0, Math.min(n - 1, i));
        const high = (samples[idx] & mask) !== 0;
        const y = high ? yHi : yLo;

        if (!seen) {
          ctx.moveTo(c + 0.5, y);
          seen = true;
        } else {
          if (high !== prevHigh) {
            /* vertical edge at the transition column */
            ctx.lineTo(c + 0.5, prevY);
            ctx.lineTo(c + 0.5, y);
          } else {
            ctx.lineTo(c + 0.5, y);
          }
        }
        prevY = y;
        prevHigh = high;
      }
      ctx.stroke();
    }

    if (hasTrig) {
      ctx.fillStyle = C.amber;
      ctx.fillRect(TRIG_COL, WAVE_Y, 1, WAVE_H);
    }
  }

  /* ---- chrome ------------------------------------------------------ */
  function drawHeader(ctx, mode, channelMask, running) {
    ctx.fillStyle = C.bg;
    ctx.fillRect(0, 0, W, HEADER_H);
    ctx.font = "10px monospace";
    ctx.textBaseline = "middle";
    const cy = HEADER_H / 2;

    ctx.fillStyle = C.text;
    if (mode === ScopeProto.MODE.LOGIC) {
      ctx.fillText("LOGIC", 4, cy);
      let bits = 0;
      for (let i = 0; i < 8; i++) if (channelMask & (1 << i)) bits++;
      ctx.fillStyle = C.dim;
      ctx.fillText(bits + "CH", 60, cy);
    } else {
      ctx.fillText("SCOPE", 4, cy);
      let x = 60;
      for (let ch = 0; ch < CH_NAME.length; ch++) {
        if (!(channelMask & (1 << ch))) continue;
        ctx.fillStyle = CH_COLOR[ch];
        ctx.fillText(CH_NAME[ch], x, cy);
        x += 34;
      }
    }

    ctx.fillStyle = running ? C.green : C.amber;
    ctx.fillText(running ? "RUN" : "STOP", 196, cy);
  }

  function fmtTime(sec) {
    if (sec >= 1) return sec.toFixed(2) + " s";
    if (sec >= 1e-3) return (sec * 1e3).toFixed(2) + " ms";
    if (sec >= 1e-6) return (sec * 1e6).toFixed(1) + " µs";
    return (sec * 1e9).toFixed(0) + " ns";
  }

  function fmtRate(r) {
    if (!r) return "--";
    if (r >= 1e6) return (r / 1e6).toFixed(r % 1e6 ? 2 : 0) + " MS/s";
    if (r >= 1e3) return (r / 1e3).toFixed(r % 1e3 ? 1 : 0) + " kS/s";
    return r + " S/s";
  }

  function drawScaleRow(ctx, mode, rate, count, channels) {
    ctx.fillStyle = C.bg;
    ctx.fillRect(0, SCALE_Y, W, SCALE_H);
    ctx.font = "9px monospace";
    ctx.textBaseline = "middle";
    const cy = SCALE_Y + SCALE_H / 2;

    /* 4095 codes over 6 vertical divisions. Only name volts once the ADC
       path is calibrated — with no AFE we must show codes.             */
    ctx.fillStyle = C.dim;
    ctx.fillText(mode === ScopeProto.MODE.LOGIC ? "RATE" : "C/DIV RAW", 4, cy);
    ctx.fillStyle = C.text;
    ctx.fillText(mode === ScopeProto.MODE.LOGIC ? fmtRate(rate) : "683",
                 mode === ScopeProto.MODE.LOGIC ? 40 : 66, cy);

    ctx.fillStyle = C.dim;
    ctx.fillText("T/DIV", 130, cy);
    ctx.fillStyle = C.text;
    const n = mode === ScopeProto.MODE.LOGIC ? count : count;
    const tdiv = (rate > 0 && n > 0) ? (n / rate) / 8 : 0;
    ctx.fillText(tdiv > 0 ? fmtTime(tdiv) : "--", 166, cy);
  }

  function drawStateRow(ctx, flags, running) {
    ctx.fillStyle = C.bg;
    ctx.fillRect(0, STATE_Y, W, STATE_H);
    ctx.font = "9px monospace";
    ctx.textBaseline = "middle";
    const cy = STATE_Y + STATE_H / 2;
    const parts = [];
    if (flags & ScopeProto.FLAGS.NO_TRIGGER) parts.push(["NO TRIG", C.err]);
    if (flags & ScopeProto.FLAGS.OVERFLOW) parts.push(["FIFO OVF", C.err]);
    if (flags & ScopeProto.FLAGS.TRUNCATED) parts.push(["TRUNC", C.amber]);
    if (flags & ScopeProto.FLAGS.NOT_CALIB) parts.push(["RAW", C.dim]);
    if (!parts.length) parts.push([running ? "STABLE" : "IDLE", C.green]);
    let x = 4;
    for (const [label, col] of parts) {
      ctx.fillStyle = col;
      ctx.fillText(label, x, cy);
      x += ctx.measureText(label).width + 8;
    }
  }

  function drawEmpty(ctx, msg) {
    ctx.fillStyle = C.dim;
    ctx.font = "10px monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(msg, W / 2, WAVE_Y + WAVE_H / 2);
    ctx.textAlign = "left";
  }

  /* Slots in the interleaved SCOPE buffer follow the channel_mask bit
     order, exactly like adc_slot_for_channel() in the firmware.         */
  function slotsForMask(mask) {
    const slots = [];
    for (let ch = 0; ch < CH_NAME.length; ch++) {
      if (mask & (1 << ch)) slots.push(ch);
    }
    return slots;
  }

  /* opts: { mode, channelMask, samples, rate, count, channels, running,
            flags, triggerIndex, triggerLevel, message } */
  function drawAll(ctx, opts) {
    const o = opts || {};
    const mode = o.mode === ScopeProto.MODE.LOGIC
      ? ScopeProto.MODE.LOGIC : ScopeProto.MODE.SCOPE;
    const mask = o.channelMask === undefined
      ? (mode === ScopeProto.MODE.LOGIC ? 0xFF : 0x01) : o.channelMask;

    ctx.fillStyle = C.bg;
    ctx.fillRect(0, 0, W, H);
    if (CHROME) drawHeader(ctx, mode, mask, !!o.running);

    if (mode === ScopeProto.MODE.LOGIC) {
      clearWave(ctx);
      drawGrid(ctx);
      if (o.samples && o.samples.length) {
        drawLogic(ctx, o.samples, o.count, o.triggerIndex, !!o.running);
      } else {
        drawEmpty(ctx, o.message || "No signal — press Demo");
      }
    } else {
      drawGrid(ctx);
      if (o.samples && o.samples.length && o.channels >= 1) {
        const slots = slotsForMask(mask);
        for (const ch of slots) {
          drawTrace(ctx, o.samples, o.channels, ch, o.triggerIndex,
                    CH_COLOR[ch]);
        }
        if (o.triggerIndex != null && o.triggerIndex !== 0xFFFF) {
          drawTrigMarker(ctx);
        }
        if (o.triggerLevel != null) drawLevelLine(ctx, o.triggerLevel);
      } else {
        drawEmpty(ctx, o.message || "No signal — press Demo");
      }
    }

    if (o.cursors) drawCursors(ctx, o.cursors);

    if (CHROME) {
      drawScaleRow(ctx, mode, o.rate || 0, o.count || 0, o.channels || 1);
      drawStateRow(ctx, o.flags || 0, !!o.running);
    }
  }

  function drawTraceOnly(ctx, opts) {
    /* cheap per-frame path: repaint wave area, grid, then traces        */
    clearWave(ctx);
    drawGrid(ctx);
    const o = opts || {};
    if (o.mode === ScopeProto.MODE.LOGIC) {
      if (o.samples && o.samples.length) {
        drawLogic(ctx, o.samples, o.count, o.triggerIndex, !!o.running);
      } else {
        drawEmpty(ctx, o.message || "No signal — press Demo");
      }
      return;
    }
    if (o.samples && o.samples.length && o.channels >= 1) {
      const mask = o.channelMask === undefined ? 0x01 : o.channelMask;
      for (const ch of slotsForMask(mask)) {
        drawTrace(ctx, o.samples, o.channels, ch, o.triggerIndex,
                  CH_COLOR[ch]);
      }
      if (o.triggerIndex != null && o.triggerIndex !== 0xFFFF) {
        drawTrigMarker(ctx);
      }
      if (o.triggerLevel != null) drawLevelLine(ctx, o.triggerLevel);
    } else {
      drawEmpty(ctx, o.message || "No signal — press Demo");
    }
  }

  return {
    get W() { return W; },
    get H() { return H; },
    CH_COLOR, CH_NAME, LANE_COLOR, LANES,
    setTheme, setLayout,
    fitCanvas, drawAll, drawTraceOnly,
    drawTrace, drawGrid, clearWave, drawHeader, drawLogic,
    drawScaleRow, drawStateRow, drawLevelLine, slotsForMask,
    codesAtY, yAtCodes, sampleAtX, drawCursors,
    get TRIG_COL() { return TRIG_COL; },
  };
})();
