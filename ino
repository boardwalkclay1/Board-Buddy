#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "esp_wifi.h"

// ===================== PIN CONFIG =====================
const int PIN_VIBRATION = 4;
const int PIN_MMWAVE_RX = 7;
const int PIN_MMWAVE_TX = 6;
const uint32_t MMWAVE_BAUD = 115200;
// ======================================================

AsyncWebServer server(80);
AsyncEventSource events("/events");

float smoothRSSI = -90;
unsigned long lastSample = 0;

int presenceCount = 0;
float presenceDistance = -1.0;
bool vibrationDetected = false;

void parseMmWaveLine(const String& line) {
  int pIndex = line.indexOf("P:");
  int dIndex = line.indexOf("D:");
  if (pIndex == -1 || dIndex == -1) return;

  int comma = line.indexOf(',', pIndex);
  if (comma == -1) return;

  presenceCount = line.substring(pIndex + 2, comma).toInt();
  presenceDistance = line.substring(dIndex + 2).toFloat();
}

void readMmWave() {
  static String buffer;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (buffer.length()) {
        parseMmWaveLine(buffer);
        buffer = "";
      }
    } else buffer += c;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_VIBRATION, INPUT);
  Serial1.begin(MMWAVE_BAUD, SERIAL_8N1, PIN_MMWAVE_RX, PIN_MMWAVE_TX);

  SPIFFS.begin(true);

  WiFi.mode(WIFI_AP);
  WiFi.softAP("BoardBuddy", "boardbuddy");

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.addHandler(&events);
  server.begin();
}

void loop() {
  readMmWave();

  if (millis() - lastSample < 500) return;
  lastSample = millis();

  wifi_sta_list_t list;
  esp_wifi_ap_get_sta_list(&list);

  float rssi = (list.num == 0) ? -127 : list.sta[0].rssi;
  smoothRSSI = 0.8f * smoothRSSI + 0.2f * rssi;

  vibrationDetected = digitalRead(PIN_VIBRATION);

  String json = "{";
  json += "\"rssi\":" + String(smoothRSSI, 1);
  json += ",\"presenceCount\":" + String(presenceCount);
  json += ",\"presenceDistance\":" + String(presenceDistance, 2);
  json += ",\"vibration\":" + String(vibrationDetected ? 1 : 0);
  json += "}";

  events.send(json.c_str(), "state", millis());
}
