#include "net.h"
#include "config.h"
#include "settings.h"
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

namespace {
bool ap = false;
DNSServer dns;
uint32_t lastCheck = 0;

void startPortal() {
  ap = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("marquee-setup");
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("setup portal at http://%s/\n", WiFi.softAPIP().toString().c_str());
}
}  // namespace

void Net::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(settings.host);
  WiFi.setAutoReconnect(true);

  if (!settings.ssid[0]) {
    startPortal();
    return;
  }

  Serial.printf("connecting to %s", settings.ssid);
  WiFi.begin(settings.ssid, settings.pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("no connection; starting setup portal");
    startPortal();
    return;
  }

  Serial.printf("online: http://%s.local/ (%s)\n", settings.host,
                WiFi.localIP().toString().c_str());

  if (MDNS.begin(settings.host)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    MDNS.addService("marquee", "tcp", HTTP_PORT);
  }

  ArduinoOTA.setHostname(settings.host);
  ArduinoOTA.begin();

  configTime(settings.tz, "pool.ntp.org", "time.nist.gov");
}

void Net::loop() {
  if (ap) {
    dns.processNextRequest();
    return;
  }
  MDNS.update();
  ArduinoOTA.handle();

  if (millis() - lastCheck > 30000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("wifi lost; reconnecting");
      WiFi.reconnect();
    }
  }
}

bool Net::apMode() { return ap; }
bool Net::online() { return !ap && WiFi.status() == WL_CONNECTED; }
String Net::ip() { return ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }
