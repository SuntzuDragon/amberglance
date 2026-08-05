#include "ui.h"

#include <U8g2lib.h>
#include <string.h>

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

// Baselines within the LAYOUT_W x LAYOUT_H region, before the burn-in shift.
constexpr int Y_TIME = 46;
constexpr int Y_DATE = 15;
constexpr int Y_STATUS = 59;
constexpr int X_MARGIN = 2;

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
  u8g2.setFont(u8g2_font_logisoso20_tr);
  const char *name = "amberglance";
  u8g2.drawStr((256 - u8g2.getStrWidth(name)) / 2, 40, name);
  u8g2.sendBuffer();
}

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
    snprintf(hhmm, sizeof(hhmm), "%d:%02d", h, s.local.tm_min);
  }

  u8g2.setFont(u8g2_font_logisoso38_tr);
  const int timeX = ox + X_MARGIN;
  u8g2.drawStr(timeX, oy + Y_TIME, hhmm);
  const int afterTime = timeX + u8g2.getStrWidth(hhmm) + 5;

  if (s.timeValid) {
    char ss[4];
    snprintf(ss, sizeof(ss), "%02d", s.local.tm_sec);
    u8g2.setFont(u8g2_font_logisoso20_tr);
    u8g2.drawStr(afterTime, oy + Y_TIME, ss);

    if (!CLOCK_24_HOUR) {
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(afterTime, oy + Y_TIME - 23,
                   s.local.tm_hour < 12 ? "AM" : "PM");
    }
  }

  // --- Date, top right ---
  if (s.timeValid) {
    char date[16];
    snprintf(date, sizeof(date), "%s %s %d", WEEKDAYS[s.local.tm_wday],
             MONTHS[s.local.tm_mon], s.local.tm_mday);
    u8g2.setFont(u8g2_font_helvB12_tr);
    drawRight(ox, oy + Y_DATE, date);
  }

  // --- Status, bottom right ---
  char status[20];
  statusText(s, status, sizeof(status));
  u8g2.setFont(u8g2_font_6x10_tf);
  drawRight(ox, oy + Y_STATUS, status);

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
