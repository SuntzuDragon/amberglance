#include "ui.h"

#include <U8g2lib.h>
#include <string.h>
#include <utility>

#include "config.h"

namespace {

// AMBER_PANEL_ALT_MAP selects U8g2's "ZJY" SSD1322 variant, which is the panel
// family the AliExpress boards use: default_x_offset 0x18 (vs 0x1c) and re-map
// 0x16 (vs 0x06) are already baked in — exactly the hand-edit the usual advice
// describes, without patching the library.
#ifdef AMBER_PANEL_ALT_MAP
#define AMBER_OLED_HW U8G2_SSD1322_ZJY_256X64_F_4W_HW_SPI
#define AMBER_OLED_SW U8G2_SSD1322_ZJY_256X64_F_4W_SW_SPI
#else
#define AMBER_OLED_HW U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI
#define AMBER_OLED_SW U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI
#endif

// Software SPI by default. U8g2's hardware-SPI constructor leaves its
// clock/data pins unset and so takes the bare SPI.begin() path, which on the
// ESP32-S3 claims MISO = GPIO13 — this project's DC line. See README.
#ifdef AMBER_USE_HW_SPI
AMBER_OLED_HW u8g2(U8G2_R0, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RESET);
#else
AMBER_OLED_SW u8g2(U8G2_R0, PIN_OLED_SCLK, PIN_OLED_MOSI, PIN_OLED_CS,
                   PIN_OLED_DC, PIN_OLED_RESET);
#endif

const char *const WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

#define FONT_TIME  u8g2_font_logisoso38_tr
#define FONT_SECS  u8g2_font_logisoso20_tr
#define FONT_TEMP  u8g2_font_logisoso20_tr
#define FONT_DATE  u8g2_font_helvB12_tr
#define FONT_SMALL u8g2_font_6x10_tf

// Baselines within the LAYOUT_W x LAYOUT_H region, before the burn-in shift.
// Right rail: date on top, temperature beneath it, humidity on the bottom
// line opposite the status text.
// Measured ink extents drive these, not eyeballing: the date's descender
// reaches y=17 and the bottom row's ascender starts 7 above its baseline, so
// the temperature is centred in the 17..50 gap between them. Y_BOTTOM is 57
// rather than 59 because the burn-in shift adds up to 4 rows and a descender
// ('y' in "sync") would otherwise be clipped off the bottom of the panel.
constexpr int Y_TIME = 42;
constexpr int Y_DATE = 13;
constexpr int Y_TEMP = 44;
constexpr int Y_BOTTOM = 57;
constexpr int X_MARGIN = 2;

// How long each half of the alternating bottom-left line is shown while a
// daylight-saving notice is active.
constexpr uint32_t NOTICE_ALTERNATE_MS = 4000;

// Walk the layout around the spare pixels so no pixel is held at one intensity
// forever. A full circuit takes (5 x 5) minutes.
void burnInShift(int &ox, int &oy) {
  const uint32_t step = millis() / SHIFT_INTERVAL_MS;
  ox = step % (SHIFT_STEPS_X + 1);
  oy = (step / (SHIFT_STEPS_X + 1)) % (SHIFT_STEPS_Y + 1);
}

void drawRight(int ox, int y, const char *s) {
  u8g2.drawStr(ox + LAYOUT_W - X_MARGIN - u8g2.getStrWidth(s), y, s);
}

// --- Tabular numerals ----------------------------------------------------
//
// The logisoso faces are proportional: '1' is narrower than '8'. Drawn
// normally, a clock or a temperature physically shifts every time a digit
// changes, which reads as the whole block twitching. These helpers lay digits
// out on a fixed pitch — the width of the widest digit — so a numeric field
// occupies the same pixels regardless of its value.
//
// The caller must have selected the font already.

// Digits, spaces and dashes all take a full cell; punctuation keeps its own
// width, which is constant anyway since those glyphs never change.
bool isCellChar(char c) { return (c >= '0' && c <= '9') || c == ' ' || c == '-'; }

int digitCellWidth() {
  int w = 0;
  for (char c = '0'; c <= '9'; c++) {
    const char buf[2] = {c, 0};
    w = max(w, (int)u8g2.getStrWidth(buf));
  }
  return w;
}

int tabularWidth(const char *s) {
  const int cell = digitCellWidth();
  int total = 0;
  for (const char *p = s; *p; ++p) {
    if (isCellChar(*p)) {
      total += cell;
    } else {
      const char buf[2] = {*p, 0};
      total += u8g2.getStrWidth(buf);
    }
  }
  return total;
}

void drawTabular(int x, int y, const char *s) {
  const int cell = digitCellWidth();
  for (const char *p = s; *p; ++p) {
    const char buf[2] = {*p, 0};
    if (isCellChar(*p)) {
      if (*p != ' ') {
        // Centre the glyph in its cell so narrow digits sit evenly.
        u8g2.drawStr(x + (cell - u8g2.getStrWidth(buf)) / 2, y, buf);
      }
      x += cell;
    } else {
      u8g2.drawStr(x, y, buf);
      x += u8g2.getStrWidth(buf);
    }
  }
}

void formatAge(uint32_t secs, char *out, size_t n) {
  if (secs < 60) snprintf(out, n, "%us", (unsigned)secs);
  else if (secs < 3600) snprintf(out, n, "%um", (unsigned)(secs / 60));
  else snprintf(out, n, "%uh", (unsigned)(secs / 3600));
}

// The condition the status line is reporting, without the age. Split out from
// the rendered text so serial logging can fire on real state changes rather
// than once a second as the age ticks over.
const char *statusKind(const ui::State &s) {
  if (!s.timeValid) {
    switch (s.net) {
      case net::Status::Connecting: return "connecting";
      case net::Status::Online:     return "syncing";
      default:                      return "no network";
    }
  }
  // Once the time is known, the question stops being "do we have a time" and
  // becomes "how stale is it" — which matters most precisely when offline, so
  // the age stays on screen either way.
  return (s.net == net::Status::Online) ? "sync" : "offline";
}

// The status line answers "should I trust these digits". While the time is
// unknown it says why; once it is known it reports how stale the sync is.
void statusText(const ui::State &s, char *out, size_t n) {
  const char *kind = statusKind(s);

  if (!s.timeValid) {
    snprintf(out, n, "%s", kind);
    return;
  }

  char age[8];
  formatAge(s.secondsSinceSync, age, sizeof(age));
  snprintf(out, n, "%s %s", kind, age);
}

// Degree sign drawn as a ring rather than a glyph: the large logisoso faces
// are ASCII-only, and mixing in a Latin-1 font just for one character costs
// more flash and alignment fiddling than two primitives.
void drawTemperature(int ox, int oy, const ui::State &s) {
  char num[8];
  if (s.haveClimate) snprintf(num, sizeof(num), "%.1f", s.tempF);
  else snprintf(num, sizeof(num), "--.-");

  u8g2.setFont(FONT_TEMP);
  const int wNum = tabularWidth(num);
  constexpr int W_UNIT = 12;  // ring + "F"

  const int x = ox + LAYOUT_W - X_MARGIN - (wNum + 3 + W_UNIT);
  const int y = oy + Y_TEMP;
  drawTabular(x, y, num);

  const int ux = x + wNum + 3;
  u8g2.drawCircle(ux + 2, y - 14, 2);
  u8g2.setFont(FONT_SMALL);
  u8g2.drawStr(ux + 6, y, "F");
}

}  // namespace

void ui::begin() {
  u8g2.begin();
  setBrightness(OLED_CONTRAST_DEFAULT);
}

void ui::setBrightness(uint8_t contrast) {
  if (contrast < OLED_CONTRAST_MIN) contrast = OLED_CONTRAST_MIN;
  if (contrast > OLED_CONTRAST_MAX) contrast = OLED_CONTRAST_MAX;
  u8g2.setContrast(contrast);
}

void ui::splash() {
  u8g2.clearBuffer();
  u8g2.setFont(FONT_SECS);
  const char *name = "amberglance";
  u8g2.drawStr((256 - u8g2.getStrWidth(name)) / 2, 40, name);
  u8g2.sendBuffer();
}

#ifdef AMBER_LAYOUT_DEBUG
void ui::debugLayout() {
  // Every block's worst-case ink rectangle, then a pairwise overlap test.
  // Comparing blocks only by x (as an earlier version did) flags harmless
  // neighbours that never share a row, and comparing only by y misses the
  // real collisions — so the check has to be two-dimensional.
  struct Block {
    const char *name;
    int x0, x1, y0, y1;
  };

  auto ink = [](const uint8_t *font, int baseline, bool descenders) {
    u8g2.setFont(font);
    const int top = baseline - u8g2.getAscent();
    const int bot = descenders ? baseline - u8g2.getDescent() : baseline;
    return std::pair<int, int>(top, bot);
  };

  u8g2.setFont(FONT_TIME);
  const int timeW = tabularWidth("88:88");
  const auto timeY = ink(FONT_TIME, Y_TIME, false);
  const int afterTime = X_MARGIN + timeW + 5;

  u8g2.setFont(FONT_SECS);
  const int secsW = tabularWidth("88");
  const auto secsY = ink(FONT_SECS, Y_TIME, false);

  u8g2.setFont(FONT_SMALL);
  const int ampmW = u8g2.getStrWidth("PM");
  const auto ampmY = ink(FONT_SMALL, Y_TIME - 23, true);

  u8g2.setFont(FONT_DATE);
  const int dateW = u8g2.getStrWidth("Wed Sep 30");
  const auto dateY = ink(FONT_DATE, Y_DATE, true);

  u8g2.setFont(FONT_TEMP);
  const int tempW = tabularWidth("-88.8") + 3 + 12;
  const auto tempY = ink(FONT_TEMP, Y_TEMP, false);

  u8g2.setFont(FONT_SMALL);
  const int humW = u8g2.getStrWidth("100% RH");
  const int botW = max(u8g2.getStrWidth("DST +1h now MDT"),
                       u8g2.getStrWidth("no network"));
  const auto botY = ink(FONT_SMALL, Y_BOTTOM, true);

  const int R = LAYOUT_W - X_MARGIN;
  const Block blocks[] = {
      {"time",   X_MARGIN,     X_MARGIN + timeW, timeY.first, timeY.second},
      {"secs",   afterTime,    afterTime + secsW, secsY.first, secsY.second},
      {"ampm",   afterTime,    afterTime + ampmW, ampmY.first, ampmY.second},
      {"date",   R - dateW,    R,                 dateY.first, dateY.second},
      {"temp",   R - tempW,    R,                 tempY.first, tempY.second},
      {"humid",  R - humW,     R,                 botY.first,  botY.second},
      {"status", X_MARGIN,     X_MARGIN + botW,   botY.first,  botY.second},
  };
  const int n = sizeof(blocks) / sizeof(blocks[0]);

  Serial.println(F("layout: worst-case ink rects (region 252x60)"));
  for (int i = 0; i < n; i++) {
    Serial.printf("  %-6s x %3d..%3d  y %2d..%2d\n", blocks[i].name,
                  blocks[i].x0, blocks[i].x1, blocks[i].y0, blocks[i].y1);
  }

  int collisions = 0;
  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      const Block &a = blocks[i], &b = blocks[j];
      const bool xo = a.x0 < b.x1 && b.x0 < a.x1;
      const bool yo = a.y0 < b.y1 && b.y0 < a.y1;
      if (xo && yo) {
        collisions++;
        Serial.printf("  *** OVERLAP %s / %s ***\n", a.name, b.name);
      }
    }
  }

  const int lowest = botY.second;
  Serial.printf("  lowest ink %d + shift %d = %d (panel max row 63)\n", lowest,
                SHIFT_STEPS_Y, lowest + SHIFT_STEPS_Y);
  if (lowest + SHIFT_STEPS_Y > 63) Serial.println(F("  *** CLIPPED ***"));
  if (X_MARGIN + timeW + 5 + secsW > LAYOUT_W) Serial.println(F("  *** OVERFLOW ***"));
  Serial.printf("  %d collisions\n", collisions);
}
#endif

void ui::render(const State &s) {
  int ox = 0, oy = 0;
  burnInShift(ox, oy);

  u8g2.clearBuffer();

  // --- Time, left ---
  char hhmm[8];
  if (!s.timeValid) {
    // An unsynced ESP32 reports an epoch near zero. Showing that as a real
    // time would be a lie, so the placeholder stays until NTP lands.
    snprintf(hhmm, sizeof(hhmm), "--:--");
  } else if (CLOCK_24_HOUR) {
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", s.local.tm_hour, s.local.tm_min);
  } else {
    int h = s.local.tm_hour % 12;
    if (h == 0) h = 12;
    // Space-padded, not zero-padded: a leading blank still occupies a cell, so
    // everything to the right holds still as the hour crosses 9 -> 10.
    snprintf(hhmm, sizeof(hhmm), "%2d:%02d", h, s.local.tm_min);
  }

  u8g2.setFont(FONT_TIME);
  const int timeX = ox + X_MARGIN;
  drawTabular(timeX, oy + Y_TIME, hhmm);
  const int afterTime = timeX + tabularWidth(hhmm) + 5;

  if (s.timeValid) {
    char ss[4];
    snprintf(ss, sizeof(ss), "%02d", s.local.tm_sec);
    u8g2.setFont(FONT_SECS);
    drawTabular(afterTime, oy + Y_TIME, ss);

    if (!CLOCK_24_HOUR) {
      u8g2.setFont(FONT_SMALL);
      u8g2.drawStr(afterTime, oy + Y_TIME - 23,
                   s.local.tm_hour < 12 ? "AM" : "PM");
    }
  }

  // --- Right rail: date, temperature, humidity ---
  if (s.timeValid) {
    char date[16];
    snprintf(date, sizeof(date), "%s %s %d", WEEKDAYS[s.local.tm_wday],
             MONTHS[s.local.tm_mon], s.local.tm_mday);
    u8g2.setFont(FONT_DATE);
    drawRight(ox, oy + Y_DATE, date);
  }

  drawTemperature(ox, oy, s);

  char humidity[12];
  if (s.haveClimate) snprintf(humidity, sizeof(humidity), "%.0f%% RH",
                              s.humidityPct);
  else snprintf(humidity, sizeof(humidity), "--%% RH");
  u8g2.setFont(FONT_SMALL);
  drawRight(ox, oy + Y_BOTTOM, humidity);

  // --- Bottom left: sync status, alternating with any DST notice ---
  char status[24];
  statusText(s, status, sizeof(status));

  const char *bottomLeft = status;
  if (s.dstNotice && ((millis() / NOTICE_ALTERNATE_MS) % 2 == 1)) {
    bottomLeft = s.dstNotice;
  }
  u8g2.setFont(FONT_SMALL);
  u8g2.drawStr(ox + X_MARGIN, oy + Y_BOTTOM, bottomLeft);

  // Mirror the panel's own account of itself to serial, so it is visible in a
  // log without anyone having to look at the glass. Keyed on the condition
  // rather than the full string, or the ticking age would emit a line a second
  // forever on an always-on device.
  static const char *lastKind = "";
  const char *kind = statusKind(s);
  if (strcmp(kind, lastKind) != 0) {
    lastKind = kind;
    Serial.printf("ui: status \"%s\"\n", status);
  }

  u8g2.sendBuffer();
}
