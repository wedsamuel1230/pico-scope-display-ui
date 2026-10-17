/**
 * plot.js — canvas waveform rendering (mirror of src/display/scope_view)
 *
 * Portrait 240x320 instrument face:
 *   header (SCOPE / CH1 / RUN|STOP), 8x6 graticule, scale row, state row.
 * Trace uses per-column min/max downsampling, bottom = code 0.
 * The canvas is the only measurement-free face: readings live in the DOM.
 */
"use strict";

const ScopePlot = (() => {
  const W = 240, H = 320;
  const HEADER_H = 18;
  const WAVE_Y = 18, WAVE_H = 242;
  const ROW_H = 40;
  const SCALE_Y = 262, SCALE_H = 16;
  const STATE_Y = 280, STATE_H = 18;

  const C = {
    bg: "#000",
    grid: "#26323f",
    text: "#e6edf3",
    dim: "#7d8b99",
    amber: "#ffb000",
    green: "#38d95b",
    err: "#ff5252",
    trace: "#c9e94f",
  };

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
    for (let d = 0; d <= 8; d++) {
      const x = d * (W / 8) - 0.5;
      if (x >= W) break;
      ctx.moveTo(x, WAVE_Y); ctx.lineTo(x, WAVE_Y + WAVE_H);
    }
    for (let d = 0; d <= 6; d++) {
      const y = WAVE_Y + d * ROW_H - 0.5;
      if (y > WAVE_Y + WAVE_H) break;
      ctx.moveTo(0, y); ctx.lineTo(W, y);
    }
    ctx.stroke();
  }

  const TRIG_COL = Math.round(W / 8); /* trigger sits ~1/8 from left      */

  function drawTrace(ctx, samples, triggerIndex) {
    if (!samples || samples.length === 0) return;
    const n = samples.length;
    /* Rotate the column mapping so the trigger sample always lands on a
       fixed column — otherwise every capture starts at a different phase
       and the trace shimmers. Pre-trigger samples fill the lead columns.  */
    const hasTrig = triggerIndex != null && triggerIndex !== 0xFFFF;
    const delta = hasTrig
      ? Math.floor(triggerIndex * W / n) - TRIG_COL
      : 0;
    const colMin = new Float64Array(W).fill(Infinity);
    const colMax = new Float64Array(W).fill(-Infinity);
    for (let i = 0; i < n; i++) {
      const c = Math.floor(i * W / n) - delta;
      if (c < 0 || c >= W) continue;
      const v = samples[i];
      if (v < colMin[c]) colMin[c] = v;
      if (v > colMax[c]) colMax[c] = v;
    }
    ctx.strokeStyle = C.trace;
    ctx.lineWidth = 1;
    const yOf = (v) => WAVE_Y + WAVE_H - 1 - v * (WAVE_H - 1) / 4095;

    /* Vertical min/max connectors — preserve glitches within a column.    */
    ctx.beginPath();
    for (let c = 0; c < W; c++) {
      if (!isFinite(colMin[c])) continue;
      ctx.moveTo(c + 0.5, yOf(colMin[c]));
      ctx.lineTo(c + 0.5, yOf(colMax[c]));
    }
    ctx.stroke();

    /* Upper and lower envelopes joined across columns, so the trace reads
       as one continuous waveform instead of isolated vertical bars.      */
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
    /* trigger marker: amber tick at the (now fixed) trigger column      */
    if (hasTrig) {
      ctx.fillStyle = C.amber;
      ctx.fillRect(TRIG_COL, WAVE_Y, 1, 6);
    }
  }

  function drawHeader(ctx, running) {
    ctx.fillStyle = C.bg;
    ctx.fillRect(0, 0, W, HEADER_H);
    ctx.font = "10px monospace";
    ctx.textBaseline = "middle";
    const cy = HEADER_H / 2;
    ctx.fillStyle = C.text;
    ctx.fillText("SCOPE", 4, cy);
    ctx.fillStyle = C.amber;
    ctx.fillText("CH1", 96, cy);
    ctx.fillStyle = running ? C.green : C.amber;
    ctx.fillText(running ? "RUN" : "STOP", 200, cy);
  }

  function fmtTime(sec) {
    if (sec >= 1) return sec.toFixed(2) + " s";
    if (sec >= 1e-3) return (sec * 1e3).toFixed(2) + " ms";
    if (sec >= 1e-6) return (sec * 1e6).toFixed(1) + " µs";
    return (sec * 1e9).toFixed(0) + " ns";
  }

  function drawScaleRow(ctx, rate, count, flags) {
    ctx.fillStyle = C.bg;
    ctx.fillRect(0, SCALE_Y, W, SCALE_H);
    ctx.font = "9px monospace";
    ctx.textBaseline = "middle";
    const cy = SCALE_Y + SCALE_H / 2;
    /* 4095 codes over 6 vertical divisions. Only name volts once the ADC
       path is calibrated — with no AFE we must show codes.              */
    const raw = !!((flags || 0) & ScopeProto.FLAGS.NOT_CALIB);
    ctx.fillStyle = C.dim;
    ctx.fillText(raw ? "C/DIV" : "V/DIV", 4, cy);
    ctx.fillStyle = C.text;
    ctx.fillText(raw ? "683" : "0.55 V", 36, cy);
    ctx.fillStyle = C.dim;
    ctx.fillText("T/DIV", 120, cy);
    ctx.fillStyle = C.text;
    const tdiv = (rate > 0 && count > 0) ? (count / rate) / 8 : 0;
    ctx.fillText(tdiv > 0 ? fmtTime(tdiv) : "--", 152, cy);
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

  /* opts: { samples, rate, count, running, flags, triggerIndex, message } */
  function drawAll(ctx, opts) {
    const o = opts || {};
    ctx.fillStyle = C.bg;
    ctx.fillRect(0, 0, W, H);
    drawHeader(ctx, !!o.running);
    drawGrid(ctx);
    if (o.samples && o.samples.length) {
      drawTrace(ctx, o.samples, o.triggerIndex);
    } else {
      drawEmpty(ctx, o.message || "No signal — press Demo");
    }
    drawScaleRow(ctx, o.rate || 0, o.count || 0, o.flags || 0);
    drawStateRow(ctx, o.flags || 0, !!o.running);
  }

  function drawTraceOnly(ctx, opts) {
    /* cheap per-frame path: repaint wave area, grid, then trace        */
    clearWave(ctx);
    drawGrid(ctx);
    const o = opts || {};
    if (o.samples && o.samples.length) {
      drawTrace(ctx, o.samples, o.triggerIndex);
    } else {
      drawEmpty(ctx, o.message || "No signal — press Demo");
    }
  }

  return {
    W, H,
    fitCanvas, drawAll, drawTraceOnly,
    drawTrace, drawGrid, clearWave, drawHeader,
    drawScaleRow, drawStateRow,
  };
})();
