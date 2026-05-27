// DOM refs
const tierLabel = document.getElementById("tierLabel");
const distanceValueEl = document.getElementById("distanceValue");
const dots = [
  document.getElementById("tier1"),
  document.getElementById("tier2"),
  document.getElementById("tier3"),
  document.getElementById("tier4")
];

const nameEl = document.getElementById("objectName");
const nameInput = document.getElementById("nameInput");
const saveNameBtn = document.getElementById("saveName");

// Distance tier sliders (FEET)
const thClose = document.getElementById("thClose");
const thAround = document.getElementById("thAround");
const thFar = document.getElementById("thFar");
const thTooFar = document.getElementById("thTooFar");
const thCloseVal = document.getElementById("thCloseVal");
const thAroundVal = document.getElementById("thAroundVal");
const thFarVal = document.getElementById("thFarVal");
const thTooFarVal = document.getElementById("thTooFarVal");
const saveThresholdsBtn = document.getElementById("saveThresholds");

// Presence
const presenceStatusEl = document.getElementById("presenceStatus");
const presenceCountEl = document.getElementById("presenceCount");
const presenceDistanceEl = document.getElementById("presenceDistance");
const presenceRange = document.getElementById("presenceRange");
const presenceRangeVal = document.getElementById("presenceRangeVal");
const presenceMode = document.getElementById("presenceMode");

// Vibration
const vibrationStatusEl = document.getElementById("vibrationStatus");

// Graph
const graphCanvas = document.getElementById("rssiGraph");
const gctx = graphCanvas.getContext("2d");

// ===================== STATE =====================
let objectName = localStorage.getItem("boardBuddyName") || "My Board";
nameEl.innerText = objectName;
nameInput.value = objectName;

// Distance thresholds in FEET
let thresholds = JSON.parse(localStorage.getItem("boardBuddyThresholds") || "null") || {
  close: 5,
  around: 15,
  far: 30,
  tooFar: 50
};

// Presence config (FEET)
let presenceConfig = JSON.parse(localStorage.getItem("boardBuddyPresence") || "null") || {
  rangeFt: 10,
  mode: "twoPlus"
};

let distanceHistory = [];

let currentTier = "INIT";
let pendingTier = "INIT";
let pendingStart = 0;

// ===================== RSSI → FEET CONVERSION =====================
function rssiToFeet(rssi) {
  const RSSI0 = -45; // RSSI at 1 foot (calibrate)
  const n = 2.0;     // environmental factor

  let meters = Math.pow(10, (RSSI0 - rssi) / (10 * n));
  let feet = meters * 3.28084;

  if (feet < 0) feet = 0;
  if (feet > 200) feet = 200;

  return feet;
}

// ===================== UI SYNC =====================
function syncThresholdUI() {
  thClose.value = thresholds.close;
  thAround.value = thresholds.around;
  thFar.value = thresholds.far;
  thTooFar.value = thresholds.tooFar;

  thCloseVal.innerText = thresholds.close + " ft";
  thAroundVal.innerText = thresholds.around + " ft";
  thFarVal.innerText = thresholds.far + " ft";
  thTooFarVal.innerText = thresholds.tooFar + " ft";
}

function syncPresenceUI() {
  presenceRange.value = presenceConfig.rangeFt;
  presenceRangeVal.innerText = presenceConfig.rangeFt + " ft";
  presenceMode.value = presenceConfig.mode;
}

syncThresholdUI();
syncPresenceUI();

// ===================== HANDLERS =====================
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
  presenceConfig.rangeFt = parseInt(presenceRange.value);
  syncPresenceUI();
  localStorage.setItem("boardBuddyPresence", JSON.stringify(presenceConfig));
});

presenceMode.addEventListener("change", () => {
  presenceConfig.mode = presenceMode.value;
  localStorage.setItem("boardBuddyPresence", JSON.stringify(presenceConfig));
});

// ===================== NOTIFICATIONS =====================
if ("Notification" in window && Notification.permission === "default") {
  Notification.requestPermission();
}

function notify(title, body) {
  if (Notification.permission !== "granted") return;
  new Notification(title, { body });
}

function vibrate() {
  if ("vibrate" in navigator) navigator.vibrate(400);
}

// ===================== TIER LOGIC (FEET) =====================
function tierFromFeet(ft) {
  if (ft >= thresholds.tooFar) return "OUT_RANGE";
  if (ft >= thresholds.far) return "TOO_FAR";
  if (ft >= thresholds.around) return "GETTING_FAR";
  if (ft >= thresholds.close) return "AROUND";
  return "CLOSE";
}

function applyHysteresis(prev, ft) {
  const margin = 2;

  switch (prev) {
    case "CLOSE":
      if (ft > thresholds.close + margin) return tierFromFeet(ft);
      return "CLOSE";

    case "AROUND":
      if (ft < thresholds.close - margin || ft > thresholds.around + margin)
        return tierFromFeet(ft);
      return "AROUND";

    case "GETTING_FAR":
      if (ft < thresholds.around - margin || ft > thresholds.far + margin)
        return tierFromFeet(ft);
      return "GETTING_FAR";

    case "TOO_FAR":
      if (ft < thresholds.far - margin || ft > thresholds.tooFar + margin)
        return tierFromFeet(ft);
      return "TOO_FAR";

    case "OUT_RANGE":
      if (ft < thresholds.tooFar - margin) return tierFromFeet(ft);
      return "OUT_RANGE";

    default:
      return tierFromFeet(ft);
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
  }
}

// ===================== GRAPH (FEET) =====================
function drawGraph(ft) {
  distanceHistory.push(ft);
  if (distanceHistory.length > 60) distanceHistory.shift();

  gctx.clearRect(0, 0, graphCanvas.width, graphCanvas.height);
  gctx.strokeStyle = "#d4af37";
  gctx.lineWidth = 2;
  gctx.beginPath();

  const minF = 0;
  const maxF = 100;

  distanceHistory.forEach((v, i) => {
    const x = (i / 59) * (graphCanvas.width - 4) + 2;
    const norm = (v - minF) / (maxF - minF);
    const y = graphCanvas.height - 4 - norm * (graphCanvas.height - 8);
    if (i === 0) gctx.moveTo(x, y);
    else gctx.lineTo(x, y);
  });

  gctx.stroke();
}

// ===================== 2-SECOND STABILITY =====================
function processTier(ft) {
  const now = Date.now();

  let target = tierFromFeet(ft);
  target = applyHysteresis(currentTier, ft);

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

// ===================== PRESENCE + VIBRATION =====================
let lastPresenceAlert = 0;
let lastVibrationAlert = 0;

function processPresence(pCount, pDistMeters) {
  const pDistFt = pDistMeters * 3.28084;

  presenceCountEl.innerText = pCount;
  presenceDistanceEl.innerText = pDistFt > 0 ? pDistFt.toFixed(1) + " ft" : "—";

  const inRange = pDistFt > 0 && pDistFt <= presenceConfig.rangeFt;

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

// ===================== SSE =====================
const evt = new EventSource("/events");

evt.addEventListener("state", (e) => {
  const data = JSON.parse(e.data);

  const rssi = parseFloat(data.rssi);
  const pCount = parseInt(data.presenceCount);
  const pDist = parseFloat(data.presenceDistance);
  const vib = !!data.vibration;

  const feet = rssiToFeet(rssi);
  distanceValueEl.innerText = feet.toFixed(1);

  drawGraph(feet);
  processTier(feet);
  processPresence(pCount, pDist);
  processVibration(vib);
});

evt.onerror = () => {
  tierLabel.innerText = "Disconnected…";
};
