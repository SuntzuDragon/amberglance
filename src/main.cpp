// amberglance — indoor clock and dashboard.
//
// The clock keeps running regardless of the network: nothing in the loop
// blocks on WiFi, and the display shows an honest placeholder rather than a
// fabricated time until NTP has actually landed. The DS3231 will later cover
// the gap that placeholder currently fills.
//
// Ordering inside loop() is deliberate. The frame is paced to land on the
// second boundary, so anything slow — sensor round-trips especially — runs
// after the frame is sent, where there is a second of slack, never in the
// lead-in window before it.

#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>

#include "climate.h"
#include "config.h"
#include "dst.h"
#include "light.h"
#include "net.h"
#include "ui.h"

// Reports what is actually on the bus at boot. Cheap, and it has caught every
// wiring question so far before any code depended on the answer.
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

void setup() {
  Serial.begin(115200);
  const uint32_t serialDeadline = millis() + 1500;
  while (!Serial && millis() < serialDeadline) delay(10);

  Serial.println();
  Serial.println(F("amberglance - slice 5 (auto-dimming)"));

  ui::begin();
  ui::splash();

  ui::debugLayout();

  scanI2C();
  climate::begin();
  light::begin();
  dst::begin();
  net::begin();
}

// The frame takes real time to draw and clock out, so the second that belongs
// on the glass is the one that will be current when the transfer *finishes*,
// not the one current as drawing begins. Measured from the previous frame, so
// the lead self-corrects as drawing cost changes with content.
static uint32_t g_renderUs = 15000;

// The next wall-clock second to display, scheduled on an absolute timeline.
//
// An earlier version inferred ticks by comparing the current second against
// the last one drawn, and slept toward `boundary - renderUs`. Those are two
// independent thresholds straddling the same instant, and waking a hair early
// put them on opposite sides: the current second still read as the previous
// one, so nothing was drawn, and the sleep then rounded forward to the
// *following* boundary. The result was an occasional two-second jump. Counting
// ticks explicitly cannot drift that way — each second is claimed before it
// arrives.
static time_t g_nextTick = 0;

void loop() {
  net::poll();

  const bool timeValid = net::timeValid();
  time_t showEpoch = 0;

  if (timeValid) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    // Establish the schedule on the first valid time, and re-establish it if
    // we ever fall behind — a long stall, or NTP stepping the clock.
    if (g_nextTick == 0 || tv.tv_sec >= g_nextTick) {
#ifdef AMBER_LAYOUT_DEBUG
      if (g_nextTick != 0) {
        Serial.printf("tick: behind by %llds, resyncing\n",
                      (long long)(tv.tv_sec - g_nextTick + 1));
      }
#endif
      g_nextTick = tv.tv_sec + 1;
    }

    // Sleep until one render-time before the target second, so the pixels
    // change as it arrives.
    const int64_t nowUs = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    const int64_t targetUs =
        (int64_t)g_nextTick * 1000000 - (int64_t)g_renderUs;
    const int64_t waitUs = targetUs - nowUs;
    if (waitUs > 0) {
      delay((uint32_t)(waitUs / 1000));
      delayMicroseconds((uint32_t)(waitUs % 1000));
    }

    showEpoch = g_nextTick;
    g_nextTick++;
  } else {
    // No time to pace against yet. Re-establish the schedule when it arrives.
    g_nextTick = 0;
    delay(500);
  }

  ui::State s{};
  s.net = net::status();
  s.timeValid = timeValid;
  s.secondsSinceSync = net::secondsSinceSync();
  if (timeValid) localtime_r(&showEpoch, &s.local);

  dst::update(s.local, s.timeValid);
  s.dstNotice = dst::noticeActive() ? dst::noticeText() : nullptr;
  s.dstDirection = dst::direction();

  s.haveClimate = climate::available();
  s.tempF = climate::temperatureF();
  s.humidityPct = climate::humidityPct();

  s.haveLight = light::available();
  s.lux = light::lux();

  // Only push a contrast change when it actually moves, so the panel is not
  // handed the same value every second.
  static uint16_t lastLevel = 0xFFFF;
  const uint8_t level = light::recommendedLevel();
  if (level != lastLevel) {
    lastLevel = level;
    ui::setBrightness(level);

    // A room going light or dark walks the curve a step or two a second, so
    // logging every one would bury everything else. Report periodically; the
    // live value is on the glass.
    static uint32_t lastLogMs = 0;
    if (s.haveLight && millis() - lastLogMs >= 15000) {
      lastLogMs = millis();
      Serial.printf("light: %.0f lx -> level %u\n", s.lux, level);
    }
  }

  static bool lastValid = false;
  if (timeValid && !lastValid) {
    // One-shot, so the TZ/DST result is visible in the boot log instead of
    // only on the glass. %Z resolves to MDT or MST via the TZ string.
    char stamp[48];
    strftime(stamp, sizeof(stamp), "%a %Y-%m-%d %H:%M:%S %Z", &s.local);
    Serial.printf("time: %s\n", stamp);
  }
  lastValid = timeValid;

  // Exactly one frame per tick, so there is nothing to suppress: the seconds
  // change every time, and at ~13ms a frame this is about 1% duty.
  const uint32_t t0 = micros();
  ui::render(s);
  g_renderUs = micros() - t0;

#ifdef AMBER_LAYOUT_DEBUG
  // Sample the landing before anything else runs, or this measures the work
  // that follows rather than the frame.
  struct timeval landed;
  gettimeofday(&landed, nullptr);
#endif

  // Sensor round-trips go here, after the frame is on the glass: the SHT45
  // conversion blocks ~10ms, which is fine against a second of slack but would
  // push the next frame late if it ran in the lead-in window.
  climate::poll();
  light::poll();

#ifdef AMBER_LAYOUT_DEBUG
  if (timeValid) {
    // How far the completed frame landed from the second boundary. Negative
    // means it finished early, which is the safe side.
    static uint8_t tick = 0;
    if ((tick++ % 10) == 0) {
      long off = landed.tv_usec;
      if (off > 500000) off -= 1000000;
      Serial.printf("tick: render %luus, landed %+ldus from the second\n",
                    (unsigned long)g_renderUs, off);
    }
  }
#endif
}
