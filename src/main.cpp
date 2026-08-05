// amberglance — slice 2: WiFi + NTP.
//
// The clock keeps running regardless of the network: nothing in the loop
// blocks on WiFi, and the display shows an honest placeholder rather than a
// fabricated time until NTP has actually landed. Slice 3 adds the DS3231, at
// which point the RTC covers the gap this placeholder currently fills.

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "config.h"
#include "dst.h"
#include "net.h"
#include "ui.h"

// Sensors arrive in later slices; scanning now costs nothing and confirms the
// bus is wired before anything depends on it.
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

#ifdef AMBER_FAKE_SENSORS
// Placeholder readings so the layout can be judged before the hardware exists.
// They drift on purpose: a fixed value would hide the width changes that break
// a right-aligned layout. Drop -DAMBER_FAKE_SENSORS once the sensors are real.
static void fillFakeSensors(ui::State &s, float &lux, bool &haveLight) {
  const float t = millis() / 1000.0f;
  s.haveClimate = true;
  s.tempF = 72.0f + 6.0f * sinf(t / 41.0f);         // 66 - 78 F
  s.humidityPct = 42.0f + 12.0f * sinf(t / 67.0f);  // 30 - 54 %
  haveLight = true;
  lux = 700.0f + 698.0f * sinf(t / 29.0f);          // 2 - 1398 lx
}
#endif

void setup() {
  Serial.begin(115200);
  const uint32_t serialDeadline = millis() + 1500;
  while (!Serial && millis() < serialDeadline) delay(10);

  Serial.println();
  Serial.println(F("amberglance - slice 2 (wifi + ntp)"));
#ifdef AMBER_FAKE_SENSORS
  Serial.println(F("sensors: FAKE placeholder data"));
#endif

  ui::begin();
  ui::splash();

  ui::debugLayout();

  scanI2C();
  dst::begin();
  net::begin();
}

void loop() {
  net::poll();

  ui::State s{};
  s.net = net::status();
  s.timeValid = net::timeValid();
  s.secondsSinceSync = net::secondsSinceSync();
  if (s.timeValid) {
    const time_t now = time(nullptr);
    localtime_r(&now, &s.local);
  }

  dst::update(s.local, s.timeValid);
  s.dstNotice = dst::noticeActive() ? dst::noticeText() : nullptr;

  float lux = 0.0f;
  bool haveLight = false;
#ifdef AMBER_FAKE_SENSORS
  fillFakeSensors(s, lux, haveLight);
#endif

  // Ambient light stays off the glass by choice — it drives brightness and is
  // logged here for tuning the curve in slice 5.
  static uint32_t lastLuxLog = 0;
  if (haveLight && millis() - lastLuxLog >= 30000) {
    lastLuxLog = millis();
    Serial.printf("light: %.0f lx\n", lux);
  }

  // Redraw only when something visible changes. At one update per second a
  // full software-SPI refresh is ~6% duty; repainting every pass would be
  // pointless work and would fight the burn-in shift's timing.
  static int lastSec = -1;
  static bool lastValid = false;
  static net::Status lastNet = net::Status::Offline;
  static uint32_t lastShiftStep = UINT32_MAX;
  static uint32_t lastNoticePhase = UINT32_MAX;

  if (s.timeValid && !lastValid) {
    // One-shot, so the TZ/DST result is visible in the boot log instead of
    // only on the glass. %Z resolves to MDT or MST via the TZ string.
    char stamp[48];
    strftime(stamp, sizeof(stamp), "%a %Y-%m-%d %H:%M:%S %Z", &s.local);
    Serial.printf("time: %s\n", stamp);
  }

  const uint32_t shiftStep = millis() / SHIFT_INTERVAL_MS;
  const uint32_t noticePhase = s.dstNotice ? (millis() / 4000) : 0;
  const bool changed = s.local.tm_sec != lastSec || s.timeValid != lastValid ||
                       s.net != lastNet || shiftStep != lastShiftStep ||
                       noticePhase != lastNoticePhase;

  if (changed) {
    lastSec = s.local.tm_sec;
    lastValid = s.timeValid;
    lastNet = s.net;
    lastShiftStep = shiftStep;
    lastNoticePhase = noticePhase;
    ui::render(s);
  }

  delay(50);
}
