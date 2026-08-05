#pragma once

#include <Arduino.h>

// Brightness calibration harness, built around a temporary pushbutton.
//
// The point is to collect real judgements instead of guessed curve anchors:
// carry the device between rooms, set what actually looks right, and record the
// (lux, level) pair. Those pairs are then fitted into the auto-dim curve.
//
// Compiled only under -DAMBER_CALIBRATE. Once the curve is set, drop the flag
// and the button and none of this ships.
#ifdef AMBER_CALIBRATE

namespace calib {

void begin();

// Must be called often — the render loop sleeps about a second between frames,
// so this is driven from inside that sleep to keep the button responsive.
void poll();

// Manually selected brightness level, 0-255.
uint8_t level();

// Line for the bottom-left of the display: current level, saved count, and
// transient confirmation after a save.
const char *statusText();

}  // namespace calib

#endif  // AMBER_CALIBRATE
