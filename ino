#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "esp_wifi.h"

// ===================== PIN + CONFIG SECTION =====================
const int PIN_VIBRATION = 4;   // Vibration sensor digital output pin
const int PIN_MMWAVE_RX = 7;   // ESP32-C3 RX (connect to mmWave TX)
const int PIN_MMWAVE_TX = 6;   // ESP32-C3 TX (optional, for config)

// Adjust if your mmWave uses a different baud rate:
const uint32_t MMWAVE_BAUD = 115200;

// AP credentials
const char* apSsid     = "BoardBuddy";
const char* apPassword = "boardbuddy";
// ================================================================

AsyncWebServer server(80);
AsyncEventSource events("/events");

float smoothRSSI = -90;
unsigned long lastSample = 0;

int presenceCount = 0;
float presenceDistance = -1.0;
bool vibrationDetected = false;

void parseMmWaveLine(const String& line) {
  // Expected format example: "P:2,D:3.4"
  int pIndex = line.indexOf("P:");
  int dIndex = line.indexOf("D:");
  if (pIndex == -1 || dIndex == -1) return;

  int comma = line.indexOf(',', pIndex);
  if (comma == -1) return;

  String pStr = line.substring(pIndex + 2, comma);
  String dStr = line.substring(dIndex + 2);

  presenceCount = pStr.toInt();
  presenceDistance = dStr.toFloat();
}

void readMmWave() {
  static String buffer;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (buffer.length() > 0) {
        parseMmWaveLine(buffer);
        buffer = "";
      }
    } else {
      buffer += c;
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_VIBRATION, INPUT);

  Serial1.begin(MMWAVE_BAUD, SERIAL_8N1, PIN_MMWAVE_RX, PIN_MMWAVE_TX);

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
  readMmWave();

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

  vibrationDetected = digitalRead(PIN_VIBRATION) == HIGH;

  String json = "{";
  json += "\"rssi\":" + String(smoothRSSI, 1);
  json += ",\"presenceCount\":" + String(presenceCount);
  json += ",\"presenceDistance\":" + String(presenceDistance, 2);
  json += ",\"vibration\":" + String(vibrationDetected ? 1 : 0);
  json += "}";

  events.send(json.c_str(), "state", now);
}
