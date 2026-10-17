# rev-a — carrier board (stub)

Stage 6 deliverable (docs/IMPLEMENTATION_GUIDE.md acquisition stages).
No carrier PCB exists yet; when designed, it must implement the pin map
in `include/board_config.h`:

| Function | GPIO | Notes |
|---|---|---|
| CH1 ADC0 | 26   | breakout ADC header |
| LCD SCK  | 18   | SPI0    |
| LCD MOSI | 19   | SPI0 ("SDA" label on modules) |
| LCD CS   | 15   |         |
| LCD DC   | 16   |         |
| LCD RST  | 14   | RC to GND, SW pulse from firmware |
| LCD BL   | 13   | backlight |
| BTN AUTO | 2    | active low, internal pull-up |
| BTN TRIG | 3    | active low, internal pull-up |
| BTN MENU | 4    | active low, internal pull-up |
| LED      | 25   | on-board |

Bring-up notes:
- stk7789 240x320 SPI panels commonly label GND/VCC(3.3V)/SCL/SDA/RES/DC/CS/BLK.
- Power LCD from 3V3 (never 5V) for IO-level safety.
- Update this table before fab; board_config.h is the single source.
