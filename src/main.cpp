// amberglance — slice 2: WiFi + NTP.
//
// The clock keeps running regardless of the network: nothing in the loop
// blocks on WiFi, and the display shows an honest placeholder rather than a
// fabricated time until NTP has actually landed. Slice 3 adds the DS3231, at
// which point the RTC covers the gap this placeholder currently fills.

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <sys/time.h>

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

// The frame takes real time to draw and clock out, so the second that belongs
// on the glass is the one that will be current when the transfer finishes —
// not the one current as drawing begins. Measured from the previous frame, so
// it self-corrects as drawing cost changes with content.
static uint32_t g_renderUs = 15000;

static void loadDisplayTime(ui::State &s) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t shown = tv.tv_sec;
  if ((uint32_t)tv.tv_usec + g_renderUs >= 1000000) shown++;
  localtime_r(&shown, &s.local);
}

// Start drawing g_renderUs before the next second so the pixels change on the
// boundary, rather than up to a poll interval after it. Without this the loop
// period is (render + delay), so the phase at which the rollover is noticed
// drifts — and drifts by a different amount depending on what was drawn.
static void paceToSecondBoundary() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  int32_t us = 1000000 - (int32_t)tv.tv_usec - (int32_t)g_renderUs;
  if (us < 2000) us += 1000000;
  delay(us / 1000);
  delayMicroseconds(us % 1000);
}

void loop() {
  net::poll();

  ui::State s{};
  s.net = net::status();
  s.timeValid = net::timeValid();
  s.secondsSinceSync = net::secondsSinceSync();
  if (s.timeValid) {
    loadDisplayTime(s);
  }

  dst::update(s.local, s.timeValid);
  s.dstNotice = dst::noticeActive() ? dst::noticeText() : nullptr;
  s.dstDirection = dst::direction();

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

    const uint32_t t0 = micros();
    ui::render(s);
    g_renderUs = micros() - t0;

#ifdef AMBER_LAYOUT_DEBUG
    // How far the completed frame landed from the second boundary. Negative
    // means it finished early, which is the safe side.
    static uint8_t tick = 0;
    if ((tick++ % 10) == 0) {
      struct timeval done;
      gettimeofday(&done, nullptr);
      long off = done.tv_usec;
      if (off > 500000) off -= 1000000;
      Serial.printf("tick: render %luus, landed %+ldus from the second\n",
                    (unsigned long)g_renderUs, off);
    }
#endif
  }

  if (s.timeValid) paceToSecondBoundary();
  else delay(50);
}
