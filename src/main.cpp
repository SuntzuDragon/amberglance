// amberglance — slice 1: display only.
//
// Goal of this slice is to validate the SSD1322 4-wire-SPI rework and the
// wiring, nothing else. It draws a deliberately diagnostic pattern: a 1px
// frame at the exact panel edges plus a ruler along the top. If the panel is
// column-shifted (the known AliExpress-glass issue in HANDOFF.md) the frame
// will be broken or wrapped rather than a clean rectangle, which makes the
// problem obvious instead of ambiguous.
//
// If that happens: rebuild with -DAMBER_PANEL_ALT_MAP (see platformio.ini).

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "config.h"

// The SSD1322 needs 4 bits per pixel on the wire, so a full-buffer refresh
// pushes 8KB. Bit-banged that is ~60ms, which is irrelevant for a 1Hz clock
// and removes any doubt about SPI bus/pin defaults. Hardware SPI is available
// behind a build flag once the wiring is proven.
// AMBER_PANEL_ALT_MAP selects U8g2's "ZJY" SSD1322 variant, which is the same
// panel family the AliExpress boards use: it ships with default_x_offset 0x18
// (vs 0x1c) and re-map 0x16 (vs 0x06) already baked in — exactly the hand-edit
// HANDOFF.md describes, without patching the library.
#ifdef AMBER_PANEL_ALT_MAP
#define AMBER_OLED_HW U8G2_SSD1322_ZJY_256X64_F_4W_HW_SPI
#define AMBER_OLED_SW U8G2_SSD1322_ZJY_256X64_F_4W_SW_SPI
#else
#define AMBER_OLED_HW U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI
#define AMBER_OLED_SW U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI
#endif

#ifdef AMBER_USE_HW_SPI
AMBER_OLED_HW u8g2(U8G2_R0, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RESET);
#else
AMBER_OLED_SW u8g2(U8G2_R0, PIN_OLED_SCLK, PIN_OLED_MOSI, PIN_OLED_CS,
                   PIN_OLED_DC, PIN_OLED_RESET);
#endif

static constexpr int SCREEN_W = 256;
static constexpr int SCREEN_H = 64;

// Scan the shared I2C bus and report to serial. The sensors are not used in
// this slice, but the scan costs nothing and answers "is the bus wired right"
// before slice 3 depends on it.
static void scanI2C() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  Serial.println(F("I2C scan on SDA=8 SCL=9:"));
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;

    found++;
    const char *name = "unknown";
    if (addr == I2C_ADDR_SHT45)  name = "SHT45 (temp/humidity)";
    if (addr == I2C_ADDR_DS3231) name = "DS3231 (RTC)";
    if (addr == I2C_ADDR_BH1750) name = "BH1750 (light)";
    Serial.printf("  0x%02X  %s\n", addr, name);
  }
  if (found == 0) {
    Serial.println(F("  none found (expected if the sensors aren't wired yet)"));
  }
}

// Corner ticks + a full-perimeter frame + a 16px ruler. Every element here is
// pinned to a panel edge on purpose, so a column/row offset cannot hide.
static void drawTestPattern() {
  u8g2.clearBuffer();

  u8g2.drawFrame(0, 0, SCREEN_W, SCREEN_H);

  // Ruler along the top edge: a taller tick every 64px, short ones every 16px.
  for (int x = 0; x < SCREEN_W; x += 16) {
    const int len = (x % 64 == 0) ? 5 : 3;
    u8g2.drawVLine(x, 1, len);
  }

  u8g2.setFont(u8g2_font_logisoso20_tr);
  const char *title = "AMBERGLANCE";
  u8g2.drawStr((SCREEN_W - u8g2.getStrWidth(title)) / 2, 36, title);

  // Edge-anchored labels: if these are clipped or wrapped, the panel is
  // offset. "L" sits at x=2, "R" ends 2px from the right edge.
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(2, 52, "L");
  u8g2.drawStr(SCREEN_W - 2 - u8g2.getStrWidth("R"), 52, "R");

  char status[40];
  snprintf(status, sizeof(status), "slice 1 - display  up %lus", millis() / 1000);
  u8g2.drawStr((SCREEN_W - u8g2.getStrWidth(status)) / 2, 61, status);

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  // Native USB CDC needs a moment to enumerate before it will accept output.
  const uint32_t serialDeadline = millis() + 2000;
  while (!Serial && millis() < serialDeadline) delay(10);

  Serial.println();
  Serial.println(F("amberglance - slice 1 (display only)"));

  u8g2.begin();

#ifdef AMBER_PANEL_ALT_MAP
  Serial.println(F("panel: ZJY variant (alt column offset + re-map)"));
#else
  Serial.println(F("panel: NHD variant (stock offset + re-map)"));
#endif

  u8g2.setContrast(OLED_CONTRAST_DEFAULT);
  Serial.printf("display: init ok, contrast %u\n", OLED_CONTRAST_DEFAULT);

  scanI2C();
}

void loop() {
  drawTestPattern();
  delay(500);
}
