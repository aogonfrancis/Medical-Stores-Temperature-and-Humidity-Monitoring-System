#include <WiFi.h>
#include <WebServer.h>
#include "DHT.h"
#include "time.h"

// Hardware Configuration
#define DHTPIN 4
#define DHTTYPE DHT11
#define AC_RELAY_PIN 27
#define BUZZER_PIN 18
#define RED_LED_PIN 19      // Danger Alert (80.1-100% or critical temp)
#define YELLOW_LED_PIN 21   // Caution Alert (60.1-80%)
#define GREEN_LED_PIN 22     // Normal Range (8-30°C, 5-60% humidity)
#define WHITE_LED_PIN 23     // Low Temp (7.99 to -2°C)

DHT dht(DHTPIN, DHTTYPE);

// Network Configuration
const char* ssid = "FRAGON";
const char* password = "123456789";
const char* http_username = "Francis Aogon";
const char* http_password = "08523";

// Thresholds
const float MIN_TEMP = 8.0;
const float MAX_TEMP = 30.0;
const float MIN_HUMIDITY = 5.0;
const float MAX_HUMIDITY = 60.0;

// Warning Ranges (Humidity)
const float CAUTION_HUM_MIN = 60.1; // Yellow LED
const float CAUTION_HUM_MAX = 80.0;
const float DANGER_HUM_MIN = 80.1;  // Red LED
const float DANGER_HUM_MAX = 100.0;

// Warning Ranges (Temperature)
const float CAUTION_TEMP_MIN = 30.1; // Yellow LED
const float CAUTION_TEMP_MAX = 32.0;
const float DANGER_TEMP_MIN = 32.1;  // Red LED
const float DANGER_TEMP_MAX = 40.0;

// Low Temp Range (White LED)
const float LOW_TEMP_MIN = -2.0;
const float LOW_TEMP_MAX = 7.99;

// State
float temperature = 0;
float humidity = 0;
bool alarmActive = false;
bool isLoggedIn = false;
String currentUser = "";

// Timing
unsigned long lastReadingTime = 0;
const unsigned long readingInterval = 2000;
unsigned long lastAlarmTime = 0;
const unsigned long alarmInterval = 1000;

// Time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3 * 3600;
const int daylightOffset_sec = 0;

WebServer server(80);

void connectToWiFi();
void setupServerRoutes();
void handleRoot();
void handleLogin();
void handleDashboard();
void handleLogout();
void handleUpdate();
void handleControl();
void handleSettings();
void handleAlarmInterrupt();
void handleNotFound();
String generateDashboard();
String getFormattedTime();
void sendLoginPage(bool authFailed);
void redirectToLogin();
void updateSensorReadings();
void activateAlarm(bool activate);
void updateLEDs();

void setup() {
  Serial.begin(115200);

  pinMode(AC_RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(WHITE_LED_PIN, OUTPUT);
  
  digitalWrite(AC_RELAY_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH); // Default to normal state
  digitalWrite(WHITE_LED_PIN, LOW);

  dht.begin();
  connectToWiFi();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  setupServerRoutes();

  Serial.println("System initialized");
  Serial.printf("Temperature: %.1f°C to %.1f°C\n", MIN_TEMP, MAX_TEMP);
  Serial.printf("Humidity: %.1f%% to %.1f%%\n", MIN_HUMIDITY, MAX_HUMIDITY);
}

void loop() {
  server.handleClient();
  updateSensorReadings();
  updateLEDs();

  if (alarmActive && millis() - lastAlarmTime >= alarmInterval) {
    digitalWrite(RED_LED_PIN, !digitalRead(RED_LED_PIN));
    digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));
    lastAlarmTime = millis();
  }
}

void updateLEDs() {
  // Reset all LEDs
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(WHITE_LED_PIN, LOW);

  // Priority: Danger > Caution > Normal > Low Temp
  if (humidity >= DANGER_HUM_MIN || temperature > MAX_TEMP || temperature < MIN_TEMP) {
    digitalWrite(RED_LED_PIN, HIGH);
  } 
  else if (humidity >= CAUTION_HUM_MIN && humidity <= CAUTION_HUM_MAX) {
    digitalWrite(YELLOW_LED_PIN, HIGH);
  } 
  else if (temperature >= MIN_TEMP && temperature <= MAX_TEMP && 
           humidity >= MIN_HUMIDITY && humidity <= MAX_HUMIDITY) {
    digitalWrite(GREEN_LED_PIN, HIGH);
  } 
  else if (temperature >= LOW_TEMP_MIN && temperature <= LOW_TEMP_MAX) {
    digitalWrite(WHITE_LED_PIN, HIGH);
  }
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
}

void setupServerRoutes() {
  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.on("/dashboard", handleDashboard);
  server.on("/logout", handleLogout);
  server.on("/update", handleUpdate);
  server.on("/control", handleControl);
  server.on("/settings", handleSettings);
  server.on("/alarm-interrupt", handleAlarmInterrupt);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void updateSensorReadings() {
  if (millis() - lastReadingTime >= readingInterval) {
    float newTemp = dht.readTemperature();
    float newHum = dht.readHumidity();

    if (!isnan(newTemp) && !isnan(newHum)) {
      temperature = newTemp;
      humidity = newHum;

      Serial.printf("Temperature: %.1f°C, Humidity: %.1f%%\n", temperature, humidity);

      bool tempOut = (temperature < MIN_TEMP) || (temperature > MAX_TEMP);
      bool humOut = (humidity < MIN_HUMIDITY) || (humidity > MAX_HUMIDITY);

      if (tempOut || humOut) {
        Serial.println("ALARM: Condition out of range!");
        activateAlarm(true);
      }
    } else {
      Serial.println("Failed to read DHT sensor!");
    }
    lastReadingTime = millis();
  }
}

void activateAlarm(bool activate) {
  alarmActive = activate;
  if (!activate) {
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void handleRoot() {
  if (isLoggedIn) {
    server.sendHeader("Location", "/dashboard");
  } else {
    server.sendHeader("Location", "/login");
  }
  server.send(302, "text/plain", "");
}

void handleLogin() {
  if (server.method() == HTTP_POST) {
    if (server.arg("username") == http_username && server.arg("password") == http_password) {
      isLoggedIn = true;
      currentUser = server.arg("username");
      server.sendHeader("Location", "/dashboard");
      server.send(302, "text/plain", "");
      return;
    }
    sendLoginPage(true);
  } else {
    sendLoginPage(false);
  }
}

void handleDashboard() {
  if (!isLoggedIn) {
    redirectToLogin();
    return;
  }
  server.send(200, "text/html", generateDashboard());
}

void handleUpdate() {
  if (!isLoggedIn) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  String json = "{";
  json += "\"temperature\":" + String(temperature) + ",";
  json += "\"humidity\":" + String(humidity) + ",";
  json += "\"acStatus\":" + String(digitalRead(AC_RELAY_PIN)) + ",";
  json += "\"alarmActive\":" + String(alarmActive ? "true" : "false") + ",";
  json += "\"time\":\"" + getFormattedTime() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleControl() {
  if (!isLoggedIn) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  if (server.method() == HTTP_POST) {
    if (server.hasArg("acState")) {
      bool newState = !digitalRead(AC_RELAY_PIN); // Toggle current state
      digitalWrite(AC_RELAY_PIN, newState ? HIGH : LOW);
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void handleAlarmInterrupt() {
  if (!isLoggedIn) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  activateAlarm(false);
  server.send(200, "text/plain", "Alarm interrupted");
}

void handleSettings() {
  server.send(200, "text/html", "Thresholds are fixed at: Temp 8&deg;C - 30&deg;C, Hum 5% - 60%");
}

void handleLogout() {
  isLoggedIn = false;
  currentUser = "";
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Time unavailable";
  char timeString[64];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeString);
}

void redirectToLogin() {
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "");
}

void sendLoginPage(bool authFailed) {
  String page = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FRAGON - Login</title>
    <style>
        :root {
            --primary: #3498db;
            --primary-dark: #2980b9;
            --danger: #e74c3c;
            --light: #ecf0f1;
            --dark: #2c3e50;
        }
        
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        
        body {
            background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
            height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        
        .login-container {
            background: white;
            border-radius: 10px;
            box-shadow: 0 15px 30px rgba(0, 0, 0, 0.1);
            width: 100%;
            max-width: 400px;
            padding: 2rem;
            text-align: center;
            animation: fadeIn 0.5s ease-in-out;
        }
        
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(20px); }
            to { opacity: 1; transform: translateY(0); }
        }
        
        .logo {
            width: 80px;
            height: 80px;
            margin-bottom: 1.5rem;
        }
        
        h1 {
            color: var(--dark);
            margin-bottom: 1.5rem;
            font-weight: 600;
        }
        
        .form-group {
            margin-bottom: 1.5rem;
            text-align: left;
        }
        
        label {
            display: block;
            margin-bottom: 0.5rem;
            color: var(--dark);
            font-weight: 500;
        }
        
        input {
            width: 100%;
            padding: 12px 15px;
            border: 1px solid #ddd;
            border-radius: 5px;
            font-size: 16px;
            transition: all 0.3s;
        }
        
        input:focus {
            border-color: var(--primary);
            outline: none;
            box-shadow: 0 0 0 3px rgba(52, 152, 219, 0.2);
        }
        
        button {
            background-color: var(--primary);
            color: white;
            border: none;
            padding: 12px;
            width: 100%;
            border-radius: 5px;
            font-size: 16px;
            font-weight: 500;
            cursor: pointer;
            transition: background-color 0.3s;
        }
        
        button:hover {
            background-color: var(--primary-dark);
        }
        
        .error-message {
            color: var(--danger);
            margin-top: 1rem;
            font-size: 14px;
            height: 20px;
        }
        
        .footer {
            margin-top: 1.5rem;
            color: #7f8c8d;
            font-size: 14px;
        }
    </style>
</head>
<body>
    <div class="login-container">
        <svg class="logo" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 3c1.66 0 3 1.34 3 3s-1.34 3-3 3-3-1.34-3-3 1.34-3 3-3zm0 14.2c-2.5 0-4.71-1.28-6-3.22.03-1.99 4-3.08 6-3.08 1.99 0 5.97 1.09 6 3.08-1.29 1.94-3.5 3.22-6 3.22z" fill="#3498db"/>
        </svg>
        <h1>FRAGON Medical Stores</h1>
        <form method="POST" action="/login">
            <div class="form-group">
                <label for="username">Username</label>
                <input type="text" id="username" name="username" placeholder="Enter your username" required>
            </div>
            <div class="form-group">
                <label for="password">Password</label>
                <input type="password" id="password" name="password" placeholder="Enter your password" required>
            </div>
            <div class="error-message">)=====";
            
  page += authFailed ? "Invalid username or password" : "";
  
  page += R"=====(</div>
            <button type="submit">Login</button>
        </form>
        <div class="footer">
            Smart Medical Storage Monitoring System
        </div>
    </div>
</body>
</html>
)=====";

  server.send(200, "text/html", page);
}

String generateDashboard() {
  String page = "<!DOCTYPE html><html><head><title>FRAGON Smart Medical Stores</title><meta charset=\"UTF-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  page += "<style>";
  page += "body{font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(to right,#e0f7fa,#ffffff);color:#333;padding:20px;max-width:1000px;margin:auto;}";
  page += "h1,h2{color:#222;} .card{background:#ffffffcc;padding:20px;margin-bottom:20px;box-shadow:0 4px 12px rgba(0,0,0,0.1);border-radius:12px;}";
  page += ".status{padding:6px 12px;border-radius:4px;color:#fff;font-weight:bold;display:inline-block;}";
  page += ".on{background:#28a745;}.off{background:#dc3545;}.alarm{background:#fd7e14;}";
  page += "button{background:#007bff;color:white;border:none;padding:10px 16px;margin:8px 4px;border-radius:6px;cursor:pointer;font-size:16px;}";
  page += "button:hover{background:#0056b3;}";
  page += ".flex{display:flex;gap:20px;flex-wrap:wrap;align-items:center;}";
  page += ".item{flex:1 1 250px;min-width:200px;}";
  page += ".icon{width:40px;height:40px;vertical-align:middle;margin-right:10px;}";
  page += ".topbar{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px;}";
  page += ".alarm-btn{background:#fd7e14;}.alarm-btn:hover{background:#e66a00;}";
  page += "</style></head><body>";

  page += "<div class='topbar'><h1>Welcome, " + currentUser + "!</h1><form action='/logout' method='get'><button type='submit'>Logout</button></form></div>";

  page += "<div class='card'><h2>📊 Sensor Readings</h2><div class='flex'>";
  page += "<div class='item'><img src='https://img.icons8.com/color/48/000000/temperature.png' class='icon'>Temperature: <span id='temp'>" + String(temperature) + "</span>&deg;C<br><small>(Safe: 8&deg;C - 30&deg;C)</small></div>";
  page += "<div class='item'><img src='https://img.icons8.com/color/48/000000/hygrometer.png' class='icon'>Humidity: <span id='hum'>" + String(humidity) + "</span>%<br><small>(Safe: 5% - 60%)</small></div>";
  page += "<div class='item'><img src='https://img.icons8.com/color/48/000000/clock--v1.png' class='icon'>Time: <span id='time'>" + getFormattedTime() + "</span></div>";
  page += "</div></div>";

  page += "<div class='card'><h2>⚙️ System Status</h2>";
  page += "<p><img src='https://img.icons8.com/color/48/000000/air-conditioner.png' class='icon'>AC Status: <span id='acStatus' class='status " + String(digitalRead(AC_RELAY_PIN) ? "on" : "off") + "'>" + String(digitalRead(AC_RELAY_PIN) ? "ON" : "OFF") + "</span></p>";
  page += "<p><img src='https://img.icons8.com/color/48/000000/alarm.png' class='icon'>Alarm Status: <span id='alarmStatus' class='status " + String(alarmActive ? "alarm" : "off") + "'>" + String(alarmActive ? "ACTIVE" : "INACTIVE") + "</span></p>";
  page += "<button id='acToggleBtn' onclick=\"toggleAC()\">" + String(digitalRead(AC_RELAY_PIN) ? "Turn AC OFF" : "Turn AC ON") + "</button>";
  page += "<button id='alarmBtn' class='alarm-btn' onclick=\"interruptAlarm()\" " + String(alarmActive ? "" : "disabled") + ">Interrupt Alarm</button>";
  page += "</div>";

  page += "<div class='card'><h2>🛡️ Fixed Safety Ranges</h2>";
  page += "<ul><li>🌡️ Temperature: 8&deg;C - 30&deg;C</li><li>💧 Humidity: 5% - 60%</li></ul>";
  page += "<p><em>Alarm will activate if values go outside these ranges.</em></p></div>";

  page += "<script>";
  page += "function updateData(){fetch('/update').then(r=>r.json()).then(d=>{";
  page += "document.getElementById('temp').textContent=d.temperature.toFixed(1);";
  page += "document.getElementById('hum').textContent=d.humidity.toFixed(1);";
  page += "document.getElementById('time').textContent=d.time;";
  page += "let ac=document.getElementById('acStatus');ac.textContent=d.acStatus?'ON':'OFF';";
  page += "ac.className='status '+(d.acStatus?'on':'off');";
  page += "let al=document.getElementById('alarmStatus');al.textContent=d.alarmActive?'ACTIVE':'INACTIVE';";
  page += "al.className='status '+(d.alarmActive?'alarm':'off');";
  page += "let btn=document.getElementById('alarmBtn');btn.disabled=!d.alarmActive;";
  page += "let acBtn=document.getElementById('acToggleBtn');acBtn.textContent=d.acStatus?'Turn AC OFF':'Turn AC ON';";
  page += "});}";
  page += "function toggleAC(){fetch('/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'acState=toggle'}).then(updateData);}";
  page += "function interruptAlarm(){fetch('/alarm-interrupt').then(updateData);}";
  page += "setInterval(updateData,2000);</script></body></html>";
  return page;
}