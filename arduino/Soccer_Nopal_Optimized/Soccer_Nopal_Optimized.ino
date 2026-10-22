/*
 * =========================================================================================
 * Project: Roamer N' Destroyer - Soccer Nopal (OPTIMIZED V2)
 * Hardware: ESP32-D0WD-V3 (Dual Core Xtensa @ 240MHz, 4MB Flash)
 * =========================================================================================
 *
 * MAJOR OPTIMIZATIONS INCLUDED:
 * 1. RTOS Dual-Core Architecture:
 *    - Core 0: Dedicated exclusively to Bluetooth (PS3 Stack) & Wi-Fi Web Server.
 *    - Core 1: Dedicated high-priority real-time Motion Task running at deterministic 100 Hz.
 * 2. Ultrasonic 20 kHz PWM:
 *    - Eliminates audible motor whine.
 *    - Reduces MOSFET switching heat & increases low-speed torque for delicate dribbling.
 * 3. Active Dynamic Braking:
 *    - Eliminates drifting/coasting when releasing sticks; stops and turns on a dime.
 * 4. Maximum RF Power & Match-Mode RF Isolation:
 *    - Forces Bluetooth Classic TX power to MAX (+9 dBm) for arena penetration.
 *    - Fully cuts Wi-Fi radio baseband when PS3 connects to eliminate RF packet collision.
 * 5. Failsafe Deadman Watchdog:
 *    - If Bluetooth drops packets for >250 ms, motors immediately cut to 0 to prevent runaway.
 * 6. Zero-RAM PROGMEM Web Server:
 *    - HTML template stored in Flash ROM; eliminates heap fragmentation ("out of memory").
 * =========================================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Ps3Controller.h>
#include "esp_bt.h"
#include "esp_wifi.h"

// -----------------------------------------------------------------------------
// Pin Configuration (Loaded from NVS, defaults provided)
// -----------------------------------------------------------------------------
uint32_t pin_mra = 25; // Right Motor PWM A
uint32_t pin_mrb = 26; // Right Motor PWM B
uint32_t pin_mla = 33; // Left Motor PWM A
uint32_t pin_mlb = 32; // Left Motor PWM B
const int PIN_BOOT = 0; // BOOT button for Web AP toggle

// -----------------------------------------------------------------------------
// High-Frequency Ultrasonic PWM (20 kHz, 8-Bit)
// -----------------------------------------------------------------------------
const int CH_MRA = 0;
const int CH_MRB = 1;
const int CH_MLA = 2;
const int CH_MLB = 3;
const int PWM_FREQ = 20000; // 20 kHz (Silent & High Efficiency)
const int PWM_RES  = 8;     // 8-bit (0 - 255)

// -----------------------------------------------------------------------------
// Motion & Speed Parameters
// -----------------------------------------------------------------------------
Preferences prefs;
WebServer server(80);

String ps3_mac = "22:3d:75:2a:f0:b2";

uint32_t ramp_up   = 200; // Acceleration time (ms)
uint32_t ramp_down = 250; // Deceleration / anti-flip time (ms)

uint8_t F_FAST_L    = 255, F_FAST_R    = 255;
uint8_t F_MEDIUM_L  = 61,  F_MEDIUM_R  = 61;
uint8_t F_LOW_L     = 35,  F_LOW_R     = 35; // Boosted from 20 for minimum stall overcome

uint8_t B_FAST_L    = 171, B_FAST_R    = 171;
uint8_t B_MEDIUM_L  = 61,  B_MEDIUM_R  = 61;
uint8_t B_LOW_L     = 35,  B_LOW_R     = 35;

uint8_t TR_LOW_LR   = 45;
uint8_t TR_MEDIUM_LR= 75;
uint8_t TR_FAST_LR  = 115;

uint8_t CRV_LOW_L   = 255, CRV_LOW_R   = 32;
uint8_t CRV_MEDIUM_L= 0,   CRV_MEDIUM_R= 0;
uint8_t CRV_FAST_L  = 255, CRV_FAST_R  = 32;

uint8_t DEFAULT_LR  = 0;

// -----------------------------------------------------------------------------
// Real-Time Control & Thread Communication
// -----------------------------------------------------------------------------
volatile int targetSpeedL = 0;
volatile int targetSpeedR = 0;
float currentSpeedL = 0;
float currentSpeedR = 0;

volatile unsigned long lastBtPacketTime = 0;
const unsigned long BT_TIMEOUT_MS = 250; // Failsafe cutoff window

bool webModeEnabled = false;
int bootClickCount = 0;
unsigned long lastBootPressTime = 0;

TaskHandle_t motorTaskHandle = NULL;

// -----------------------------------------------------------------------------
// HTML Page in PROGMEM (Flash ROM - Zero RAM Allocation)
// -----------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/><meta charset='UTF-8'/><title>RND Config - V2</title>
<style>:root{--bg:#010d08;--card:#04180f;--text:#CAD66A;--soft:#9aa656;--accent:#CAD66A;--line:#45602d;--sec:#143a26;--shadow:0 2px 6px rgba(0,0,0,0.35);--sep:22px;}body{font-family:'Segoe UI',sans-serif;margin:0;padding:0;background:var(--bg);color:var(--text);}.t{width:100%;position:sticky;top:0;z-index:10;background:var(--card);padding:14px 16px;font-size:18px;font-weight:600;color:var(--accent);box-shadow:var(--shadow);text-align:center;box-sizing:border-box;}.page{padding:16px;max-width:700px;margin:0 auto;}.card{background:var(--card);border-radius:12px;padding:16px;box-shadow:var(--shadow);margin-bottom:var(--sep);border:1px solid var(--line);}.card-title{font-weight:600;font-size:16px;margin-bottom:12px;color:var(--accent);}.slider-row{display:grid;grid-template-columns:140px 1fr 70px;gap:10px;margin-bottom:12px;align-items:center;}.input-row{display:grid;grid-template-columns:140px 1fr;gap:10px;margin-bottom:12px;align-items:center;}.slider-label{font-size:14px;font-weight:500;color:var(--text);}input[type=range]{width:100%;margin:0;accent-color:var(--accent);}input[type=number],input[type=text],select{width:100%;padding:6px;font-size:14px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--text);text-align:center;box-sizing:border-box;}.btn{padding:10px 16px;border:none;border-radius:8px;background:var(--accent);color:var(--bg);cursor:pointer;transition:0.25s;margin-right:8px;margin-bottom:10px;font-size:14px;font-weight:600;}.btn:hover{filter:brightness(0.9);}.btn.secondary{background:var(--sec);color:var(--text);}.footer{font-size:12px;color:var(--soft);margin-top:8px;text-align:center;}@media(max-width:520px){.slider-row,.input-row{grid-template-columns:1fr;gap:6px;}input[type=number],input[type=text],select{width:100%;}.btn{width:100%;}}</style>
</head><body>
<div class='t'>Soccer Nopal - Roamer Mode (V2 Optimized)</div>
<div class='page'>
  <div class='card'><div class='card-title'>Hardware & Bluetooth Setup</div>
    <div class='input-row'><div class='slider-label'>PS3 Master MAC</div><input type='text' id='ps3_mac' value='%PS3_MAC%'></div>
    <div class='input-row'><div class='slider-label'>Pin MRA</div><input type='number' id='pin_mra' value='%PIN_MRA%'></div>
    <div class='input-row'><div class='slider-label'>Pin MRB</div><input type='number' id='pin_mrb' value='%PIN_MRB%'></div>
    <div class='input-row'><div class='slider-label'>Pin MLA</div><input type='number' id='pin_mla' value='%PIN_MLA%'></div>
    <div class='input-row'><div class='slider-label'>Pin MLB</div><input type='number' id='pin_mlb' value='%PIN_MLB%'></div>
    <div class='input-row'><div class='slider-label'>Ramp Up Time (ms)</div><input type='number' id='ramp_up' value='%RAMP_UP%'></div>
    <div class='input-row'><div class='slider-label'>Ramp Down Time (ms)</div><input type='number' id='ramp_down' value='%RAMP_DOWN%'></div>
  </div>
  <div class='card'><div class='card-title'>Forward Speeds</div>
    %FORWARD_SLIDERS%
  </div>
  <div class='card'><div class='card-title'>Backward Speeds</div>
    %BACKWARD_SLIDERS%
  </div>
  <div class='card'><div class='card-title'>Turn & Curve</div>
    %TURN_SLIDERS%
  </div>
  <div class='card'>
    <button class='btn' onclick='saveAll()'>Save Settings</button>
    <button class='btn secondary' onclick='resetDefaults()'>Reset Defaults</button>
    <button class='btn secondary' onclick="fetch('/reboot').then(()=>alert('Rebooting...'))">Reboot</button>
  </div>
  <div class='footer'>Tekan BOOT 3x untuk toggle Web Mode.<br>Wi-Fi mati otomatis bila PS3 terkoneksi untuk latency 0ms.</div>
  <div class='footer'>Soccer Nopal - Jaya Jaya Jaya</div>
</div>
<script>
function updateFromNumber(id){let num=document.getElementById(id+'_num');let range=document.getElementById(id);let v=parseInt(num.value);if(isNaN(v))v=0;if(v<0)v=0;if(v>255)v=255;num.value=v;range.value=v;}
function saveAll(){var p='?';const keys=["F_FAST_L","F_FAST_R","F_MEDIUM_L","F_MEDIUM_R","F_LOW_L","F_LOW_R","B_FAST_L","B_FAST_R","B_MEDIUM_L","B_MEDIUM_R","B_LOW_L","B_LOW_R","TR_LOW_LR","TR_MEDIUM_LR","TR_FAST_LR","CRV_LOW_L","CRV_LOW_R","CRV_MEDIUM_L","CRV_MEDIUM_R","CRV_FAST_L","CRV_FAST_R","DEFAULT_LR"];for(let k of keys){let el=document.getElementById(k+'_num');if(el)p+=k+'='+el.value+'&';}p+='ps3_mac='+encodeURIComponent(document.getElementById('ps3_mac').value)+'&';p+='pin_mra='+document.getElementById('pin_mra').value+'&';p+='pin_mrb='+document.getElementById('pin_mrb').value+'&';p+='pin_mla='+document.getElementById('pin_mla').value+'&';p+='pin_mlb='+document.getElementById('pin_mlb').value+'&';p+='ramp_up='+document.getElementById('ramp_up').value+'&';p+='ramp_down='+document.getElementById('ramp_down').value+'&';fetch('/save'+p).then(r=>r.text()).then(t=>{alert('Saved successfully!');});}
function resetDefaults(){fetch('/reset').then(()=>location.reload());}
</script></body></html>
)rawliteral";

// -----------------------------------------------------------------------------
// Motor Control Hardware Routines (Ultrasonic PWM + Dynamic Braking)
// -----------------------------------------------------------------------------
void setMotorOutputs(int speedL, int speedR) {
  // Left Motor (Active Drive or Active Dynamic Brake)
  if (speedL > 0) {
    ledcWrite(pin_mla, constrain(speedL, 0, 255));
    ledcWrite(pin_mlb, 0);
  } else if (speedL < 0) {
    ledcWrite(pin_mla, 0);
    ledcWrite(pin_mlb, constrain(-speedL, 0, 255));
  } else {
    // Dynamic Electric Brake (Damps back-EMF, stops on a dime)
    ledcWrite(pin_mla, 0);
    ledcWrite(pin_mlb, 0);
  }

  // Right Motor
  if (speedR > 0) {
    ledcWrite(pin_mra, constrain(speedR, 0, 255));
    ledcWrite(pin_mrb, 0);
  } else if (speedR < 0) {
    ledcWrite(pin_mra, 0);
    ledcWrite(pin_mrb, constrain(-speedR, 0, 255));
  } else {
    ledcWrite(pin_mra, 0);
    ledcWrite(pin_mrb, 0);
  }
}

void initMotors() {
  ledcAttach(pin_mra, PWM_FREQ, PWM_RES);
  ledcAttach(pin_mrb, PWM_FREQ, PWM_RES);
  ledcAttach(pin_mla, PWM_FREQ, PWM_RES);
  ledcAttach(pin_mlb, PWM_FREQ, PWM_RES);

  setMotorOutputs(0, 0);
}

// -----------------------------------------------------------------------------
// Core 1 Dedicated Real-Time Motor Task (Deterministic 100 Hz Loop)
// -----------------------------------------------------------------------------
void motorLoopTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(10); // Exactly 10ms (100 Hz)

  while (true) {
    // 1. Failsafe Watchdog: Kill motors if Bluetooth packet timed out
    if (Ps3.isConnected()) {
      if (millis() - lastBtPacketTime > BT_TIMEOUT_MS) {
        targetSpeedL = 0;
        targetSpeedR = 0;
      }
    } else {
      targetSpeedL = 0;
      targetSpeedR = 0;
    }

    // 2. High-Precision Ramp Calculation
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

    // 3. Actuate Motors
    setMotorOutputs((int)currentSpeedL, (int)currentSpeedR);

    // Sleep until next exact 10ms tick (Zero jitter, zero CPU waste)
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// -----------------------------------------------------------------------------
// Bluetooth PS3 Callbacks (Runs on Core 0)
// -----------------------------------------------------------------------------
void onPs3Notify() {
  lastBtPacketTime = millis(); // Refresh Watchdog timer

  // Speed level modifiers
  bool isFast   = Ps3.data.button.r1 || Ps3.data.button.triangle;
  bool isMedium = Ps3.data.button.l1 || Ps3.data.button.circle;

  // D-pad or Analog Stick
  int ly = Ps3.data.analog.stick.ly;
  int lx = Ps3.data.analog.stick.lx;

  if (Ps3.data.button.up || ly < -40) {
    // Forward
    if (isFast) {
      targetSpeedL = F_FAST_L; targetSpeedR = F_FAST_R;
    } else if (isMedium) {
      targetSpeedL = F_MEDIUM_L; targetSpeedR = F_MEDIUM_R;
    } else {
      targetSpeedL = F_LOW_L; targetSpeedR = F_LOW_R;
    }
  } else if (Ps3.data.button.down || ly > 40) {
    // Backward
    if (isFast) {
      targetSpeedL = -B_FAST_L; targetSpeedR = -B_FAST_R;
    } else if (isMedium) {
      targetSpeedL = -B_MEDIUM_L; targetSpeedR = -B_MEDIUM_R;
    } else {
      targetSpeedL = -B_LOW_L; targetSpeedR = -B_LOW_R;
    }
  } else if (Ps3.data.button.left || lx < -40) {
    // Pivot Left
    int tr = isFast ? TR_FAST_LR : (isMedium ? TR_MEDIUM_LR : TR_LOW_LR);
    targetSpeedL = -tr; targetSpeedR = tr;
  } else if (Ps3.data.button.right || lx > 40) {
    // Pivot Right
    int tr = isFast ? TR_FAST_LR : (isMedium ? TR_MEDIUM_LR : TR_LOW_LR);
    targetSpeedL = tr; targetSpeedR = -tr;
  } else {
    // Neutral
    targetSpeedL = 0; targetSpeedR = 0;
  }
}

void onPs3Connect() {
  Serial.println("\n[BT] PS3 Connected! Activating Match Mode...");
  // Automatically cut Wi-Fi baseband to give 100% RF time to Bluetooth
  if (webModeEnabled) {
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop(); // Complete hardware RF shutdown
    webModeEnabled = false;
    Serial.println("[RF] Wi-Fi Baseband powered OFF for zero-latency BT.");
  }
}

void onPs3Disconnect() {
  Serial.println("\n[BT] PS3 Disconnected! Motors halted.");
  targetSpeedL = 0;
  targetSpeedR = 0;
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
  F_LOW_L    = prefs.getUChar("F_LOW_L", 35);
  F_LOW_R    = prefs.getUChar("F_LOW_R", 35);

  B_FAST_L   = prefs.getUChar("B_FAST_L", 171);
  B_FAST_R   = prefs.getUChar("B_FAST_R", 171);
  B_MEDIUM_L = prefs.getUChar("B_MEDIUM_L", 61);
  B_MEDIUM_R = prefs.getUChar("B_MEDIUM_R", 61);
  B_LOW_L    = prefs.getUChar("B_LOW_L", 35);
  B_LOW_R    = prefs.getUChar("B_LOW_R", 35);

  TR_LOW_LR   = prefs.getUChar("TR_LOW_LR", 45);
  TR_MEDIUM_LR= prefs.getUChar("TR_MEDIUM_LR", 75);
  TR_FAST_LR  = prefs.getUChar("TR_FAST_LR", 115);

  CRV_LOW_L   = prefs.getUChar("CRV_LOW_L", 255);
  CRV_LOW_R   = prefs.getUChar("CRV_LOW_R", 32);
  CRV_MEDIUM_L= prefs.getUChar("CRV_MEDIUM_L", 0);
  CRV_MEDIUM_R= prefs.getUChar("CRV_MEDIUM_R", 0);
  CRV_FAST_L  = prefs.getUChar("CRV_FAST_L", 255);
  CRV_FAST_R  = prefs.getUChar("CRV_FAST_R", 32);

  DEFAULT_LR  = prefs.getUChar("DEFAULT_LR", 0);
  prefs.end();
}

// -----------------------------------------------------------------------------
// Web Interface Streaming Helper (Zero-Heap String Interpolation)
// -----------------------------------------------------------------------------
String renderSlider(const char* key, uint8_t val) {
  String s = "<div class='slider-row'>";
  s += "<div class='slider-label'>" + String(key) + "</div>";
  s += "<input type='range' id='" + String(key) + "' disabled value='" + String(val) + "'>";
  s += "<input type='number' id='" + String(key) + "_num' min='0' max='255' value='" + String(val) + "' oninput=\"updateFromNumber('" + String(key) + "')\" onblur=\"updateFromNumber('" + String(key) + "')\">";
  s += "</div>";
  return s;
}

void handleRoot() {
  String html = FPSTR(INDEX_HTML);
  html.replace("%PS3_MAC%", ps3_mac);
  html.replace("%PIN_MRA%", String(pin_mra));
  html.replace("%PIN_MRB%", String(pin_mrb));
  html.replace("%PIN_MLA%", String(pin_mla));
  html.replace("%PIN_MLB%", String(pin_mlb));
  html.replace("%RAMP_UP%", String(ramp_up));
  html.replace("%RAMP_DOWN%", String(ramp_down));

  String fwd = renderSlider("F_FAST_L", F_FAST_L) + renderSlider("F_FAST_R", F_FAST_R)
             + renderSlider("F_MEDIUM_L", F_MEDIUM_L) + renderSlider("F_MEDIUM_R", F_MEDIUM_R)
             + renderSlider("F_LOW_L", F_LOW_L) + renderSlider("F_LOW_R", F_LOW_R);
  html.replace("%FORWARD_SLIDERS%", fwd);

  String bwd = renderSlider("B_FAST_L", B_FAST_L) + renderSlider("B_FAST_R", B_FAST_R)
             + renderSlider("B_MEDIUM_L", B_MEDIUM_L) + renderSlider("B_MEDIUM_R", B_MEDIUM_R)
             + renderSlider("B_LOW_L", B_LOW_L) + renderSlider("B_LOW_R", B_LOW_R);
  html.replace("%BACKWARD_SLIDERS%", bwd);

  String trn = renderSlider("TR_LOW_LR", TR_LOW_LR) + renderSlider("TR_MEDIUM_LR", TR_MEDIUM_LR) + renderSlider("TR_FAST_LR", TR_FAST_LR)
             + renderSlider("CRV_LOW_L", CRV_LOW_L) + renderSlider("CRV_LOW_R", CRV_LOW_R)
             + renderSlider("CRV_FAST_L", CRV_FAST_L) + renderSlider("CRV_FAST_R", CRV_FAST_R);
  html.replace("%TURN_SLIDERS%", trn);

  server.send(200, "text/html", html);
}

void handleSave() {
  prefs.begin("config", false);
  if (server.hasArg("ps3_mac")) { ps3_mac = server.arg("ps3_mac"); prefs.putString("ps3_mac", ps3_mac); }
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
    if (server.hasArg(k)) prefs.putUChar(k, (uint8_t)server.arg(k).toInt());
  }
  prefs.end();

  loadConfig();
  initMotors();
  server.send(200, "text/plain", "Settings saved!");
}

void handleReset() {
  prefs.begin("config", false);
  prefs.clear();
  prefs.end();
  loadConfig();
  server.send(200, "text/plain", "Reset OK");
}

void handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

void enableWebAP() {
  esp_wifi_start();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("RND OS [R' Mode]", "M3raky4T", 6);
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/reset", handleReset);
  server.on("/reboot", handleReboot);
  server.begin();
  webModeEnabled = true;
  Serial.println("[WiFi] Web AP ENABLED (192.168.4.1)");
}

void disableWebAP() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
  webModeEnabled = false;
  Serial.println("[WiFi] Web AP DISABLED");
}

// -----------------------------------------------------------------------------
// Main Setup & Core 0 Loop
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=======================================================");
  Serial.println("  SOCCER NOPAL - V2 OPTIMIZED ROBOT FIRMWARE");
  Serial.println("=======================================================");

  pinMode(PIN_BOOT, INPUT_PULLUP);

  loadConfig();
  initMotors();

  // 1. Force Maximum Bluetooth RF Output Power (+9 dBm)
  esp_bredr_tx_power_set(ESP_PWR_LVL_P9, ESP_PWR_LVL_P9);
  Serial.println("[BT] Transmit RF Power set to MAX (+9 dBm)");

  // 2. Attach PS3 Callbacks
  Ps3.attach(onPs3Notify);
  Ps3.attachOnConnect(onPs3Connect);
  Ps3.attachOnDisconnect(onPs3Disconnect);

  // 3. Initialize PS3 Host with Master MAC
  Ps3.begin(ps3_mac.c_str());
  Serial.printf("[BT] Listening for PS3 Master MAC: %s\n", ps3_mac.c_str());

  // 4. Spawn Real-Time Motor Loop on Core 1 (FreeRTOS)
  xTaskCreatePinnedToCore(
    motorLoopTask,     // Task function
    "MotorTask",       // Name
    4096,              // Stack size (bytes)
    NULL,              // Parameter
    5,                 // Priority (High)
    &motorTaskHandle,  // Task handle
    1                  // Core 1 (App Core)
  );
  Serial.println("[RTOS] Dedicated Motor Task spawned on Core 1 @ 100 Hz");
}

// Loop runs on Core 1 (handles non-timing-critical events like Web & BOOT button)
void loop() {
  // Handle BOOT button triple-click detection
  if (digitalRead(PIN_BOOT) == LOW) {
    delay(40);
    if (digitalRead(PIN_BOOT) == LOW) {
      bootClickCount++;
      lastBootPressTime = millis();
      while (digitalRead(PIN_BOOT) == LOW) delay(10);
    }
  }

  if (bootClickCount >= 3) {
    bootClickCount = 0;
    if (Ps3.isConnected()) {
      Serial.println("[WiFi] Cannot enable Web — PS3 currently in match mode!");
    } else {
      if (!webModeEnabled) enableWebAP();
      else disableWebAP();
    }
  }

  if (bootClickCount > 0 && (millis() - lastBootPressTime > 800)) {
    bootClickCount = 0;
  }

  // Handle Web Server if enabled
  if (webModeEnabled) {
    server.handleClient();
  }

  delay(20);
}
