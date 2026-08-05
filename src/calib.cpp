#include "calib.h"

#ifdef AMBER_CALIBRATE

#include <Preferences.h>

#include "config.h"
#include "light.h"

namespace {

// Spaced wider at the top because perception is logarithmic: single steps near
// the bottom matter, the same steps near the top are invisible.
const uint8_t LEVELS[] = {0,  8,   16,  24,  32,  48, 64,
                          96, 128, 160, 192, 224, 255};
constexpr uint8_t LEVEL_COUNT = sizeof(LEVELS) / sizeof(LEVELS[0]);

struct Point {
  float lux;
  uint8_t level;
};
constexpr uint8_t MAX_POINTS = 24;

Preferences g_prefs;
Point g_points[MAX_POINTS];
uint8_t g_count = 0;
uint8_t g_index = 0;

char g_status[24] = {0};
uint32_t g_savedFlashUntilMs = 0;

// Button is wired to ground with the internal pull-up, so pressed reads LOW.
constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t LONG_PRESS_MS = 800;
constexpr uint32_t SAVED_FLASH_MS = 1500;

bool g_lastRaw = true;
uint32_t g_lastChangeMs = 0;
bool g_pressed = false;
uint32_t g_pressStartMs = 0;
bool g_longFired = false;

void dumpPoints() {
  Serial.printf("calib: %u saved point(s)\n", g_count);
  for (uint8_t i = 0; i < g_count; i++) {
    Serial.printf("  calib-point %u: %.1f lx -> level %u\n", i + 1,
                  g_points[i].lux, g_points[i].level);
  }
}

void load() {
  g_count = g_prefs.getUChar("calN", 0);
  if (g_count > MAX_POINTS) g_count = 0;
  if (g_count) {
    g_prefs.getBytes("calP", g_points, sizeof(Point) * g_count);
  }
}

void savePoint() {
  if (g_count >= MAX_POINTS) {
    Serial.println(F("calib: point store full"));
    return;
  }

  g_points[g_count].lux = light::lux();
  g_points[g_count].level = LEVELS[g_index];
  g_count++;

  g_prefs.putUChar("calN", g_count);
  g_prefs.putBytes("calP", g_points, sizeof(Point) * g_count);

  Serial.printf("calib: saved %.1f lx -> level %u  (%u total)\n",
                g_points[g_count - 1].lux, g_points[g_count - 1].level,
                g_count);
  g_savedFlashUntilMs = millis() + SAVED_FLASH_MS;
}

}  // namespace

void calib::begin() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  g_prefs.begin("amberglance-cal", false);

  // Holding the button through boot wipes the store, so a bad run can be
  // discarded without a reflash.
  delay(50);
  if (digitalRead(PIN_BUTTON) == LOW) {
    g_prefs.clear();
    g_count = 0;
    Serial.println(F("calib: button held at boot - cleared saved points"));
  } else {
    load();
  }

  dumpPoints();
  Serial.println(F("calib: short press = brightness, long press = save point"));
}

void calib::poll() {
  const bool raw = digitalRead(PIN_BUTTON);

  if (raw != g_lastRaw) {
    g_lastRaw = raw;
    g_lastChangeMs = millis();
    return;
  }
  if (millis() - g_lastChangeMs < DEBOUNCE_MS) return;

  const bool down = (raw == LOW);

  if (down && !g_pressed) {
    g_pressed = true;
    g_pressStartMs = millis();
    g_longFired = false;
  } else if (down && g_pressed && !g_longFired &&
             millis() - g_pressStartMs >= LONG_PRESS_MS) {
    // Fire on the way down rather than on release, so the save is felt at the
    // moment it happens.
    g_longFired = true;
    savePoint();
  } else if (!down && g_pressed) {
    g_pressed = false;
    if (!g_longFired) {
      g_index = (g_index + 1) % LEVEL_COUNT;
      Serial.printf("calib: level %u  (%.1f lx)\n", LEVELS[g_index],
                    light::lux());
    }
  }
}

uint8_t calib::level() { return LEVELS[g_index]; }

const char *calib::statusText() {
  if (millis() < g_savedFlashUntilMs) {
    snprintf(g_status, sizeof(g_status), "SAVED #%u", g_count);
  } else {
    snprintf(g_status, sizeof(g_status), "LV %u  #%u", LEVELS[g_index],
             g_count);
  }
  return g_status;
}

#endif  // AMBER_CALIBRATE
