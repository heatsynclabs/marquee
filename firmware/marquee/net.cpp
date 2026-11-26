#include "net.h"
#include "config.h"
#include "settings.h"
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

// Wi-Fi never blocks the loop. The station keeps trying for as long as the
// panel is powered; the setup portal only opens on a board with no network
// saved, or one that has not reached its network for PORTAL_AFTER_MS since
// boot. Even then the station keeps trying in the background, and the portal
// closes itself once the network is back.

namespace {
bool     portal = false;      // setup AP is up
bool     configured = false;  // a network is saved
bool     everOnline = false;
bool     servicesUp = false;  // mDNS + OTA started
bool     wasOnline = false;
uint32_t bootAt = 0;
uint32_t lostAt = 0;          // when the station last went down, 0 while up
uint32_t lastKick = 0;
uint32_t onlineAt = 0;
uint32_t reconnectCount = 0;
DNSServer dns;

void openPortal() {
  if (portal) return;
  portal = true;
  WiFi.mode(configured ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP("marquee-setup");
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("setup portal at http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void closePortal() {
  if (!portal) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portal = false;
  Serial.println("setup portal closed");
}

void startServices() {
  if (servicesUp) {
    MDNS.notifyAPChange();
    return;
  }
  servicesUp = true;
  if (MDNS.begin(settings.host)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    MDNS.addService("marquee", "tcp", HTTP_PORT);
  }
  ArduinoOTA.setHostname(settings.host);
  ArduinoOTA.begin(false);  // mDNS is already running
}
}  // namespace

void Net::begin() {
  bootAt = millis();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // Modem sleep adds hundreds of ms of latency and drops multicast (mDNS).
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.hostname(settings.host);
  WiFi.setAutoReconnect(true);

  Net::applyTime();

  configured = settings.ssid[0] != 0;
  if (!configured) {
    openPortal();
    return;
  }
  Serial.printf("connecting to %s\n", settings.ssid);
  WiFi.begin(settings.ssid, settings.pass);
  lastKick = millis();
}

void Net::applyTime() {
  // The TZ string decides local time for the clock; NTP only supplies UTC.
  configTime(settings.tz, "pool.ntp.org", "time.nist.gov", "time.google.com");
}

void Net::loop() {
  if (portal) dns.processNextRequest();
  if (!configured) return;

  const uint32_t now = millis();
  const bool up = WiFi.status() == WL_CONNECTED;

  if (up && !wasOnline) {
    wasOnline = true;
    everOnline = true;
    lostAt = 0;
    onlineAt = now;
    Serial.printf("online: http://%s.local/ (%s)\n", settings.host,
                  WiFi.localIP().toString().c_str());
    startServices();
  } else if (!up && wasOnline) {
    wasOnline = false;
    lostAt = now ? now : 1;
    reconnectCount++;
    Serial.println("wifi lost");
  }

  if (up) {
    MDNS.update();
    ArduinoOTA.handle();
    // Leave the portal up a minute in case someone is on it, then close it.
    if (portal && now - onlineAt > 60000 && WiFi.softAPgetStationNum() == 0) closePortal();
    return;
  }

  // Down. Kick the station every 20 s; the SDK's own auto-reconnect gives up
  // quietly if the access point vanished and came back.
  if (now - lastKick > 20000) {
    lastKick = now;
    WiFi.begin(settings.ssid, settings.pass);
  }

  if (!everOnline && !portal && now - bootAt > PORTAL_AFTER_MS) openPortal();

  if (everOnline && lostAt && now - lostAt > WIFI_LOST_REBOOT_MS && !portal) {
    Serial.println("wifi gone too long; restarting");
    delay(100);
    ESP.restart();
  }
}

bool Net::apMode() { return portal && !online(); }
bool Net::portalOpen() { return portal; }
bool Net::online() { return configured && WiFi.status() == WL_CONNECTED; }
uint32_t Net::reconnects() { return reconnectCount; }
String Net::ip() { return online() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
