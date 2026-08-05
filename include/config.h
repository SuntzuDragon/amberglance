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
//  14 (/DC) DC        GPIO13
//  15 (/RST) RESET    GPIO14
//  16 (/CS) CS        GPIO10
constexpr int PIN_OLED_SCLK  = 12;
constexpr int PIN_OLED_MOSI  = 11;
constexpr int PIN_OLED_DC    = 13;
constexpr int PIN_OLED_RESET = 14;
constexpr int PIN_OLED_CS    = 10;

// ---- Display brightness policy ----
// Always-on OLED. The ceiling sits well below the hardware maximum of 255:
// full drive current is both glarier than useful and the fastest route to
// burn-in on a passive-matrix panel.
//
// The floor is 0 — the register's own minimum — because this panel stays
// comfortably legible there in a dark room, so there is nothing to gain by
// holding it higher. Note 0 is not off: the contrast register scales drive
// current, and pixels are still lit at 0. Actually blanking the panel would
// need the display-off command, not a contrast of 0.
constexpr uint8_t OLED_CONTRAST_MIN =  0;
constexpr uint8_t OLED_CONTRAST_MAX = 90;

// Slice 1 runs at a fixed mid brightness; the BH1750 takes over in slice 5.
constexpr uint8_t OLED_CONTRAST_DEFAULT = 48;

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
