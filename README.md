# Pico Scope Display UI

Single-shot scope on the RP2040 (Pico) with an ST7789 240x320 LCD and an
optional Web Serial UI. **v2** adds three analog channels and a
logic-analyzer mode with eight digital lanes.

- **Scope mode** — up to **3 ADC channels** (GPIO26/27/28) captured
  round-robin and overlaid on the LCD and in the web UI.
- **Logic mode** — **8 digital lanes** (GPIO5–12) captured simultaneously by
  the PIO block, drawn as stacked timing lanes.
- Amplitudes are **raw ADC codes**, not volts: there is no analog front end and
  no calibration, so the UI shows `C/DIV`, never `V/DIV`.

## Layout

- `platformio.ini` — firmware build (native Pico SDK framework via the
  `maxgerhardt/platform-raspberrypi` fork; see `docs/PLATFORMIO_SETUP.md`)
- `include/` — `board_config.h` (pins, the single source of truth),
  `scope_config.h` (limits + protocol **v2** constants),
  `message_protocol.h` (packed wire payload layouts), `scope_types.h`
- `src/` — `main.cpp` (mode-aware config + capture loop),
  `capture/` (round-robin ADC + DMA, PIO/DMA logic capture, de-interleave,
  edge trigger with hysteresis), `display/` (ST7789 window driver,
  multi-trace + 8-lane scope_view, font), `measurements.cpp`,
  `protocol/` (COBS+CRC16 codec, TinyUSB CDC link when `SCOPE_ENABLE_USB_CDC=1`)
- `web/` — vanilla JS + Canvas + Web Serial UI, plus a deterministic demo
  simulator transport (identical v2 binary frames, no device needed)
- `tests/host/` — pure-C++ host test bench: `test_frame_codec` (18 checks)
  and `test_capture` (48 checks: de-interleave + analog/digital triggers)
- `docs/` — implementation guides, `PROTOCOL_V2.md`, wiring guide, references
- `hardware/rev-a/` — carrier-board stub pin table
- `.github/workflows/pages.yml` — publishes `web/` to GitHub Pages

## Build & test

```sh
pio run -e pico              # firmware → .pio/build/pico/*.uf2
cd tests/host && make test   # frame codec (18) + capture (48) host tests
```

## Hardware

Wiring the Pico to the ST7789 panel, the three buttons, the three analog inputs
and the eight logic inputs is documented pin-by-pin in
[`docs/WIRING_GUIDE.md`](docs/WIRING_GUIDE.md). Read the safety notes first:
everything is 3.3 V only, and the analog and logic inputs are uncalibrated and
unprotected on their own.

## Protocol

The host ⇄ Pico protocol is **v2**: 13-byte `SET_CONFIG`, 9-byte `ACK`,
11-byte `CAPTURE_DATA` head and 14-byte `HELLO_RSP`, with a `mode` byte that
tells the parser whether the payload is interleaved analog codes or packed
logic words. The full field-by-field spec is
[`docs/PROTOCOL_V2.md`](docs/PROTOCOL_V2.md); v1 is preserved in
`docs/PROTOCOL_V1.md`. The two are **not** wire-compatible.

## Web UI

Serve `web/` from any HTTPS static host (all files use relative paths, so a
`/repo/` subpath works). Open `index.html`, then click **Demo** for the
built-in simulator — no hardware needed, byte-identical v2 frames — or
**Connect** to talk to a Pico over Web Serial. A mode switch toggles between
scope and logic views.

GitHub Pages deployment is automated by `.github/workflows/pages.yml`, which
uploads `web/` on every push to `main` that touches it. Enable it once under
**Settings → Pages → Source: GitHub Actions**.
