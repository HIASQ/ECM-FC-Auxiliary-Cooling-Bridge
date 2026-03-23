#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <OneWire.h> 
#include <DallasTemperature.h>
#include <ArduinoOTA.h>

#define RELAY_PIN D1 
#define ONE_WIRE_BUS D2

// WiFi Settings
const char* ssid = "MyHomeNetwork";      
const char* password = "MyPassword123";  

// OTA Settings
const char* hostName = "Fan-Timer-Control";
const char* otaPassword = "ota123";      // OTA Password for security

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
ESP8266WebServer server(80);

// Timer and Control Variables
unsigned long previousMillis = 0;
// Reverse Logic: isOn = true means Relay HIGH (Fan OFF)
//                isOn = false means Relay LOW (Fan ON)
bool isOn = true; 
bool isManualStop = false;
bool isAutoMode = true;
unsigned long onTime = 3 * 60 * 1000UL; 
unsigned long offTime = 1 * 60 * 1000UL;

// Temperature Variables
float temperature = -99.9;
unsigned long lastTempUpdate = 0;
const unsigned long tempUpdateInterval = 5000;
bool isTempConditionActive = false;
float targetTemp = 50.0;

// EEPROM Addresses
#define ADDR_ON_TIME 0
#define ADDR_OFF_TIME (ADDR_ON_TIME + sizeof(onTime))
#define ADDR_TEMP_ACTIVE (ADDR_OFF_TIME + sizeof(offTime))
#define ADDR_TARGET_TEMP (ADDR_TEMP_ACTIVE + sizeof(isTempConditionActive))

// Function Prototypes
void handleRoot();
void handleSet();
void handleStart();
void handleStop();
void handleTemp();

// ===================================
// EEPROM FUNCTIONS
// ===================================

void saveSettings() {
  EEPROM.put(ADDR_ON_TIME, onTime);
  EEPROM.put(ADDR_OFF_TIME, offTime);
  EEPROM.put(ADDR_TEMP_ACTIVE, isTempConditionActive);
  EEPROM.put(ADDR_TARGET_TEMP, targetTemp);
  EEPROM.commit();
  Serial.println("Settings saved to EEPROM.");
}

void loadSettings() {
  EEPROM.get(ADDR_ON_TIME, onTime);
  EEPROM.get(ADDR_OFF_TIME, offTime);
  EEPROM.get(ADDR_TEMP_ACTIVE, isTempConditionActive);
  EEPROM.get(ADDR_TARGET_TEMP, targetTemp);

  // Default values if EEPROM is empty or corrupted
  if (onTime == 0 || onTime > 60UL * 60UL * 1000UL) onTime = 3 * 60 * 1000UL;
  if (offTime == 0 || offTime > 60UL * 60UL * 1000UL) offTime = 1 * 60 * 1000UL;
  if (targetTemp < 10.0 || targetTemp > 100.0) targetTemp = 50.0;
  
  Serial.println("Settings Loaded: Fan OFF for " + String(onTime/60000) + "m - Fan ON for " + String(offTime/60000) + "m");
  Serial.println("Temp Condition: " + String(isTempConditionActive ? "ENABLED" : "DISABLED") + " @ " + String(targetTemp, 1) + " C");
}

// ===================================
// TEMPERATURE FUNCTION
// ===================================

void updateTemperature() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastTempUpdate >= tempUpdateInterval) {
    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);

    if (tempC != DEVICE_DISCONNECTED_C) {
      temperature = tempC;
    } else {
      temperature = -99.9; 
    }
    
    lastTempUpdate = currentMillis;
    Serial.print("Temperature (DS18B20): ");
    Serial.print(temperature, 1);
    Serial.println(" C");
  }
}

// ===================================
// SETUP AND LOOP
// ===================================

void setup() {
  Serial.begin(115200);

  // Relay Setup: Active-High (HIGH = OFF, LOW = ON)
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Start with Fan OFF

  sensors.begin();
  EEPROM.begin(512);
  loadSettings();

  WiFi.mode(WIFI_AP_STA);

  // 1. Connect to Home WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // 2. Access Point Backup
  WiFi.softAP("TimerControl_AP", "12345678");
  Serial.println("AP IP: " + WiFi.softAPIP().toString());

  // 3. OTA Setup
  ArduinoOTA.setHostname(hostName);
  ArduinoOTA.setPassword(otaPassword);
  
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Update Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update End");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
  });
  
  ArduinoOTA.begin();

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/temp", handleTemp);
  server.begin();
  Serial.println("Web Server Ready");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  updateTemperature();

  bool shouldAutoTimerRun = isAutoMode && !isManualStop;

  // Temperature Condition Logic
  if (isTempConditionActive) {
    if (temperature != -99.9 && temperature >= targetTemp) {
      if (!isAutoMode) {
        // Threshold exceeded: Start Auto Timer
        isAutoMode = true;
        previousMillis = millis();
        digitalWrite(RELAY_PIN, HIGH); // Fan OFF initially
        isOn = true; 
        Serial.println("Temp Threshold reached. Auto Timer Started (Fan OFF phase).");
      }
      shouldAutoTimerRun = true;
    } else {
      // Below threshold: Safety Fan ON
      shouldAutoTimerRun = false;
      isAutoMode = false;
      if (digitalRead(RELAY_PIN) == HIGH) {
        digitalWrite(RELAY_PIN, LOW); // Fan ON
        isOn = false; 
        Serial.println("Below Threshold. Safety Fan ON triggered.");
      }
      previousMillis = millis(); 
    }
  }
  
  // Timer Logic
  if (shouldAutoTimerRun && !isManualStop) {
    unsigned long currentMillis = millis();
    
    if (isOn && (currentMillis - previousMillis >= onTime)) {
      // Transition from Timer ON (Fan OFF) to Timer OFF (Fan ON)
      digitalWrite(RELAY_PIN, LOW); 
      isOn = false;
      previousMillis = currentMillis;
      Serial.println("Timer Switch: Fan is now ON");
    } else if (!isOn && (currentMillis - previousMillis >= offTime)) {
      // Transition from Timer OFF (Fan ON) to Timer ON (Fan OFF)
      digitalWrite(RELAY_PIN, HIGH); 
      isOn = true;
      previousMillis = currentMillis;
      Serial.println("Timer Switch: Fan is now OFF");
    }
  }
}

// ===================================
// WEB HANDLERS
// ===================================

void handleSet() {
  if (server.hasArg("on") && server.hasArg("off")) {
    onTime = server.arg("on").toInt() * 60 * 1000UL;
    offTime = server.arg("off").toInt() * 60 * 1000UL;
  }
  
  if (server.hasArg("temp_active")) {
    isTempConditionActive = (server.arg("temp_active").toInt() == 1);
  } else {
    isTempConditionActive = false;
  }

  if (server.hasArg("target_temp")) {
    targetTemp = server.arg("target_temp").toFloat();
  }
  
  saveSettings();
  if (isAutoMode) previousMillis = millis();
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStart() {
  isManualStop = false;
  isAutoMode = true;
  isTempConditionActive = false;  
  
  digitalWrite(RELAY_PIN, HIGH); 
  isOn = true;
  
  previousMillis = millis();
  saveSettings();
  
  Serial.println("Normal Auto Mode Started (Fan OFF).");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStop() {
  isManualStop = true;
  isAutoMode = false;
  
  digitalWrite(RELAY_PIN, LOW); // Immediate Fan ON (Safety)
  isOn = false;
  
  previousMillis = millis();
  Serial.println("Manual Override - Fan forced ON (Relay LOW)");
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleTemp() {
  String response = String(temperature, 1) + "," + String(isTempConditionActive ? 1 : 0);
  server.send(200, "text/plain", response);
}

// ===================================
// WEB PAGE (HTML)
// ===================================

void handleRoot() {
  unsigned long remaining = 0;
  if (!isManualStop && isAutoMode) {
    unsigned long currentTime = millis();
    if (isOn) {
      remaining = (onTime - (currentTime - previousMillis)) / 1000;
    } else {
      remaining = (offTime - (currentTime - previousMillis)) / 1000;
    }
    if ((long)remaining < 0) remaining = 0;
  }

  String statusStr = "";
  String statusClass = "";
  
  if (isManualStop) {
    statusStr = "Manual Override (Fan ON)";
    statusClass = "status-on";
  } else if (isTempConditionActive) {
    if (isAutoMode) {
      statusStr = isOn ? "Conditional Stop (Fan OFF)" : "Conditional Run (Fan ON)";
      statusClass = isOn ? "status-off" : "status-on";
    } else {
      statusStr = "Waiting for Temp (" + String(targetTemp, 0) + "C)";
      statusClass = "status-wait";
    }
  } else {
    statusStr = isOn ? "Auto Pause (Fan OFF)" : "Auto Running (Fan ON)";
    statusClass = isOn ? "status-off" : "status-on";
  }

  String html = R"====(
  <html dir='ltr'>
  <head>
    <meta charset='UTF-8'>
    <title>Smart Timer Control</title>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <style>
      :root {
        --main-color: #2196F3;
        --on-color: #4CAF50;
        --off-color: #f44336;
        --manual-color: #FF9800;
        --bg-color: #f8f9fa;
      }
      body {
        background-color: var(--bg-color);
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        text-align: center;
        padding: 15px;
        margin: 0;
      }
      h2 {
        color: var(--main-color);
        margin-bottom: 25px;
        border-bottom: 2px solid #eee;
        padding-bottom: 10px;
      }
      .container {
        max-width: 600px;
        margin: 0 auto;
      }
      .card {
        background-color: white;
        padding: 20px;
        margin: 15px 0;
        border-radius: 12px;
        box-shadow: 0 4px 12px rgba(0,0,0,0.05);
      }
      .btn-group {
        display: flex;
        gap: 10px;
        justify-content: center;
        margin: 15px 0;
      }
      .btn {
        padding: 12px 20px;
        border: none;
        border-radius: 8px;
        cursor: pointer;
        font-size: 14px;
        transition: all 0.3s;
        text-decoration: none;
        display: inline-block;
        font-weight: bold;
        flex-grow: 1;
      }
      .btn-start { background-color: var(--on-color); color: white; }
      .btn-stop { background-color: var(--off-color); color: white; }
      .btn:hover { opacity: 0.85; transform: translateY(-1px); }
      form label {
        display: block;
        margin: 10px 0 5px;
        text-align: left;
        font-weight: bold;
      }
      input[type=number], select {
        width: 100%;
        padding: 10px;
        margin: 5px 0;
        border: 1px solid #ddd;
        border-radius: 5px;
        box-sizing: border-box;
      }
      input[type=submit] {
        padding: 12px 25px;
        margin-top: 15px;
        border: none;
        background-color: var(--main-color);
        color: white;
        border-radius: 8px;
        cursor: pointer;
        font-weight: bold;
        transition: background-color 0.3s;
        width: 100%;
      }
      .status {
        font-size: 18px;
        margin: 15px 0;
        padding: 12px;
        border-radius: 8px;
        font-weight: bold;
      }
      .status-on { background-color: #E8F5E8; color: var(--on-color); }
      .status-off { background-color: #FFEBEE; color: var(--off-color); }
      .status-wait { background-color: #E3F2FD; color: var(--main-color); }
      .temp-display {
        font-size: 36px;
        font-weight: bold;
        color: var(--main-color);
        margin: 10px 0;
      }
      .info-box {
        background-color: #f5f5f5;
        padding: 10px;
        border-radius: 5px;
        margin: 10px 0;
        font-size: 14px;
        color: #555;
        text-align: left;
      }
      .timer-box {
        font-size: 24px;
        font-weight: bold;
        color: #333;
        margin: 10px 0;
      }
    </style>
  </head>
  <body>
    <div class='container'>
      <h2>Smart Fan Controller</h2>
      
      <div class='card'>
        <h3>Current Status</h3>
        <div class='temp-display' id='temperature'>
  )====";
  if (temperature == -99.9) {
    html += "Sensor Error";
  } else {
    html += String(temperature, 1);
  }
  html += R"====( C</div>
        <div style='color: #666;'>Ambient Temperature</div>
  
        <div class='status )====";
  html += statusClass;
  html += R"====(' id='status'>)====";
  html += statusStr;
  html += R"====(</div>
      
        <div class='timer-box' id='elapsed'>
          )====";
  html += String(remaining / 60) + "m " + String(remaining % 60) + "s";
  html += R"====(
        </div>
        <div style='color: #666;'>Time remaining in current phase</div>
      </div>

      <div class='card'>
        <h3>Quick Controls</h3>
        <div class='btn-group'>
          <a href='/start' class='btn btn-start'>Start Auto Timer</a>
          <a href='/stop' class='btn btn-stop'>Force Fan ON (Manual)</a>
        </div>
        <div class='info-box'>
          Current logic: Fan stays <b>OFF</b> for )====";
  html += String(onTime / 60000);
  html += R"====( min, then stays <b>ON</b> for )====";
  html += String(offTime / 60000);
  html += R"====( min.
        </div>
      </div>
  
      <div class='card'>
        <h3>Configuration</h3>
        <form action='/set'>
          <label>OFF Duration (minutes):</label>
          <input type='number' name='on' value=)====";
  html += String(onTime / 60000);
  html += R"====( min='1' max='60' required>
  
          <label>ON Duration (minutes):</label>
          <input type='number' name='off' value=)====";
  html += String(offTime / 60000);
  html += R"====( min='1' max='60' required>
  
          <hr style='margin: 15px 0; border: 0; border-top: 1px solid #eee;'>
          
          <label>Thermal Activation Mode:</label>
          <select name='temp_active' id='temp_active'>
            <option value='0' )====";
  if (!isTempConditionActive) html += "selected";
  html += R"====(>Always Running (Timer Always Active)</option>
            <option value='1' )====";
  if (isTempConditionActive) html += "selected";
  html += R"====(>Conditional (Start Timer only at Threshold)</option>
          </select>
  
          <label>Threshold Temperature (°C):</label>
          <input type='number' name='target_temp' value=)====";
  html += String(targetTemp, 1);
  html += R"====( min='10' max='100' step='0.1' required>
  
          <input type='submit' value='Save All Settings'>
        </form>
      </div>
  
    </div>

    <script>
      var remaining = )====" + String(remaining) + R"====(;
      var isTimerRunning = )====" + String(!isManualStop && isAutoMode ? 1 : 0) + R"====(;
      
      setInterval(function() {
        if (isTimerRunning && remaining > 0) {
          remaining--;
          var minutes = Math.floor(remaining / 60);
          var seconds = remaining % 60;
          document.getElementById('elapsed').innerText = minutes + 'm ' + seconds + 's';
        } else if (isTimerRunning && remaining === 0) {
          window.location.reload();
        }
      }, 1000);

      setInterval(updateData, 5000);
      
      function updateData() {
        fetch('/temp')
          .then(response => response.text())
          .then(data => {
            var parts = data.split(',');
            var temp = parts[0];
            var isTempActive = parts[1];
            
            if (temp == "-99.9") {
              document.getElementById('temperature').innerText = 'Sensor Error';
            } else {
              document.getElementById('temperature').innerText = temp + ' C';
            }                             

            if (isTempActive == 1 && parseFloat(temp) >= parseFloat(document.querySelector("input[name='target_temp']").value)) {
              if (document.getElementById('status').innerText.includes('Wait')) {
                window.location.reload();
              }
            }
          });
      }
    </script>
  </body>
  </html>
  )====";

  server.send(200, "text/html", html);
}