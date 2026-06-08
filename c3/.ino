#include <WiFi.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>              // REQUIRED for ESP32 / ESP32‑C3
#include <ESPAsyncWebServer.h>     // REQUIRED for async server
#include <SPIFFS.h>

// ===================== CONFIG =====================
const char* AP_SSID     = "BoardBuddy";
const char* AP_PASSWORD = "boardbuddy";

// Password required to access /upload
const char* UPLOAD_USER = "admin";
const char* UPLOAD_PASS = "boardbuddy123";

// ==================================================

AsyncWebServer server(80);
AsyncEventSource events("/events");

float smoothRSSI = -90.0f;
unsigned long lastSample = 0;

// Presence from radar
int   presenceCount     = 0;
float presenceDistance  = -1.0f;  // meters

bool vibrationDetected = false;

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
    } else buffer += c;
  }
}

// ===================== RSSI SMOOTHING =====================
float getSmoothedRSSI() {
  wifi_sta_list_t staList;
  wifi_ap_record_t apInfo;

  esp_wifi_ap_get_sta_list(&staList);

  float rssi = -127;
  if (staList.num > 0) {
    esp_wifi_sta_get_ap_info(&apInfo);
    rssi = apInfo.rssi;
  }

  smoothRSSI = 0.8f * smoothRSSI + 0.2f * rssi;
  return smoothRSSI;
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  delay(200);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  // Create upload.html if missing
  if (!SPIFFS.exists("/upload.html")) {
    File f = SPIFFS.open("/upload.html", FILE_WRITE);
    f.print(
      "<html><body>"
      "<h2>BoardBuddy File Upload</h2>"
      "<form method='POST' action='/upload' enctype='multipart/form-data'>"
      "<input type='file' name='data'><br><br>"
      "<input type='submit' value='Upload'>"
      "</form>"
      "</body></html>"
    );
    f.close();
  }

  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Serve UI
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // SSE
  server.addHandler(&events);

  // ===================== SECURE SPIFFS UPLOADER =====================
  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->authenticate(UPLOAD_USER, UPLOAD_PASS))
      return request->requestAuthentication();

    request->send(SPIFFS, "/upload.html", "text/html");
  });

  server.on(
    "/upload",
    HTTP_POST,
    [](AsyncWebServerRequest *request){
      if (!request->authenticate(UPLOAD_USER, UPLOAD_PASS))
        return request->requestAuthentication();
      request->send(200, "text/plain", "Upload complete.");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){

      if (!request->authenticate(UPLOAD_USER, UPLOAD_PASS))
        return;

      String path = "/" + filename;

      if (index == 0) {
        if (SPIFFS.exists(path)) SPIFFS.remove(path);
      }

      File f = SPIFFS.open(path, FILE_APPEND);
      if (!f) return;
      f.write(data, len);
      f.close();

      if (final) {
        Serial.printf("Uploaded %s (%u bytes)\n", filename.c_str(), index + len);
      }
    }
  );

  server.begin();
  Serial.println("BoardBuddy ESP32‑C3 started.");
}

// ===================== LOOP =====================
void loop() {
  readRadar();

  unsigned long now = millis();
  if (now - lastSample < 500) return;
  lastSample = now;

  float rssi = getSmoothedRSSI();

  vibrationDetected = false;

  String json = "{";
  json += "\"rssi\":" + String(rssi, 1);
  json += ",\"presenceCount\":" + String(presenceCount);
  json += ",\"presenceDistance\":" + String(presenceDistance, 2);
  json += ",\"vibration\":0";
  json += "}";

  events.send(json.c_str(), "state", now);
}
