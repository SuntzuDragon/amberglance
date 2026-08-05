#include "net.h"

#include <WiFi.h>
#include <esp_sntp.h>

#include "config.h"
#include "secrets.h"

namespace {

net::Status g_status = net::Status::Offline;
uint32_t g_attemptStartedMs = 0;
uint32_t g_lastSyncMs = 0;
bool g_everSynced = false;

// How long a single association attempt gets before we tear it down and start
// over. Repeated WiFi.begin() on a stuck association tends to go nowhere.
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t RETRY_DELAY_MS = 10000;

// Any real wall-clock reading is far past this (2023-11-14). An unsynced ESP32
// reports an epoch near zero, which is how we tell "no time yet" from "time".
constexpr time_t PLAUSIBLE_EPOCH = 1700000000;

void startAttempt() {
  WiFi.disconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  g_attemptStartedMs = millis();
  g_status = net::Status::Connecting;
}

// SNTP calls this on every successful sync, including the periodic re-syncs it
// runs on its own (hourly by default), which is what makes secondsSinceSync()
// a live staleness measure rather than time-since-boot.
void onNtpSync(struct timeval *) {
  g_lastSyncMs = millis();

  if (!g_everSynced) {
    // Report the servers actually in use. DHCP option 42 can replace slot 0 at
    // runtime (CONFIG_LWIP_DHCP_GET_NTP_SRV is compiled in), so the configured
    // list is not necessarily the list that answered.
    for (uint8_t i = 0; i < 3; i++) {
      const char *name = sntp_getservername(i);
      const ip_addr_t *ip = sntp_getserver(i);
      const bool haveIp = ip && !ip_addr_isany(ip);
      if (!name && !haveIp) continue;
      Serial.printf("ntp: server[%u] %s%s%s\n", i, name ? name : "",
                    (name && haveIp) ? " -> " : "",
                    haveIp ? ipaddr_ntoa(ip) : "");
    }
  }

  g_everSynced = true;
  Serial.println(F("ntp: time synced"));
}

}  // namespace

void net::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  sntp_set_time_sync_notification_cb(onNtpSync);

  // Configure SNTP up front. It retries by itself once a link appears, so
  // there is nothing to re-arm when WiFi comes and goes.
  configTzTime(TZ_MOUNTAIN, NTP_SERVER_1, NTP_SERVER_2);

  Serial.printf("wifi: connecting to \"%s\"\n", WIFI_SSID);
  startAttempt();
}

void net::poll() {
  const bool linkUp = (WiFi.status() == WL_CONNECTED);
  const uint32_t now = millis();

  if (linkUp) {
    if (g_status != Status::Online) {
      g_status = Status::Online;
      Serial.print(F("wifi: online, ip "));
      Serial.println(WiFi.localIP());
    }
    return;
  }

  if (g_status == Status::Online) {
    Serial.println(F("wifi: link lost"));
    g_status = Status::Offline;
    g_attemptStartedMs = now;  // start the retry delay from the drop
    return;
  }

  const uint32_t elapsed = now - g_attemptStartedMs;
  if (g_status == Status::Connecting && elapsed >= CONNECT_TIMEOUT_MS) {
    Serial.println(F("wifi: attempt timed out"));
    g_status = Status::Offline;
    g_attemptStartedMs = now;
  } else if (g_status == Status::Offline && elapsed >= RETRY_DELAY_MS) {
    startAttempt();
  }
}

net::Status net::status() { return g_status; }

const char *net::statusStr() {
  switch (g_status) {
    case Status::Online:     return "online";
    case Status::Connecting: return "connecting";
    default:                 return "offline";
  }
}

bool net::timeValid() { return time(nullptr) > PLAUSIBLE_EPOCH; }

uint32_t net::secondsSinceSync() {
  if (!g_everSynced) return 0;
  return (millis() - g_lastSyncMs) / 1000;
}
