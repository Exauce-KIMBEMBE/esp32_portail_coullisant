const speedInput = document.getElementById("motorSpeed");
const speedValue = document.getElementById("speedValue");

speedInput.addEventListener("input", () => {
  speedValue.textContent = speedInput.value;
});

async function sendCommand(command) {
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

  updateStatus();
  updateLogs();
}

async function saveSettings() {
  const speed = Number(document.getElementById("motorSpeed").value);
  const delay = Number(document.getElementById("autoCloseDelay").value);

  await fetch("/api/settings", {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({
      motorSpeed: speed,
      autoCloseDelay: delay
    })
  });

  alert("Réglages enregistrés");
}

async function updateStatus() {
  try {
    const response = await fetch("/api/status");
    const data = await response.json();

    document.getElementById("rtcTime").textContent = data.dateTime;
    document.getElementById("gateState").textContent = "Portail : " + data.gateState;
    document.getElementById("motorState").textContent = data.motorState;
    document.getElementById("lastCommand").textContent = data.lastCommand;
    document.getElementById("obstacle").textContent = data.obstacle ? "Détecté" : "Aucun";

    document.getElementById("motorSpeed").value = data.motorSpeed;
    document.getElementById("speedValue").textContent = data.motorSpeed;
    document.getElementById("autoCloseDelay").value = data.autoCloseDelay;

    const light = document.getElementById("stateLight");
    light.className = "light";

    if (data.obstacle) {
      light.classList.add("obstacle");
    } else if (data.gateState === "ouvert") {
      light.classList.add("open");
    } else if (data.gateState === "fermé") {
      light.classList.add("closed");
    } else if (data.gateState === "mouvement") {
      light.classList.add("moving");
    } else {
      light.classList.add("unknown");
    }

  } catch (error) {
    console.log("Erreur status :", error);
  }
}

async function updateLogs() {
  try {
    const response = await fetch("/api/logs");
    const logs = await response.json();

    const list = document.getElementById("logs");
    list.innerHTML = "";

    logs.slice(-8).reverse().forEach(log => {
      const li = document.createElement("li");
      li.textContent = `${log.dateTime} - ${log.action} - ${log.source}`;
      list.appendChild(li);
    });

  } catch (error) {
    console.log("Erreur logs :", error);
  }
}

setInterval(updateStatus, 1000);
setInterval(updateLogs, 5000);

updateStatus();
updateLogs();
