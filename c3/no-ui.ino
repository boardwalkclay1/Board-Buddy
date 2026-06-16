#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <esp_wifi.h>

// ===================== CONFIG =====================
const char* AP_SSID     = "BoardBuddy";
const char* AP_PASSWORD = "boardbuddy";

WebServer server(80);

// STATE
float smoothRSSI       = -70.0f;
int   presenceCount    = 0;
float presenceDistance = -1.0f;
bool  vibrationDetected = false;

// ===================== SEND ALERT TO PHONE =====================
// Your iPhone Shortcut listens on port 1234
void notifyPhone(const String& type) {
  WiFiClient client;
  if (client.connect("192.168.4.2", 1234)) {   // phone IP inside BoardBuddy AP
    client.print("GET /alert?type=" + type + " HTTP/1.1\r\n");
    client.print("Host: 192.168.4.2\r\n");
    client.print("Connection: close\r\n\r\n");
  }
}

// ===================== STATUS ENDPOINT (OPTIONAL) =====================
void handleStatus() {
  String json = "{";
  json += "\"rssi\":" + String(smoothRSSI) + ",";
  json += "\"presenceCount\":" + String(presenceCount) + ",";
  json += "\"presenceDistance\":" + String(presenceDistance) + ",";
  json += "\"vibration\":" + String(vibrationDetected ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

// ===================== RADAR PARSING =====================
void parseRadarLine(const String& line) {
  int pIndex = line.indexOf("P:");
  int dIndex = line.indexOf("D:");
  if (pIndex == -1 || dIndex == -1) return;

  int comma = line.indexOf(',', pIndex);
  if (comma == -1) return;

  presenceCount    = line.substring(pIndex + 2, comma).toInt();
  presenceDistance = line.substring(dIndex + 2).toFloat();
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
      if (buffer.length() > 256) buffer = "";
    }
  }
}

// ===================== WIFI RSSI =====================
float getPhoneRSSI() {
  wifi_sta_list_t staList;
  wifi_sta_info_t staInfo[10];

  if (esp_wifi_ap_get_sta_list(&staList) != ESP_OK) {
    return -90.0f;
  }

  if (staList.num == 0) {
    return -90.0f;
  }

  memcpy(staInfo, staList.sta, sizeof(wifi_sta_info_t) * staList.num);
  int8_t rssi = staInfo[0].rssi;
  return (float)rssi;
}

float getSmoothedRSSI() {
  float raw = getPhoneRSSI();
  smoothRSSI = 0.85f * smoothRSSI + 0.15f * raw;
  return smoothRSSI;
}

// ===================== RSSI → DISTANCE =====================
float rssiToFeet(float rssi) {
  float txPower = -59.0f;
  float ratio   = (txPower - rssi) / 20.0f;
  float meters  = pow(10.0f, ratio);
  return meters * 3.28084f;
}

// ===================== FUSED DISTANCE =====================
float fusedDistance(float wifiFeet, float radarFeet) {
  if (radarFeet < 0) return wifiFeet;
  return (wifiFeet * 0.6f) + (radarFeet * 0.4f);
}

// ===================== EVENTS ENDPOINT =====================
void handleEvents() {
  float rssi      = getSmoothedRSSI();
  float wifiFeet  = rssiToFeet(rssi);
  float radarFeet = presenceDistance;
  float fused     = fusedDistance(wifiFeet, radarFeet);

  String json = "{";
  json += "\"rssi\":" + String(rssi, 1);
  json += ",\"wifiFeet\":" + String(wifiFeet, 1);
  json += ",\"radarFeet\":" + String(radarFeet, 1);
  json += ",\"distance\":" + String(fused, 1);
  json += ",\"presenceCount\":" + String(presenceCount);
  json += ",\"presenceDistance\":" + String(presenceDistance, 2);
  json += ",\"vibration\":" + String(vibrationDetected ? 1 : 0);
  json += "}";

  server.send(200, "application/json", json);

  // ====== ALERT LOGIC ======
  if (fused > 25) notifyPhone("too_far");
  if (presenceCount > 1) notifyPhone("presence");
  if (vibrationDetected) notifyPhone("vibration");
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);

  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/events", HTTP_GET, handleEvents);
  server.on("/status", HTTP_GET, handleStatus);

  server.begin();
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();
  readRadar();
}
