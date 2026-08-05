# amberglance

An always-on indoor clock and dashboard built around an ESP32-S3 and a 256x64
yellow OLED. Time, indoor temperature and humidity, date, and brightness that
tracks the ambient light in the room.

Firmware is PlatformIO + Arduino. Built in slices, where every slice is a
device that actually works rather than a half-finished step.

**Status: slices 1, 2, 4 and 5 complete** — NTP-corrected time, indoor
temperature and humidity, ambient light. Only the RTC remains.

## Hardware

| Part | Role | Interface | I2C addr |
|------|------|-----------|----------|
| ESP32-S3-DevKitC-1 N16R8 | MCU (16MB flash / 8MB octal PSRAM) | — | — |
| SSD1322 256x64 yellow OLED, 3.12" | Display | 4-wire SPI | — |
| SHT45 | Indoor temp/humidity | I2C | 0x44 |
| DS3231 | RTC | I2C | 0x68 |
| BH1750 (GY-302) | Ambient light | I2C | 0x23 |

Everything runs at **3.3V**.

## Wiring

Shared I2C bus — `SDA = GPIO8`, `SCL = GPIO9`.

Display, 4-wire SPI:

| OLED pin | Signal | ESP32-S3 |
|----------|--------|----------|
| 1 (Vss)   | GND       | GND    |
| 2 (VBAT)  | 3.3V      | 3.3V   |
| 4 (D0)    | SCLK      | GPIO12 |
| 5 (D1)    | SDIN/MOSI | GPIO11 |
| 14 (/DC)  | DC        | GPIO18 |
| 15 (/RST) | RESET     | GPIO14 |
| 16 (/CS)  | CS        | GPIO10 |

`SDA`/`SCL` and `CS`/`MOSI`/`SCLK` are the ESP32-S3's own defaults, so both
buses land on their IOMUX pins and `Wire.begin()`/`SPI.begin()` need no
arguments. `DC` deliberately sits outside that block: `GPIO13` is the default
SPI `MISO`, and U8g2's hardware-SPI path attaches MISO to it, so a DC line
parked there fights its own transport — and blocks MISO for any future
readable SPI device. `GPIO18` is adjacent to `GPIO8` on the header, so the
harness still runs in one region.

The rest avoid the ESP32-S3 strapping pins (0/3/45/46), the native USB pins
(19/20), the SPI flash pins (26–32), the UART0 console (43/44), and the
octal-PSRAM pins (33–37). The pin map lives in
[`include/config.h`](include/config.h) and is the single source of truth.

The SSD1322 ships in 8080 parallel mode; this panel's resistor jumpers were
reworked to 4-wire SPI (BS0=0, BS1=0) before any code was written.

## Slices

1. **Display only** — validate the SPI rework and wiring. ✅
2. **WiFi + NTP** — time on screen. ✅
3. DS3231 RTC — survives reboot and WiFi loss.
4. **SHT45** — indoor temperature and humidity. ✅
5. **BH1750** — ambient light. ✅

Slices 4 and 5 were taken before slice 3; the sensors do not depend on the RTC.

Deliberately out of scope for now: outdoor ESP-NOW sensor node, WWVB radio time
(ES100), CO2 (SCD41), battery backup, enclosure.

## Design notes

- **Always on**, and — as it turns out — always at the panel's dimmest setting.
  See *Brightness* below. No presence sensor.
- **Burn-in mitigation from day one.** The layout shifts a few pixels on a slow
  cycle and brightness is capped well below maximum. Static digits on a
  passive-matrix OLED will ghost otherwise.
- **The clock must work with the network down.** The RTC drives the display
  immediately at boot; WiFi and NTP correct it in the background.
- Mountain Time via the TZ string `MST7MDT,M3.2.0,M11.1.0`, so DST is automatic
  and needs no network. The rule is evaluated on every conversion, not baked in
  at sync time.
- **Daylight-saving transitions are announced.** For 24 hours afterwards the
  bottom-left line alternates between the sync status and e.g.
  `DST +1h now MDT` with a small face — smiling when you gained an hour of
  sleep, frowning when you lost one — so an hour that vanished overnight is
  accounted for. The
  observed state is persisted, so a transition is still reported if it happened
  while the device was unplugged — and a *clock correction* that crosses a
  boundary (a wrong RTC being fixed by NTP) re-baselines silently instead of
  claiming a transition that never occurred.
- **Tabular numerals.** The `logisoso` faces are proportional, so a naively
  drawn clock twitches as digit widths change. Digits are laid out on a fixed
  pitch and the hour is space-padded, so nothing moves.
- **Indoor climate is smoothed, not raw.** The SHT45 resolves finer than the
  displayed tenth of a degree, so raw readings make the last digit flicker. A
  light exponential average settles that. It is read every 5 seconds — room
  temperature cannot change faster than that, and reading harder only heats the
  die, which biases the reading warm.
- Ambient light is shown on screen beside the humidity.
- Temperature in °F.

## Building

```sh
python3 -m venv .venv && .venv/bin/pip install platformio
.venv/bin/pio run
```

Copy `include/secrets.h.example` to `include/secrets.h` and fill in your
network before slice 2. That file is gitignored. The ESP32-S3 radio is 2.4GHz
only.

### Build flags

Set in `platformio.ini`.

| Flag | Effect |
|---|---|
| `AMBER_FAKE_LIGHT` | Drifting placeholder lux so the dimming curve can be exercised before the BH1750 is wired. Remove once it is. |
| `AMBER_LAYOUT_DEBUG` | Prints every block's worst-case ink rectangle at boot and runs a pairwise overlap test. Re-run it whenever a font or baseline changes. |
| `AMBER_PANEL_ALT_MAP` | Selects the ZJY SSD1322 variant — see *Panel variants*. |
| `AMBER_USE_HW_SPI` | **On by default.** Hardware SPI — 13ms per frame instead of 424ms. See *Software vs hardware SPI*. |

The layout check exists because the right rail is right-aligned: an overflow
silently overlaps rather than failing, and a vertical collision is invisible to
a width-only check. It reports rectangles, so both axes are covered:

```
layout: worst-case ink rects (region 252x60)
  time   x   2..121  y  4..42
  secs   x 126..156  y 21..42
  ampm   x 126..137  y 12..21
  date   x 157..250  y  1..17
  temp   x 166..250  y 23..44
  humid  x 209..250  y 50..59
  status x   2.. 91  y 50..59
  lowest ink 59 + shift 4 = 63 (panel max row 63)
  0 collisions
```

### Flashing

If your host can see the board directly, plain PlatformIO works:

```sh
.venv/bin/pio run -t upload && .venv/bin/pio device monitor
```

This project is developed under **WSL2 in NAT mode without usbipd-win**, where
the board never appears as `/dev/ttyACM*`. The scripts in `tools/` work around
that by staging build artifacts into the Windows temp directory and driving
esptool and pyserial through the Windows Python interpreter:

```sh
tools/flash.sh          # build in WSL, flash from Windows
tools/monitor.sh 10     # capture 10s of serial, resetting the board first
```

They need `py -m pip install esptool` on the Windows side, and default to
`COM4` (override with `$AMBER_PORT`).

Use the jack silkscreened **`UART`**, not `USB`. The UART bridge has DTR/RTS
wired to `EN`/`BOOT` so esptool's auto-reset works over real hardware lines; on
the native USB jack the board enumerates but its ROM loader does not answer
esptool's reset. Because of that choice, `Serial` is UART0 — the build sets
`ARDUINO_USB_CDC_ON_BOOT=0`. Flip it back if you move to the `USB` jack.

## Panel variants

If the display shows a blank vertical bar, or text that is shifted, wrapped, or
mirrored, the glass is wired differently from the Newhaven reference that
U8g2's default SSD1322 driver assumes. This is common on the AliExpress panels.

The usual advice is to hand-edit `u8x8_d_ssd1322.c`. You don't need to — U8g2
ships a `ZJY` variant carrying exactly the corrected constants (`x_offset`
`0x18` vs `0x1c`, re-map `0x16` vs `0x06`). Uncomment `-DAMBER_PANEL_ALT_MAP`
in `platformio.ini` to select it, and the library stays unpatched across cleans.

The panel this was developed against needs the stock NHD driver.

Slice 1's test pattern exists to make this diagnosable: it draws a 1px frame at
the exact panel edges, a 16px ruler, and edge-anchored `L`/`R` labels. A
column-shifted panel produces a visibly broken or wrapped rectangle rather than
text that merely "looks a bit off", so the offset problem is distinguishable
from a wiring problem on sight.

## Software vs hardware SPI

The display uses **hardware SPI**, and the margin is much larger than it looks
on paper. Measured on this panel, drawing and clocking out one full 256x64
frame:

| Transport | Frame time | CPU duty at 1 Hz |
|---|---|---|
| Software (bit-banged) | **424 ms** | 42% |
| Hardware | **13 ms** | 1.3% |

Bit-banging means roughly 196,000 `digitalWrite` calls per frame through the
Arduino HAL, and the ESP32's `digitalWrite` is not cheap. A 424ms frame also
means the panel physically cannot refresh faster than ~2.4 Hz, which rules out
grayscale or any animation later.

It needs no special handling, because the wiring was chosen to suit it. U8g2's
`_4W_HW_SPI` constructor leaves its clock/data pins unset and therefore takes
the bare `SPI.begin()` path, which on the ESP32-S3 resolves to the FSPI
defaults — `SCK 12`, `MOSI 11`, `MISO 13`. The first two match this wiring, and
MISO is unused.

DC originally sat on `GPIO13`, which is exactly that MISO pin, so `spiAttachMISO`
reconfigured the DC line as an SPI input the moment hardware SPI initialised.
That was workable by claiming the bus first with MISO disabled:

```cpp
SPI.begin(PIN_OLED_SCLK, -1, PIN_OLED_MOSI, -1);   // before u8g2.begin()
```

`SPIClass::begin()` opens with `if (_spi) return;`, which makes U8g2's later
call a no-op. But moving DC to `GPIO18` removes the conflict outright and frees
MISO for any future readable SPI device, so that is what the wiring does now.
Keep the trick in mind if you ever need DC back on `13`.

Build with `-DAMBER_USE_HW_SPI` (on by default); drop the flag to fall back to
bit-banging on different wiring.

## Brightness

The SSD1322 has **four** independent brightness controls, and U8g2 leaves three
of them pinned at maximum:

| Command | Range | What it changes | U8g2 default | Effect at the dim end |
|---|---|---|---|---|
| `0xBB` pre-charge voltage | `0x00`–`0x1F` | voltage each pixel is charged to | **`0x1F` (max)** | **dominant** |
| `0xC1` contrast | 0–255 | segment current, fine | `0x9F` | small |
| `0xC7` master current | 0–15 | segment current scale | **`0x0F` (max)** | small |
| `0xB8` grayscale table | GS15 `0`–`180` | drive *pulse width* | linear via `0xB9` | **none measured** |

Per the datasheet `ISEG = Contrast/256 × IREF × scale_factor × 2`, so contrast
and master current multiply, and the grayscale table is a third axis because it
changes duty rather than current. That is the theory. Measured on this panel,
it is mostly wrong about what matters:

- Sweeping `GS15` from `180` down to `1` — a 14× reduction in current-drive
  pulse width — produced **no visible change**. At minimum settings the light is
  not coming from the drive phase at all.
- Cutting pre-charge *timing* (`0xB1`, `0xB6`) does dim the panel, but
  **unevenly**: pre-charge is what equalises pixels, so starving it makes them
  inconsistent rather than uniformly darker. Not used.
- Cutting pre-charge *voltage* (`0xBB`) dims it **cleanly**, and is the only
  control that reaches genuinely dim. U8g2 pins it at `0x1F`, higher than the
  chip's own `0x17` reset default.

Measured on this panel: `0x1F` and `0x18` are bright, `0x0D` is the lowest value
that still renders thin strokes and the small 6×10 text evenly, and below `0x09`
the panel goes black entirely. Judge this against the real clock face — a screen
of large text stays clean well past the point where fine detail goes blotchy.

Driving only the contrast register — the obvious thing, and what most code does
— barely moves the bottom of the range.

**This build sits at that floor permanently.** With all four at minimum the
display is still comfortably legible in a bright room with the blinds open, so
there is nothing to gain by ever going higher, and holding an always-on OLED at
its floor is the best case for both burn-in and power. `AUTO_DIM_LEVEL_MIN` and
`AUTO_DIM_LEVEL_MAX` in `include/config.h` clamp the curve's output; both are
`0`. The sensor, the log-space mapping and the whole range are still live behind
that clamp, so widening it is a one-line change.

Deliberately unused: phase length (`0xB1`), second pre-charge period (`0xB6`)
and VCOMH (`0xBE`). All three affect apparent brightness, but they are drive and
timing parameters rather than brightness controls, and lowering them produced
uneven pixels rather than a uniformly dimmer image.

## Second-boundary alignment

A naive loop polls on a fixed delay and redraws when the second changes, so its
period is `render + delay`. The phase at which a rollover is noticed then
drifts — and drifts by a different amount depending on how much ink the last
frame contained, which is visible as the seconds landing unevenly.

Instead each second is **claimed before it arrives**. The loop holds the next
epoch second it intends to display, sleeps until one render-time before that
instant (using the previous frame's measured duration, so the lead self-corrects
as content changes), draws that second, and increments. Frames land within a
millisecond of the boundary:

```
tick: render 13932us, landed +410us from the second
tick: render 14017us, landed -324us from the second
```

Scheduling on an absolute timeline matters, and an earlier version got it
wrong. It inferred a tick by comparing the current second against the last one
drawn, *and* slept toward `boundary - renderUs`. Those are two independent
thresholds either side of the same instant, so waking a hair early put them on
opposite sides: the current second still read as the previous one, nothing was
drawn, and the sleep then rounded forward to the following boundary. The symptom
was the clock occasionally hanging and jumping two seconds. Counting ticks
explicitly cannot drift that way — and if the schedule is ever genuinely missed,
it says so:

```
tick: behind by 1s, resyncing
```

`AMBER_LAYOUT_DEBUG` reports the landing every tenth frame, and every resync.

## License

MIT
