// DOM refs
const tierLabel = document.getElementById("tierLabel");
const rssiValueEl = document.getElementById("rssiValue");
const dots = [
  document.getElementById("tier1"),
  document.getElementById("tier2"),
  document.getElementById("tier3"),
  document.getElementById("tier4")
];

const nameEl = document.getElementById("objectName");
const nameInput = document.getElementById("nameInput");
const saveNameBtn = document.getElementById("saveName");

const thClose = document.getElementById("thClose");
const thAround = document.getElementById("thAround");
const thFar = document.getElementById("thFar");
const thTooFar = document.getElementById("thTooFar");
const thCloseVal = document.getElementById("thCloseVal");
const thAroundVal = document.getElementById("thAroundVal");
const thFarVal = document.getElementById("thFarVal");
const thTooFarVal = document.getElementById("thTooFarVal");
const saveThresholdsBtn = document.getElementById("saveThresholds");

const presenceStatusEl = document.getElementById("presenceStatus");
const presenceCountEl = document.getElementById("presenceCount");
const presenceDistanceEl = document.getElementById("presenceDistance");
const presenceRange = document.getElementById("presenceRange");
const presenceRangeVal = document.getElementById("presenceRangeVal");
const presenceMode = document.getElementById("presenceMode");

const vibrationStatusEl = document.getElementById("vibrationStatus");

const graphCanvas = document.getElementById("rssiGraph");
const gctx = graphCanvas.getContext("2d");

// State
let objectName = localStorage.getItem("boardBuddyName") || "My Board";
nameEl.innerText = objectName;
nameInput.value = objectName;

let thresholds = JSON.parse(localStorage.getItem("boardBuddyThresholds") || "null") || {
  close: -60,
  around: -70,
  far: -80,
  tooFar: -90
};

let presenceConfig = JSON.parse(localStorage.getItem("boardBuddyPresence") || "null") || {
  rangeM: 3,
  mode: "twoPlus"
};

let rssiHistory = [];

let currentTier = "INIT";
let pendingTier = "INIT";
let pendingStart = 0;

// UI sync
function syncThresholdUI() {
  thClose.value = thresholds.close;
  thAround.value = thresholds.around;
  thFar.value = thresholds.far;
  thTooFar.value = thresholds.tooFar;
  thCloseVal.innerText = thresholds.close + " dBm";
  thAroundVal.innerText = thresholds.around + " dBm";
  thFarVal.innerText = thresholds.far + " dBm";
  thTooFarVal.innerText = thresholds.tooFar + " dBm";
}

function syncPresenceUI() {
  presenceRange.value = presenceConfig.rangeM;
  presenceRangeVal.innerText = presenceConfig.rangeM + " m";
  presenceMode.value = presenceConfig.mode;
}

syncThresholdUI();
syncPresenceUI();

// Handlers
saveNameBtn.onclick = () => {
  objectName = nameInput.value || "My Board";
  nameEl.innerText = objectName;
  localStorage.setItem("boardBuddyName", objectName);
};

function updateSliderLabels() {
  thresholds.close = parseInt(thClose.value);
  thresholds.around = parseInt(thAround.value);
  thresholds.far = parseInt(thFar.value);
  thresholds.tooFar = parseInt(thTooFar.value);
  syncThresholdUI();
}

[thClose, thAround, thFar, thTooFar].forEach(sl => {
  sl.addEventListener("input", updateSliderLabels);
});

saveThresholdsBtn.onclick = () => {
  localStorage.setItem("boardBuddyThresholds", JSON.stringify(thresholds));
};

presenceRange.addEventListener("input", () => {
  presenceConfig.rangeM = parseInt(presenceRange.value);
  syncPresenceUI();
  localStorage.setItem("boardBuddyPresence", JSON.stringify(presenceConfig));
});

presenceMode.addEventListener("change", () => {
  presenceConfig.mode = presenceMode.value;
  localStorage.setItem("boardBuddyPresence", JSON.stringify(presenceConfig));
});

// Notifications + vibration
if ("Notification" in window && Notification.permission === "default") {
  Notification.requestPermission();
}

function notify(title, body) {
  if (!("Notification" in window)) return;
  if (Notification.permission !== "granted") return;
  new Notification(title, { body });
}

function vibrate() {
  if (!("vibrate" in navigator)) return;
  navigator.vibrate(400);
}

// Tier logic
function tierFromRSSI(rssi) {
  if (rssi <= -120) return "OUT_RANGE";
  if (rssi > thresholds.close) return "CLOSE";
  if (rssi > thresholds.around) return "AROUND";
  if (rssi > thresholds.far) return "GETTING_FAR";
  if (rssi > thresholds.tooFar) return "TOO_FAR";
  return "OUT_RANGE";
}

function applyHysteresis(prev, rssi) {
  const margin = 2;
  switch (prev) {
    case "CLOSE":
      if (rssi < thresholds.close - margin) return tierFromRSSI(rssi);
      return "CLOSE";
    case "AROUND":
      if (rssi > thresholds.close + margin || rssi < thresholds.around - margin) return tierFromRSSI(rssi);
      return "AROUND";
    case "GETTING_FAR":
      if (rssi > thresholds.around + margin || rssi < thresholds.far - margin) return tierFromRSSI(rssi);
      return "GETTING_FAR";
    case "TOO_FAR":
      if (rssi > thresholds.far + margin || rssi < thresholds.tooFar - margin) return tierFromRSSI(rssi);
      return "TOO_FAR";
    case "OUT_RANGE":
      if (rssi > thresholds.tooFar + margin) return tierFromRSSI(rssi);
      return "OUT_RANGE";
    default:
      return tierFromRSSI(rssi);
  }
}

function setTierUI(tier) {
  dots.forEach(d => d.className = "tier-dot");

  switch (tier) {
    case "CLOSE":
      tierLabel.innerText = `${objectName} is close by`;
      dots[0].classList.add("active-close");
      break;
    case "AROUND":
      tierLabel.innerText = `${objectName} is around you`;
      dots[1].classList.add("active-around");
      break;
    case "GETTING_FAR":
      tierLabel.innerText = `${objectName} is getting far`;
      dots[2].classList.add("active-far");
      notify("Board Buddy", `${objectName} is getting far`);
      vibrate();
      break;
    case "TOO_FAR":
      tierLabel.innerText = `${objectName} is too far`;
      dots[3].classList.add("active-too");
      notify("Board Buddy", `${objectName} is too far`);
      vibrate();
      break;
    case "OUT_RANGE":
      tierLabel.innerText = `${objectName} is OUT OF RANGE`;
      dots[3].classList.add("active-too");
      notify("Board Buddy ALERT", `${objectName} is OUT OF RANGE`);
      vibrate();
      break;
    default:
      tierLabel.innerText = "Connecting…";
  }
}

// Graph
function drawGraph(rssi) {
  rssiHistory.push(rssi);
  if (rssiHistory.length > 60) rssiHistory.shift();

  gctx.clearRect(0, 0, graphCanvas.width, graphCanvas.height);
  gctx.strokeStyle = "#d4af37";
  gctx.lineWidth = 2;
  gctx.beginPath();

  const minR = -110;
  const maxR = -40;
  rssiHistory.forEach((v, i) => {
    const x = (i / 59) * (graphCanvas.width - 4) + 2;
    const norm = (v - minR) / (maxR - minR);
    const y = graphCanvas.height - 4 - norm * (graphCanvas.height - 8);
    if (i === 0) gctx.moveTo(x, y);
    else gctx.lineTo(x, y);
  });

  gctx.stroke();
}

// 2-second stability for tier changes
function processTier(rssi) {
  const now = Date.now();

  let target = tierFromRSSI(rssi);
  target = applyHysteresis(currentTier, rssi);

  if (target !== pendingTier) {
    pendingTier = target;
    pendingStart = now;
    return;
  }

  if (now - pendingStart >= 2000 && target !== currentTier) {
    currentTier = target;
    setTierUI(currentTier);
  }
}

// Presence + vibration logic
let lastPresenceAlert = 0;
let lastVibrationAlert = 0;

function processPresence(pCount, pDist) {
  presenceCountEl.innerText = pCount;
  presenceDistanceEl.innerText = pDist > 0 ? pDist.toFixed(1) + " m" : "—";

  const inRange = pDist > 0 && pDist <= presenceConfig.rangeM;

  if (!inRange || pCount === 0) {
    presenceStatusEl.innerText = "No one nearby";
    presenceStatusEl.style.color = "#999";
    return;
  }

  if (pCount === 1 && presenceConfig.mode === "twoPlus") {
    presenceStatusEl.innerText = "You are near your board";
    presenceStatusEl.style.color = "#3ddc84";
    return;
  }

  presenceStatusEl.innerText = `${pCount} people near your board`;
  presenceStatusEl.style.color = "#d4af37";

  const now = Date.now();
  if (now - lastPresenceAlert > 5000) {
    notify("Board Buddy", `${pCount} people near ${objectName}`);
    vibrate();
    lastPresenceAlert = now;
  }
}

function processVibration(vib) {
  if (vib) {
    vibrationStatusEl.innerText = "Movement detected";
    vibrationStatusEl.style.color = "#ff4444";

    const now = Date.now();
    if (now - lastVibrationAlert > 5000) {
      notify("Board Buddy", `${objectName} is being moved`);
      vibrate();
      lastVibrationAlert = now;
    }
  } else {
    vibrationStatusEl.innerText = "None";
    vibrationStatusEl.style.color = "#999";
  }
}

// SSE
const evt = new EventSource("/events");

evt.addEventListener("state", (e) => {
  const data = JSON.parse(e.data);

  const rssi = parseFloat(data.rssi);
  const pCount = parseInt(data.presenceCount);
  const pDist = parseFloat(data.presenceDistance);
  const vib = !!data.vibration;

  rssiValueEl.innerText = isNaN(rssi) ? "—" : rssi.toFixed(1);
  drawGraph(rssi);
  processTier(rssi);
  processPresence(pCount, pDist);
  processVibration(vib);
});

evt.onerror = () => {
  tierLabel.innerText = "Disconnected…";
};
