#pragma once

// amberglance pin map — MUST match the physical wiring in HANDOFF.md.
//
// Pins were chosen to avoid ESP32-S3 strapping pins (0/3/45/46), the native
// USB pins (19/20), and the octal-PSRAM pins (33-37). Do not reshuffle these
// without re-checking against that list.

// ---- Shared I2C bus (SHT45 0x44, DS3231 0x68, BH1750 0x23) ----
// Not used until slice 3+, but kept here so there is one pin-map file.
constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 9;

constexpr uint8_t I2C_ADDR_SHT45  = 0x44;
constexpr uint8_t I2C_ADDR_DS3231 = 0x68;
constexpr uint8_t I2C_ADDR_BH1750 = 0x23;

// ---- SSD1322 256x64 OLED, 4-wire SPI ----
// OLED header pin -> signal -> ESP32-S3 GPIO
//   4 (D0)  SCLK      GPIO12
//   5 (D1)  SDIN/MOSI GPIO11
//  14 (/DC) DC        GPIO18
//  15 (/RST) RESET    GPIO14
//  16 (/CS) CS        GPIO10
// DC deliberately sits outside the 10-14 block. GPIO13 is the ESP32-S3's
// default SPI MISO, and U8g2's hardware-SPI path attaches MISO to it — so
// parking DC there meant the display transport fought its own DC line, and
// blocked MISO for any future readable SPI device. GPIO18 is adjacent to
// GPIO8 on the header, so the harness still runs in one region.
constexpr int PIN_OLED_SCLK  = 12;
constexpr int PIN_OLED_MOSI  = 11;
constexpr int PIN_OLED_DC    = 18;
constexpr int PIN_OLED_RESET = 14;
constexpr int PIN_OLED_CS    = 10;

// ---- Calibration button (temporary, -DAMBER_CALIBRATE only) ----
// Momentary switch to ground; the internal pull-up does the rest, so pressed
// reads LOW. GPIO13 is deliberately avoided even though it is free: it was
// vacated to keep SPI MISO available.
constexpr int PIN_BUTTON = 17;

// ---- Display brightness policy ----
// The SSD1322 has TWO independent brightness controls, and using only one
// wastes most of the panel's range:
//
//   0xBB  pre-charge voltage  0x00-0x1F, and by far the most effective
//   0xC1  contrast            0-255, fine
//   0xC7  master current      0-15,  scales segment drive current
//   0xB8  grayscale table     pulse width per grey level
//
// U8g2 pins master current at 0x0F and selects the default linear grayscale
// table (0xB9), then never touches either — so contrast alone only spans the
// top of what the panel can do. Mono rendering lights every pixel at grey
// level 15, so GS15's pulse width scales the whole image, and it reaches far
// dimmer than the other two combined.
//
// Code works in an abstract 0-255 brightness level; ui::setBrightness maps it
// onto both registers. Note even level 0 is not off — pixels are still lit.
// Actually blanking the panel needs the display-off command.
constexpr uint8_t OLED_CONTRAST_MIN =  0;
constexpr uint8_t OLED_CONTRAST_MAX = 90;
constexpr uint8_t OLED_MASTER_MIN   =  0;
constexpr uint8_t OLED_MASTER_MAX   = 15;

// GS15 pulse width, in display clocks (0-180). GS1..GS14 stay at 0..13 and
// only GS15 moves. Measured on this panel, sweeping GS15 from 14 down to 1 made
// no visible difference at all — at minimum settings the light is not coming
// from the current-drive phase.
constexpr uint8_t OLED_GRAY_MIN     =  14;
constexpr uint8_t OLED_GRAY_MAX     = 180;

// Pre-charge voltage, 0x00 (0.20 x VCC) to 0x1F (0.60 x VCC). u8g2 pins this at
// 0x1F — above even the chip's own 0x17 reset default — and this turned out to
// be the control that actually dims the panel. Measured: 0x1F and 0x18 are
// bright, and below 0x09 the panel goes black entirely.
//
// The floor was chosen against the real clock face, not a test pattern: thin
// digit strokes and the small 6x10 text go blotchy well before a screen full of
// large text does, so they are the binding constraint. 0x0D is the lowest value
// that still renders them evenly.
//
// If it ever looks blotchy in a cold room or after a few years, raise this a
// step or two — OLED threshold voltages drift with both.
constexpr uint8_t OLED_PRECHARGE_MIN = 0x0D;
constexpr uint8_t OLED_PRECHARGE_MAX = 0x1F;

// Used before the light sensor has a reading, and whenever it is unavailable.
constexpr uint8_t OLED_BRIGHTNESS_DEFAULT = 128;

// The auto-dim curve's output is clamped to this band. It spans the full range
// now that the curve is fitted to real judgements; the clamp exists so the
// range can be narrowed without touching the curve.
constexpr uint8_t AUTO_DIM_LEVEL_MIN = 0;
constexpr uint8_t AUTO_DIM_LEVEL_MAX = 255;

// ---- Auto-dimming curve ----
// Anchor points for the lux-to-level mapping, interpolated in log space. At or
// below LUX_DARK the display sits at its floor; at or above LUX_BRIGHT, its
// ceiling.
//
// These are not guesses — they are a least-squares fit to 17 judgements made by
// carrying the device around the house with a temporary button, choosing what
// looked right in each room, and recording (lux, level) pairs. See the
// calibration harness in calib.cpp.
//
// The fit's RMSE is 18 levels, against a measured self-consistency of ~24
// levels across repeat judgements at similar light. In other words the curve
// already tracks the judgement more closely than the judgement repeats itself,
// so a more elaborate model would only be fitting noise.
constexpr float LUX_DARK = 2.7f;
constexpr float LUX_BRIGHT = 250.0f;

// ---- Burn-in mitigation ----
// Always-on OLED with static digits will ghost. The whole layout is drawn into
// a region smaller than the panel and walked around the spare pixels on a slow
// cycle, so no pixel stays lit at the same intensity indefinitely.
constexpr int LAYOUT_W = 252;
constexpr int LAYOUT_H = 60;
constexpr int SHIFT_STEPS_X = 256 - LAYOUT_W;  // 4
constexpr int SHIFT_STEPS_Y = 64 - LAYOUT_H;   // 4
constexpr uint32_t SHIFT_INTERVAL_MS = 60UL * 1000UL;

// ---- Time ----
// TZ string carries the US DST rules, so localtime() handles the switch with
// no code of ours. Mountain Time (Utah).
constexpr char TZ_MOUNTAIN[] = "MST7MDT,M3.2.0,M11.1.0";
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.nist.gov";

// Set true for 24-hour time.
constexpr bool CLOCK_24_HOUR = false;
