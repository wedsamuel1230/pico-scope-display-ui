# Wiring Guide — rev-a breadboard build

How to wire a Raspberry Pi Pico (RP2040) to an ST7789 240x320 SPI LCD plus
the three front-panel buttons, for the firmware in `src/`.

The single source of truth for pin numbers is `include/board_config.h`.
If you change a pin there, change it here too.

---

## ⚠️ Safety first

- **The LCD runs on 3.3 V logic. Power VCC from `3V3(OUT)` — never from
  `VSYS`/`VBUS`/5 V.** Feeding 5 V into a 3.3 V panel can destroy it, and
  5 V logic on the SPI lines will destroy the RP2040 pin.
- **The ADC input (GPIO26) is 0–3.3 V, single-ended, and unprotected.**
  There is no divider, no offset stage and no clamp in v1. Exceeding 3.3 V
  (or going below 0 V) will damage the RP2040. See
  [Protecting the ADC input](#protecting-the-adc-input) before you connect
  anything but a known-safe bench signal.
- Disconnect USB before rewiring. Double-check polarity before powering up.

---

## Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 1 | Raspberry Pi Pico (RP2040) | non-W is simplest; Pico W works, LED is on the wireless chip |
| 1 | ST7789 SPI LCD, **240x320** | 8-pin module, 3.3 V logic (labels vary, see below) |
| 3 | Tactile pushbuttons | 6 mm through-hole is fine |
| 1 | Breadboard + jumper wires | keep LCD leads short (< 15 cm) |
| 2 | 1 kΩ resistor | optional — ADC series protection |
| 2 | Schottky diode (BAT54 / 1N5819) | optional — ADC clamp |
| 1 | USB cable | data-capable, for power + Web Serial |

> 1.14" / 1.3" panels are often **135x240**, not 240x320. This firmware
> targets the 240x320 class (2.0"–2.4" modules). Check your panel before buying.

---

## Module pin labels

ST7789 SPI modules are not labelled consistently. Match by function:

| Function | Common labels |
|---|---|
| Ground | `GND` |
| 3.3 V supply | `VCC`, `VDD`, `3V3` |
| SPI clock | `SCL`, `SCK`, `CLK` |
| SPI data (MOSI) | `SDA`, `MOSI`, `DIN` |
| Reset (active low) | `RES`, `RST`, `RESET` |
| Data/command select | `DC`, `A0`, `RS` |
| Chip select (active low) | `CS`, `CE` |
| Backlight | `BLK`, `BL`, `LED` |

`SDA` here means **MOSI**, not I²C. This panel is SPI-only.

---

## LCD wiring (hardware SPI0)

| ST7789 module | Pico signal | GPIO | Pico physical pin |
|---|---|---|---|
| `GND`  | GND             | —  | **38** (or 3, 8, 13, 18, 23, 28, 33) |
| `VCC`  | 3V3(OUT)        | —  | **36** |
| `SCL`  | SPI0 SCK        | 18 | **24** |
| `SDA`  | SPI0 MOSI (TX)  | 19 | **25** |
| `RES`  | LCD reset       | 14 | **19** |
| `DC`   | data/command    | 16 | **21** |
| `CS`   | chip select     | 15 | **20** |
| `BLK`  | backlight       | 13 | **17** |

Notes:

- **`CS` must be connected.** The driver drives it explicitly
  (`SCOPE_LCD_CS_GPIO`); tying it to GND will make every transfer a command
  or every command a transfer, depending on `DC` timing.
- **`BLK` is driven high at init** (`SCOPE_LCD_BL_GPIO`). If your module's
  backlight is hard-wired to VCC through a resistor, leave `BLK` unconnected.
  If your module ties `BLK` to a transistor that expects 3.3 V, connect it.
- Some modules have no `RES` pin. In that case `SCOPE_LCD_RST_GPIO` may be
  left unconnected; the panel will reset on power-up only.
- Keep `SCL`/`SDA` short and route a ground return next to them. Long ribbon
  leads at 31.25 MHz will show as sparkle/noise on screen.
- The driver starts SPI0 at **31.25 MHz** (`SCOPE_LCD_SPI_BAUDRATE`) —
  deliberately SPI clock ÷ 4 for margin. If you get a noisy or blank panel,
  drop it to 15.625 MHz first before chasing anything else.

---

## Button wiring

Buttons are read **active-low with the internal pull-up**, so each button
connects its GPIO to **GND**. No external resistors required.

| Button | Function | GPIO | Pico physical pin | Other leg |
|---|---|---|---|---|
| AUTO | auto-set / free-run | 2 | **4** | GND |
| TRIG | trigger menu / arm | 3 | **5** | GND |
| MENU | menu / cycle | 4 | **6** | GND |

- Each button: one leg to the GPIO, the other leg to GND.
- Swap the legs freely — a tactile switch is not polarised.
- Use a nearby GND pin (pin 3, 8 or 13 for these three).
- No capacitor needed; the firmware samples at loop rate and the pins are
  read as level, not edge, so contact bounce is not critical yet.

> In the current firmware revision the three buttons are **initialised but
> not yet bound to actions**. Wiring them now is correct and future-proof.

---

## LED

GPIO25 is the **on-board LED** on a standard Pico — nothing to wire. The
firmware uses it as a heartbeat while idle, and as the only failure
indicator if the LCD never initialises.

On a **Pico W** the LED is behind the wireless chip; the heartbeat will not
be visible, which is harmless.

---

## USB

Plug the Pico into your computer with a data-capable USB cable.

- The firmware **never writes** to the USB CDC stream except as framed
  protocol packets (`src/protocol/usb_link.cpp`). Debug `printf` goes to
  UART only, so the byte stream stays valid for the parser.
- For the web UI: open the page over **HTTPS or localhost** in a Chromium
  browser, click **Connect**, and pick the Pico's port in the picker.

---

## Protecting the ADC input

GPIO26 (ADC0) expects **0 – 3.3 V**. Without an analog front end (AFE) the
firmware reports **raw ADC codes**, not volts (`C/DIV 683` on screen), and
there is nothing protecting the pin.

Minimum protection for bench use:

```
signal ──[ 1 kΩ ]──┬── GPIO26 (ADC0)
                   │
        BAT54 ──────┤ (anode to GND, cathode to 3V3)
     (or 1N5819)    │
                   └── clamp diodes to 3V3 and GND
```

- The **1 kΩ series resistor** limits current into the pin's clamp diodes.
- The **clamp diodes** divert anything above 3.3 V + Vf or below −Vf.
- Keep the ground return for the signal tied to a **GND near GPIO26**
  (pin 33 is AGND) to reduce noise pickup.

For anything above 3.3 V, use a **10× probe or a resistive divider** so the
pin only ever sees ≤ 3.3 V. Never connect mains or an unknown supply
directly.

v1 has **no calibration**. Codes map to volts only after an AFE exists and
`include/` gains a calibration stage; until then treat the displayed volts
as indicative and the raw codes as authoritative.

---

## Breadboard assembly order

Wire and verify in this order — it matches the acquisition stages in
`docs/IMPLEMENTATION_GUIDE.md`:

1. **Power only** — `VCC`→`3V3(OUT)` (36), `GND`→`GND` (38). Confirm 3.3 V
   across the module's `VCC`/`GND` with a meter before connecting data lines.
2. **SPI + control** — `SCL` 18, `SDA` 19, `CS` 15, `DC` 16, `RES` 14,
   `BLK` 13.
3. **Power up** — the display should clear to the dark background and draw
   the scope chrome (header, graticule, scale row). If not, see below.
4. **Buttons** — AUTO 2, TRIG 3, MENU 4, each to GND.
5. **ADC** — add your 1 kΩ protection resistor, then signal to GPIO26 (31).
   Start with a known-safe source (a 1.5 V AA cell is ideal: 0.5 V of range).

---

## Troubleshooting

| Symptom | Likely cause | Try |
|---|---|---|
| Screen completely blank / dark | Backlight off, or no power | Check 3.3 V at module; try `BLK` tied to 3V3 |
| Backlight on, no image | SPI not reaching the panel | Verify `SCL`/`SDA` not swapped; lower `SCOPE_LCD_SPI_BAUDRATE` to 15.6 MHz |
| White screen | Panel initialised but never written | Usually a wiring issue on `SDA`/`DC`; recheck `MOSI` |
| Colours inverted / negative | Wrong inversion setting for this panel | Toggle the `0x21` inversion command in `src/display/st7789.cpp` |
| Image mirrored or rotated | Module orientation differs | Set `SCOPE_LCD_MIRROR` in `board_config.h` and rebuild, or adjust MADCTL |
| Image offset / edge lines | Panel needs a column/row offset | Adjust the window offsets in `st7789.cpp` `set_window` for your panel |
| Sparkle / random pixels | Signal integrity at 31.25 MHz | Shorten leads, add ground return, lower the baudrate |
| Waveform pinned to 0 or 4095 | Input out of range or floating | Ground GPIO26 — it should read mid/lower codes; check the signal is 0–3.3 V |
| Web UI: "Web Serial unsupported" | Not a Chromium browser, or not a secure context | Use Chrome/Edge over HTTPS or `localhost`, or use **Demo** |

---

## rev-a carrier board

`hardware/rev-a/README.md` carries the same pin map for when this becomes a
PCB instead of a breadboard. `include/board_config.h` remains the single
source of truth for both.
