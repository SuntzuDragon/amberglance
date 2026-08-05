#include "dst.h"

#include <Preferences.h>
#include <string.h>

namespace {

Preferences g_prefs;

// -1 means "never observed", which is distinct from "observed as standard
// time". Without that distinction the very first boot would look like a
// transition from DST to standard and announce a jump that never happened.
int8_t g_lastIsDst = -1;
int8_t g_direction = 0;   // +1 sprang forward, -1 fell back
uint32_t g_atEpoch = 0;   // when the transition was detected
char g_notice[24] = {0};
uint32_t g_lastCheckEpoch = 0;

// A real transition is observed between two consecutive loop passes, so the
// clock moves by milliseconds across it. Anything larger is the clock being
// *corrected* — NTP fixing a wrong RTC, say — and a correction that happens to
// cross a DST boundary must not be announced as a transition that occurred.
constexpr uint32_t MAX_NATURAL_STEP_SECONDS = 3600;

constexpr uint32_t NOTICE_SECONDS = 24UL * 60UL * 60UL;
constexpr const char *NS = "amberglance";

void buildNotice(const struct tm &local) {
  char abbrev[8];
  strftime(abbrev, sizeof(abbrev), "%Z", &local);
  snprintf(g_notice, sizeof(g_notice), "DST %+dh now %s", (int)g_direction,
           abbrev);
}

}  // namespace

void dst::begin() {
  g_prefs.begin(NS, false);
  g_lastIsDst = g_prefs.getChar("isdst", -1);
  g_direction = g_prefs.getChar("dstdir", 0);
  g_atEpoch = g_prefs.getUInt("dstat", 0);

  if (g_atEpoch != 0) {
    Serial.printf("dst: stored transition %+dh at epoch %lu\n", (int)g_direction,
                  (unsigned long)g_atEpoch);
  }
}

void dst::update(const struct tm &local, bool timeValid) {
  if (!timeValid) return;

  const int8_t current = (local.tm_isdst > 0) ? 1 : 0;

  const uint32_t now = (uint32_t)time(nullptr);
  const uint32_t delta = (now > g_lastCheckEpoch) ? now - g_lastCheckEpoch
                                                  : g_lastCheckEpoch - now;
  // First check after boot has no previous reading to compare against, and
  // must not be treated as a jump — a transition that happened while the
  // device was unplugged is exactly what we do want to report.
  const bool jumped =
      g_lastCheckEpoch != 0 && delta > MAX_NATURAL_STEP_SECONDS;
  g_lastCheckEpoch = now;

  if (g_lastIsDst < 0) {
    // First time we have ever known the DST state. Record it as the baseline;
    // there is nothing to compare against yet.
    g_lastIsDst = current;
    g_prefs.putChar("isdst", current);
    return;
  }

  if (current == g_lastIsDst) {
    // Rebuild the notice text after a reboot inside the notice window, since
    // it lives in RAM but the transition that caused it is persisted.
    if (g_notice[0] == '\0' && noticeActive()) buildNotice(local);
    return;
  }

  if (jumped) {
    // The clock was corrected across a boundary rather than crossing it.
    // Re-baseline silently so the next genuine transition is still caught.
    Serial.println(F("dst: clock jumped across a boundary, re-baselining"));
    g_lastIsDst = current;
    g_prefs.putChar("isdst", current);
    return;
  }

  g_direction = current ? +1 : -1;
  g_atEpoch = now;
  g_lastIsDst = current;

  g_prefs.putChar("isdst", current);
  g_prefs.putChar("dstdir", g_direction);
  g_prefs.putUInt("dstat", g_atEpoch);

  buildNotice(local);
  Serial.printf("dst: %s -> %s\n",
                g_direction > 0 ? "spring forward" : "fall back", g_notice);
}

bool dst::noticeActive() {
  if (g_atEpoch == 0) return false;
  const uint32_t now = (uint32_t)time(nullptr);
  if (now < g_atEpoch) return false;  // clock stepped backwards; ignore
  return (now - g_atEpoch) < NOTICE_SECONDS;
}

const char *dst::noticeText() { return g_notice; }

int dst::direction() { return g_direction; }
