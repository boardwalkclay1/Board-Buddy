#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <SPIFFS.h>

// ===================== CONFIG =====================
const char* AP_SSID     = "BoardBuddy";
const char* AP_PASSWORD = "boardbuddy";

const char* UPLOAD_USER = "admin";
const char* UPLOAD_PASS = "boardbuddy123";

// ==================================================

WebServer server(80);

float smoothRSSI = -90.0f;
unsigned long lastSample = 0;

int   presenceCount     = 0;
float presenceDistance  = -1.0f;

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

// ===================== EVENTS ENDPOINT =====================
void handleEvents() {
  float rssi = getSmoothedRSSI();

  String json = "{";
  json += "\"rssi\":" + String(rssi, 1);
  json += ",\"presenceCount\":" + String(presenceCount);
  json += ",\"presenceDistance\":" + String(presenceDistance, 2);
  json += ",\"vibration\":0";
  json += "}";

  server.send(200, "application/json", json);
}

// ===================== UPLOAD PAGE =====================
void handleUploadPage() {
  if (!server.authenticate(UPLOAD_USER, UPLOAD_PASS))
    return server.requestAuthentication();

  File f = SPIFFS.open("/upload.html", "r");
  if (!f) {
    server.send(500, "text/plain", "Missing upload.html");
    return;
  }

  server.streamFile(f, "text/html");
  f.close();
}

// ===================== FILE UPLOAD HANDLER =====================
void handleFileUpload() {
  if (!server.authenticate(UPLOAD_USER, UPLOAD_PASS))
    return server.requestAuthentication();

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    if (SPIFFS.exists(filename)) SPIFFS.remove(filename);
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    File f = SPIFFS.open("/" + upload.filename, FILE_APPEND);
    if (f) {
      f.write(upload.buf, upload.currentSize);
      f.close();
    }
  }

  if (upload.status == UPLOAD_FILE_END) {
    server.send(200, "text/plain", "Upload complete.");
  }
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);

  SPIFFS.begin(true);

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

  // Serve index.html manually
  server.on("/", HTTP_GET, []() {
    File f = SPIFFS.open("/index.html", "r");
    if (!f) {
      server.send(404, "text/plain", "index.html missing");
      return;
    }
    server.streamFile(f, "text/html");
    f.close();
  });

  server.on("/events", HTTP_GET, handleEvents);
  server.on("/upload", HTTP_GET, handleUploadPage);
  server.on("/upload", HTTP_POST, [](){}, handleFileUpload);

  server.begin();
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();
  readRadar();

  if (millis() - lastSample > 500) {
    lastSample = millis();
  }
}
