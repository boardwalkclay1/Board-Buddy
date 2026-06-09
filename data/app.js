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
// RSSI → Distance
// =========================
function rssiToFeet(rssi) {
  // Tuned for BoardBuddy
  const txPower = -59;
  const ratio = (txPower - rssi) / 20;
  return Math.pow(10, ratio) * 3.2;
}

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
      const dist = rssiToFeet(j.rssi);
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
