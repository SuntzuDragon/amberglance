#include "climate.h"

#include <SensirionI2cSht4x.h>
#include <Wire.h>

#include "config.h"

namespace {

SensirionI2cSht4x g_sht;

bool g_available = false;
bool g_seeded = false;
float g_tempF = 0.0f;
float g_humidity = 0.0f;
uint32_t g_lastReadMs = 0;
uint8_t g_errorRun = 0;

// Room temperature moves slowly, and reading harder only heats the die: the
// SHT45's own measurement current is what biases it warm. Five seconds is far
// more often than the room can actually change.
constexpr uint32_t READ_INTERVAL_MS = 5000;

// Tolerate a couple of dropped reads — a NAK during a burst of WiFi activity
// should not blank the display — but stop claiming a value if it really goes.
constexpr uint8_t MAX_ERROR_RUN = 3;

// The SHT45 resolves finer than the last displayed digit, so raw readings
// wobble ±0.2F and the tenths place flickers. A light exponential average
// settles that without adding perceptible lag at this update rate.
constexpr float SMOOTHING = 0.25f;

void logReading() {
  Serial.printf("climate: %.1f F  %.0f%% RH\n", g_tempF, g_humidity);
}

}  // namespace

void climate::begin() {
  // Wire is already up from the boot I2C scan.
  g_sht.begin(Wire, I2C_ADDR_SHT45);

  uint32_t serial = 0;
  if (g_sht.serialNumber(serial) == 0) {
    Serial.printf("climate: SHT45 at 0x%02X, serial %lu\n", I2C_ADDR_SHT45,
                  (unsigned long)serial);
  } else {
    Serial.println(F("climate: SHT45 did not answer (will keep retrying)"));
  }
}

void climate::poll() {
  if (g_lastReadMs != 0 && millis() - g_lastReadMs < READ_INTERVAL_MS) return;
  g_lastReadMs = millis();

  float tempC = 0.0f, humidity = 0.0f;
  const int16_t err = g_sht.measureHighPrecision(tempC, humidity);

  if (err != 0) {
    if (g_errorRun < MAX_ERROR_RUN) g_errorRun++;
    if (g_errorRun == MAX_ERROR_RUN && g_available) {
      g_available = false;
      g_seeded = false;
      Serial.printf("climate: sensor stopped answering (error %d)\n", err);
    }
    return;
  }

  if (g_errorRun >= MAX_ERROR_RUN) Serial.println(F("climate: sensor back"));
  g_errorRun = 0;

  const float tempF = tempC * 9.0f / 5.0f + 32.0f;

  if (!g_seeded) {
    g_tempF = tempF;
    g_humidity = humidity;
    g_seeded = true;
    g_available = true;
    logReading();
    return;
  }

  g_tempF += SMOOTHING * (tempF - g_tempF);
  g_humidity += SMOOTHING * (humidity - g_humidity);
  g_available = true;

  // Slow enough to be useful in a log without drowning it.
  static uint32_t lastLogMs = 0;
  if (millis() - lastLogMs >= 60000) {
    lastLogMs = millis();
    logReading();
  }
}

bool climate::available() { return g_available; }
float climate::temperatureF() { return g_tempF; }
float climate::humidityPct() { return g_humidity; }
