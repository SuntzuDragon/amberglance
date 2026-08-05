#pragma once

#include <Arduino.h>

// Indoor temperature and humidity from the SHT45 on the shared I2C bus.
//
// Polling is non-blocking in the sense that it does the sensor round-trip on a
// slow interval rather than every pass. The round-trip itself blocks for the
// conversion (~10ms at high precision), so poll() should be called after the
// frame has been sent, where there is a second of slack, rather than in the
// window before it.
namespace climate {

void begin();
void poll();

// False until a reading has succeeded, and again if the sensor stops
// answering, so the UI can show a placeholder rather than a stale number.
bool available();

float temperatureF();
float humidityPct();

}  // namespace climate
