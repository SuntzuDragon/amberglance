#pragma once

#include <Arduino.h>
#include <time.h>

#include "net.h"

// Everything that reaches the panel. Owns the U8g2 instance so no other module
// needs to know which display driver or transport is in use.
namespace ui {

// What the screen should show. Sensor readings join this struct in slices 4-5.
struct State {
  bool timeValid;
  struct tm local;
  net::Status net;
  uint32_t secondsSinceSync;
};

void begin();
void splash();
void render(const State &s);

// 0-255, clamped to the readability floor and the burn-in ceiling.
void setBrightness(uint8_t contrast);

}  // namespace ui
