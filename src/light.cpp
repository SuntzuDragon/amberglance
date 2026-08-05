#include "light.h"

#include <BH1750.h>
#include <Wire.h>
#include <math.h>

#include "config.h"

namespace {

BH1750 g_meter(I2C_ADDR_BH1750);

bool g_available = false;
bool g_seeded = false;
float g_lux = 0.0f;
uint8_t g_errorRun = 0;

constexpr uint8_t MAX_ERROR_RUN = 3;

// Brightness should drift, not snap. A cloud, a passing shadow or someone
// walking between the lamp and the sensor should not visibly step the display,
// so the average is deliberately slow — a few seconds to settle after a light
// is switched, which reads as the room changing rather than the clock reacting.
constexpr float SMOOTHING = 0.2f;

}  // namespace

void light::begin() {
  // Wire is already up from the boot I2C scan.
  if (g_meter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, I2C_ADDR_BH1750, &Wire)) {
    Serial.printf("light: BH1750 at 0x%02X, continuous high-res\n",
                  I2C_ADDR_BH1750);
  } else {
    Serial.println(F("light: BH1750 did not answer (will keep retrying)"));
  }
}

void light::poll() {
  // The register holds nothing meaningful for ~180ms after the mode is set,
  // and the first reading seeds the average outright, so gate on the sensor
  // actually having converted rather than trusting whatever is latched.
  if (!g_meter.measurementReady(false)) return;

  const float raw = g_meter.readLightLevel();

  if (raw < 0.0f) {
    if (g_errorRun < MAX_ERROR_RUN) g_errorRun++;
    if (g_errorRun == MAX_ERROR_RUN && g_available) {
      g_available = false;
      g_seeded = false;
      Serial.println(F("light: sensor stopped answering"));
    }
    return;
  }

  if (g_errorRun >= MAX_ERROR_RUN) Serial.println(F("light: sensor back"));
  g_errorRun = 0;

  if (!g_seeded) {
    // Adopt the first reading outright, so the display is not visibly ramping
    // up from zero every time it boots.
    g_lux = raw;
    g_seeded = true;
  } else {
    g_lux += SMOOTHING * (raw - g_lux);
  }
  g_available = true;
}

bool light::available() { return g_available; }
float light::lux() { return g_lux; }

uint8_t light::recommendedLevel() {
  uint16_t level;

  if (!g_available) {
    level = OLED_BRIGHTNESS_DEFAULT;
  } else if (g_lux <= LUX_DARK) {
    level = 0;
  } else if (g_lux >= LUX_BRIGHT) {
    level = 255;
  } else {
    // Both halves of this are logarithmic in perception, so the mapping is
    // done in log space: a linear lux-to-level ramp would spend almost all of
    // its range on bright rooms and leave the dim end — where this display
    // actually spends most of its life — crammed into a few steps.
    const float t = log10f(g_lux / LUX_DARK) / log10f(LUX_BRIGHT / LUX_DARK);
    level = (uint16_t)(t * 255.0f + 0.5f);
  }

  if (level < AUTO_DIM_LEVEL_MIN) level = AUTO_DIM_LEVEL_MIN;
  if (level > AUTO_DIM_LEVEL_MAX) level = AUTO_DIM_LEVEL_MAX;
  return (uint8_t)level;
}
