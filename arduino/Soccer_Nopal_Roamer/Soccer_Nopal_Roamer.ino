/*
 * =========================================================================================
 * Project: Roamer N' Destroyer - Roamer Mode (Soccer Nopal)
 * Reverse-Engineered & Reconstructed from ESP32 Firmware Dump
 * Target Hardware: ESP32-D0WD-V3 (4MB Flash, Dual Core Xtensa @ 240MHz)
 * =========================================================================================
 *
 * System Features:
 *   - Differential Drive Robot (Left Motor MLA/MLB, Right Motor MRA/MRB)
 *   - Wireless Control: Sony PS3 DualShock Controller via Bluetooth L2CAP/HID
 *   - Built-in Web Configuration AP (SSID: "RND OS [R' Mode]", Pass: "M3raky4T")
 *   - Web Mode Activation: Press onboard BOOT button (GPIO 0) 3 times
 *   - Automatic Web Mode shutdown when PS3 Controller connects
 *   - Non-Volatile Storage (NVS / Preferences) for live tuning without re-flashing
 * =========================================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Ps3Controller.h>

// -----------------------------------------------------------------------------
// Preferences & Configuration Storage
// -----------------------------------------------------------------------------
Preferences prefs;
WebServer server(80);

// Hardware Pin Definitions (Stored in NVS)
uint32_t pin_mra = 25; // Right Motor Forward / PWM A
uint32_t pin_mrb = 26; // Right Motor Reverse / PWM B
uint32_t pin_mla = 33; // Left Motor Forward / PWM A
uint32_t pin_mlb = 32; // Left Motor Reverse / PWM B
const int PIN_BOOT = 0; // BOOT button for toggling Web AP

// LEDC PWM Channels
const int CH_MRA = 0;
const int CH_MRB = 1;
const int CH_MLA = 2;
const int CH_MLB = 3;
const int PWM_FREQ = 1000;
const int PWM_RES  = 8; // 8-bit (0-255)

// Bluetooth PS3 MAC Address
String ps3_mac = "22:3d:75:2a:f0:b2";

// Acceleration & Deceleration Ramps (ms)
uint32_t ramp_up   = 200; // 50(Cepat), 100(Sedang), 150(Lambat), 200(Sangat Lambat)
uint32_t ramp_down = 250; // 150(Halus/Anti-Jungkir), 250(Sangat Halus)

// Motion Speed Parameters (Calibrated in NVS)
uint8_t F_FAST_L    = 255, F_FAST_R    = 255;
uint8_t F_MEDIUM_L  = 61,  F_MEDIUM_R  = 61;
uint8_t F_LOW_L     = 20,  F_LOW_R     = 20;

uint8_t B_FAST_L    = 171, B_FAST_R    = 171;
uint8_t B_MEDIUM_L  = 61,  B_MEDIUM_R  = 61;
uint8_t B_LOW_L     = 20,  B_LOW_R     = 20;

uint8_t TR_LOW_LR   = 35;
uint8_t TR_MEDIUM_LR= 65;
uint8_t TR_FAST_LR  = 100;

uint8_t CRV_LOW_L   = 255, CRV_LOW_R   = 32;
uint8_t CRV_MEDIUM_L= 0,   CRV_MEDIUM_R= 0;
uint8_t CRV_FAST_L  = 255, CRV_FAST_R  = 32;

uint8_t DEFAULT_LR  = 0;

// State Variables
bool webModeEnabled = false;
int targetSpeedL = 0;
int targetSpeedR = 0;
float currentSpeedL = 0;
float currentSpeedR = 0;

// BOOT Button Multi-click Detection
int bootClickCount = 0;
unsigned long lastBootPressTime = 0;
const unsigned long MULTI_CLICK_WINDOW = 800; // ms to complete 3 clicks

// -----------------------------------------------------------------------------
// Motor Output Functions
// -----------------------------------------------------------------------------
void setMotorOutputs(int speedL, int speedR) {
  // Left Motor
  if (speedL > 0) {
    ledcWrite(CH_MLA, constrain(speedL, 0, 255));
    ledcWrite(CH_MLB, 0);
  } else if (speedL < 0) {
    ledcWrite(CH_MLA, 0);
    ledcWrite(CH_MLB, constrain(-speedL, 0, 255));
  } else {
    ledcWrite(CH_MLA, 0);
    ledcWrite(CH_MLB, 0);
  }

  // Right Motor
  if (speedR > 0) {
    ledcWrite(CH_MRA, constrain(speedR, 0, 255));
    ledcWrite(CH_MRB, 0);
  } else if (speedR < 0) {
    ledcWrite(CH_MRA, 0);
    ledcWrite(CH_MRB, constrain(-speedR, 0, 255));
  } else {
    ledcWrite(CH_MRA, 0);
    ledcWrite(CH_MRB, 0);
  }
}

void initMotors() {
  ledcSetup(CH_MRA, PWM_FREQ, PWM_RES);
  ledcSetup(CH_MRB, PWM_FREQ, PWM_RES);
  ledcSetup(CH_MLA, PWM_FREQ, PWM_RES);
  ledcSetup(CH_MLB, PWM_FREQ, PWM_RES);

  ledcAttachPin(pin_mra, CH_MRA);
  ledcAttachPin(pin_mrb, CH_MRB);
  ledcAttachPin(pin_mla, CH_MLA);
  ledcAttachPin(pin_mlb, CH_MLB);

  setMotorOutputs(0, 0);
}

// -----------------------------------------------------------------------------
// NVS Configuration Load & Save
// -----------------------------------------------------------------------------
void loadConfig() {
  prefs.begin("config", false);
  ps3_mac    = prefs.getString("ps3_mac", "22:3d:75:2a:f0:b2");
  pin_mra    = prefs.getUInt("pin_mra", 25);
  pin_mrb    = prefs.getUInt("pin_mrb", 26);
  pin_mla    = prefs.getUInt("pin_mla", 33);
  pin_mlb    = prefs.getUInt("pin_mlb", 32);
  ramp_up    = prefs.getUInt("ramp_up", 200);
  ramp_down  = prefs.getUInt("ramp_down", 250);

  F_FAST_L   = prefs.getUChar("F_FAST_L", 255);
  F_FAST_R   = prefs.getUChar("F_FAST_R", 255);
  F_MEDIUM_L = prefs.getUChar("F_MEDIUM_L", 61);
  F_MEDIUM_R = prefs.getUChar("F_MEDIUM_R", 61);
  F_LOW_L    = prefs.getUChar("F_LOW_L", 20);
  F_LOW_R    = prefs.getUChar("F_LOW_R", 20);

  B_FAST_L   = prefs.getUChar("B_FAST_L", 171);
  B_FAST_R   = prefs.getUChar("B_FAST_R", 171);
  B_MEDIUM_L = prefs.getUChar("B_MEDIUM_L", 61);
  B_MEDIUM_R = prefs.getUChar("B_MEDIUM_R", 61);
  B_LOW_L    = prefs.getUChar("B_LOW_L", 20);
  B_LOW_R    = prefs.getUChar("B_LOW_R", 20);

  TR_LOW_LR   = prefs.getUChar("TR_LOW_LR", 35);
  TR_MEDIUM_LR= prefs.getUChar("TR_MEDIUM_LR", 65);
  TR_FAST_LR  = prefs.getUChar("TR_FAST_LR", 100);

  CRV_LOW_L   = prefs.getUChar("CRV_LOW_L", 255);
  CRV_LOW_R   = prefs.getUChar("CRV_LOW_R", 32);
  CRV_MEDIUM_L= prefs.getUChar("CRV_MEDIUM_L", 0);
  CRV_MEDIUM_R= prefs.getUChar("CRV_MEDIUM_R", 0);
  CRV_FAST_L  = prefs.getUChar("CRV_FAST_L", 255);
  CRV_FAST_R  = prefs.getUChar("CRV_FAST_R", 32);

  DEFAULT_LR  = prefs.getUChar("DEFAULT_LR", 0);
  prefs.end();
}

void resetConfig() {
  prefs.begin("config", false);
  prefs.clear();
  prefs.end();
  loadConfig();
}

// -----------------------------------------------------------------------------
// Web Interface HTML Builder
// -----------------------------------------------------------------------------
String buildSliderRow(const String &key, const String &label, uint8_t value) {
  String s = "<div class='slider-row'>";
  s += "<div class='slider-label'>" + label + "</div>";
  s += "<input type='range' id='" + key + "' disabled style='accent-color:#CAD66A;' value='" + String(value) + "'>";
  s += "<input type='number' id='" + key + "_num' min='0' max='255' value='" + String(value) + "' oninput=\"updateFromNumber('" + key + "')\" onblur=\"updateFromNumber('" + key + "')\">";
  s += "</div>";
  return s;
}

String generateHtml() {
  String h = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/><meta charset='UTF-8'/><title>RND Config</title>";
  h += "<style>:root{--bg:#010d08;--card:#04180f;--text:#CAD66A;--soft:#9aa656;--accent:#CAD66A;--line:#45602d;--sec:#143a26;--shadow:0 2px 6px rgba(0,0,0,0.35);--sep:22px;}body{font-family:'Segoe UI',sans-serif;margin:0;padding:0;background:var(--bg);color:var(--text);}.t{width:100%;position:sticky;top:0;z-index:10;background:var(--card);padding:14px 16px;font-size:18px;font-weight:600;color:var(--accent);box-shadow:var(--shadow);text-align:center;}.page{padding:16px;}.card{background:var(--card);border-radius:12px;padding:16px;box-shadow:var(--shadow);margin-bottom:var(--sep);border:1px solid var(--line);}.card-title{font-weight:600;font-size:16px;margin-bottom:12px;color:var(--accent);}.slider-row{display:grid;grid-template-columns:140px 1fr 70px;gap:10px;margin-bottom:12px;}.input-row{display:grid;grid-template-columns:140px 1fr;gap:10px;margin-bottom:12px;align-items:center;}.slider-label{font-size:14px;font-weight:500;color:var(--text);}input[type=range]{width:100%;margin:0;accent-color:var(--accent);}input[type=number], input[type=text], select{width:100%;padding:6px;font-size:14px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--text);text-align:center;}.btn{padding:10px 16px;border:none;border-radius:8px;background:var(--accent);color:var(--bg);cursor:pointer;transition:0.25s;margin-right:8px;margin-bottom:10px;font-size:14px;font-weight:600;}.btn:hover{filter:brightness(0.9);}.btn.secondary{background:var(--sec);color:var(--text);}.footer{font-size:12px;color:var(--soft);margin-top:8px;text-align:center;}@media(max-width:520px){.slider-row, .input-row{grid-template-columns:1fr;gap:6px;} input[type=number], input[type=text], select{width:100%;} .btn{width:100%;}}</style></head><body>";
  h += "<div class='t'>Roamer N' Destroyer - Roamer Mode</div><div class='page'>";

  // System Config Card
  h += "<div class='card'><div class='card-title'>System & Hardware Config</div>";
  h += "<div class='input-row'><div class='slider-label'>PS3 MAC Address</div><input type='text' id='ps3_mac' value='" + ps3_mac + "'></div>";
  h += "<div class='input-row'><div class='slider-label'>Pin MRA</div><input type='number' id='pin_mra' value='" + String(pin_mra) + "'></div>";
  h += "<div class='input-row'><div class='slider-label'>Pin MRB</div><input type='number' id='pin_mrb' value='" + String(pin_mrb) + "'></div>";
  h += "<div class='input-row'><div class='slider-label'>Pin MLA</div><input type='number' id='pin_mla' value='" + String(pin_mla) + "'></div>";
  h += "<div class='input-row'><div class='slider-label'>Pin MLB</div><input type='number' id='pin_mlb' value='" + String(pin_mlb) + "'></div>";
  h += "<div class='input-row'><div class='slider-label'>Ramp Up Time</div><select id='ramp_up'>";
  h += "<option value='50'" + String(ramp_up==50?" selected":"") + ">50 (Cepat)</option>";
  h += "<option value='100'" + String(ramp_up==100?" selected":"") + ">100 (Sedang)</option>";
  h += "<option value='150'" + String(ramp_up==150?" selected":"") + ">150 (Lambat)</option>";
  h += "<option value='200'" + String(ramp_up==200?" selected":"") + ">200 (Sangat Lambat)</option>";
  h += "</select></div>";
  h += "<div class='input-row'><div class='slider-label'>Ramp Down Time (Mundur)</div><select id='ramp_down'>";
  h += "<option value='150'" + String(ramp_down==150?" selected":"") + ">150 (Halus/Anti-Jungkir)</option>";
  h += "<option value='250'" + String(ramp_down==250?" selected":"") + ">250 (Sangat Halus)</option>";
  h += "</select></div></div>";

  // Forward Speeds
  h += "<div class='card'><div class='card-title'>Forward</div>";
  h += buildSliderRow("F_FAST_L", "F_FAST_L", F_FAST_L);
  h += buildSliderRow("F_FAST_R", "F_FAST_R", F_FAST_R);
  h += buildSliderRow("F_MEDIUM_L", "F_MEDIUM_L", F_MEDIUM_L);
  h += buildSliderRow("F_MEDIUM_R", "F_MEDIUM_R", F_MEDIUM_R);
  h += buildSliderRow("F_LOW_L", "F_LOW_L", F_LOW_L);
  h += buildSliderRow("F_LOW_R", "F_LOW_R", F_LOW_R);
  h += "</div>";

  // Backward Speeds
  h += "<div class='card'><div class='card-title'>Backward</div>";
  h += buildSliderRow("B_FAST_L", "B_FAST_L", B_FAST_L);
  h += buildSliderRow("B_FAST_R", "B_FAST_R", B_FAST_R);
  h += buildSliderRow("B_MEDIUM_L", "B_MEDIUM_L", B_MEDIUM_L);
  h += buildSliderRow("B_MEDIUM_R", "B_MEDIUM_R", B_MEDIUM_R);
  h += buildSliderRow("B_LOW_L", "B_LOW_L", B_LOW_L);
  h += buildSliderRow("B_LOW_R", "B_LOW_R", B_LOW_R);
  h += "</div>";

  // Turn & Curve
  h += "<div class='card'><div class='card-title'>Curve Movement / Turn</div>";
  h += buildSliderRow("TR_LOW_LR", "TR_LOW_LR", TR_LOW_LR);
  h += buildSliderRow("TR_MEDIUM_LR", "TR_MEDIUM_LR", TR_MEDIUM_LR);
  h += buildSliderRow("TR_FAST_LR", "TR_FAST_LR", TR_FAST_LR);
  h += buildSliderRow("CRV_LOW_L", "CRV_LOW_L", CRV_LOW_L);
  h += buildSliderRow("CRV_LOW_R", "CRV_LOW_R", CRV_LOW_R);
  h += buildSliderRow("CRV_MEDIUM_L", "CRV_MEDIUM_L", CRV_MEDIUM_L);
  h += buildSliderRow("CRV_MEDIUM_R", "CRV_MEDIUM_R", CRV_MEDIUM_R);
  h += buildSliderRow("CRV_FAST_L", "CRV_FAST_L", CRV_FAST_L);
  h += buildSliderRow("CRV_FAST_R", "CRV_FAST_R", CRV_FAST_R);
  h += "</div>";

  // S_Speed
  h += "<div class='card'><div class='card-title'>S_Speed</div>";
  h += buildSliderRow("DEFAULT_LR", "DEFAULT_LR", DEFAULT_LR);
  h += "</div>";

  // Action Buttons
  h += "<div class='card'>";
  h += "<button class='btn' onclick='saveAll()'>Save</button>";
  h += "<button class='btn secondary' onclick='resetDefaults()'>Reset Defaults</button>";
  h += "<button class='btn secondary' onclick=\"fetch('/reboot').then(()=>alert('Rebooting...'))\">Reboot</button>";
  h += "</div>";

  // Footers
  h += "<div class='footer'>Tekan BOOT 3x untuk toggle Web Mode.<br>Web mati otomatis bila PS3 terkoneksi.</div>";
  h += "<div class='footer'>Go Go Go Jaya Jaya Jaya</div>";
  h += "<div class='footer'>ID ROBOT <span id='lbl_mac'>" + WiFi.macAddress() + "</span></div>";
  h += "</div>";

  // JavaScript
  h += "<script>function updateFromNumber(id){let num=document.getElementById(id+'_num');let range=document.getElementById(id);let v=parseInt(num.value);if(isNaN(v))v=0;if(v<0)v=0;if(v>255)v=255;num.value=v;range.value=v;}function saveAll(){var p='?';const keys=[\"F_FAST_L\",\"F_FAST_R\",\"F_MEDIUM_L\",\"F_MEDIUM_R\",\"F_LOW_L\",\"F_LOW_R\",\"B_FAST_L\",\"B_FAST_R\",\"B_MEDIUM_L\",\"B_MEDIUM_R\",\"B_LOW_L\",\"B_LOW_R\",\"TR_LOW_LR\",\"TR_MEDIUM_LR\",\"TR_FAST_LR\",\"CRV_LOW_L\",\"CRV_LOW_R\",\"CRV_MEDIUM_L\",\"CRV_MEDIUM_R\",\"CRV_FAST_L\",\"CRV_FAST_R\",\"DEFAULT_LR\"];for(let k of keys){p+=k+'='+document.getElementById(k+'_num').value+'&';}p+='ps3_mac='+encodeURIComponent(document.getElementById('ps3_mac').value)+'&';p+='pin_mra='+document.getElementById('pin_mra').value+'&';p+='pin_mrb='+document.getElementById('pin_mrb').value+'&';p+='pin_mla='+document.getElementById('pin_mla').value+'&';p+='pin_mlb='+document.getElementById('pin_mlb').value+'&';p+='ramp_up='+document.getElementById('ramp_up').value+'&';p+='ramp_down='+document.getElementById('ramp_down').value+'&';fetch('/save'+p).then(r=>r.text()).then(t=>{alert('Saved successfully!');});}function resetDefaults(){fetch('/reset').then(()=>location.reload());}</script>";
  h += "</body></html>";
  return h;
}

// -----------------------------------------------------------------------------
// Web Server Route Handlers
// -----------------------------------------------------------------------------
void handleRoot() {
  server.send(200, "text/html", generateHtml());
}

void handleSave() {
  prefs.begin("config", false);
  if (server.hasArg("ps3_mac")) {
    ps3_mac = server.arg("ps3_mac");
    prefs.putString("ps3_mac", ps3_mac);
  }
  if (server.hasArg("pin_mra")) { pin_mra = server.arg("pin_mra").toInt(); prefs.putUInt("pin_mra", pin_mra); }
  if (server.hasArg("pin_mrb")) { pin_mrb = server.arg("pin_mrb").toInt(); prefs.putUInt("pin_mrb", pin_mrb); }
  if (server.hasArg("pin_mla")) { pin_mla = server.arg("pin_mla").toInt(); prefs.putUInt("pin_mla", pin_mla); }
  if (server.hasArg("pin_mlb")) { pin_mlb = server.arg("pin_mlb").toInt(); prefs.putUInt("pin_mlb", pin_mlb); }
  if (server.hasArg("ramp_up")) { ramp_up = server.arg("ramp_up").toInt(); prefs.putUInt("ramp_up", ramp_up); }
  if (server.hasArg("ramp_down")) { ramp_down = server.arg("ramp_down").toInt(); prefs.putUInt("ramp_down", ramp_down); }

  const char *keys[] = {
    "F_FAST_L", "F_FAST_R", "F_MEDIUM_L", "F_MEDIUM_R", "F_LOW_L", "F_LOW_R",
    "B_FAST_L", "B_FAST_R", "B_MEDIUM_L", "B_MEDIUM_R", "B_LOW_L", "B_LOW_R",
    "TR_LOW_LR", "TR_MEDIUM_LR", "TR_FAST_LR",
    "CRV_LOW_L", "CRV_LOW_R", "CRV_MEDIUM_L", "CRV_MEDIUM_R", "CRV_FAST_L", "CRV_FAST_R",
    "DEFAULT_LR"
  };

  for (const char *k : keys) {
    if (server.hasArg(k)) {
      uint8_t val = (uint8_t)server.arg(k).toInt();
      prefs.putUChar(k, val);
    }
  }
  prefs.end();

  loadConfig();
  initMotors(); // Re-initialize pins if changed
  Serial.println("Settings saved!");
  server.send(200, "text/plain", "Settings saved!");
}

void handleReset() {
  resetConfig();
  Serial.println("Reset OK");
  server.send(200, "text/plain", "Reset OK");
}

void handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

void enableWebAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("RND OS [R' Mode]", "M3raky4T", 6);
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/reset", handleReset);
  server.on("/reboot", handleReboot);
  server.begin();
  webModeEnabled = true;
  Serial.println("Web AP ENABLED");
}

void disableWebAP() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  webModeEnabled = false;
  Serial.println("Web AP DISABLED");
}

// -----------------------------------------------------------------------------
// PS3 Controller Callbacks & Input Handling
// -----------------------------------------------------------------------------
void onPs3Connect() {
  Serial.println("PS3 connected.");
  if (webModeEnabled) {
    disableWebAP(); // Auto-disable web when PS3 connects
  }
}

void onPs3Disconnect() {
  Serial.println("PS3 disconnected!");
  targetSpeedL = 0;
  targetSpeedR = 0;
}

void processPs3Input() {
  if (!Ps3.isConnected()) {
    targetSpeedL = 0;
    targetSpeedR = 0;
    return;
  }

  // Speed selection modifier:
  // L1 / R1 or Cross/Triangle can select speed level
  bool isFast   = Ps3.data.button.r1 || Ps3.data.button.triangle;
  bool isMedium = Ps3.data.button.l1 || Ps3.data.button.circle;
  bool isLow    = Ps3.data.button.cross || (!isFast && !isMedium);

  // Direction handling (D-pad & Analog Sticks)
  if (Ps3.data.button.up || Ps3.data.analog.stick.ly < -40) {
    // Forward
    if (isFast) {
      targetSpeedL = F_FAST_L;
      targetSpeedR = F_FAST_R;
    } else if (isMedium) {
      targetSpeedL = F_MEDIUM_L;
      targetSpeedR = F_MEDIUM_R;
    } else {
      targetSpeedL = F_LOW_L;
      targetSpeedR = F_LOW_R;
    }
  } else if (Ps3.data.button.down || Ps3.data.analog.stick.ly > 40) {
    // Backward
    if (isFast) {
      targetSpeedL = -B_FAST_L;
      targetSpeedR = -B_FAST_R;
    } else if (isMedium) {
      targetSpeedL = -B_MEDIUM_L;
      targetSpeedR = -B_MEDIUM_R;
    } else {
      targetSpeedL = -B_LOW_L;
      targetSpeedR = -B_LOW_R;
    }
  } else if (Ps3.data.button.left || Ps3.data.analog.stick.lx < -40) {
    // Rotate Left
    int tr = isFast ? TR_FAST_LR : (isMedium ? TR_MEDIUM_LR : TR_LOW_LR);
    targetSpeedL = -tr;
    targetSpeedR = tr;
  } else if (Ps3.data.button.right || Ps3.data.analog.stick.lx > 40) {
    // Rotate Right
    int tr = isFast ? TR_FAST_LR : (isMedium ? TR_MEDIUM_LR : TR_LOW_LR);
    targetSpeedL = tr;
    targetSpeedR = -tr;
  } else {
    // Stop
    targetSpeedL = 0;
    targetSpeedR = 0;
  }
}

// -----------------------------------------------------------------------------
// Arduino Setup & Loop
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n[Soccer Nopal] Initializing Roamer N' Destroyer...");

  pinMode(PIN_BOOT, INPUT_PULLUP);

  loadConfig();
  initMotors();

  // Initialize PS3 Controller Host
  Ps3.attach(onPs3Connect);
  Ps3.attachOnDisconnect(onPs3Disconnect);
  Ps3.begin(ps3_mac.c_str());
  Serial.printf("PS3 Host initialized for MAC: %s\n", ps3_mac.c_str());
}

void loop() {
  // Handle BOOT button triple-click detection
  if (digitalRead(PIN_BOOT) == LOW) {
    delay(50); // Debounce
    if (digitalRead(PIN_BOOT) == LOW) {
      bootClickCount++;
      lastBootPressTime = millis();
      while (digitalRead(PIN_BOOT) == LOW) delay(10);
    }
  }

  // Check if 3 clicks reached
  if (bootClickCount >= 3) {
    bootClickCount = 0;
    if (Ps3.isConnected()) {
      Serial.println("Cannot toggle Web — PS3 connected.");
    } else {
      if (!webModeEnabled) {
        enableWebAP();
      } else {
        disableWebAP();
      }
    }
  }

  // Reset click count after window expires
  if (bootClickCount > 0 && (millis() - lastBootPressTime > MULTI_CLICK_WINDOW)) {
    bootClickCount = 0;
  }

  // Handle Web Server requests
  if (webModeEnabled) {
    server.handleClient();
  }

  // Handle PS3 Controller inputs
  processPs3Input();

  // Apply smooth Acceleration / Deceleration Ramps
  float stepUp   = 255.0f / (ramp_up > 0 ? (ramp_up / 10.0f) : 1.0f);
  float stepDown = 255.0f / (ramp_down > 0 ? (ramp_down / 10.0f) : 1.0f);

  // Left Motor Ramp
  if (currentSpeedL < targetSpeedL) {
    currentSpeedL = min((float)targetSpeedL, currentSpeedL + stepUp);
  } else if (currentSpeedL > targetSpeedL) {
    currentSpeedL = max((float)targetSpeedL, currentSpeedL - stepDown);
  }

  // Right Motor Ramp
  if (currentSpeedR < targetSpeedR) {
    currentSpeedR = min((float)targetSpeedR, currentSpeedR + stepUp);
  } else if (currentSpeedR > targetSpeedR) {
    currentSpeedR = max((float)targetSpeedR, currentSpeedR - stepDown);
  }

  setMotorOutputs((int)currentSpeedL, (int)currentSpeedR);
  delay(10);
}
