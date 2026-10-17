# Pico Scope Display UI

Single-shot oscilloscope on the RP2040 (Pico) with ST7789 240x320 LCD
and an optional Web Serial UI. Driver docs: `docs/`.

## Layout

- `platformio.ini` — firmware build (native Pico SDK framework via
  `maxgerhardt/platform-raspberrypi` fork; version pins TBD after
  first successful build, see `docs/PLATFORMIO_SETUP.md`)
- `include/` — `board_config.h` (pins), `scope_config.h` (limits +
  protocol v1 constants), `message_protocol.h` (wire payload layouts)
- `src/` — `main.cpp` (BOOT→IDLE→ARM→CAPTURE→PROCESS→DRAW state machine),
  `capture/` (ADC+DMA single shot, edge trigger w/ hysteresis),
  `display/` (ST7789 window-based driver, scope_view composition, font),
  `measurements.cpp` (Vpp/RMS/freq), `protocol/` (COBS+CRC16 v1 codec;
  TinyUSB CDC link when `SCOPE_ENABLE_USB_CDC=1`)
- `web/` — vanilla JS + Canvas + Web Serial UI, plus a deterministic
  demo simulator transport (identical binary frames, no device needed)
  and tests in the browser via `js/protocol.js`
- `tests/host/` — COBS/CRC host test bench (`make test`, 18 checks)
- `docs/` — implementation guides, protocol spec, reference notes
- `hardware/rev-a/` — carrier-board stub pin table

## Build & test

```sh
pio run -e pico              # firmware → .pio/build/pico/*.uf2
cd tests/host && make test   # frame codec host tests (all 18 pass)
```

Web UI: serve `web/` from any HTTPS static host (GitHub Pages has
exactly these 7 files referenced with relative paths); open
`index.html`, click **Demo** (simulator) or **Connect** (Web Serial).
