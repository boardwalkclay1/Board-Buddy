#include <WiFi.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

// ===================== CONFIG =====================
const char* AP_SSID     = "BoardBuddy";
const char* AP_PASSWORD = "boardbuddy";

// Radar is assumed on Serial1 (recommended for ESP32-C3)
// Radar sends lines like: "P:2,D:3.4" (people, distance in meters)
// ==================================================

AsyncWebServer server(80);
AsyncEventSource events("/events");

float smoothRSSI = -90.0f;
unsigned long lastSample = 0;

// Presence from radar
int   presenceCount     = 0;
float presenceDistance  = -1.0f;  // meters

// Always 0 for UI compatibility
bool vibrationDetected = false;

// ===================== RADAR PARSING =====================
void parseRadarLine(const String& line) {
  int pIndex = line.indexOf("P:");
  int dIndex = line.indexOf("D:");
  if (pIndex == -1 || dIndex == -1) return;

  int comma = line.indexOf(',', pIndex);
  if (comma == -1) return;

  String pStr = line.substring(pIndex + 2, comma);
  String dStr = line.substring(dIndex + 2);

  presenceCount    = pStr.toInt();
  presenceDistance = dStr.toFloat();
}

void readRadar() {
  static String buffer;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (buffer.length()) {
        parseRadarLine(buffer);
        buffer = "";
      }
    } else {
      buffer += c;
    }
  }
}

// ===================== RSSI SMOOTHING =====================
float getSmoothedRSSI() {
  wifi_sta_list_t staList;
  wifi_ap_record_t apInfo;

  // Get list of connected stations
  esp_wifi_ap_get_sta_list(&staList);

  float rssi = -127;

  if (staList.num > 0) {
    // Only use the first connected device
    wifi_sta_info_t sta = staList.sta[0];
    esp_wifi_sta_get_ap_info(&apInfo);
    rssi = apInfo.rssi;
  }

  smoothRSSI = 0.8f * smoothRSSI + 0.2f * rssi;
  return smoothRSSI;
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);   // Radar UART
  delay(200);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.addHandler(&events);

  server.begin();
  Serial.println("Board Buddy ESP32-C3 (radar + smooth RSSI) started");
}

// ===================== LOOP =====================
void loop() {
  // Read radar continuously
  readRadar();

  unsigned long now = millis();
  if (now - lastSample < 500) return;  // sample every 500 ms
  lastSample = now;

  float rssi = getSmoothedRSSI();

  // Always false (no vibration sensor)
  vibrationDetected = false;

  // Build JSON payload
  String json = "{";
  json += "\"rssi\":" + String(rssi, 1);
  json += ",\"presenceCount\":" + String(presenceCount);
  json += ",\"presenceDistance\":" + String(presenceDistance, 2);
  json += ",\"vibration\":" + String(vibrationDetected ? 1 : 0);
  json += "}";

  events.send(json.c_str(), "state", now);
}
