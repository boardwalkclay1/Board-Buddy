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

float smoothRSSI       = -70.0f;   // smoothed phone RSSI
int   presenceCount    = 0;        // from radar
float presenceDistance = -1.0f;    // from radar (feet)

bool vibrationDetected = false;    // hook this to a real sensor later

File uploadFile;

// ===================== RADAR PARSING =====================
// Expects lines like: P:3,D:4.2
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
      if (buffer.length() > 256) buffer = "";  // safety reset
    }
  }
}

// ===================== WIFI RSSI (PHONE → ESP) =====================
// Get RSSI of the first connected station (phone) in AP mode
float getPhoneRSSI() {
  wifi_sta_list_t staList;
  wifi_sta_info_t staInfo[10];

  if (esp_wifi_ap_get_sta_list(&staList) != ESP_OK) {
    return -90.0f; // no info
  }

  if (staList.num == 0) {
    return -90.0f; // no phone connected
  }

  // Copy station info
  memcpy(staInfo, staList.sta, sizeof(wifi_sta_info_t) * staList.num);

  // Assume one phone; use first station
  int8_t rssi = staInfo[0].rssi;
  return (float)rssi;
}

// Smooth RSSI for stability
float getSmoothedRSSI() {
  float raw = getPhoneRSSI();
  smoothRSSI = 0.85f * smoothRSSI + 0.15f * raw;
  return smoothRSSI;
}

// ===================== RSSI → DISTANCE (FEET) =====================
float rssiToFeet(float rssi) {
  float txPower = -59.0f; // typical RSSI at 1m
  float ratio   = (txPower - rssi) / 20.0f;
  float meters  = pow(10.0f, ratio);
  return meters * 3.28084f;
}

// ===================== FUSED DISTANCE =====================
// Combine WiFi distance + radar distance for better accuracy
float fusedDistance(float wifiFeet, float radarFeet) {
  if (radarFeet < 0) return wifiFeet;          // no radar reading
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
    uploadFile = SPIFFS.open(filename, FILE_WRITE);
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
    server.send(200, "text/plain", "Upload complete.");
  }
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);

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

  // Serve index.html
  server.on("/", HTTP_GET, []() {
    File f = SPIFFS.open("/index.html", "r");
    if (!f) {
      server.send(404, "text/plain", "index.html missing");
      return;
    }
    server.streamFile(f, "text/html");
    f.close();
  });

  // Serve static assets
  server.on("/style.css", HTTP_GET, []() {
    File f = SPIFFS.open("/style.css", "r");
    if (!f) {
      server.send(404, "text/plain", "style.css missing");
      return;
    }
    server.streamFile(f, "text/css");
    f.close();
  });

  server.on("/app.js", HTTP_GET, []() {
    File f = SPIFFS.open("/app.js", "r");
    if (!f) {
      server.send(404, "text/plain", "app.js missing");
      return;
    }
    server.streamFile(f, "application/javascript");
    f.close();
  });

  server.on("/board-buddy.jpg", HTTP_GET, []() {
    File f = SPIFFS.open("/board-buddy.jpg", "r");
    if (!f) {
      server.send(404, "text/plain", "board-buddy.jpg missing");
      return;
    }
    server.streamFile(f, "image/jpeg");
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
}
