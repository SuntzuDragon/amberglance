# amberglance

An always-on indoor clock and dashboard built around an ESP32-S3 and a 256x64
yellow OLED. Time, indoor temperature and humidity, date, and brightness that
tracks the ambient light in the room.

Firmware is PlatformIO + Arduino. Built in slices, where every slice is a
device that actually works rather than a half-finished step.

**Status: slice 2 complete** — NTP-corrected time on screen.

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
| 14 (/DC)  | DC        | GPIO13 |
| 15 (/RST) | RESET     | GPIO14 |
| 16 (/CS)  | CS        | GPIO10 |

Pins avoid the ESP32-S3 strapping pins (0/3/45/46), the native USB pins
(19/20), and the octal-PSRAM pins (33–37). The pin map lives in
[`include/config.h`](include/config.h) and is the single source of truth.

The SSD1322 ships in 8080 parallel mode; this panel's resistor jumpers were
reworked to 4-wire SPI (BS0=0, BS1=0) before any code was written.

## Slices

1. **Display only** — validate the SPI rework and wiring. ✅
2. **WiFi + NTP** — time on screen. ✅
3. DS3231 RTC — survives reboot and WiFi loss.
4. SHT45 — indoor temperature and humidity.
5. BH1750 — ambient-light auto-dimming.

Deliberately out of scope for now: outdoor ESP-NOW sensor node, WWVB radio time
(ES100), CO2 (SCD41), battery backup, enclosure.

## Design notes

- **Always on.** No presence sensor. Brightness comes purely from the BH1750.
- **Burn-in mitigation from day one.** The layout shifts a few pixels on a slow
  cycle and brightness is capped well below maximum. Static digits on a
  passive-matrix OLED will ghost otherwise.
- **The clock must work with the network down.** The RTC drives the display
  immediately at boot; WiFi and NTP correct it in the background.
- Mountain Time via the TZ string `MST7MDT,M3.2.0,M11.1.0`, so DST is automatic.
- Temperature in °F.

## Building

```sh
python3 -m venv .venv && .venv/bin/pip install platformio
.venv/bin/pio run
```

Copy `include/secrets.h.example` to `include/secrets.h` and fill in your
network before slice 2. That file is gitignored. The ESP32-S3 radio is 2.4GHz
only.

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

The display defaults to bit-banged SPI. U8g2's `_4W_HW_SPI` constructor leaves
its clock/data pins unset and so takes the bare `SPI.begin()` path, which on the
ESP32-S3 resolves to the FSPI defaults — including `MISO = GPIO13`, this
project's DC line. `spiAttachMISO` then reconfigures DC as an SPI input.

At one refresh per second the speed difference (~60ms vs ~7ms per full frame)
is irrelevant. If you want hardware SPI later, claim the bus yourself before
`u8g2.begin()`:

```cpp
SPI.begin(PIN_OLED_SCLK, -1, PIN_OLED_MOSI, -1);
```

`SPIClass::begin()` opens with `if (_spi) return;`, so U8g2's later call becomes
a no-op and GPIO13 stays a GPIO. Then build with `-DAMBER_USE_HW_SPI`.

## License

MIT
