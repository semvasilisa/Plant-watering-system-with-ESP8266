#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
ESP8266WebServer server(80);

const int relayPin = D5;
const int sensorPin = A0;

bool isWatering = false; // watering tracking
unsigned long wateringStartTime = 0; // how much time has passed since the start of watering
unsigned long interval = 3000; // watering time is 3 seconds
bool relayState = HIGH; // init stay of relay is OFF
unsigned long breakStartTime = 0; // when will the break after watering start
unsigned long breakInterval = 100000; // how lond is the break
bool isInBreak = false; // break tracking
unsigned long lowPoint = 370; // lower limit of hysteresis
unsigned long highPoint = 600; // upper limit of hysteresis
int currentPulse = 0; // current pulse count
int totalPulses = 0; // max amount of pulses
bool pumpOn = false; // pump state tracking 
unsigned long pulseStart = 0; // impulse start 
unsigned long pauseStart = 0; // when the break has started 
unsigned long lastCycleTime = 0; // for a 30-minute interval after a full cycle
unsigned long minCycleInterval = 180000; //30 minut 

// variables for web interface
bool autoMode = true;
bool manualRelayState = false;

// setting a static IP for SoftAP
IPAddress local_IP(192, 168, 4, 22);
IPAddress gateway(192, 168, 4, 9);
IPAddress subnet(255, 255, 255, 0);

// method for changing humidity measurement values
int reverseValue(int measurement){
  // if [a,b] = [1024,0] and [x,y]=[0,1024]
  int mapped = 1024 - measurement; //(measurement - 1024) * (1024 - 0) / (0 - 1024) + 0;
  return mapped;
}

// method for passing the state to the display
void displayStatus(String status, int percent) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("current moisture: ");
  display.print(percent);
  display.print("%");
  display.setCursor(0, 8);
  display.println(status);
  display.display();
}

// method to determining the content type
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  return "text/plain";
}

// method to read files from SPIFFS
bool handleFileRead(String path) {
  if (path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  
  if (SPIFFS.exists(path)) {
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

// homepage handler
void handleRoot() {
  handleFileRead("/index.html");
}

// status handler (JSON)
void handleStatus() {
  int fiveMeasurements = 0;
  for(int i = 0; i < 5; i++){
    fiveMeasurements += reverseValue(analogRead(sensorPin));
    delay(10);
  }
  int soilMoisture = fiveMeasurements/5;
  int moisturePercent = soilMoisture * 100 / 1024;
  
  String status = "OK";
  if (isWatering) {
    status = pumpOn ? "WATERING" : "PAUSE";
  } else if (soilMoisture > highPoint) {
    status = "HUMID";
  } else if (soilMoisture < lowPoint) {
    status = "DRY";
  }
  
  bool relayOn = (digitalRead(relayPin) == LOW);
  
  String json = "{";
  json += "\"moisture\":" + String(moisturePercent) + ",";
  json += "\"status\":\"" + status + "\",";
  json += "\"autoMode\":" + String(autoMode ? "true" : "false") + ",";
  json += "\"relayOn\":" + String(relayOn ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

// mode switch handler
void handleMode() {
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    if (mode == "auto") {
      autoMode = true;
    } else if (mode == "manual") {
      autoMode = false;
      isWatering = false;
      pumpOn = false;
      digitalWrite(relayPin, HIGH);
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing mode parameter");
  }
}

// relay control handler
void handleRelay() {
  if (!autoMode && server.hasArg("state")) {
    String state = server.arg("state");
    if (state == "on") {
      manualRelayState = true;
      digitalWrite(relayPin, LOW);
    } else if (state == "off") {
      manualRelayState = false;
      digitalWrite(relayPin, HIGH);
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

// settings update handler
void handleUpdate() {
  if (server.hasArg("breakInterval") && server.hasArg("cycleInterval") && server.hasArg("totalPulses")) {
    breakInterval = server.arg("breakInterval").toInt() * 1000;
    unsigned long newCycleInterval = server.arg("cycleInterval").toInt() * 60 * 1000;
    *const_cast<unsigned long*>(&minCycleInterval) = newCycleInterval;
    
    if (!isWatering) {
      totalPulses = server.arg("totalPulses").toInt();
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

// settings receiver handler
void handleSettings() {
  String json = "{";
  json += "\"breakInterval\":" + String(breakInterval / 1000) + ",";
  json += "\"cycleInterval\":" + String(minCycleInterval / 60000) + ",";
  json += "\"totalPulses\":" + String(totalPulses);
  json += "}";
  
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  pinMode(sensorPin, INPUT); //set this pin as an input -> the sensor receives values ​​from the ground
  
  // display settings
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  delay(2000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  
  /// set the initial state of the relay
  pinMode(relayPin, OUTPUT); // relas as an output
  digitalWrite(relayPin, HIGH); // HIGH = start in OFF state
  
  lastCycleTime = millis() - minCycleInterval; // because of the 3-minute break we couldn't start watering, this line allows us to immediately start the first watering, the next one in 3 minutes
  
  // ------- Configuring SoftAP -------
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Plant", "12345678");
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());
  
  // create a SoftAP network with a username and password
  if (WiFi.softAP("Plant", "12345678")) {
    Serial.println("SoftAP started successfully");
  } else {
    Serial.println("SoftAP start failed");
  }
  
  // displaying ESP IP on the network
  Serial.print("SoftAP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // initialize SPIFFS to read HTML/CSS/JS files
  if(!SPIFFS.begin()){
    Serial.println("SPIFFS Mount Failed");
    Serial.println("Upload files using: Tools -> ESP8266 Sketch Data Upload");
  } else {
    Serial.println("SPIFFS mounted successfully");
  }
  
  // setting up a web server
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/mode", HTTP_POST, handleMode);
  server.on("/relay", HTTP_POST, handleRelay);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/settings", handleSettings);
  
  // processing all other requests (for CSS and JS)
  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "File Not Found");
    }
  });
  
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient(); // processing HTTP requests
  
  unsigned long currentMillis = millis();
  
  // --- auto mode ---
  if (autoMode) {
    // 1) The sensor checks the soil moisture five times. Based on the average value, a decision is made about whether to water the plant.
    int fiveMeasurements = 0;
    for(int i = 0; i < 5; i++){
      fiveMeasurements += reverseValue(analogRead(sensorPin)); // change the reference system
      delay(10);
    }
    int soilMoisture = fiveMeasurements/5; // average soil moisture value based on 5 measurements
    int moisturePercent = soilMoisture * 100 / 1024; // current soil moisture in percent
    
    // 2) Checking the sensor - if there is no sensor, turn off the pump
    if(soilMoisture == 0){
      digitalWrite(relayPin, HIGH);
      isWatering = false;
      pumpOn = false;
      displayStatus("DISCONNECTED", 0);
      return;
    }
    
    // 3) maintaining hysteresis 36–58%

    //----------lower limit of hysteresis - we water the plant----------
    if (!isWatering && !isInBreak && soilMoisture < lowPoint){
      totalPulses = (soilMoisture < 250) ? 6 : 4;
      currentPulse = 0;
      isWatering = true;
      pumpOn = true;
      pulseStart = currentMillis;
      digitalWrite(relayPin, LOW);
      displayStatus("DRY", moisturePercent);
    }
    
    // 4) pulse watering logic
    if (isWatering) {
      // The pump is on - check the pulse duration
      // If the pump is on and 3 seconds have passed since it was turned on - turn it off
      if (pumpOn && (currentMillis - pulseStart >= interval)) {
        digitalWrite(relayPin, HIGH); // turn the motor OFF
        pumpOn = false;
        pauseStart = currentMillis; // remember the start of the break
        displayStatus("WATER OFF - PAUSE START", moisturePercent);
      }

      // Pause between pulses
      // If the pump is off and a 10-second pause has passed, it's time to issue the next pulse.
      if (!pumpOn && (currentMillis - pauseStart >= breakInterval)) {
        currentPulse++;
        if (currentPulse < totalPulses) {
          digitalWrite(relayPin, LOW);
          pumpOn = true;
          pulseStart = currentMillis;
          Serial.println("WATER ON - NEXT PULSE");
        } else {
          isWatering = false;
          isInBreak = true;
          lastCycleTime = currentMillis;
          breakStartTime = currentMillis;
          Serial.println("CYCLE FINISHED - WAIT 30 MIN");
        }
      }
    }
    
    // 5) display update
    if (isWatering) {
      displayStatus(pumpOn ? "WATER ON" : "PAUSE", moisturePercent);
    } else {
      if (soilMoisture > highPoint)
        displayStatus("HUMID", moisturePercent);
      else if (soilMoisture < lowPoint)
        displayStatus("DRY", moisturePercent);
      else
        displayStatus("OK", moisturePercent);
    }
    
    if (isInBreak && (currentMillis - breakStartTime >= minCycleInterval)) {
      isInBreak = false;
    }
  } else {
    // in manual mode, we control the relay via the web interface
    digitalWrite(relayPin, manualRelayState ? LOW : HIGH);
    
    // update display in manual mode
    int fiveMeasurements = 0;
    for(int i = 0; i < 5; i++){
      fiveMeasurements += reverseValue(analogRead(sensorPin));
      delay(10);
    }
    int soilMoisture = fiveMeasurements/5;
    int moisturePercent = soilMoisture * 100 / 1024;
    displayStatus("MANUAL", moisturePercent);
  }
}
