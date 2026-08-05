#pragma once

#include <Arduino.h>
#include <time.h>

#include "net.h"

// Everything that reaches the panel. Owns the U8g2 instance so no other module
// needs to know which display driver or transport is in use.
namespace ui {

// What the screen should show. The `have*` flags exist so a missing sensor
// renders as a placeholder rather than a plausible-looking zero.
struct State {
  bool timeValid;
  struct tm local;
  net::Status net;
  uint32_t secondsSinceSync;

  bool haveClimate;
  float tempF;
  float humidityPct;

  bool haveLight;
  float lux;

  // Non-null while a daylight-saving transition is worth announcing, with
  // the direction it went: +1 lost an hour, -1 gained one.
  const char *dstNotice;
  int dstDirection;

  // Replaces the bottom-left status line when set. Used by the calibration
  // build to show the level being judged.
  const char *statusOverride;
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

// Abstract 0-255 brightness, mapped onto both of the panel's brightness
// controls and clamped to the configured floor and ceiling.
void setBrightness(uint8_t level);

}  // namespace ui
