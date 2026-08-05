#pragma once

#include <Arduino.h>
#include <time.h>

// WiFi + NTP, entirely non-blocking.
//
// The clock is the product; the network is an accessory to it. Nothing here is
// allowed to stall the render loop, so connection and sync are polled rather
// than waited on, and every accessor is safe to call before anything has
// connected. Once the DS3231 lands in slice 3 this module keeps its shape —
// it will correct the RTC instead of being the only source of time.
namespace net {

enum class Status {
  Offline,     // no link, not currently trying
  Connecting,  // association in progress
  Online,      // associated, IP acquired
};

void begin();
void poll();

Status status();
const char *statusStr();

// True once the system clock holds a plausible wall-clock time. Before this,
// time(nullptr) returns an epoch near zero and must not be shown to the user.
bool timeValid();

// Seconds since the last successful NTP sync. Meaningless unless timeValid().
uint32_t secondsSinceSync();

}  // namespace net
