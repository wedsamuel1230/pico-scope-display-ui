/**
 * plot.js — canvas waveform rendering (mirror of src/display/scope_view)
 *
 * Ring: header (SCOPE/CH1/RUN), 8x6 graticule, scale row, tiles row.
 * Trace uses per-column min/max downsampling, bottom = code 0.
 */
"use strict";

const ScopePlot = (() => {
  const W = 240, H = 320;
  const HEADER_H = 18;
  const WAVE_Y = 18, WAVE_H = 242;
  const SCALE_Y = 261, TILES_Y = 279, SOFTKEY_Y = 300;

  function fitCanvas(canvas) {
    /* devicePixelRatio-aware blur-free render                          */
    const dpr = window.devicePixelRatio || 1;
    const cssW = canvas.clientWidth;
    const cssH = Math.round(cssW * H / W);
    canvas.width = Math.round(cssW * dpr);
    canvas.height = Math.round(cssH * dpr);
    const ctx = canvas.getContext("2d");
    ctx.setTransform(dpr * cssW / W, 0, 0, dpr * cssW / W, 0, 0);
    return ctx;
  }

  function clearWave(ctx) {
    ctx.fillStyle = "#000";
    ctx.fillRect(0, WAVE_Y, W, WAVE_H);
  }

  function drawGrid(ctx) {
    ctx.strokeStyle = "#262e3a";
    ctx.lineWidth = 0.75;
    ctx.beginPath();
    for (let d = 0; d <= 8; d++) {
      const x = d * 30 - 0.5;
      if (x >= W) break;
      ctx.moveTo(x, WAVE_Y); ctx.lineTo(x, WAVE_Y + WAVE_H);
    }
    for (let d = 0; d <= 6; d++) {
      const y = WAVE_Y + d * 40 - 0.5;
      if (y > WAVE_Y + WAVE_H) break;
      ctx.moveTo(0, y); ctx.lineTo(W, y);
    }
    ctx.stroke();
  }

  function drawTrace(ctx, samples) {
    clearWave(ctx);
    if (!samples || samples.length === 0) return;
    const colMin = new Float64Array(W).fill(Infinity);
    const colMax = new Float64Array(W).fill(-Infinity);
    for (let i = 0; i < samples.length; i++) {
      const c = Math.min(W - 1, Math.floor(i * W / samples.length));
      const v = samples[i];
      if (v < colMin[c]) colMin[c] = v;
      if (v > colMax[c]) colMax[c] = v;
    }
    ctx.strokeStyle = "#c9e94f";
    ctx.lineWidth = 1;
    for (let c = 0; c < W; c++) {
      if (!isFinite(colMin[c])) continue;
      const yHi = WAVE_Y + WAVE_H - 1 - colMax[c] * (WAVE_H - 1) / 4095;
      const yLo = WAVE_Y + WAVE_H - 1 - colMin[c] * (WAVE_H - 1) / 4095;
      ctx.beginPath();
      ctx.moveTo(c + 0.5, yLo);
      ctx.lineTo(c + 0.5, yHi);
      ctx.stroke();
    }
  }

  function drawHeader(ctx, running) {
    ctx.fillStyle = "#000",
    ctx.fillRect(0, 0, W, HEADER_H);
    ctx.font = "10px monospace";
    ctx.textBaseline = "middle";
    ctx.fillStyle = "#e6edf3";
    ctx.fillText("SCOPE", 4, HEADER_H / 2);
    ctx.fillStyle = "#ffb000";
    ctx.fillText("CH1", 96, HEADER_H / 2);
    if (running) {
      ctx.fillStyle = "#38d95b";
      ctx.fillText("RUN", 204, HEADER_H / 2);
    }
  }

  function drawScaleRow(ctx, rate) {
    ctx.fillStyle = "#000";
    ctx.fillRect(0, SCALE_Y, W, 17);
    ctx.font = "9px monospace";
    ctx.fillStyle = "#7d8b99";
    ctx.fillText("V/DIV  RAW", 4, SCALE_Y + 9);
    ctx.fillText("US/DIV", 160, SCALE_Y + 9);
    if (rate) {
      ctx.fillStyle = "#e6edf3";
      ctx.fillText(String(Math.round(rate / 1000)) + "K", 214, SCALE_Y + 9);
    }
  }

  function drawTiles(ctx, m) {
    ctx.fillStyle = "#000";
    ctx.fillRect(0, TILES_Y, W, 19);
    ctx.font = "9px monospace";
    ctx.fillStyle = "#e6edf3";
    const fmt = (v, unit) => (v == null ? "--" : v + unit);
    ctx.fillText("FREQ " + fmt(m.freqText, ""), 4, TILES_Y + 10);
    ctx.fillText("VPP " + fmt(m.vppText, ""), 120, TILES_Y + 10);
  }

  function drawSoftKeys(ctx) {
    ctx.fillStyle = "#000";
    ctx.fillRect(0, SOFTKEY_Y, W, 20);
    ctx.font = "9px monospace";
    ctx.fillStyle = "#7d8b99";
    ctx.fillText("AUTO", 8, SOFTKEY_Y + 10);
    ctx.fillText("TRIG", 104, SOFTKEY_Y + 10);
    ctx.fillText("MENU", 192, SOFTKEY_Y + 10);
    /* separator line                                                  */
    ctx.fillStyle = "#262e3a";
    ctx.fillRect(0, SOFTKEY_Y - 1, W, 1);
  }

  function drawAll(ctx, samples, rate, running, measurements) {
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, W, H);
    drawHeader(ctx, running);
    drawGrid(ctx);
    if (samples && samples.length) drawTrace(ctx, samples);
    drawGrid(ctx); /* keep grid visible under/over trace staging       */
    drawScaleRow(ctx, rate);
    drawTiles(ctx, measurements || {});
    drawSoftKeys(ctx);
  }

  return { fitCanvas, drawAll, drawTrace, drawGrid, clearWave };
})();
