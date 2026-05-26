#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "esp_wifi.h"

const char* apSsid     = "BoardBuddy";
const char* apPassword = "boardbuddy";

AsyncWebServer server(80);
AsyncEventSource events("/events");

float smoothRSSI = -90;
unsigned long lastSample = 0;

void setup() {
  Serial.begin(115200);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPassword);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.addHandler(&events);

  server.begin();
}

void loop() {
  unsigned long now = millis();
  if (now - lastSample < 500) return;
  lastSample = now;

  wifi_sta_list_t wifi_sta_list;
  esp_wifi_ap_get_sta_list(&wifi_sta_list);

  float rssi;
  if (wifi_sta_list.num == 0) {
    rssi = -127; // out of range
  } else {
    int8_t raw = wifi_sta_list.sta[0].rssi;
    rssi = raw;
  }

  smoothRSSI = 0.8f * smoothRSSI + 0.2f * rssi;
  String payload = String(smoothRSSI, 1);
  events.send(payload.c_str(), "rssi", now);
}
