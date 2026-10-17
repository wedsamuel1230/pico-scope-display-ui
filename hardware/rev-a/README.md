# rev-a — carrier board (stub)

Stage 6 deliverable (docs/IMPLEMENTATION_GUIDE.md acquisition stages).
No carrier PCB exists yet; when designed, it must implement the pin map
in `include/board_config.h`:

| Function | GPIO | Notes |
|---|---|---|
| ADC1 (CH1) | 26 | analog input, ADC0, breakout header |
| ADC2 (CH2) | 27 | analog input, ADC1, breakout header |
| ADC3 (CH3) | 28 | analog input, ADC2, breakout header |
| LOGIC D0 | 5  | PIO in_base, contiguous window D0–D7 |
| LOGIC D1 | 6  |         |
| LOGIC D2 | 7  |         |
| LOGIC D3 | 8  |         |
| LOGIC D4 | 9  |         |
| LOGIC D5 | 10 |         |
| LOGIC D6 | 11 |         |
| LOGIC D7 | 12 |         |
| LCD SCK  | 18 | SPI0    |
| LCD MOSI | 19 | SPI0 ("SDA" label on modules) |
| LCD CS   | 15 |         |
| LCD DC   | 16 |         |
| LCD RST  | 14 | RC to GND, SW pulse from firmware |
| LCD BL   | 13 | backlight |
| BTN AUTO | 2  | active low, internal pull-up |
| BTN TRIG | 3  | active low, internal pull-up |
| BTN MENU | 4  | active low, internal pull-up |
| LED      | 25 | on-board |

rev-a is **v2**: three analog inputs and eight logic inputs. ADC4
(GPIO29) is deliberately unused — on a stock Pico it is hard-wired to
the VSYS/3 sense divider and can never be a free input.

Bring-up notes:
- ST7789 240x320 SPI panels commonly label GND/VCC(3.3V)/SCL/SDA/RES/DC/CS/BLK.
  Full wiring instructions: `docs/WIRING_GUIDE.md`.
- Power LCD from 3V3 (never 5V) for IO-level safety.
- The logic window must stay **contiguous** (the PIO in_pins source is a base
  plus a count). GPIO5–12 was chosen to avoid the LCD, buttons, LED and ADC.
- Every logic sample is masked with `SCOPE_LOGIC_PIN_MASK` (0xFF) before it
  reaches the wire, so GPIO13+ cannot leak into a capture.
- Add series protection on the analog and logic headers before fab: 1 kΩ +
  clamp diodes per ADC channel, 100 Ω–1 kΩ per logic lane. v2 has **no AFE and
  no calibration**, so nothing else limits over-voltage.
- Update this table before fab; `include/board_config.h` is the single source.
