#define DECODE_NEC

#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <RTClib.h>
#include <IRremote.hpp>
#include <TMCStepper.h>
#include <Preferences.h>

// ================= WIFI =================

const char* AP_SSID = "Portail_Connecte";
const char* AP_PASSWORD = "12345678";

IPAddress localIP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);
Preferences prefs;
RTC_DS1307 rtc;

// ================= IR NEC =================

#define CMD_ouverture  0x08
#define CMD_Fermeture  0x5A
#define CMD_STOP       0x1C

// ================= GPIO =================

#define PIN_LED_ROUGE        13
#define PIN_LED_VERTE        12
#define PIN_BUZZER           14

#define PIN_BOUTON           27
#define PIN_IR               26

#define PIN_FIN_FERMETURE    25
#define PIN_FIN_OUVERTURE    33

#define PIN_ULTRASON_TRIG    32
#define PIN_ULTRASON_ECHO    35

#define PIN_TMC_STEP         18
#define PIN_TMC_DIR          19
#define PIN_TMC_EN           23

#define PIN_TMC_RX           16
#define PIN_TMC_TX           17

#define PIN_RTC_SDA          21
#define PIN_RTC_SCL          22

// ================= TMC2209 =================

#define R_SENSE 0.11f
#define DRIVER_ADDRESS 0b00

HardwareSerial TMCSerial(2);
TMC2209Stepper driver(&TMCSerial, R_SENSE, DRIVER_ADDRESS);

const int MOTOR_FULL_STEPS = 200;
const int MICROSTEPS = 16;
const int STEPS_PER_REV = MOTOR_FULL_STEPS * MICROSTEPS;

// ================= PARAMETRES =================

int motorSpeedPercent = 50;
int obstacleDistanceCm = 40;
int autoCloseDelaySec = 30;

unsigned long stepIntervalUs = 1200;
unsigned long lastStepMicros = 0;

unsigned long gateOpenedMillis = 0;
unsigned long motorStartMillis = 0;

const unsigned long MAX_MOVE_TIME = 60000;

bool motorRunning = false;
bool directionOpen = true;
bool autoCloseWaiting = false;
bool blinkState = false;

unsigned long lastBlinkMillis = 0;

String gateState = "fermé";
String motorState = "arrêté";
String lastCommand = "--";

// ================= HISTORIQUE =================

const int MAX_LOGS = 40;

struct LogEntry {
  String dateTime;
  String action;
  String source;
};

LogEntry logs[MAX_LOGS];
int logIndex = 0;
int logCount = 0;

// ================= OUTILS =================

String nowString() {
  DateTime now = rtc.now();

  char buffer[25];
  sprintf(
    buffer,
    "%02d/%02d/%04d %02d:%02d:%02d",
    now.day(), now.month(), now.year(),
    now.hour(), now.minute(), now.second()
  );

  return String(buffer);
}

void addLog(String action, String source) {
  logs[logIndex] = { nowString(), action, source };
  logIndex = (logIndex + 1) % MAX_LOGS;

  if (logCount < MAX_LOGS) logCount++;
}

void loadSettings() {
  prefs.begin("portail", false);

  motorSpeedPercent = prefs.getInt("speed", 50);
  obstacleDistanceCm = prefs.getInt("dist", 40);
  autoCloseDelaySec = prefs.getInt("tempo", 30);

  motorSpeedPercent = constrain(motorSpeedPercent, 10, 100);
  obstacleDistanceCm = constrain(obstacleDistanceCm, 10, 150);
  autoCloseDelaySec = constrain(autoCloseDelaySec, 5, 600);
}

void saveSettings() {
  prefs.putInt("speed", motorSpeedPercent);
  prefs.putInt("dist", obstacleDistanceCm);
  prefs.putInt("tempo", autoCloseDelaySec);
}

void updateSpeed() {
  stepIntervalUs = map(motorSpeedPercent, 10, 100, 2500, 350);
}

// ================= CAPTEURS =================

bool isClosedLimit() {
  return digitalRead(PIN_FIN_FERMETURE) == LOW;
}

bool isOpenLimit() {
  return digitalRead(PIN_FIN_OUVERTURE) == LOW;
}

long readDistanceCm() {
  digitalWrite(PIN_ULTRASON_TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(PIN_ULTRASON_TRIG, HIGH);
  delayMicroseconds(10);

  digitalWrite(PIN_ULTRASON_TRIG, LOW);

  long duration = pulseIn(PIN_ULTRASON_ECHO, HIGH, 25000);

  if (duration == 0) return 999;

  return duration * 0.034 / 2;
}

bool obstacleDetected() {
  return readDistanceCm() <= obstacleDistanceCm;
}

// ================= TMC2209 =================

void setupTMC2209() {
  TMCSerial.begin(115200, SERIAL_8N1, PIN_TMC_RX, PIN_TMC_TX);

  driver.begin();
  driver.toff(5);
  driver.rms_current(700);
  driver.microsteps(MICROSTEPS);
  driver.en_spreadCycle(false);
  driver.pwm_autoscale(true);

  digitalWrite(PIN_TMC_EN, HIGH);
}

void enableMotor() {
  digitalWrite(PIN_TMC_EN, LOW);
}

void disableMotor() {
  digitalWrite(PIN_TMC_EN, HIGH);
  digitalWrite(PIN_TMC_STEP, LOW);
}

// ================= MOTEUR =================

void startMotor(bool openDirection, String source) {
  directionOpen = openDirection;
  motorRunning = true;
  motorStartMillis = millis();
  autoCloseWaiting = false;

  digitalWrite(PIN_TMC_DIR, openDirection ? HIGH : LOW);
  enableMotor();

  gateState = "mouvement";
  motorState = openDirection ? "ouverture" : "fermeture";
  lastCommand = source;

  addLog(openDirection ? "Ouverture" : "Fermeture", source);
}

void stopMotor(String reason, String source) {
  motorRunning = false;
  motorState = "arrêté";
  disableMotor();

  addLog("Stop : " + reason, source);
}

void motorStepLoop() {
  if (!motorRunning) return;

  if (micros() - lastStepMicros >= stepIntervalUs) {
    lastStepMicros = micros();

    digitalWrite(PIN_TMC_STEP, HIGH);
    delayMicroseconds(4);
    digitalWrite(PIN_TMC_STEP, LOW);
  }
}

void movementSecurityLoop() {
  if (!motorRunning) return;

  if (millis() - motorStartMillis > MAX_MOVE_TIME) {
    stopMotor("temps maximum", "sécurité");
    gateState = "inconnu";
    return;
  }

  if (directionOpen && isOpenLimit()) {
    stopMotor("fin ouverture", "capteur");
    gateState = "ouvert";
    gateOpenedMillis = millis();
    autoCloseWaiting = true;
    addLog("Portail ouvert", "capteur ouverture");
    return;
  }

  if (!directionOpen && isClosedLimit()) {
    stopMotor("fin fermeture", "capteur");
    gateState = "fermé";
    autoCloseWaiting = false;
    addLog("Portail fermé", "capteur fermeture");
    return;
  }

  if (!directionOpen && obstacleDetected()) {
    stopMotor("obstacle", "ultrason");
    gateState = "obstacle";
    addLog("Obstacle détecté", "ultrason");

    delay(300);

    startMotor(true, "sécurité obstacle");
  }
}

// ================= COMMANDES =================

void commandOpen(String source) {
  if (motorRunning) return;
  if (gateState == "ouvert") return;

  startMotor(true, source);
}

void commandClose(String source) {
  if (motorRunning) return;
  if (gateState == "fermé") return;

  startMotor(false, source);
}

void commandStop(String source) {
  if (motorRunning) {
    stopMotor("commande stop", source);
  }
}

void commandHome(String source) {
  if (motorRunning) {
    commandStop(source);
  } else if (gateState == "fermé") {
    commandOpen(source);
  } else if (gateState == "ouvert") {
    commandClose(source);
  } else {
    commandOpen(source);
  }
}

// ================= BOUTON =================

void buttonLoop() {
  static bool lastState = HIGH;
  static unsigned long lastChange = 0;

  bool state = digitalRead(PIN_BOUTON);

  if (state != lastState) {
    lastChange = millis();
  }

  if ((millis() - lastChange) > 70) {
    if (lastState == HIGH && state == LOW) {
      commandHome("bouton");
    }
  }

  lastState = state;
}

// ================= IR =================

void irLoop() {
  if (!IrReceiver.decode()) return;

  if (IrReceiver.decodedIRData.protocol == NEC) {
    uint8_t command = IrReceiver.decodedIRData.command;

    if (command == CMD_ouverture) {
      commandOpen("IR NEC");
    } 
    else if (command == CMD_Fermeture) {
      commandClose("IR NEC");
    } 
    else if (command == CMD_STOP) {
      commandStop("IR NEC");
    }
  }

  IrReceiver.resume();
}

// ================= AUTO CLOSE =================

void autoCloseLoop() {
  if (!autoCloseWaiting) return;
  if (motorRunning) return;
  if (gateState != "ouvert") return;

  if (millis() - gateOpenedMillis >= autoCloseDelaySec * 1000UL) {
    commandClose("fermeture auto");
  }
}

// ================= VOYANTS =================

void ledsLoop() {
  if (millis() - lastBlinkMillis >= 500) {
    lastBlinkMillis = millis();
    blinkState = !blinkState;
  }

  if (motorRunning) {
    digitalWrite(PIN_LED_ROUGE, blinkState);
    digitalWrite(PIN_LED_VERTE, LOW);
    digitalWrite(PIN_BUZZER, HIGH);
    return;
  }

  if (gateState == "fermé") {
    digitalWrite(PIN_LED_ROUGE, HIGH);
    digitalWrite(PIN_LED_VERTE, LOW);
  } else if (gateState == "ouvert") {
    digitalWrite(PIN_LED_ROUGE, LOW);
    digitalWrite(PIN_LED_VERTE, HIGH);
  } else if (gateState == "obstacle") {
    digitalWrite(PIN_LED_ROUGE, blinkState);
    digitalWrite(PIN_LED_VERTE, LOW);
  } else {
    digitalWrite(PIN_LED_ROUGE, LOW);
    digitalWrite(PIN_LED_VERTE, LOW);
  }

  digitalWrite(PIN_BUZZER, LOW);
}

String ledState(int pin, bool blink) {
  if (blink) return "blink";
  return digitalRead(pin) ? "on" : "off";
}

// ================= WEB =================

void serveFile(String path, String type) {
  File file = SPIFFS.open(path, "r");

  if (!file) {
    server.send(404, "text/plain", "Fichier introuvable");
    return;
  }

  server.streamFile(file, type);
  file.close();
}


void handleRoot() {
  serveFile("/index.html", "text/html");
}

void handleCSS() {
  serveFile("/style.css", "text/css");
}

void handleJS() {
  serveFile("/script.js", "application/javascript");
}

String body() {
  if (server.hasArg("plain")) return server.arg("plain");
  return "";
}

String getJsonString(String b, String key) {
  String k = "\"" + key + "\"";
  int i = b.indexOf(k);
  if (i < 0) return "";

  int c = b.indexOf(":", i);
  int q1 = b.indexOf("\"", c + 1);
  int q2 = b.indexOf("\"", q1 + 1);

  if (q1 < 0 || q2 < 0) return "";

  return b.substring(q1 + 1, q2);
}

int getJsonInt(String b, String key, int def) {
  String k = "\"" + key + "\"";
  int i = b.indexOf(k);
  if (i < 0) return def;

  int c = b.indexOf(":", i);
  int e = b.indexOf(",", c);
  if (e < 0) e = b.indexOf("}", c);

  String v = b.substring(c + 1, e);
  v.trim();

  return v.toInt();
}

void handleStatus() {
  bool obs = obstacleDetected();

  String json = "{";
  json += "\"gateState\":\"" + gateState + "\",";
  json += "\"motorState\":\"" + motorState + "\",";
  json += "\"dateTime\":\"" + nowString() + "\",";
  json += "\"lastCommand\":\"" + lastCommand + "\",";
  json += "\"obstacle\":" + String(obs ? "true" : "false") + ",";
  json += "\"motorSpeedPercent\":" + String(motorSpeedPercent) + ",";
  json += "\"obstacleDistanceCm\":" + String(obstacleDistanceCm) + ",";
  json += "\"autoCloseDelay\":" + String(autoCloseDelaySec) + ",";
  json += "\"redLed\":\"" + ledState(PIN_LED_ROUGE, motorRunning) + "\",";
  json += "\"greenLed\":\"" + ledState(PIN_LED_VERTE, false) + "\",";
  json += "\"buzzer\":\"" + String(digitalRead(PIN_BUZZER) ? "on" : "off") + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleCommand() {
  String b = body();

  String cmd = getJsonString(b, "command");
  String source = getJsonString(b, "source");

  if (source == "") source = "web";

  if (cmd == "open") commandOpen(source);
  else if (cmd == "close") commandClose(source);
  else if (cmd == "stop") commandStop(source);
  else if (cmd == "home") commandHome(source);

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSettings() {
  String b = body();

  motorSpeedPercent = constrain(getJsonInt(b, "motorSpeedPercent", motorSpeedPercent), 10, 100);
  obstacleDistanceCm = constrain(getJsonInt(b, "obstacleDistanceCm", obstacleDistanceCm), 10, 150);
  autoCloseDelaySec = constrain(getJsonInt(b, "autoCloseDelay", autoCloseDelaySec), 5, 600);

  updateSpeed();
  saveSettings();

  addLog("Réglages modifiés", "web");

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleLogs() {
  String json = "[";

  int start = (logIndex - logCount + MAX_LOGS) % MAX_LOGS;

  for (int i = 0; i < logCount; i++) {
    int idx = (start + i) % MAX_LOGS;

    if (i > 0) json += ",";

    json += "{";
    json += "\"dateTime\":\"" + logs[idx].dateTime + "\",";
    json += "\"action\":\"" + logs[idx].action + "\",";
    json += "\"source\":\"" + logs[idx].source + "\"";
    json += "}";
  }

  json += "]";

  server.send(200, "application/json", json);
}

// ================= WIFI =================

void startWiFiAP() {
  WiFi.mode(WIFI_AP);

  WiFi.softAPConfig(localIP, gateway, subnet);

  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD);

  if (ok) {
    Serial.println("Point d'accès WiFi démarré");
    Serial.print("Nom WiFi : ");
    Serial.println(AP_SSID);
    Serial.print("Mot de passe : ");
    Serial.println(AP_PASSWORD);
    Serial.print("Adresse IP : ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Erreur création WiFi AP");
  }
}

// ================= SETUP =================

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_ROUGE, OUTPUT);
  pinMode(PIN_LED_VERTE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  pinMode(PIN_BOUTON, INPUT_PULLUP);
  pinMode(PIN_IR, INPUT);

  pinMode(PIN_FIN_FERMETURE, INPUT_PULLUP);
  pinMode(PIN_FIN_OUVERTURE, INPUT_PULLUP);

  pinMode(PIN_ULTRASON_TRIG, OUTPUT);
  pinMode(PIN_ULTRASON_ECHO, INPUT);

  pinMode(PIN_TMC_STEP, OUTPUT);
  pinMode(PIN_TMC_DIR, OUTPUT);
  pinMode(PIN_TMC_EN, OUTPUT);

  disableMotor();

  Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);

  if (!rtc.begin()) {
    Serial.println("RTC DS1307 non détecté");
  }

  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("Erreur SPIFFS");
  }

  setupTMC2209();

  IrReceiver.begin(PIN_IR, ENABLE_LED_FEEDBACK);

  loadSettings();
  updateSpeed();

  gateState = "fermé";
  motorState = "arrêté";
  autoCloseWaiting = false;

  addLog("Démarrage système", "système");
  addLog("État initial : fermé", "système");

  startWiFiAP();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/style.css", HTTP_GET, handleCSS);
  server.on("/script.js", HTTP_GET, handleJS);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/command", HTTP_POST, handleCommand);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/logs", HTTP_GET, handleLogs);

  server.begin();
}

// ================= LOOP =================

void loop() {
  server.handleClient();

  buttonLoop();
  irLoop();

  motorStepLoop();
  movementSecurityLoop();

  autoCloseLoop();
  ledsLoop();
}
