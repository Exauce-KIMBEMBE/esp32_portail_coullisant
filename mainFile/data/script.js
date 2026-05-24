let logsCache = [];

const motorSpeed = document.getElementById("motorSpeed");
const speedValue = document.getElementById("speedValue");

const obstacleDistance = document.getElementById("obstacleDistance");
const distanceValue = document.getElementById("distanceValue");

motorSpeed.addEventListener("input", () => {
  speedValue.textContent = motorSpeed.value;
});

obstacleDistance.addEventListener("input", () => {
  distanceValue.textContent = obstacleDistance.value;
});

async function manualReconnect() {
  setConnection(false, "Reconnexion...");
  await updateStatus();
  await updateLogsIfVisible();
}

async function sendCommand(command) {
  try {
    await fetch("/api/command", {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify({
        command: command,
        source: "web"
      })
    });

    await updateStatus();

  } catch (error) {
    setConnection(false);
  }
}

async function saveSettings() {
  try {
    await fetch("/api/settings", {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify({
        motorSpeedPercent: Number(motorSpeed.value),
        obstacleDistanceCm: Number(obstacleDistance.value),
        autoCloseDelay: Number(document.getElementById("autoCloseDelay").value)
      })
    });

    await updateStatus();

  } catch (error) {
    setConnection(false);
  }
}

async function updateStatus() {
  try {
    const response = await fetch("/api/status");

    if (!response.ok) {
      throw new Error("ESP32 non joignable");
    }

    const data = await response.json();

    setConnection(true);

    document.getElementById("rtcTime").textContent = data.dateTime || "--:--:--";
    document.getElementById("gateState").textContent = data.gateState || "Inconnu";
    document.getElementById("motorState").textContent = "Moteur : " + (data.motorState || "--");
    document.getElementById("obstacleState").textContent =
      "Obstacle : " + (data.obstacle ? "Détecté" : "Aucun");

    motorSpeed.value = data.motorSpeedPercent ?? 50;
    speedValue.textContent = data.motorSpeedPercent ?? 50;

    obstacleDistance.value = data.obstacleDistanceCm ?? 40;
    distanceValue.textContent = data.obstacleDistanceCm ?? 40;

    document.getElementById("autoCloseDelay").value = data.autoCloseDelay ?? 30;

    updateGateIndicator(data);
    updateLed("redLed", data.redLed, "red");
    updateLed("greenLed", data.greenLed, "green");
    updateLed("buzzerLed", data.buzzer, "orange");

  } catch (error) {
    setConnection(false);
  }
}

function setConnection(connected, text = null) {
  const element = document.getElementById("connectionText");

  if (connected) {
    element.textContent = text || "ESP32 connecté";
    element.className = "connected";
  } else {
    element.textContent = text || "ESP32 déconnecté";
    element.className = "disconnected";
  }
}

function updateGateIndicator(data) {
  const indicator = document.getElementById("gateIndicator");
  indicator.className = "gate-indicator";

  if (data.obstacle) {
    indicator.classList.add("obstacle");
  } else if (data.gateState === "ouvert") {
    indicator.classList.add("open");
  } else if (data.gateState === "fermé") {
    indicator.classList.add("closed");
  } else if (data.gateState === "mouvement") {
    indicator.classList.add("moving");
  } else {
    indicator.classList.add("unknown");
  }
}

function updateLed(id, state, color) {
  const led = document.getElementById(id);
  led.className = "led";

  if (state === "on") {
    led.classList.add(color);
  } else if (state === "blink") {
    led.classList.add(color, "blink");
  } else {
    led.classList.add("off");
  }
}

function toggleHistory() {
  const box = document.getElementById("historyBox");

  if (box.classList.contains("hidden")) {
    box.classList.remove("hidden");
    updateLogs();
  } else {
    box.classList.add("hidden");
  }
}

async function updateLogsIfVisible() {
  const box = document.getElementById("historyBox");

  if (!box.classList.contains("hidden")) {
    await updateLogs();
  }
}

async function updateLogs() {
  try {
    const response = await fetch("/api/logs");

    if (!response.ok) {
      throw new Error("Historique indisponible");
    }

    logsCache = await response.json();

    const list = document.getElementById("logs");
    list.innerHTML = "";

    logsCache.slice().reverse().forEach(log => {
      const li = document.createElement("li");
      li.textContent = `${log.dateTime} - ${log.action} - ${log.source}`;
      list.appendChild(li);
    });

  } catch (error) {
    setConnection(false);
  }
}

function downloadLogsPDF() {
  if (!logsCache.length) {
    alert("Aucun historique à télécharger");
    return;
  }

  let content = "Historique SlideGate\n\n";

  logsCache.forEach(log => {
    content += `${log.dateTime} - ${log.action} - ${log.source}\n`;
  });

  const pdf = createSimplePDF(content);
  const blob = new Blob([pdf], { type: "application/pdf" });

  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = "historique_portail.pdf";
  link.click();

  URL.revokeObjectURL(link.href);
}

function createSimplePDF(text) {
  const lines = text.split("\n");
  let y = 780;
  let stream = "BT\n/F1 12 Tf\n";

  lines.forEach(line => {
    const safeLine = line.replace(/[()]/g, "");
    stream += `50 ${y} Td (${safeLine}) Tj\n`;
    y -= 18;
  });

  stream += "ET";

  return `%PDF-1.4
1 0 obj
<< /Type /Catalog /Pages 2 0 R >>
endobj
2 0 obj
<< /Type /Pages /Kids [3 0 R] /Count 1 >>
endobj
3 0 obj
<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>
endobj
4 0 obj
<< /Length ${stream.length} >>
stream
${stream}
endstream
endobj
5 0 obj
<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>
endobj
trailer
<< /Root 1 0 R >>
%%EOF`;
}

setInterval(updateStatus, 1500);
updateStatus();
