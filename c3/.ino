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

float smoothRSSI       = -70.0f;
int   presenceCount    = 0;
float presenceDistance = -1.0f;
bool  vibrationDetected = false;

File uploadFile;

// ===================== EMBEDDED UI =====================
const char index_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Board Buddy</title>
  <link rel="stylesheet" href="style.css">
</head>
<body>

<div class="wrap">

  <!-- HEADER -->
  <header class="header">
    <div class="logo">Board Buddy</div>
    <div class="object-name" id="objectName">My Board</div>
  </header>

  <!-- DISTANCE / TIER SECTION -->
  <section class="tier-section">
    <div id="tierLabel" class="tier-label">Connecting…</div>

    <div class="tier-bar">
      <div id="tier1" class="tier-dot"></div>
      <div id="tier2" class="tier-dot"></div>
      <div id="tier3" class="tier-dot"></div>
      <div id="tier4" class="tier-dot"></div>
    </div>

    <div class="rssi-readout">
      Distance: <span id="distanceValue">—</span> ft
    </div>
  </section>

  <!-- PRESENCE SECTION -->
  <section class="presence-section">
    <div class="presence-row">
      <span>Presence:</span>
      <span id="presenceStatus">No one nearby</span>
    </div>

    <div class="presence-row">
      <span>People:</span>
      <span id="presenceCount">0</span>
    </div>

    <div class="presence-row">
      <span>Distance:</span>
      <span id="presenceDistance">— ft</span>
    </div>

    <div class="presence-row">
      <span>Presence radius</span>
      <input id="presenceRange" type="range" min="3" max="20" step="1">
      <span id="presenceRangeVal">10 ft</span>
    </div>

    <div class="presence-row">
      <span>Presence mode</span>
      <select id="presenceMode">
        <option value="any">Alert on any person</option>
        <option value="twoPlus" selected>Ignore 1, alert on 2+</option>
      </select>
    </div>
  </section>

  <!-- OBJECT NAME + DISTANCE TIERS -->
  <section class="controls">

    <div class="control-block">
      <label>Object name</label>
      <div class="row">
        <input id="nameInput" type="text" placeholder="My Board">
        <button id="saveName">Save</button>
      </div>
    </div>

    <div class="control-block">
      <label>Distance tiers (feet)</label>

      <div class="slider-row">
        <span>Close / Around</span>
        <input id="thClose" type="range" min="1" max="20" step="1">
        <span id="thCloseVal"></span>
      </div>

      <div class="slider-row">
        <span>Around / Getting far</span>
        <input id="thAround" type="range" min="5" max="40" step="1">
        <span id="thAroundVal"></span>
      </div>

      <div class="slider-row">
        <span>Getting far / Too far</span>
        <input id="thFar" type="range" min="10" max="60" step="1">
        <span id="thFarVal"></span>
      </div>

      <div class="slider-row">
        <span>Too far / Out of range</span>
        <input id="thTooFar" type="range" min="20" max="100" step="1">
        <span id="thTooFarVal"></span>
      </div>

      <button id="saveThresholds">Save thresholds</button>
    </div>

  </section>

  <!-- GRAPH -->
  <section class="graph-section">
    <label>Distance trend</label>
    <canvas id="rssiGraph" width="360" height="80"></canvas>
  </section>

  <!-- VIBRATION -->
  <section class="status-section">
    <div class="status-row">
      <span>Vibration:</span>
      <span id="vibrationStatus">None</span>
    </div>
  </section>

</div>

<script src="app.js"></script>
</body>
</html>
)rawliteral";

const char style_css[] PROGMEM = R"rawliteral(body {
  margin: 0;
  padding: 0;
  background: linear-gradient(rgba(0,0,0,0.55), rgba(0,0,0,0.55)),
              url('board-buddy.jpg') no-repeat center center fixed;
  background-size: cover;
  font-family: 'Segoe UI', sans-serif;
  color: #fff;
}

.wrap {
  backdrop-filter: blur(6px);
  padding-bottom: 40px;
}

.header {
  text-align: center;
  padding: 20px;
  font-size: 28px;
  font-weight: 700;
  color: #ff2a2a;
  text-shadow: 0 0 8px #000;
}

.tier-label, .presence-row span, .status-row span {
  text-shadow: 0 0 6px #000;
}
)rawliteral";

const char app_js[] PROGMEM = R"rawliteral(
// =========================
// CONFIG
// =========================
let rssiHistory = [];
const maxPoints = 60;

// Default thresholds (feet)
let thClose = 5;
let thAround = 12;
let thFar = 25;
let thTooFar = 40;

// =========================
// DOM SHORTCUTS
// =========================
const tierLabel = document.getElementById("tierLabel");
const distanceValue = document.getElementById("distanceValue");
const presenceStatus = document.getElementById("presenceStatus");
const presenceCount = document.getElementById("presenceCount");
const presenceDistance = document.getElementById("presenceDistance");
const vibrationStatus = document.getElementById("vibrationStatus");

const thCloseSlider = document.getElementById("thClose");
const thAroundSlider = document.getElementById("thAround");
const thFarSlider = document.getElementById("thFar");
const thTooFarSlider = document.getElementById("thTooFar");

const thCloseVal = document.getElementById("thCloseVal");
const thAroundVal = document.getElementById("thAroundVal");
const thFarVal = document.getElementById("thFarVal");
const thTooFarVal = document.getElementById("thTooFarVal");

const nameInput = document.getElementById("nameInput");
const objectName = document.getElementById("objectName");

// =========================
// LOAD SAVED SETTINGS
// =========================
function loadSettings() {
  const savedName = localStorage.getItem("bb_name");
  if (savedName) {
    objectName.textContent = savedName;
    nameInput.value = savedName;
  }

  const saved = JSON.parse(localStorage.getItem("bb_thresholds"));
  if (saved) {
    thClose = saved.thClose;
    thAround = saved.thAround;
    thFar = saved.thFar;
    thTooFar = saved.thTooFar;
  }

  thCloseSlider.value = thClose;
  thAroundSlider.value = thAround;
  thFarSlider.value = thFar;
  thTooFarSlider.value = thTooFar;

  thCloseVal.textContent = thClose + " ft";
  thAroundVal.textContent = thAround + " ft";
  thFarVal.textContent = thFar + " ft";
  thTooFarVal.textContent = thTooFar + " ft";
}

// =========================
// SAVE SETTINGS
// =========================
document.getElementById("saveName").onclick = () => {
  const name = nameInput.value.trim();
  if (name.length > 0) {
    localStorage.setItem("bb_name", name);
    objectName.textContent = name;
  }
};

document.getElementById("saveThresholds").onclick = () => {
  thClose = parseInt(thCloseSlider.value);
  thAround = parseInt(thAroundSlider.value);
  thFar = parseInt(thFarSlider.value);
  thTooFar = parseInt(thTooFarSlider.value);

  localStorage.setItem(
    "bb_thresholds",
    JSON.stringify({ thClose, thAround, thFar, thTooFar })
  );

  thCloseVal.textContent = thClose + " ft";
  thAroundVal.textContent = thAround + " ft";
  thFarVal.textContent = thFar + " ft";
  thTooFarVal.textContent = thTooFar + " ft";
};

// =========================
// TIER UPDATE
// =========================
function updateTiers(dist) {
  const t1 = document.getElementById("tier1");
  const t2 = document.getElementById("tier2");
  const t3 = document.getElementById("tier3");
  const t4 = document.getElementById("tier4");

  t1.classList.remove("active");
  t2.classList.remove("active");
  t3.classList.remove("active");
  t4.classList.remove("active");

  if (dist < thClose) {
    tierLabel.textContent = "Close";
    t1.classList.add("active");
  } else if (dist < thAround) {
    tierLabel.textContent = "Around";
    t1.classList.add("active");
    t2.classList.add("active");
  } else if (dist < thFar) {
    tierLabel.textContent = "Getting Far";
    t1.classList.add("active");
    t2.classList.add("active");
    t3.classList.add("active");
  } else if (dist < thTooFar) {
    tierLabel.textContent = "Too Far";
    t1.classList.add("active");
    t2.classList.add("active");
    t3.classList.add("active");
    t4.classList.add("active");
  } else {
    tierLabel.textContent = "Out of Range";
  }
}

// =========================
// GRAPH
// =========================
function drawGraph() {
  const canvas = document.getElementById("rssiGraph");
  const ctx = canvas.getContext("2d");

  ctx.clearRect(0, 0, canvas.width, canvas.height);

  ctx.strokeStyle = "#ff2a2a";
  ctx.lineWidth = 2;
  ctx.beginPath();

  rssiHistory.forEach((v, i) => {
    const x = (i / maxPoints) * canvas.width;
    const y = canvas.height - (v / 80) * canvas.height;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });

  ctx.stroke();
}

// =========================
// MAIN POLLING LOOP
// =========================
function poll() {
  fetch("/events")
    .then((r) => r.json())
    .then((j) => {
      const dist = j.distance; // fused distance from firmware
      const distFt = dist.toFixed(1);

      distanceValue.textContent = distFt;
      updateTiers(dist);

      // Graph
      rssiHistory.push(dist);
      if (rssiHistory.length > maxPoints) rssiHistory.shift();
      drawGraph();

      // Presence
      presenceCount.textContent = j.presenceCount;
      presenceDistance.textContent = j.presenceDistance.toFixed(1) + " ft";

      if (j.presenceCount > 0) {
        presenceStatus.textContent = "Someone nearby";
      } else {
        presenceStatus.textContent = "No one nearby";
      }

      // Vibration
      vibrationStatus.textContent = j.vibration ? "Active" : "None";
    })
    .catch(() => {
      tierLabel.textContent = "Disconnected";
    });
}

loadSettings();
setInterval(poll, 500);
poll();
)rawliteral";

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

// ===================== RSSI → DISTANCE (FEET) =====================
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

// ===================== WRITE EMBEDDED FILES =====================
void writeFileFromProgmem(const char* path, const char* data) {
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) return;
  f.print(data);
  f.close();
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  // Auto-install UI files
  writeFileFromProgmem("/index.html", index_html);
  writeFileFromProgmem("/style.css", style_css);
  writeFileFromProgmem("/app.js", app_js);

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
