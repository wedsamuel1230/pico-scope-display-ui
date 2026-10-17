# Wiring Guide — rev-a breadboard build (v2)

Wires a Raspberry Pi Pico (RP2040) to an ST7789 240x320 SPI LCD, the three
front-panel buttons, **three analog inputs** and **eight logic-analyzer inputs**,
for the firmware in `src/`.

The single source of truth for pin numbers is `include/board_config.h`. If you
change a pin there, change it here and in `hardware/rev-a/README.md` too.

## ⚠️ Safety first

- Everything here is **3.3 V logic**. The LCD, the analog inputs and the logic
  inputs are all 3.3 V-referenced and **unprotected on their own**.
- Power the LCD from **`3V3(OUT)`** (physical pin 36). Never feed it 5 V.
- The three ADC inputs (**GPIO26 / GPIO27 / GPIO28**) accept **0 – 3.3 V**
  single-ended. There is **no divider, no offset stage and no clamp in the
  firmware** — see [Protecting the inputs](#protecting-the-inputs).
- The eight logic inputs (**GPIO5 – GPIO12**) are 3.3 V CMOS and equally
  unprotected. Never connect them to 5 V, RS-232 levels or an unknown supply.
- **Disconnect USB before rewiring anything.** Back-powering through a GPIO is
  the easiest way to lose a Pico.

## Bill of materials

| Qty | Part | Notes |
|----:|------|-------|
| 1 | Raspberry Pi Pico (RP2040) | non-W is simplest; Pico W works, but the LED is on the wireless chip |
| 1 | ST7789 SPI LCD, **240x320** | 8-pin module, 3.3 V logic (labels vary, see below) |
| 3 | Tactile pushbuttons | auto / trig / menu |
| 1 | Breadboard + jumper wires | keep the LCD leads short (< 15 cm) |
| up to 3 | 1 kΩ resistor | optional — ADC series protection, one per analog channel |
| up to 3 | Schottky diode (BAT54 / 1N5819) | optional — ADC clamp, one per analog channel |
| up to 8 | 100 Ω – 1 kΩ resistor | optional — logic series damping, one per lane |
| 1 | USB cable | data-capable, not charge-only |

> 1.14" / 1.3" panels are often **135x240**, not 240x320. This firmware targets
> the 240x320 class (2.0"–2.4" modules, e.g. the 2.0" ST7789 above). Check your
> panel before buying.

## Module pin labels

| Function | Common silkscreen labels |
|----------|--------------------------|
| Ground | `GND` |
| 3.3 V supply | `VCC`, `VDD`, `3V3` |
| SPI clock | `SCL`, `SCK`, `CLK` |
| SPI data (MOSI) | `SDA`, `MOSI`, `DIN` |
| Reset (active low) | `RES`, `RST`, `RESET` |
| Data/command select | `DC`, `A0`, `RS` |
| Chip select (active low) | `CS`, `CE` |
| Backlight | `BLK`, `BL`, `LED` |

`SDA` here means **MOSI**, not I²C. This panel is SPI-only.

## LCD wiring (hardware SPI0)

| ST7789 module | Pico signal | GPIO | Pico physical pin |
|---------------|-------------|-----:|------------------:|
| `GND` | GND | — | **38** (or 3, 8, 13, 18, 23, 28, 33) |
| `VCC` | 3V3(OUT) | — | **36** |
| `SCL` | SPI0 SCK | 18 | **24** |
| `SDA` | SPI0 MOSI (TX) | 19 | **25** |
| `RES` | LCD reset | 14 | **19** |
| `DC` | data/command | 16 | **21** |
| `CS` | chip select | 15 | **20** |
| `BLK` | backlight | 13 | **17** |

Notes:

- `CS` must be connected. The driver drives it explicitly as
  `SCOPE_LCD_CS_GPIO`; tying it to GND makes every transfer a command or every
  command a transfer, depending on `DC` timing.
- `BLK` is driven high at init (`SCOPE_LCD_BL_GPIO`), so hard-wired-backlight
  modules can leave `BLK` unconnected.
- Some modules have no `RES`. `SCOPE_LCD_RST_GPIO` may be left unconnected; the
  panel then resets on power-up only.
- Keep `SCL`/`SDA` short with a ground return. Long ribbon leads at 31.25 MHz
  show up as sparkle/noise.
- The driver starts SPI0 at **31.25 MHz** (`SCOPE_LCD_SPI_BAUDRATE`) —
  deliberately SPI clock ÷ 4 for margin. If you get a noisy or blank panel, drop
  it to 15.625 MHz first before chasing anything else.

## Analog inputs (3 channels)

v2 captures **three** ADC channels round-robin and interleaves them in the
buffer.

| Pico signal | GPIO | ADC | Pico physical pin |
|-------------|-----:|----:|------------------:|
| ANALOG 1 | 26 | ADC0 | **31** |
| ANALOG 2 | 27 | ADC1 | **32** |
| ANALOG 3 | 28 | ADC2 | **34** |
| Analog ground | — | AGND | **33** |

- **ADC3 / GPIO29 is deliberately not used.** On a stock Pico, GPIO29 is
  hard-wired to the VSYS/3 sense divider, so it can never be a free input.
- The per-channel sample rate is the configured **total** rate divided by the
  number of enabled channels. Three channels at 300 kS/s total give 100 kS/s
  each. The header and the graphs show the per-channel rate.
- Enabling only some channels is fine and is the way to get a higher rate per
  channel. Unenabled channels are skipped by the capture engine.
- Leave unused inputs tied to GND (or AGND) rather than floating, so they do
  not read noise.

## Logic inputs (8 channels)

v2 adds a logic-analyzer mode with **eight** digital lanes captured by the PIO
block, so they are sampled simultaneously and at a consistent rate.

| Lane | Pico signal | GPIO | Pico physical pin |
|-----:|-------------|-----:|------------------:|
| D0 | LOGIC 0 | 5 | **7** |
| D1 | LOGIC 1 | 6 | **9** |
| D2 | LOGIC 2 | 7 | **10** |
| D3 | LOGIC 3 | 8 | **11** |
| D4 | LOGIC 4 | 9 | **12** |
| D5 | LOGIC 5 | 10 | **14** |
| D6 | LOGIC 6 | 11 | **15** |
| D7 | LOGIC 7 | 12 | **16** |

- The lanes must be a **contiguous** GPIO window: the PIO "in pins" source is a
  base plus a count, so GPIO5–12 was chosen.
- Those pins are unused by the LCD (13–19), the buttons (2/3/4), the LED (25)
  and the ADC (26–28).
- The window is shifted so that **bit 0 of each sample is D0 (GPIO5)**. Bits
  8–15 of the 32-bit PIO snapshot leak GPIO13+ (the LCD backlight and reset),
  so the firmware masks every sample with `SCOPE_LOGIC_PIN_MASK` (`0xFF`). You
  will never see those bits on the wire.
- Logic rates are **nominal and uncalibrated**: the PIO clock divisor is derived
  from the system clock and rounded, so treat the reported rate as indicative.
- Ground reference matters more than anything else here. Bring a GND next to the
  lane pins (pin 8 or 13) and use it for your probe's ground clip.

## Button wiring

Buttons are read **active-low with the internal pull-up**, so each button
connects its GPIO to **GND**. No external resistors required.

| Button | Function | GPIO | Pico physical pin | Other leg |
|--------|----------|-----:|------------------:|-----------|
| AUTO | auto-set / free-run | 2 | **4** | GND |
| TRIG | trigger menu / arm | 3 | **5** | GND |
| MENU | menu cycle | 4 | **6** | GND |

- One leg to the GPIO, the other to GND.
- Tactile switches are not polarised.
- Use a nearby GND (pin 3, 8 or 13).
- No capacitor needed: the pins are read as a level, not on an edge.

> In the current firmware revision the three buttons are **initialised but not
> yet bound to actions**. Wiring them now is correct and future-proof.

## LED

GPIO25 is the on-board LED on a standard Pico — nothing to wire. The firmware
uses it as a heartbeat while idle and as the only failure indicator if the LCD
never initialises. On a Pico W the LED is behind the wireless chip, so the
heartbeat is invisible; that is harmless.

## USB

Plug in with a data-capable cable. The firmware **never writes** to the USB CDC
stream except as framed protocol packets (`src/protocol/usb_link.cpp`). Debug
`printf` goes to UART only, so the byte stream stays valid for the parser.

For the web UI:

1. Open the page over **HTTPS or localhost** in a Chromium browser.
2. Click **Connect** and pick the Pico's port.
3. Or click **Demo** to drive the same UI from the built-in simulator with no
   hardware at all.

Protocol details live in `docs/PROTOCOL_V2.md`.

## Protecting the inputs

### Analog (GPIO26 / 27 / 28)

Each ADC pin expects **0 – 3.3 V**. Without an analog front end (AFE) the
firmware reports **raw ADC codes**, not volts (`C/DIV 683` on screen), and there
is nothing protecting the pin.

```
signal ──[ 1 kΩ ]──┬── GPIO26 (ADC0)
                   │
        BAT54 / 1N5819 clamp
        anode to GND, cathode to 3V3
```

- The 1 kΩ series resistor limits current into the pin's clamp diodes.
- The clamp diodes divert anything above 3.3 V + Vf, or below −Vf.
- Keep the ground return tied to a **GND near the ADC pins** (pin 33 is AGND) to
  reduce noise pickup.
- For anything above 3.3 V, use a **10× probe or a resistive divider** so the pin
  only ever sees ≤ 3.3 V. Never connect mains or an unknown supply directly.
- Repeat the same protection on GPIO27 and GPIO28 if you plan to use them.

### Logic (GPIO5 – GPIO12)

These are ordinary 3.3 V digital inputs. A **100 Ω – 1 kΩ series resistor** per
lane is cheap insurance: it dampens ringing on long leads and limits current if
something is mis-wired. They are **not** 5 V tolerant.

If you need to look at a 5 V signal, level-shift it first — a simple divider or a
74LVC245-class buffer. Do not rely on the RP2040's clamp diodes as a level
shifter.

### No calibration

There is **no calibration** in v2, exactly as in v1. The firmware always sets
`SCOPE_FLAG_NOT_CALIB`, the UI shows `C/DIV` and never `V/DIV`, and the screen
amply labels the amplitude as raw ADC codes. Codes map to volts only after an
AFE exists and `include/` gains a calibration stage; until then treat the raw
codes as authoritative.

## Breadboard assembly order

This matches the acquisition stages in `docs/IMPLEMENTATION_GUIDE.md`.

1. **Power only.** VCC → 3V3(OUT) (36), GND → GND (38). Confirm 3.3 V with a
   meter before connecting any data line.
2. **SPI + control.** SCL 18, SDA 19, CS 15, DC 16, RES 14, BLK 13.
3. **Power up.** The display clears to the dark background and draws the scope
   chrome (header, graticule, scale row).
4. **Buttons.** AUTO 2, TRIG 3, MENU 4, each to GND.
5. **Analog inputs.** Add a 1 kΩ protection resistor per channel, then signal to
   GPIO26 (31), GPIO27 (32) and GPIO28 (34). Start with a known-safe source —
   a 1.5 V AA cell is ideal (about half of the 0–3.3 V window).
6. **Logic inputs.** Wire D0–D7 to GPIO5–12 (pins 7, 9, 10, 11, 12, 14, 15, 16)
   with a ground return on pin 8 or 13. Start from something slow and known —
   an output you can toggle at a few hundred hertz — before trusting the
   timing.

## Troubleshooting

| Symptom | Likely cause | Try |
|---------|--------------|-----|
| Blank / dark screen | Backlight off, or no power | Check `VCC` on 3V3(OUT) and `BLK` (or that the module ties it high) |
| Backlight on but no image | SPI not reaching the panel | Verify SCL/SDA are not swapped; drop the baudrate to 15.6 MHz |
| White screen | Panel initialised but never written | Recheck MOSI (`SDA`) |
| Colours inverted | Panel default inversion | Toggle the `0x21` inversion command in `src/display/st7789.cpp` |
| Image mirrored / rotated | MADCTL orientation | Set `SCOPE_LCD_MIRROR` in `board_config.h` and rebuild |
| Image offset or edge lines | Window offsets | Adjust `set_window` offsets in `src/display/st7789.cpp` |
| Sparkle / random pixels | Signal integrity at 31.25 MHz | Shorten leads, add a ground return, lower the baudrate |
| Waveform pinned to 0 or 4095 | Input out of range or floating | Ground the pin; confirm the signal is 0–3.3 V |
| Channel 2/3 always flat | Channel not enabled, or pin floating | Tick the channel in the UI; tie unused inputs to GND |
| Amplitude looks "too small" | Expected: codes, not volts | There is no AFE — divide the code by 4095 × 3.3 V only as a rough guide |
| Per-channel rate is lower than set | Total rate is split across channels | Disable channels, or raise the total rate; see [Analog inputs](#analog-inputs-3-channels) |
| Logic lane always 0 or 1 | Floating input, or wrong GPIO | Tie it low/high and confirm against the lane table above |
| Logic timing looks off | Nominal, uncalibrated PIO rate | Trust *relative* timing; verify against a known clock if it matters |
| Web UI "Web Serial unsupported" | Not Chromium, or not a secure context | Use Chrome/Edge over HTTPS or `localhost`, or use **Demo** |

## rev-a carrier board

`hardware/rev-a/README.md` carries the same pin map for when this becomes a PCB
instead of a breadboard. `include/board_config.h` remains the single source of
truth for both.
