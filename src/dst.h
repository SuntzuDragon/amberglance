#pragma once

#include <Arduino.h>
#include <time.h>

// Watches for daylight-saving transitions and reports them for a while after
// they happen, so a clock that silently jumped an hour can account for itself.
//
// The observed DST state is persisted, so a transition is still detected if it
// happened while the device was rebooting or unplugged — the comparison is
// against the last state we ever saw, not the last state since power-on.
namespace dst {

void begin();

// Call whenever local time is known. Cheap; safe to call every render.
void update(const struct tm &local, bool timeValid);

// True for NOTICE_HOURS after a detected transition.
bool noticeActive();

// e.g. "DST +1h now MDT" or "DST -1h now MST". Empty when no notice.
const char *noticeText();

// +1 sprang forward (an hour lost), -1 fell back (an hour gained), 0 if no
// transition has been observed.
int direction();

}  // namespace dst
