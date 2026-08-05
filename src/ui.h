#pragma once

#include <Arduino.h>
#include <time.h>

#include "net.h"

// Everything that reaches the panel. Owns the U8g2 instance so no other module
// needs to know which display driver or transport is in use.
namespace ui {

// What the screen should show. The `have*` flags exist so a missing sensor
// renders as a placeholder rather than a plausible-looking zero.
//
// Ambient light is deliberately absent: it drives brightness and is logged to
// serial for tuning, but never occupies space on the glass.
struct State {
  bool timeValid;
  struct tm local;
  net::Status net;
  uint32_t secondsSinceSync;

  bool haveClimate;
  float tempF;
  float humidityPct;

  // Non-null while a daylight-saving transition is worth announcing.
  const char *dstNotice;
};

void begin();
void splash();
void render(const State &s);

#ifdef AMBER_LAYOUT_DEBUG
// Measures worst-case string widths and reports the gaps between layout
// blocks, so a right-aligned rail can be proven not to collide with the time
// without anyone squinting at the panel. Worth re-running whenever the layout
// or a font changes.
void debugLayout();
#endif

// 0-255, clamped to the readability floor and the burn-in ceiling.
void setBrightness(uint8_t contrast);

}  // namespace ui
