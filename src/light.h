#pragma once

#include <Arduino.h>

// Ambient light from the BH1750, and the display brightness derived from it.
//
// The sensor runs in continuous mode, so poll() only reads a register rather
// than waiting on a conversion — unlike the SHT45 it costs nothing to call
// often.
namespace light {

void begin();
void poll();

// False until a reading has succeeded, and again if the sensor stops
// answering, so brightness can fall back to a sane fixed value.
bool available();

float lux();

// Smoothed lux mapped onto an abstract 0-255 brightness level. Falls back to
// OLED_BRIGHTNESS_DEFAULT when the sensor is unavailable.
uint8_t recommendedLevel();

}  // namespace light
