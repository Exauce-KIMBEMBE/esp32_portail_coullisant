#define DECODE_NEC

#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <RTClib.h>
#include <IRremote.hpp>
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

#define CMD_ouverture  0x08 // touche 6
#define CMD_Fermeture  0x5A // touche 2
#define CMD_STOP       0x1C // touche 5

// ================= GPIO =================

#define PIN_LED_ROUGE        13
#define PIN_LED_VERTE        12
#define PIN_BUZZER           27

#define PIN_BOUTON           14
#define PIN_IR               5

#define PIN_FIN_FERMETURE    4
#define PIN_FIN_OUVERTURE    2

#define PIN_MOTOR_IN1 19
#define PIN_MOTOR_IN2 23

#define PIN_RTC_SDA          21
#define PIN_RTC_SCL          22


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

unsigned long lastSerialMillis = 0;
unsigned long lastPositionCheckMillis = 0;

bool lastButtonPrinted = HIGH;
bool lastClosedLimitPrinted = HIGH;
bool lastOpenLimitPrinted = HIGH;

unsigned long lastBuzzerMillis = 0;
bool buzzerState = false;

const unsigned long BUZZER_ON_TIME = 100;
const unsigned long BUZZER_OFF_TIME = 100;

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

int motorPWM = 70;

void updateSpeed() {
  motorPWM = map(motorSpeedPercent, 10, 100, 80, 255);
}

void buzzerIntermittentLoop() {
  if (!motorRunning) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerState = false;
    return;
  }

  unsigned long interval = buzzerState ? BUZZER_ON_TIME : BUZZER_OFF_TIME;

  if (millis() - lastBuzzerMillis >= interval) {
    lastBuzzerMillis = millis();
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
  }
}

// ================= CAPTEURS =================

bool isClosedLimit() {
  return digitalRead(PIN_FIN_FERMETURE) == LOW;
}

bool isOpenLimit() {
  return digitalRead(PIN_FIN_OUVERTURE) == LOW;
}


// ================= Motor =================

void setupMotorDriver() {
  ledcAttach(PIN_MOTOR_IN1, 1000, 8);
  ledcAttach(PIN_MOTOR_IN2, 1000, 8);

  ledcWrite(PIN_MOTOR_IN1, 0);
  ledcWrite(PIN_MOTOR_IN2, 0);
}


void disableMotor() {
  ledcWrite(PIN_MOTOR_IN1, 0);
  ledcWrite(PIN_MOTOR_IN2, 0);
}

// ================= MOTEUR =================

void startMotor(bool openDirection, String source) {
  directionOpen = openDirection;
  motorRunning = true;
  motorStartMillis = millis();
  autoCloseWaiting = false;

  updateSpeed();

  if (openDirection) {
    ledcWrite(PIN_MOTOR_IN1, motorPWM);
    ledcWrite(PIN_MOTOR_IN2, 0);
  } else {
    ledcWrite(PIN_MOTOR_IN1, 0);
    ledcWrite(PIN_MOTOR_IN2, motorPWM);
  }

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
}

// ================= COMMANDES =================

void commandOpen(String source) {
  if (motorRunning) return;

  if (gateState == "ouvert" && isOpenLimit()) return;

  startMotor(true, source);
}


void commandClose(String source) {
  if (motorRunning) return;

  if (gateState == "fermé" && isClosedLimit()) return;

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
    return;
  }

  if (isClosedLimit()) {
    gateState = "fermé";
    commandOpen(source);
    return;
  }

  if (isOpenLimit()) {
    gateState = "ouvert";
    commandClose(source);
    return;
  }

  if (gateState == "fermé") {
    commandOpen(source);
  } else if (gateState == "ouvert") {
    commandClose(source);
  } else {
    commandClose(source);
  }
}


void serialStatusLoop() {
  if (millis() - lastSerialMillis < 1000) return;
  lastSerialMillis = millis();

  Serial.println("------ ETAT SYSTEME ------");
  Serial.print("Portail : ");
  Serial.println(gateState);

  Serial.print("Moteur : ");
  Serial.println(motorState);

  Serial.print("En mouvement : ");
  Serial.println(motorRunning ? "OUI" : "NON");

  Serial.print("Bouton : ");
  Serial.println(digitalRead(PIN_BOUTON) == LOW ? "APPUYE" : "RELACHE");

  Serial.print("Fin fermé : ");
  Serial.println(isClosedLimit() ? "ACTIF" : "INACTIF");

  Serial.print("Fin ouvert : ");
  Serial.println(isOpenLimit() ? "ACTIF" : "INACTIF");

  Serial.print("Vitesse : ");
  Serial.print(motorSpeedPercent);
  Serial.println("%");

  Serial.print("PWM : ");
  Serial.println(motorPWM);

  Serial.print("Derniere commande : ");
  Serial.println(lastCommand);

  Serial.println("--------------------------");
}

void debugInputChangesLoop() {
  bool buttonState = digitalRead(PIN_BOUTON);
  bool closedState = digitalRead(PIN_FIN_FERMETURE);
  bool openState = digitalRead(PIN_FIN_OUVERTURE);

  if (buttonState != lastButtonPrinted) {
    Serial.print("[DEBUG] Bouton = ");
    Serial.println(buttonState == LOW ? "APPUYE" : "RELACHE");
    lastButtonPrinted = buttonState;
  }

  if (closedState != lastClosedLimitPrinted) {
    Serial.print("[DEBUG] Fin fermeture = ");
    Serial.println(closedState == LOW ? "ACTIF" : "INACTIF");
    lastClosedLimitPrinted = closedState;
  }

  if (openState != lastOpenLimitPrinted) {
    Serial.print("[DEBUG] Fin ouverture = ");
    Serial.println(openState == LOW ? "ACTIF" : "INACTIF");
    lastOpenLimitPrinted = openState;
  }
}

// ================= BOUTON =================

void buttonLoop() {
  static bool lastReading = HIGH;
  static bool stableState = HIGH;
  static unsigned long lastDebounceTime = 0;

  bool reading = digitalRead(PIN_BOUTON);

  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > 70) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == LOW) {
        Serial.println("[BOUTON] Appui detecte");
        commandHome("bouton");
      }
    }
  }

  lastReading = reading;
}

// ================= IR =================

void irLoop() {
  if (!IrReceiver.decode()) return;

  Serial.println();
  Serial.println("===== COMMANDE IR RECUE =====");
  Serial.print("Protocole : ");
  Serial.println(IrReceiver.getProtocolString());

  Serial.print("Adresse : 0x");
  Serial.println(IrReceiver.decodedIRData.address, HEX);

  Serial.print("Commande : 0x");
  Serial.println(IrReceiver.decodedIRData.command, HEX);

  Serial.print("Raw data : 0x");
  Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);

  if (IrReceiver.decodedIRData.protocol == NEC) {
    uint8_t command = IrReceiver.decodedIRData.command;

    if (command == CMD_ouverture) {
      Serial.println("Action IR : OUVERTURE");
      commandOpen("IR NEC");
    } 
    else if (command == CMD_Fermeture) {
      Serial.println("Action IR : FERMETURE");
      commandClose("IR NEC");
    } 
    else if (command == CMD_STOP) {
      Serial.println("Action IR : STOP");
      commandStop("IR NEC");
    }
    else {
      Serial.println("IR reconnu mais commande inconnue");
    }
  } else {
    Serial.println("Protocole IR non NEC");
  }

  Serial.println("=============================");
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


void positionProtectionLoop() {
  if (millis() - lastPositionCheckMillis < 300) return;
  lastPositionCheckMillis = millis();

  if (motorRunning) return;

  if (gateState == "fermé" && !isClosedLimit()) {
    Serial.println("[SECURITE] Portail ferme deplace manuellement -> fermeture");
    addLog("Déplacement manuel détecté depuis fermé", "sécurité");
    commandClose("sécurité position");
    return;
  }

  if (gateState == "ouvert" && !isOpenLimit()) {
    Serial.println("[SECURITE] Portail ouvert deplace manuellement -> ouverture");
    addLog("Déplacement manuel détecté depuis ouvert", "sécurité");
    commandOpen("sécurité position");
    return;
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
  bool obs = false;

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

  Serial.println();
  Serial.println("===== COMMANDE WEB RECUE =====");
  Serial.print("Body brut : ");
  Serial.println(b);

  String cmd = getJsonString(b, "command");
  String source = getJsonString(b, "source");

  if (source == "") source = "web";

  Serial.print("Commande extraite : ");
  Serial.println(cmd);
  Serial.print("Source : ");
  Serial.println(source);

  if (cmd == "open") {
    Serial.println("Action : OUVERTURE");
    commandOpen(source);
  }
  else if (cmd == "close") {
    Serial.println("Action : FERMETURE");
    commandClose(source);
  }
  else if (cmd == "stop") {
    Serial.println("Action : STOP");
    commandStop(source);
  }
  else if (cmd == "home") {
    Serial.println("Action : HOME");
    commandHome(source);
  }
  else {
    Serial.println("ERREUR : commande inconnue");
  }

  Serial.println("==============================");

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



void initialClose() {
  if (motorRunning) return;

  gateState = "mouvement";
  motorState = "fermeture";
  lastCommand = "initialisation";

  addLog("Fermeture initiale", "système");

  startMotor(false, "initialisation");
}


// ================= SETUP =================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("===== DEMARRAGE PORTAIL CONNECTE =====");

  pinMode(PIN_LED_ROUGE, OUTPUT);
  pinMode(PIN_LED_VERTE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  pinMode(PIN_BOUTON, INPUT_PULLUP);
  pinMode(PIN_IR, INPUT);

  pinMode(PIN_FIN_FERMETURE, INPUT_PULLUP);
  pinMode(PIN_FIN_OUVERTURE, INPUT_PULLUP);

  setupMotorDriver();
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

  IrReceiver.begin(PIN_IR, ENABLE_LED_FEEDBACK);

  loadSettings();
  updateSpeed();

  gateState = "inconnu";
  motorState = "arrêté";
  autoCloseWaiting = false;

  addLog("Démarrage système", "système");

  if (isClosedLimit()) {
    gateState = "fermé";
    motorState = "arrêté";
    addLog("État initial : fermé", "système");
  } else {
    initialClose();
  }

  startWiFiAP();

  Serial.println("[SETUP] Serveur Web pret");
  Serial.println("Connecte-toi au WiFi : Portail_Connecte");
  Serial.println("Adresse : http://192.168.4.1");

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

  movementSecurityLoop();
  positionProtectionLoop();

  autoCloseLoop();
  ledsLoop();
  buzzerIntermittentLoop();

  debugInputChangesLoop();
  serialStatusLoop();
}
