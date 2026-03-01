/*
 * ESP32 IoT Room Controller - AC Remote Cloner (ESP32 Core 3.x Compatible)
 * Compatible with ESP32 Arduino Core 3.x
 * Hardware Connections:
 * - IR Receiver: Pin 16
 * - IR Transmitter: Pin 17
 * - OLED Display: I2C (SDA: Pin 21, SCL: Pin 22)
 *
 * Serial Commands:
 * - "learn" - Start learning mode to capture IR signals
 * - "stop" - Stop learning mode
 * - "send <index>" - Send stored command by index (0-9)
 * - "list" - List all stored commands
 * - "clear" - Clear all stored commands
 * - "status" - Show system status
 * - "help" - Show available commands
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

// Display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pin definitions
#define IR_RECEIVE_PIN 16
#define IR_SEND_PIN 17

// Relay pins for 220V lights
#define RELAY_LIGHT_1 32
#define RELAY_LIGHT_2 33
#define RELAY_LIGHT_3 25
#define RELAY_DEVICE_1 26
#define RELAY_DEVICE_2 27

// LED pins
#define ONBOARD_LED 2
#define RGB_LED_R 5
#define RGB_LED_G 18
#define RGB_LED_B 19

// IR signal storage
struct IRCommand {
  String name;
  uint32_t protocol;
  uint32_t address;
  uint32_t command;
  uint16_t rawData[200]; // Raw timing data
  uint16_t rawLen;
  bool isRaw;
};

// WiFi and Web Server
WebServer server(80);
DNSServer dnsServer;
const char* ap_ssid = "birdnest-roomcontrol";
const char* ap_password = "kintamani";
bool isAPMode = false;
String savedSSID = "";
String savedPassword = "";

// MQTT
WiFiClient mqttNet;
PubSubClient mqttClient(mqttNet);
String mqttHost = "";
uint16_t mqttPort = 1883;
String mqttUser = "";
String mqttPass = "";
String mqttClientId = "";
String mqttBaseTopic = "";
unsigned long lastMqttConnectAttemptMs = 0;
unsigned long lastMqttStatusPublishMs = 0;
const unsigned long mqttConnectRetryMs = 5000;
const unsigned long mqttStatusPublishIntervalMs = 2000;

// Device identity + OTA
String unitName = "";
String otaPassword = "";
bool otaReady = false;
bool relayActiveLow = true;
bool wifiEventsReady = false;
unsigned long lastWifiReconnectAttemptMs = 0;
const unsigned long wifiReconnectIntervalMs = 10000;

// Global variables
Preferences preferences;
IRCommand commands[10];
int commandCount = 0;
bool learningMode = false;
volatile bool irReceived = false;
volatile unsigned long lastEdgeTime = 0;
volatile uint16_t rawBuffer[200];
volatile uint16_t rawIndex = 0;
volatile bool lastState = HIGH;

// Device states
bool lightStates[3] = {false, false, false};
bool deviceStates[2] = {false, false};
bool onboardLedState = false;
int rgbColor[3] = {0, 0, 0}; // R, G, B values (0-255)

// Function declarations
String getMainHTML();
String getWiFiConfigHTML();
String getMQTTConfigHTML();
void loadCommands();
void setupMQTT();
void mqttLoop();
bool mqttIsEnabled();
bool mqttConnect();
void mqttCallback(char* topic, byte* payload, unsigned int length);
String buildStatusJson();
void mqttPublishStatus(bool force);
bool mqttPublishJson(const String& topic, const String& json, bool retained);
bool parseJsonBool(JsonVariant v, bool& out);
bool parseTopicCommand(const String& topic, String& command);
String getDeviceId();
void setupOTA();
void otaLoop();
uint8_t relayWriteLevel(bool on);
bool relayStateFromPin(int pin);
void setupWiFiEvents();
void wifiLoop();

// IR receive interrupt handler
void IRAM_ATTR irReceiveISR() {
  unsigned long currentTime = micros();
  bool currentState = digitalRead(IR_RECEIVE_PIN);
  
  if (learningMode && rawIndex < 199) {
    unsigned long duration = currentTime - lastEdgeTime;
    if (duration > 50 && duration < 20000) { // Filter noise
      rawBuffer[rawIndex++] = (uint16_t)duration;
    }
  }
  
  lastEdgeTime = currentTime;
  lastState = currentState;
  
  if (learningMode && rawIndex > 10) {
    irReceived = true;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 AC Controller (Core 3.x Compatible) Starting...");
  
  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("BIRDNEST KINTAMANI");
  display.println("Room Controller");
  display.println("");
  display.println("by Orca Technology");
  display.println("Type 'help' for commands");
  display.println("");
  display.println("Initializing...");
  display.display();
  
  preferences.begin("ac_controller", false);
  relayActiveLow = preferences.getBool("relay_active_low", true);

  setupWiFiEvents();

  // Initialize pins
  pinMode(IR_RECEIVE_PIN, INPUT_PULLUP);
  pinMode(IR_SEND_PIN, OUTPUT);
  digitalWrite(IR_SEND_PIN, LOW);
  
  // Initialize relay pins
  pinMode(RELAY_LIGHT_1, OUTPUT);
  pinMode(RELAY_LIGHT_2, OUTPUT);
  pinMode(RELAY_LIGHT_3, OUTPUT);
  pinMode(RELAY_DEVICE_1, OUTPUT);
  pinMode(RELAY_DEVICE_2, OUTPUT);
  digitalWrite(RELAY_LIGHT_1, relayWriteLevel(false));
  digitalWrite(RELAY_LIGHT_2, relayWriteLevel(false));
  digitalWrite(RELAY_LIGHT_3, relayWriteLevel(false));
  digitalWrite(RELAY_DEVICE_1, relayWriteLevel(false));
  digitalWrite(RELAY_DEVICE_2, relayWriteLevel(false));
  
  // Initialize LED pins
  pinMode(ONBOARD_LED, OUTPUT);
  pinMode(RGB_LED_R, OUTPUT);
  pinMode(RGB_LED_G, OUTPUT);
  pinMode(RGB_LED_B, OUTPUT);
  digitalWrite(ONBOARD_LED, LOW);
  analogWrite(RGB_LED_R, 0);
  analogWrite(RGB_LED_G, 0);
  analogWrite(RGB_LED_B, 0);
  
  // Attach interrupt for IR receiving
  attachInterrupt(digitalPinToInterrupt(IR_RECEIVE_PIN), irReceiveISR, CHANGE);
  
  loadCommands();
  
  // Initialize WiFi
  setupWiFi();
  
  // Setup web server routes
  setupWebServer();
  setupMQTT();
  setupOTA();
  
  Serial.println("Setup complete!");
  Serial.println("Available commands: learn, stop, send <index>, list, clear, status, help");
  Serial.println("Device control: light <1-3> <on/off>, device <1-2> <on/off>, led <on/off>, rgb <r> <g> <b>");
  Serial.println("Pin 23 available for other devices");
  
  if (isAPMode) {
    Serial.println("\n=== ACCESS POINT MODE ===");
    Serial.println("SSID: " + String(ap_ssid));
    Serial.println("IP: " + WiFi.softAPIP().toString());
    Serial.println("Open browser and go to: http://" + WiFi.softAPIP().toString());
  } else {
    Serial.println("\n=== STATION MODE ===");
    Serial.println("Connected to: " + WiFi.SSID());
    Serial.println("IP: " + WiFi.localIP().toString());
    Serial.println("Open browser and go to: http://" + WiFi.localIP().toString());
  }
  Serial.println("========================\n");
}

void loop() {
  handleSerial();
  handleIR();
  wifiLoop();
  mqttLoop();
  otaLoop();
  
  // Handle web server
  server.handleClient();
  
  // Handle DNS server in AP mode
  if (isAPMode) {
    dnsServer.processNextRequest();
  }
  
  delay(10);
}

String getDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[17];
  snprintf(buf, sizeof(buf), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
}

bool mqttIsEnabled() {
  return mqttHost.length() > 0 && !isAPMode;
}

void setupMQTT() {
  mqttHost = preferences.getString("mqtt_host", mqttHost);
  mqttPort = preferences.getUShort("mqtt_port", mqttPort);
  mqttUser = preferences.getString("mqtt_user", mqttUser);
  mqttPass = preferences.getString("mqtt_pass", mqttPass);

  mqttClientId = String("birdnest-") + getDeviceId();
  mqttBaseTopic = String("birdnest/controllers/") + mqttClientId;

  unitName = preferences.getString("unit_name", unitName);
  otaPassword = preferences.getString("ota_pass", otaPassword);
  relayActiveLow = preferences.getBool("relay_active_low", relayActiveLow);

  mqttClient.setBufferSize(2048);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(5);
  mqttClient.setCallback(mqttCallback);

  if (!mqttIsEnabled()) {
    Serial.println("MQTT disabled (mqttHost empty or AP mode).");
    return;
  }

  Serial.println(String("MQTT base topic: ") + mqttBaseTopic);
}

bool mqttPublishJson(const String& topic, const String& json, bool retained) {
  if (!mqttClient.connected()) return false;
  return mqttClient.publish(topic.c_str(), json.c_str(), retained);
}

String buildStatusJson() {
  DynamicJsonDocument doc(1024);

  doc["device"]["id"] = mqttClientId;
  if (unitName.length() > 0) doc["device"]["name"] = unitName;
  doc["wifi"]["connected"] = !isAPMode;
  doc["wifi"]["ssid"] = isAPMode ? String(ap_ssid) : WiFi.SSID();
  doc["wifi"]["ip"] = isAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["wifi"]["mode"] = isAPMode ? "AP" : "STA";

  JsonArray lights = doc.createNestedArray("lights");
  lightStates[0] = relayStateFromPin(RELAY_LIGHT_1);
  lightStates[1] = relayStateFromPin(RELAY_LIGHT_2);
  lightStates[2] = relayStateFromPin(RELAY_LIGHT_3);
  for (int i = 0; i < 3; i++) lights.add(lightStates[i]);

  JsonArray devices = doc.createNestedArray("devices");
  deviceStates[0] = relayStateFromPin(RELAY_DEVICE_1);
  deviceStates[1] = relayStateFromPin(RELAY_DEVICE_2);
  for (int i = 0; i < 2; i++) devices.add(deviceStates[i]);

  doc["onboard_led"] = onboardLedState;

  JsonArray rgb = doc.createNestedArray("rgb");
  for (int i = 0; i < 3; i++) rgb.add(rgbColor[i]);

  doc["ir"]["learning"] = learningMode;
  doc["ir"]["commands"] = commandCount;
  doc["ir"]["max_commands"] = 10;

  String out;
  serializeJson(doc, out);
  return out;
}

void mqttPublishStatus(bool force) {
  if (!mqttClient.connected()) return;

  unsigned long now = millis();
  if (!force && now - lastMqttStatusPublishMs < mqttStatusPublishIntervalMs) return;
  lastMqttStatusPublishMs = now;

  String topic = mqttBaseTopic + "/status";
  String json = buildStatusJson();
  mqttPublishJson(topic, json, true);
}

bool mqttConnect() {
  if (!mqttIsEnabled()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  mqttClient.setServer(mqttHost.c_str(), mqttPort);

  String willTopic = mqttBaseTopic + "/availability";
  bool ok;

  if (mqttUser.length() > 0) {
    ok = mqttClient.connect(
      mqttClientId.c_str(),
      mqttUser.c_str(),
      mqttPass.c_str(),
      willTopic.c_str(),
      1,
      true,
      "offline"
    );
  } else {
    ok = mqttClient.connect(
      mqttClientId.c_str(),
      willTopic.c_str(),
      1,
      true,
      "offline"
    );
  }

  if (!ok) return false;

  mqttClient.publish(willTopic.c_str(), "online", true);

  mqttClient.subscribe((mqttBaseTopic + "/cmd/#").c_str());
  mqttClient.subscribe("birdnest/cmd/all/#");

  DynamicJsonDocument info(512);
  info["id"] = mqttClientId;
  if (unitName.length() > 0) info["name"] = unitName;
  info["ip"] = WiFi.localIP().toString();
  info["ssid"] = WiFi.SSID();
  info["base_topic"] = mqttBaseTopic;
  info["capabilities"]["lights"] = 3;
  info["capabilities"]["devices"] = 2;
  info["capabilities"]["rgb"] = true;
  info["capabilities"]["ir"] = true;
  String infoJson;
  serializeJson(info, infoJson);
  mqttPublishJson(mqttBaseTopic + "/info", infoJson, true);
  mqttPublishStatus(true);

  Serial.println(String("MQTT connected as ") + mqttClientId + " to " + mqttHost + ":" + String(mqttPort));
  return true;
}

void mqttLoop() {
  if (!mqttIsEnabled()) return;

  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttConnectAttemptMs >= mqttConnectRetryMs) {
      lastMqttConnectAttemptMs = now;
      mqttConnect();
    }
    return;
  }

  mqttClient.loop();
  mqttPublishStatus(false);
}

bool parseJsonBool(JsonVariant v, bool& out) {
  if (v.is<bool>()) {
    out = v.as<bool>();
    return true;
  }
  if (v.is<int>()) {
    out = v.as<int>() != 0;
    return true;
  }
  if (v.is<const char*>()) {
    String s = v.as<const char*>();
    s.toLowerCase();
    if (s == "true" || s == "1" || s == "on") {
      out = true;
      return true;
    }
    if (s == "false" || s == "0" || s == "off") {
      out = false;
      return true;
    }
  }
  return false;
}

bool parseTopicCommand(const String& topic, String& command) {
  if (topic.startsWith(mqttBaseTopic + "/cmd/")) {
    command = topic.substring((mqttBaseTopic + "/cmd/").length());
    return true;
  }
  if (topic.startsWith("birdnest/cmd/all/")) {
    command = topic.substring(String("birdnest/cmd/all/").length());
    return true;
  }
  return false;
}

void mqttCallback(char* topicRaw, byte* payload, unsigned int length) {
  String topic(topicRaw);
  String cmd;
  if (!parseTopicCommand(topic, cmd)) return;

  String body;
  body.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) body += (char)payload[i];

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, body);

  DynamicJsonDocument resp(512);
  resp["ok"] = false;
  resp["cmd"] = cmd;
  resp["ts_ms"] = (uint32_t)millis();

  if (err) {
    resp["error"] = "invalid_json";
    String out;
    serializeJson(resp, out);
    mqttPublishJson(mqttBaseTopic + "/ack", out, false);
    return;
  }

  bool handled = false;
  bool ok = false;

  if (cmd == "light") {
    int lightNum = doc["light"] | 0;
    bool state;
    if (lightNum >= 1 && lightNum <= 3 && parseJsonBool(doc["state"], state)) {
      handleLightCommand(String("light ") + String(lightNum) + " " + (state ? "on" : "off"));
      handled = true;
      ok = true;
    } else {
      handled = true;
      resp["error"] = "invalid_params";
    }
  } else if (cmd == "device") {
    int deviceNum = doc["device"] | 0;
    bool state;
    if (deviceNum >= 1 && deviceNum <= 2 && parseJsonBool(doc["state"], state)) {
      handleDeviceCommand(String("device ") + String(deviceNum) + " " + (state ? "on" : "off"));
      handled = true;
      ok = true;
    } else {
      handled = true;
      resp["error"] = "invalid_params";
    }
  } else if (cmd == "led") {
    bool state;
    if (parseJsonBool(doc["state"], state)) {
      handleLedCommand(String("led ") + (state ? "on" : "off"));
      handled = true;
      ok = true;
    } else {
      handled = true;
      resp["error"] = "invalid_params";
    }
  } else if (cmd == "rgb") {
    int r = doc["r"] | -1;
    int g = doc["g"] | -1;
    int b = doc["b"] | -1;
    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
      handleRgbCommand(String("rgb ") + String(r) + " " + String(g) + " " + String(b));
      handled = true;
      ok = true;
    } else {
      handled = true;
      resp["error"] = "invalid_params";
    }
  } else if (cmd == "ir/learn") {
    if (doc.containsKey("slot")) {
      int slot = doc["slot"] | -1;
      if (slot >= 0 && slot < 5 && slot < 10) {
        if (slot < commandCount) {
          for (int i = slot; i < commandCount - 1; i++) commands[i] = commands[i + 1];
          commandCount--;
        }
        startLearning();
        handled = true;
        ok = true;
      } else {
        handled = true;
        resp["error"] = "invalid_slot";
      }
    } else {
      startLearning();
      handled = true;
      ok = true;
    }
  } else if (cmd == "ir/stop") {
    stopLearning();
    handled = true;
    ok = true;
  } else if (cmd == "ir/send") {
    int slot = doc["slot"] | -1;
    if (slot >= 0 && slot < commandCount) {
      sendCommand(slot);
      handled = true;
      ok = true;
    } else {
      handled = true;
      resp["error"] = "invalid_slot";
    }
  } else if (cmd == "ir/list") {
    DynamicJsonDocument listDoc(1024);
    JsonArray cmds = listDoc.createNestedArray("commands");
    for (int i = 0; i < commandCount; i++) {
      JsonObject c = cmds.createNestedObject();
      c["slot"] = i;
      c["name"] = commands[i].name;
      c["type"] = commands[i].isRaw ? "raw" : "decoded";
    }
    listDoc["count"] = commandCount;
    listDoc["max"] = 10;
    String out;
    serializeJson(listDoc, out);
    mqttPublishJson(mqttBaseTopic + "/ir/list", out, false);
    handled = true;
    ok = true;
  } else if (cmd == "ir/clear") {
    clearCommands();
    handled = true;
    ok = true;
  } else if (cmd == "status") {
    mqttPublishStatus(true);
    handled = true;
    ok = true;
  }

  if (!handled) {
    resp["error"] = "unknown_cmd";
  }

  resp["ok"] = ok;
  String out;
  serializeJson(resp, out);
  mqttPublishJson(mqttBaseTopic + "/ack", out, false);
  if (ok) mqttPublishStatus(true);
}

void handleSerial() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.toLowerCase();
    
    if (input == "learn") {
      startLearning();
    } else if (input == "stop") {
      stopLearning();
    } else if (input.startsWith("send ")) {
      int index = input.substring(5).toInt();
      sendCommand(index);
    } else if (input.startsWith("light ")) {
      handleLightCommand(input);
    } else if (input.startsWith("device ")) {
      handleDeviceCommand(input);
    } else if (input.startsWith("led ")) {
      handleLedCommand(input);
    } else if (input.startsWith("rgb ")) {
      handleRgbCommand(input);
    } else if (input == "list") {
      listCommands();
    } else if (input == "clear") {
      clearCommands();
    } else if (input == "status") {
      showStatus();
    } else if (input == "help") {
      showHelp();
    } else {
      Serial.println("Unknown command. Type 'help' for available commands.");
    }
  }
}

void handleIR() {
  if (irReceived && learningMode) {
    irReceived = false;
    
    // Wait for signal to complete
    delay(100);
    
    if (rawIndex > 20) { // Minimum signal length
      Serial.println("IR signal captured!");
      
      // Store the command
      if (commandCount < 10) {
        commands[commandCount].name = "CMD_" + String(commandCount);
        commands[commandCount].isRaw = true;
        commands[commandCount].rawLen = rawIndex;
        
        // Copy raw data
        for (int i = 0; i < rawIndex && i < 200; i++) {
          commands[commandCount].rawData[i] = rawBuffer[i];
        }
        
        commandCount++;
        saveCommands();
        
        Serial.println(String("Command stored as: ") + commands[commandCount-1].name);
        updateDisplay(String("Signal Learned!"), String("Stored as: ") + commands[commandCount-1].name);
      } else {
        Serial.println("Storage full! Clear commands first.");
        updateDisplay(String("Storage Full!"), String("Clear commands first"));
      }
      
      stopLearning();
    } else {
      Serial.println("Signal too short, try again.");
    }
    
    rawIndex = 0;
  }
}

void startLearning() {
  if (commandCount >= 10) {
    Serial.println("Storage full! Clear commands first.");
    updateDisplay(String("Storage Full!"), String("Clear commands first"));
    return;
  }
  
  learningMode = true;
  rawIndex = 0;
  irReceived = false;
  
  Serial.println("Learning mode started. Point remote at IR receiver and press a button.");
  updateDisplay(String("Learning Mode"), String("Press remote button"));
}

void stopLearning() {
  learningMode = false;
  rawIndex = 0;
  irReceived = false;
  
  Serial.println("Learning mode stopped.");
  updateDisplay(String("Ready"), String("Type 'help' for commands"));
}

void sendCommand(int index) {
  if (index < 0 || index >= commandCount) {
    Serial.println("Invalid command index. Use 'list' to see available commands.");
    return;
  }
  
  Serial.println(String("Sending command: ") + commands[index].name);
  updateDisplay(String("Sending..."), commands[index].name);
  
  if (commands[index].isRaw) {
    sendRawIR(commands[index].rawData, commands[index].rawLen);
  }
  
  Serial.println("Command sent!");
  updateDisplay(String("Command Sent!"), commands[index].name);
  
  delay(1000);
  updateDisplay(String("Ready"), String("Type 'help' for commands"));
}

void sendRawIR(uint16_t* rawData, uint16_t len) {
  // Simple IR transmission using bit-banging
  // 38kHz carrier frequency
  const int carrierFreq = 38000;
  const int halfPeriod = 1000000 / carrierFreq / 2; // microseconds
  
  noInterrupts();
  
  for (int i = 0; i < len; i++) {
    bool sendCarrier = (i % 2 == 0); // Alternate between mark and space
    unsigned long duration = rawData[i];
    unsigned long startTime = micros();
    
    while (micros() - startTime < duration) {
      if (sendCarrier) {
        digitalWrite(IR_SEND_PIN, HIGH);
        delayMicroseconds(halfPeriod);
        digitalWrite(IR_SEND_PIN, LOW);
        delayMicroseconds(halfPeriod);
      } else {
        digitalWrite(IR_SEND_PIN, LOW);
        delayMicroseconds(26); // Small delay for timing
      }
    }
  }
  
  digitalWrite(IR_SEND_PIN, LOW);
  interrupts();
}

void handleLightCommand(String input) {
  int spaceIndex1 = input.indexOf(' ');
  int spaceIndex2 = input.indexOf(' ', spaceIndex1 + 1);
  
  if (spaceIndex1 == -1 || spaceIndex2 == -1) {
    Serial.println("Usage: light <1-3> <on/off>");
    return;
  }
  
  int lightNum = input.substring(spaceIndex1 + 1, spaceIndex2).toInt();
  String state = input.substring(spaceIndex2 + 1);
  
  if (lightNum < 1 || lightNum > 3) {
    Serial.println("Light number must be 1-3");
    return;
  }
  
  bool turnOn = (state == "on");
  int relayPin;
  
  switch (lightNum) {
    case 1: relayPin = RELAY_LIGHT_1; break;
    case 2: relayPin = RELAY_LIGHT_2; break;
    case 3: relayPin = RELAY_LIGHT_3; break;
  }
  
  digitalWrite(relayPin, relayWriteLevel(turnOn));
  lightStates[lightNum - 1] = turnOn;
  
  Serial.println(String("Light ") + String(lightNum) + String(" turned ") + (turnOn ? String("ON") : String("OFF")));
  updateDisplay(String("Light ") + String(lightNum), turnOn ? String("ON") : String("OFF"));
}

void handleDeviceCommand(String input) {
  int spaceIndex1 = input.indexOf(' ');
  int spaceIndex2 = input.indexOf(' ', spaceIndex1 + 1);
  
  if (spaceIndex1 == -1 || spaceIndex2 == -1) {
    Serial.println("Usage: device <1-2> <on/off>");
    return;
  }
  
  int deviceNum = input.substring(spaceIndex1 + 1, spaceIndex2).toInt();
  String state = input.substring(spaceIndex2 + 1);
  
  if (deviceNum < 1 || deviceNum > 2) {
    Serial.println("Device number must be 1-2");
    return;
  }
  
  bool turnOn = (state == "on");
  int relayPin = (deviceNum == 1) ? RELAY_DEVICE_1 : RELAY_DEVICE_2;
  
  digitalWrite(relayPin, relayWriteLevel(turnOn));
  deviceStates[deviceNum - 1] = turnOn;
  
  Serial.println(String("Device ") + String(deviceNum) + String(" turned ") + (turnOn ? String("ON") : String("OFF")));
  updateDisplay(String("Device ") + String(deviceNum), turnOn ? String("ON") : String("OFF"));
}

void handleLedCommand(String input) {
  int spaceIndex = input.indexOf(' ');
  
  if (spaceIndex == -1) {
    Serial.println("Usage: led <on/off>");
    return;
  }
  
  String state = input.substring(spaceIndex + 1);
  bool turnOn = (state == "on");
  
  digitalWrite(ONBOARD_LED, turnOn ? HIGH : LOW);
  onboardLedState = turnOn;
  
  Serial.println(String("Onboard LED turned ") + (turnOn ? String("ON") : String("OFF")));
  updateDisplay(String("Onboard LED"), turnOn ? String("ON") : String("OFF"));
}

void handleRgbCommand(String input) {
  int spaceIndex1 = input.indexOf(' ');
  int spaceIndex2 = input.indexOf(' ', spaceIndex1 + 1);
  int spaceIndex3 = input.indexOf(' ', spaceIndex2 + 1);
  
  if (spaceIndex1 == -1 || spaceIndex2 == -1 || spaceIndex3 == -1) {
    Serial.println("Usage: rgb <r> <g> <b> (0-255)");
    return;
  }
  
  int r = input.substring(spaceIndex1 + 1, spaceIndex2).toInt();
  int g = input.substring(spaceIndex2 + 1, spaceIndex3).toInt();
  int b = input.substring(spaceIndex3 + 1).toInt();
  
  if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
    Serial.println("RGB values must be 0-255");
    return;
  }
  
  analogWrite(RGB_LED_R, r);
  analogWrite(RGB_LED_G, g);
  analogWrite(RGB_LED_B, b);
  
  rgbColor[0] = r;
  rgbColor[1] = g;
  rgbColor[2] = b;
  
  Serial.println(String("RGB LED set to R:") + String(r) + String(" G:") + String(g) + String(" B:") + String(b));
  updateDisplay(String("RGB LED"), String("R:") + String(r) + String(" G:") + String(g) + String(" B:") + String(b));
}

void listCommands() {
  Serial.println("\nStored Commands:");
  if (commandCount == 0) {
    Serial.println("No commands stored.");
  } else {
    for (int i = 0; i < commandCount; i++) {
      Serial.println(String(i) + String(": ") + commands[i].name);
    }
  }
  Serial.println("");
}

void clearCommands() {
  commandCount = 0;
  preferences.clear();
  Serial.println("All commands cleared.");
  updateDisplay(String("Commands Cleared"), String("Storage empty"));
  delay(1000);
  updateDisplay(String("Ready"), String("Type 'help' for commands"));
}

void showStatus() {
  Serial.println("\n=== System Status ===");
  Serial.println(String("IR Commands stored: ") + String(commandCount) + String("/10"));
  Serial.println(String("Free heap: ") + String(ESP.getFreeHeap()) + String(" bytes"));
  Serial.println(String("Uptime: ") + String(millis() / 1000) + String(" seconds"));
  Serial.println(String("ESP32 Core: ") + String(ESP_ARDUINO_VERSION_MAJOR) + String(".") + String(ESP_ARDUINO_VERSION_MINOR) + String(".") + String(ESP_ARDUINO_VERSION_PATCH));
  Serial.println(String("Learning mode: ") + String(learningMode ? "ON" : "OFF"));
  Serial.println("");
  Serial.println("=== Device Status ===");
  Serial.println("220V Lights:");
  for (int i = 0; i < 3; i++) {
    Serial.println(String("  Light ") + String(i + 1) + String(": ") + (lightStates[i] ? String("ON") : String("OFF")));
  }
  Serial.println("Other Devices:");
  for (int i = 0; i < 2; i++) {
    Serial.println(String("  Device ") + String(i + 1) + String(": ") + (deviceStates[i] ? String("ON") : String("OFF")));
  }
  Serial.println("LEDs:");
  Serial.println(String("  Onboard LED: ") + String(onboardLedState ? "ON" : "OFF"));
  Serial.println(String("  RGB LED: R:") + String(rgbColor[0]) + String(" G:") + String(rgbColor[1]) + String(" B:") + String(rgbColor[2]));
  Serial.println("");
  Serial.println("Available pins: 23");
  Serial.println("=====================\n");
}

void showHelp() {
  Serial.println("\n=== Available Commands ===");
  Serial.println("IR Commands:");
  Serial.println("learn          - Start learning mode to capture IR signals");
  Serial.println("stop           - Stop learning mode");
  Serial.println("send <index>   - Send stored command by index (0-9)");
  Serial.println("list           - List all stored commands");
  Serial.println("clear          - Clear all stored commands");
  Serial.println("");
  Serial.println("Device Control:");
  Serial.println("light <1-3> <on/off> - Control 220V lights");
  Serial.println("device <1-2> <on/off> - Control other devices");
  Serial.println("led <on/off>   - Control onboard LED");
  Serial.println("rgb <r> <g> <b> - Set RGB LED color (0-255)");
  Serial.println("");
  Serial.println("System:");
  Serial.println("status         - Show system status");
  Serial.println("help           - Show this help message");
  Serial.println("\nExamples:");
  Serial.println("send 0         - Send first stored IR command");
  Serial.println("light 1 on     - Turn on light 1");
  Serial.println("device 2 off   - Turn off device 2");
  Serial.println("rgb 255 0 0    - Set RGB LED to red");
  Serial.println("========================\n");
}

void updateDisplay(String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("BIRDNEST KINTAMANI");
  display.println("Room Controller");
  
  // Display IP address
  if (isAPMode) {
    display.println("AP: " + WiFi.softAPIP().toString());
  } else if (WiFi.status() == WL_CONNECTED) {
    display.println("IP: " + WiFi.localIP().toString());
  } else {
    display.println("WiFi: Disconnected");
  }
  
  display.println("");
  display.println(line1);
  display.println(line2);
  display.println("");
  display.println(String("Commands: ") + String(commandCount) + String("/10"));
  display.display();
}

void saveCommands() {
  preferences.putInt("count", commandCount);
  
  for (int i = 0; i < commandCount; i++) {
    String prefix = "cmd" + String(i) + "_";
    String nameKey = prefix + String("name");
    String rawKey = prefix + String("raw");
    String lenKey = prefix + String("len");
    
    preferences.putString(nameKey.c_str(), commands[i].name);
    preferences.putBool(rawKey.c_str(), commands[i].isRaw);
    preferences.putUShort(lenKey.c_str(), commands[i].rawLen);
    
    // Save raw data in chunks
    for (int j = 0; j < commands[i].rawLen && j < 200; j += 10) {
      String key = prefix + String("data") + String(j/10);
      size_t chunkSize = min(10, commands[i].rawLen - j);
      preferences.putBytes(key.c_str(), &commands[i].rawData[j], chunkSize * sizeof(uint16_t));
    }
  }
}

void setupWiFi() {
  // Load saved WiFi credentials
  savedSSID = preferences.getString("wifi_ssid", "");
  savedPassword = preferences.getString("wifi_pass", "");
  
  if (savedSSID.length() > 0) {
    Serial.println("Attempting to connect to saved WiFi: " + savedSSID);
    updateDisplay("Connecting WiFi...", savedSSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    
    // Wait for connection for 10 seconds
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed. Starting Access Point mode...");
    startAPMode();
    updateDisplay("AP Mode Active", "Connect to setup");
  } else {
    Serial.println("WiFi connected successfully!");
    isAPMode = false;
    updateDisplay("WiFi Connected!", "Ready for control");
  }
}

void setupWiFiEvents() {
  if (wifiEventsReady) return;
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      isAPMode = false;
      lastWifiReconnectAttemptMs = 0;
      setupMQTT();
      setupOTA();
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      otaReady = false;
      if (mqttClient.connected()) mqttClient.disconnect();
    }
  });
  wifiEventsReady = true;
}

void wifiLoop() {
  if (isAPMode) return;
  if (savedSSID.length() == 0) return;
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastWifiReconnectAttemptMs < wifiReconnectIntervalMs) return;
  lastWifiReconnectAttemptMs = now;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.disconnect();
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  updateDisplay("WiFi reconnecting...", savedSSID);
}

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  // Setup captive portal
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  isAPMode = true;
  Serial.println("Access Point started");
  Serial.println("SSID: " + String(ap_ssid));
  Serial.println("IP: " + WiFi.softAPIP().toString());
  
  // Update OLED display with AP information
  updateDisplay("AP: " + String(ap_ssid), "Setup via browser");
}

void setupWebServer() {
  // Serve main page
  server.on("/", HTTP_GET, handleRoot);
  
  // WiFi configuration endpoints
  server.on("/wifi", HTTP_GET, handleWiFiConfig);
  server.on("/wifi", HTTP_POST, handleWiFiSave);
  server.on("/scan", HTTP_GET, handleWiFiScan);

  // MQTT configuration endpoints
  server.on("/mqtt", HTTP_GET, handleMQTTConfig);
  server.on("/mqtt", HTTP_POST, handleMQTTSave);
  
  // Device control API endpoints
  server.on("/api/status", HTTP_GET, handleAPIStatus);
  server.on("/api/light", HTTP_POST, handleAPILight);
  server.on("/api/device", HTTP_POST, handleAPIDevice);
  server.on("/api/led", HTTP_POST, handleAPILed);
  server.on("/api/rgb", HTTP_POST, handleAPIRgb);
  
  // IR control API endpoints
  server.on("/api/ir/learn", HTTP_POST, handleAPIIRLearn);
  server.on("/api/ir/stop", HTTP_POST, handleAPIIRStop);
  server.on("/api/ir/send", HTTP_POST, handleAPIIRSend);
  server.on("/api/ir/list", HTTP_GET, handleAPIIRList);
  server.on("/api/ir/clear", HTTP_POST, handleAPIIRClear);
  
  // Handle 404
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("Web server started");
}

// Web Server Handlers
void handleRoot() {
  String html = getMainHTML();
  server.send(200, "text/html", html);
}

void handleWiFiConfig() {
  String html = getWiFiConfigHTML();
  server.send(200, "text/html", html);
}

void handleWiFiSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    preferences.putString("wifi_ssid", ssid);
    preferences.putString("wifi_pass", password);
    
    server.send(200, "text/html", "<html><body><h2>WiFi settings saved!</h2><p>Restarting...</p><script>setTimeout(function(){window.location.href=\"/\";}, 3000);</script></body></html>");
    
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "<html><body><h2>Error</h2><p>Missing SSID or password</p></body></html>");
  }
}

void handleMQTTConfig() {
  String html = getMQTTConfigHTML();
  server.send(200, "text/html", html);
}

void handleMQTTSave() {
  if (server.hasArg("host")) {
    String host = server.arg("host");
    String portStr = server.arg("port");
    String user = server.arg("user");
    String pass = server.arg("pass");
    bool clearPass = server.hasArg("clear_password");
    String unit = server.arg("unit_name");
    String otaPass = server.arg("ota_pass");
    bool clearOtaPass = server.hasArg("clear_ota_password");
    bool activeLow = server.hasArg("relay_active_low");

    uint32_t port = portStr.toInt();
    if (port == 0 || port > 65535) port = 1883;

    preferences.putString("mqtt_host", host);
    preferences.putUShort("mqtt_port", (uint16_t)port);
    preferences.putString("mqtt_user", user);
    if (unit.length() > 0) {
      preferences.putString("unit_name", unit);
    } else {
      preferences.putString("unit_name", "");
    }
    if (clearPass) {
      preferences.putString("mqtt_pass", "");
    } else if (pass.length() > 0) {
      preferences.putString("mqtt_pass", pass);
    }
    if (clearOtaPass) {
      preferences.putString("ota_pass", "");
    } else if (otaPass.length() > 0) {
      preferences.putString("ota_pass", otaPass);
    }
    preferences.putBool("relay_active_low", activeLow);

    server.send(200, "text/html", "<html><body><h2>MQTT settings saved!</h2><p>Restarting...</p><script>setTimeout(function(){window.location.href=\"/\";}, 3000);</script></body></html>");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "<html><body><h2>Error</h2><p>Missing host</p></body></html>");
  }
}

void handleWiFiScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"secure\":" + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  
  json += "]";
  server.send(200, "application/json", json);
}

void handleAPIStatus() {
  DynamicJsonDocument doc(1024);
  
  doc["wifi"]["connected"] = !isAPMode;
  doc["wifi"]["ssid"] = isAPMode ? String(ap_ssid) : WiFi.SSID();
  doc["wifi"]["ip"] = isAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["wifi"]["mode"] = isAPMode ? "AP" : "STA";
  
  JsonArray lights = doc.createNestedArray("lights");
  lightStates[0] = relayStateFromPin(RELAY_LIGHT_1);
  lightStates[1] = relayStateFromPin(RELAY_LIGHT_2);
  lightStates[2] = relayStateFromPin(RELAY_LIGHT_3);
  for (int i = 0; i < 3; i++) lights.add(lightStates[i]);
  
  JsonArray devices = doc.createNestedArray("devices");
  deviceStates[0] = relayStateFromPin(RELAY_DEVICE_1);
  deviceStates[1] = relayStateFromPin(RELAY_DEVICE_2);
  for (int i = 0; i < 2; i++) devices.add(deviceStates[i]);
  
  doc["onboard_led"] = onboardLedState;
  
  JsonArray rgb = doc.createNestedArray("rgb");
  for (int i = 0; i < 3; i++) {
    rgb.add(rgbColor[i]);
  }
  
  doc["ir"]["learning"] = learningMode;
  doc["ir"]["commands"] = commandCount;
  doc["ir"]["max_commands"] = 10;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAPILight() {
  if (server.hasArg("light") && server.hasArg("state")) {
    int lightNum = server.arg("light").toInt();
    bool state = server.arg("state") == "true";
    
    if (lightNum >= 1 && lightNum <= 3) {
      int relayPin;
      switch (lightNum) {
        case 1: relayPin = RELAY_LIGHT_1; break;
        case 2: relayPin = RELAY_LIGHT_2; break;
        case 3: relayPin = RELAY_LIGHT_3; break;
      }
      
      digitalWrite(relayPin, relayWriteLevel(state));
      lightStates[lightNum - 1] = state;
      
      server.send(200, "application/json", "{\"success\":true}");
      updateDisplay(String("Light ") + String(lightNum), state ? String("ON") : String("OFF"));
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid light number\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameters\"}");
  }
}

void handleAPIDevice() {
  if (server.hasArg("device") && server.hasArg("state")) {
    int deviceNum = server.arg("device").toInt();
    bool state = server.arg("state") == "true";
    
    if (deviceNum >= 1 && deviceNum <= 2) {
      int relayPin = (deviceNum == 1) ? RELAY_DEVICE_1 : RELAY_DEVICE_2;
      
      digitalWrite(relayPin, relayWriteLevel(state));
      deviceStates[deviceNum - 1] = state;
      
      server.send(200, "application/json", "{\"success\":true}");
      updateDisplay(String("Device ") + String(deviceNum), state ? String("ON") : String("OFF"));
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid device number\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameters\"}");
  }
}

void handleAPILed() {
  if (server.hasArg("state")) {
    bool state = server.arg("state") == "true";
    
    digitalWrite(ONBOARD_LED, state ? HIGH : LOW);
    onboardLedState = state;
    
    server.send(200, "application/json", "{\"success\":true}");
    updateDisplay(String("Onboard LED"), state ? String("ON") : String("OFF"));
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing state parameter\"}");
  }
}

void handleAPIRgb() {
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    int r = server.arg("r").toInt();
    int g = server.arg("g").toInt();
    int b = server.arg("b").toInt();
    
    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
      analogWrite(RGB_LED_R, r);
      analogWrite(RGB_LED_G, g);
      analogWrite(RGB_LED_B, b);
      
      rgbColor[0] = r;
      rgbColor[1] = g;
      rgbColor[2] = b;
      
      server.send(200, "application/json", "{\"success\":true}");
      updateDisplay(String("RGB LED"), String("R:") + String(r) + String(" G:") + String(g) + String(" B:") + String(b));
    } else {
      server.send(400, "application/json", "{\"error\":\"RGB values must be 0-255\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing RGB parameters\"}");
  }
}

void handleAPIIRLearn() {
  if (server.hasArg("slot")) {
    int slot = server.arg("slot").toInt();
    if (slot >= 0 && slot < 5 && slot < 10) {
      // Clear the specific slot first
      if (slot < commandCount) {
        // Shift commands down to remove the slot
        for (int i = slot; i < commandCount - 1; i++) {
          commands[i] = commands[i + 1];
        }
        commandCount--;
      }
      
      startLearning();
      server.send(200, "application/json", "{\"success\":true,\"message\":\"Learning mode started\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid slot number\"}");
    }
  } else {
    startLearning();
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Learning mode started\"}");
  }
}

void handleAPIIRStop() {
  stopLearning();
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Learning mode stopped\"}");
}

void handleAPIIRSend() {
  if (server.hasArg("slot")) {
    int slot = server.arg("slot").toInt();
    if (slot >= 0 && slot < commandCount) {
      sendCommand(slot);
      server.send(200, "application/json", "{\"success\":true,\"message\":\"Command sent\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid slot or no command stored\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing slot parameter\"}");
  }
}

void handleAPIIRList() {
  DynamicJsonDocument doc(1024);
  JsonArray cmds = doc.createNestedArray("commands");
  
  for (int i = 0; i < commandCount; i++) {
    JsonObject cmd = cmds.createNestedObject();
    cmd["slot"] = i;
    cmd["name"] = commands[i].name;
    cmd["type"] = commands[i].isRaw ? "raw" : "decoded";
  }
  
  doc["count"] = commandCount;
  doc["max"] = 10;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAPIIRClear() {
  clearCommands();
  server.send(200, "application/json", "{\"success\":true,\"message\":\"All commands cleared\"}");
}

void handleNotFound() {
  if (isAPMode) {
    // Redirect to main page for captive portal
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

void loadCommands() {
  commandCount = preferences.getInt("count", 0);
  
  for (int i = 0; i < commandCount && i < 10; i++) {
    String prefix = "cmd" + String(i) + "_";
    String nameKey = prefix + String("name");
    String rawKey = prefix + String("raw");
    String lenKey = prefix + String("len");
    String defaultName = "CMD_" + String(i);
    
    commands[i].name = preferences.getString(nameKey.c_str(), defaultName);
    commands[i].isRaw = preferences.getBool(rawKey.c_str(), true);
    commands[i].rawLen = preferences.getUShort(lenKey.c_str(), 0);
    
    // Load raw data in chunks
    for (int j = 0; j < commands[i].rawLen && j < 200; j += 10) {
      String key = prefix + String("data") + String(j/10);
      size_t chunkSize = min(10, commands[i].rawLen - j);
      preferences.getBytes(key.c_str(), &commands[i].rawData[j], chunkSize * sizeof(uint16_t));
    }
  }
  
  Serial.println(String("Loaded ") + String(commandCount) + String(" stored commands."));
}

// HTML Generation Functions
String getMainHTML() {
  String html = R"HTMLDELIM(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Room Controller - OrcaTech</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            padding: 30px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
        }
        
        .header {
            text-align: center;
            margin-bottom: 40px;
        }
        
        .header h1 {
            color: #333;
            font-size: 2.5em;
            margin-bottom: 10px;
        }
        
        .header p {
            color: #666;
            font-size: 1.1em;
        }
        
        .status-bar {
            background: #f8f9fa;
            border-radius: 10px;
            padding: 15px;
            margin-bottom: 30px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
        }
        
        .status-item {
            display: flex;
            align-items: center;
            margin: 5px;
        }
        
        .status-dot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
        }
        
        .status-dot.connected {
            background: #28a745;
        }
        
        .status-dot.disconnected {
            background: #dc3545;
        }
        
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 30px;
        }
        
        .card {
            background: white;
            border-radius: 15px;
            padding: 25px;
            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.1);
            transition: transform 0.3s ease;
        }
        
        .card:hover {
            transform: translateY(-5px);
        }
        
        .card h3 {
            color: #333;
            margin-bottom: 20px;
            font-size: 1.4em;
            display: flex;
            align-items: center;
        }
        
        .card-icon {
            width: 24px;
            height: 24px;
            margin-right: 10px;
            fill: #667eea;
        }
        
        .control-group {
            margin-bottom: 20px;
        }
        
        .control-group label {
            display: block;
            margin-bottom: 8px;
            font-weight: 600;
            color: #555;
        }
        
        .switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 34px;
            margin-right: 10px;
        }
        
        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            transition: .4s;
            border-radius: 34px;
        }
        
        .slider:before {
            position: absolute;
            content: "";
            height: 26px;
            width: 26px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }
        
        input:checked + .slider {
            background-color: #667eea;
        }
        
        input:checked + .slider:before {
            transform: translateX(26px);
        }
        
        .rgb-controls {
            margin-top: 15px;
        }
        
        .color-slider {
            width: 100%;
            height: 8px;
            border-radius: 4px;
            outline: none;
            margin: 10px 0;
            cursor: pointer;
        }
        
        .color-slider::-webkit-slider-thumb {
            appearance: none;
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: white;
            border: 2px solid #667eea;
            cursor: pointer;
        }
        
        .color-preview {
            width: 100%;
            height: 60px;
            border-radius: 10px;
            border: 2px solid #ddd;
            margin: 15px 0;
            transition: all 0.3s ease;
        }
        
        .ir-buttons {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
            gap: 15px;
            margin-top: 15px;
        }
        
        .ir-button {
            background: linear-gradient(135deg, #667eea, #764ba2);
            color: white;
            border: none;
            border-radius: 10px;
            padding: 15px;
            cursor: pointer;
            font-size: 14px;
            font-weight: 600;
            transition: all 0.3s ease;
            position: relative;
            overflow: hidden;
        }
        
        .ir-button:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4);
        }
        
        .ir-button:active {
            transform: translateY(0);
        }
        
        .ir-button.learning {
            background: linear-gradient(135deg, #ff6b6b, #ee5a24);
            animation: pulse 1.5s infinite;
        }
        
        .ir-button.learned {
            background: linear-gradient(135deg, #00d2d3, #54a0ff);
        }
        
        @keyframes pulse {
            0% { transform: scale(1); }
            50% { transform: scale(1.05); }
            100% { transform: scale(1); }
        }
        
        .btn {
            background: linear-gradient(135deg, #667eea, #764ba2);
            color: white;
            border: none;
            border-radius: 8px;
            padding: 12px 24px;
            cursor: pointer;
            font-size: 14px;
            font-weight: 600;
            transition: all 0.3s ease;
            margin: 5px;
        }
        
        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4);
        }
        
        .btn-danger {
            background: linear-gradient(135deg, #ff6b6b, #ee5a24);
        }
        
        .btn-danger:hover {
            box-shadow: 0 5px 15px rgba(255, 107, 107, 0.4);
        }
        
        .wifi-config {
            text-align: center;
            margin-top: 20px;
        }
        
        @media (max-width: 768px) {
            .container {
                padding: 20px;
            }
            
            .header h1 {
                font-size: 2em;
            }
            
            .status-bar {
                flex-direction: column;
                align-items: flex-start;
            }
            
            .grid {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🏠 Room Controller</h1>
            <p>OrcaTech IoT Device Management</p>
        </div>
        
        <div class="status-bar">
            <div class="status-item">
                <div class="status-dot" id="wifiStatus"></div>
                <span id="wifiInfo">Loading...</span>
            </div>
            <div class="status-item">
                <div class="status-dot" id="irStatus"></div>
                <span id="irInfo">IR Ready</span>
            </div>
        </div>
        
        <div class="grid">
            <!-- Lights Control -->
            <div class="card">
                <h3>
                    <svg class="card-icon" viewBox="0 0 24 24">
                        <path d="M9,21C9,21.5 9.4,22 10,22H14C14.6,22 15,21.5 15,21V20H9V21M12,2A7,7 0 0,0 5,9C5,11.38 6.19,13.47 8,14.74V17A1,1 0 0,0 9,18H15A1,1 0 0,0 16,17V14.74C17.81,13.47 19,11.38 19,9A7,7 0 0,0 12,2Z"/>
                    </svg>
                    Lights
                </h3>
                <div class="control-group">
                    <label>Main Light</label>
                    <label class="switch">
                        <input type="checkbox" id="light1" onchange="toggleLight(1, this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span id="light1Status">OFF</span>
                </div>
                <div class="control-group">
                    <label>Bedside Light</label>
                    <label class="switch">
                        <input type="checkbox" id="light2" onchange="toggleLight(2, this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span id="light2Status">OFF</span>
                </div>
                <div class="control-group">
                    <label>Bathroom Light</label>
                    <label class="switch">
                        <input type="checkbox" id="light3" onchange="toggleLight(3, this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span id="light3Status">OFF</span>
                </div>
                <div class="control-group">
                    <label>Outdoor Light</label>
                    <label class="switch">
                        <input type="checkbox" id="device1" onchange="toggleDevice(1, this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span id="device1Status">OFF</span>
                </div>
                <div class="control-group">
                    <label>Bathtub Light</label>
                    <label class="switch">
                        <input type="checkbox" id="device2" onchange="toggleDevice(2, this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span id="device2Status">OFF</span>
                </div>
            </div>
            
            <!-- Other Control -->
            <div class="card">
                <h3>
                    <svg class="card-icon" viewBox="0 0 24 24">
                        <path d="M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4A8,8 0 0,1 20,12A8,8 0 0,1 12,20A8,8 0 0,1 4,12A8,8 0 0,1 12,4M12,6A6,6 0 0,0 6,12A6,6 0 0,0 12,18A6,6 0 0,0 18,12A6,6 0 0,0 12,6M12,8A4,4 0 0,1 16,12A4,4 0 0,1 12,16A4,4 0 0,1 8,12A4,4 0 0,1 12,8Z"/>
                    </svg>
                    Other
                </h3>
                <div class="control-group">
                    <label>Onboard LED</label>
                    <label class="switch">
                        <input type="checkbox" id="onboardLed" onchange="toggleOnboardLed(this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span id="onboardLedStatus">OFF</span>
                </div>
            </div>
            
            <!-- RGB LED Control -->
            <div class="card">
                <h3>
                    <svg class="card-icon" viewBox="0 0 24 24">
                        <path d="M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4A8,8 0 0,1 20,12A8,8 0 0,1 12,20A8,8 0 0,1 4,12A8,8 0 0,1 12,4M12,6A6,6 0 0,0 6,12A6,6 0 0,0 12,18A6,6 0 0,0 18,12A6,6 0 0,0 12,6M12,8A4,4 0 0,1 16,12A4,4 0 0,1 12,16A4,4 0 0,1 8,12A4,4 0 0,1 12,8Z"/>
                    </svg>
                    RGB LED Control
                </h3>
                <div class="color-preview" id="colorPreview"></div>
                <div class="rgb-controls">
                    <div style="margin-bottom: 15px; text-align: center;">
                        <button class="btn" onclick="turnOnRGB()" style="margin-right: 10px; background-color: #28a745; color: white;">ON (White)</button>
                        <button class="btn" onclick="turnOffRGB()" style="background-color: #dc3545; color: white;">OFF</button>
                    </div>
                    <label>Color Mix: <span id="colorValue">0</span></label>
                    <input type="range" class="color-slider" id="colorSlider" min="0" max="255" value="0" 
                           style="background: linear-gradient(to right, #ffffff, #ff0000, #ffff00, #00ff00, #00ffff, #0000ff, #ff00ff, #ff0000)" 
                           oninput="updateRGB()">
                    <br><br>
                    <label>Brightness: <span id="brightnessValue">100</span>%</label>
                    <input type="range" class="color-slider" id="brightnessSlider" min="0" max="100" value="100" 
                           style="background: linear-gradient(to right, #000000, #ffffff)" 
                           oninput="updateRGB()">
                </div>
            </div>
            
            <!-- IR Remote Control -->
            <div class="card">
                <h3>
                    <svg class="card-icon" viewBox="0 0 24 24">
                        <path d="M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4A8,8 0 0,1 20,12A8,8 0 0,1 12,20A8,8 0 0,1 4,12A8,8 0 0,1 12,4M12,6A6,6 0 0,0 6,12A6,6 0 0,0 12,18A6,6 0 0,0 18,12A6,6 0 0,0 12,6M12,8A4,4 0 0,1 16,12A4,4 0 0,1 12,16A4,4 0 0,1 8,12A4,4 0 0,1 12,8Z"/>
                    </svg>
                    IR Remote Control
                </h3>
                <div class="ir-buttons">
                    <button class="ir-button" id="irBtn0" onclick="handleIRButton(0)">Button 1<br><small>Empty</small></button>
                    <button class="ir-button" id="irBtn1" onclick="handleIRButton(1)">Button 2<br><small>Empty</small></button>
                    <button class="ir-button" id="irBtn2" onclick="handleIRButton(2)">Button 3<br><small>Empty</small></button>
                    <button class="ir-button" id="irBtn3" onclick="handleIRButton(3)">Button 4<br><small>Empty</small></button>
                    <button class="ir-button" id="irBtn4" onclick="handleIRButton(4)">Button 5<br><small>Empty</small></button>
                </div>
                <div style="margin-top: 20px; text-align: center;">
                    <button class="btn btn-danger" onclick="clearAllIR()">Clear All</button>
                    <button class="btn" onclick="stopLearning()">Stop Learning</button>
                </div>
            </div>
        </div>
        
        <div class="wifi-config">
            <button class="btn" id="wifiConfigBtn">📶 WiFi Configuration</button>
            <button class="btn" id="mqttConfigBtn">📡 MQTT Configuration</button>
        </div>
    </div>
    
    <script>
        let currentLearningSlot = -1;
        let irCommands = [];
        
        // Initialize the interface
        document.addEventListener('DOMContentLoaded', function() {
            updateStatus();
            updateIRButtons();
            setInterval(updateStatus, 5000); // Update every 5 seconds
            
            // Add WiFi Configuration button event listener
            const wifiBtn = document.getElementById('wifiConfigBtn');
            if (wifiBtn) {
                wifiBtn.addEventListener('click', function() {
                    console.log('WiFi Configuration button clicked');
                    window.location.href = '/wifi';
                });
                console.log('WiFi Configuration button event listener added');
            } else {
                console.error('WiFi Configuration button not found');
            }

            const mqttBtn = document.getElementById('mqttConfigBtn');
            if (mqttBtn) {
                mqttBtn.addEventListener('click', function() {
                    console.log('MQTT Configuration button clicked');
                    window.location.href = '/mqtt';
                });
                console.log('MQTT Configuration button event listener added');
            } else {
                console.error('MQTT Configuration button not found');
            }
        });
        
        async function updateStatus() {
            try {
                const response = await fetch('/api/status');
                const data = await response.json();
                
                // Update WiFi status
                const wifiStatus = document.getElementById('wifiStatus');
                const wifiInfo = document.getElementById('wifiInfo');
                
                if (data.wifi.connected) {
                    wifiStatus.className = 'status-dot connected';
                    wifiInfo.textContent = 'Connected to ' + data.wifi.ssid + ' (' + data.wifi.ip + ')';
                } else {
                    wifiStatus.className = 'status-dot disconnected';
                    wifiInfo.textContent = 'AP Mode: ' + data.wifi.ssid + ' (' + data.wifi.ip + ')';
                }
                
                // Update device states
                for (let i = 0; i < 3; i++) {
                    const checkbox = document.getElementById('light' + (i+1));
        const status = document.getElementById('light' + (i+1) + 'Status');
                    if (!checkbox || !status) continue;
                    checkbox.checked = data.lights[i];
                    status.textContent = data.lights[i] ? 'ON' : 'OFF';
                }
                
                for (let i = 0; i < 2; i++) {
                    const checkbox = document.getElementById('device' + (i+1));
        const status = document.getElementById('device' + (i+1) + 'Status');
                    if (!checkbox || !status) continue;
                    checkbox.checked = data.devices[i];
                    status.textContent = data.devices[i] ? 'ON' : 'OFF';
                }
                
                const onboardLed = document.getElementById('onboardLed');
                const onboardStatus = document.getElementById('onboardLedStatus');
                if (onboardLed && onboardStatus) {
                onboardLed.checked = data.onboard_led;
                onboardStatus.textContent = data.onboard_led ? 'ON' : 'OFF';
                }
                
                // Update RGB values - preserve current slider values, only set on first load
                // Note: Don't set colorSlider from RGB values as it creates incorrect mapping
                // Preserve current brightness value, only set default on first load
                if (!document.getElementById('brightnessSlider').value) {
                    document.getElementById('brightnessSlider').value = 100;
                }
                updateRGBDisplay();
                
                // Update IR status
                const irStatus = document.getElementById('irStatus');
                const irInfo = document.getElementById('irInfo');
                
                if (data.ir.learning) {
                    irStatus.className = 'status-dot disconnected';
                    irInfo.textContent = 'Learning Mode Active';
                } else {
                    irStatus.className = 'status-dot connected';
                    irInfo.textContent = 'IR Ready (' + data.ir.commands + '/' + data.ir.max_commands + ')';
                }
                
            } catch (error) {
                console.error('Error updating status:', error);
            }
        }
        
        async function updateIRButtons() {
            try {
                const response = await fetch('/api/ir/list');
                const data = await response.json();
                irCommands = data.commands;
                
                for (let i = 0; i < 5; i++) {
                    const button = document.getElementById('irBtn' + i);
                    const command = irCommands.find(cmd => cmd.slot === i);
                    
                    if (command) {
                        button.className = 'ir-button learned';
                        button.innerHTML = 'Button ' + (i+1) + '<br><small>' + command.name + '</small>';
                    } else {
                        button.className = 'ir-button';
                        button.innerHTML = 'Button ' + (i+1) + '<br><small>Empty</small>';
                    }
                }
            } catch (error) {
                console.error('Error updating IR buttons:', error);
            }
        }
        
        async function toggleLight(lightNum, state) {
            const checkbox = document.getElementById('light' + lightNum);
            if (checkbox) checkbox.disabled = true;
            try {
                const response = await fetch('/api/light', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: 'light=' + lightNum + '&state=' + state
                });
                
                await updateStatus();
            } catch (error) {
                console.error('Error toggling light:', error);
                await updateStatus();
            } finally {
                if (checkbox) checkbox.disabled = false;
            }
        }
        
        async function toggleDevice(deviceNum, state) {
            const checkbox = document.getElementById('device' + deviceNum);
            if (checkbox) checkbox.disabled = true;
            try {
                const response = await fetch('/api/device', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: 'device=' + deviceNum + '&state=' + state
                });
                
                await updateStatus();
            } catch (error) {
                console.error('Error toggling device:', error);
                await updateStatus();
            } finally {
                if (checkbox) checkbox.disabled = false;
            }
        }
        
        async function toggleOnboardLed(state) {
            const checkbox = document.getElementById('onboardLed');
            if (checkbox) checkbox.disabled = true;
            try {
                const response = await fetch('/api/led', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: 'state=' + state
                });
                
                await updateStatus();
            } catch (error) {
                console.error('Error toggling onboard LED:', error);
                await updateStatus();
            } finally {
                if (checkbox) checkbox.disabled = false;
            }
        }
        
        async function updateRGB() {
            updateRGBDisplay();
            
            const colorValue = document.getElementById('colorSlider').value;
            const brightnessValue = document.getElementById('brightnessSlider').value;
            const rgb = calculateRGBFromSlider(colorValue, brightnessValue);
            
            try {
                const response = await fetch('/api/rgb', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: 'r=' + rgb.r + '&g=' + rgb.g + '&b=' + rgb.b
                });
            } catch (error) {
                console.error('Error updating RGB:', error);
            }
        }
        
        function calculateRGBFromSlider(value, brightness = 100) {
             // Convert brightness from 0-100 to 0-1
             const brightnessFactor = brightness / 100;
             
             // Special case: position 0 = white color
             if (value === 0) {
                 const whiteValue = Math.round(255 * brightnessFactor);
                 return {
                     r: whiteValue,
                     g: whiteValue,
                     b: whiteValue
                 };
             }
             
             // Convert slider value (1-255) to RGB color wheel
             const hue = ((value - 1) / 254) * 360; // Convert to hue (0-360 degrees)
             const saturation = 1; // Full saturation
             const lightness = 0.5; // Medium lightness
             
             // Convert HSL to RGB
             const c = (1 - Math.abs(2 * lightness - 1)) * saturation;
             const x = c * (1 - Math.abs((hue / 60) % 2 - 1));
             const m = lightness - c / 2;
             
             let r, g, b;
             
             if (hue >= 0 && hue < 60) {
                 r = c; g = x; b = 0;
             } else if (hue >= 60 && hue < 120) {
                 r = x; g = c; b = 0;
             } else if (hue >= 120 && hue < 180) {
                 r = 0; g = c; b = x;
             } else if (hue >= 180 && hue < 240) {
                 r = 0; g = x; b = c;
             } else if (hue >= 240 && hue < 300) {
                 r = x; g = 0; b = c;
             } else {
                 r = c; g = 0; b = x;
             }
             
             return {
                 r: Math.round((r + m) * 255 * brightnessFactor),
                 g: Math.round((g + m) * 255 * brightnessFactor),
                 b: Math.round((b + m) * 255 * brightnessFactor)
             };
         }
        
        function updateRGBDisplay() {
            const colorValue = document.getElementById('colorSlider').value;
            const brightnessValue = document.getElementById('brightnessSlider').value;
            const rgb = calculateRGBFromSlider(colorValue, brightnessValue);
            
            document.getElementById('colorValue').textContent = colorValue;
            document.getElementById('brightnessValue').textContent = brightnessValue;
            
            const colorPreview = document.getElementById('colorPreview');
            colorPreview.style.backgroundColor = 'rgb(' + rgb.r + ', ' + rgb.g + ', ' + rgb.b + ')';
        }
        
        async function turnOnRGB() {
            // Set to white color (slider position 0) with full brightness
            document.getElementById('colorSlider').value = 0;
            document.getElementById('brightnessSlider').value = 100;
            
            // Update display and send to device
            updateRGBDisplay();
            
            try {
                const response = await fetch('/api/rgb', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: 'r=255&g=255&b=255'
                });
            } catch (error) {
                console.error('Error turning on RGB:', error);
            }
        }
        
        async function turnOffRGB() {
            // Turn off RGB LED
            try {
                const response = await fetch('/api/rgb', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: 'r=0&g=0&b=0'
                });
                
                // Update the color preview to show black
                const colorPreview = document.getElementById('colorPreview');
                colorPreview.style.backgroundColor = 'rgb(0, 0, 0)';
            } catch (error) {
                console.error('Error turning off RGB:', error);
            }
        }
        
        async function handleIRButton(slot) {
            const button = document.getElementById('irBtn' + slot);
            const command = irCommands.find(cmd => cmd.slot === slot);
            
            if (command) {
                // Send the command
                try {
                    const response = await fetch('/api/ir/send', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'slot=' + slot
                    });
                    
                    if (response.ok) {
                        button.style.transform = 'scale(0.95)';
                        setTimeout(() => {
                            button.style.transform = 'scale(1)';
                        }, 150);
                    }
                } catch (error) {
                    console.error('Error sending IR command:', error);
                }
            } else {
                // Learn new command
                currentLearningSlot = slot;
                button.className = 'ir-button learning';
                button.innerHTML = 'Button ' + (slot+1) + '<br><small>Learning...</small>';
                
                try {
                    const response = await fetch('/api/ir/learn', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'slot=' + slot
                    });
                    
                    if (response.ok) {
                        // Wait for learning to complete
                        setTimeout(() => {
                            updateIRButtons();
                        }, 5000);
                    }
                } catch (error) {
                    console.error('Error starting IR learning:', error);
                    button.className = 'ir-button';
                    button.innerHTML = 'Button ' + (slot+1) + '<br><small>Empty</small>';
                }
            }
        }
        
        async function stopLearning() {
            try {
                await fetch('/api/ir/stop', {method: 'POST'});
                updateIRButtons();
            } catch (error) {
                console.error('Error stopping learning:', error);
            }
        }
        
        async function clearAllIR() {
            if (confirm('Are you sure you want to clear all IR commands?')) {
                try {
                    await fetch('/api/ir/clear', {method: 'POST'});
                    updateIRButtons();
                } catch (error) {
                    console.error('Error clearing IR commands:', error);
                }
            }
        }
    </script>
</body>
</html>
)HTMLDELIM";
  return html;
}

String getWiFiConfigHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>WiFi Configuration</title>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
  html += ".container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
  html += "input,button{width:100%;padding:10px;margin:5px 0;border:1px solid #ddd;border-radius:5px;}";
  html += "button{background:#007bff;color:white;cursor:pointer;}";
  html += "</style></head><body>";
  html += "<div class='container'><h2>WiFi Configuration</h2>";
  html += "<button onclick='scanNetworks()'>Scan Networks</button>";
  html += "<div id='networkList'></div>";
  html += "<form method='POST' action='/wifi'>";
  html += "<input type='text' name='ssid' placeholder='WiFi Name' required>";
  html += "<input type='password' name='password' placeholder='Password'>";
  html += "<button type='submit'>Connect</button></form>";
  html += "<script>";
  html += "async function scanNetworks(){";
  html += "document.getElementById('networkList').innerHTML='Scanning...';";
  html += "try{";
  html += "const response=await fetch('/scan');";
  html += "const networks=await response.json();";
  html += "let html='';";
  html += "networks.forEach(n=>{";
  html += "html+='<div onclick=\\\"selectNetwork(\\\''+n.ssid+'\\\')\\\" style=\\\"padding:10px;border:1px solid #ddd;margin:5px 0;cursor:pointer;\\\">'+n.ssid+' ('+n.rssi+'dBm)</div>';";
  html += "});";
  html += "document.getElementById('networkList').innerHTML=html;";
  html += "}catch(e){document.getElementById('networkList').innerHTML='Error scanning';}";
  html += "}";
  html += "function selectNetwork(ssid){";
  html += "document.querySelector('input[name=\\\"ssid\\\"]').value=ssid;";
  html += "}";
  html += "</script></div></body></html>";
  return html;
}

String getMQTTConfigHTML() {
  String currentHost = preferences.getString("mqtt_host", mqttHost);
  uint16_t currentPort = preferences.getUShort("mqtt_port", mqttPort);
  String currentUser = preferences.getString("mqtt_user", mqttUser);
  String currentUnit = preferences.getString("unit_name", unitName);
  bool currentActiveLow = preferences.getBool("relay_active_low", relayActiveLow);

  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>MQTT Configuration</title>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
  html += ".container{max-width:460px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
  html += "input,button{width:100%;padding:10px;margin:5px 0;border:1px solid #ddd;border-radius:5px;}";
  html += "button{background:#007bff;color:white;cursor:pointer;}";
  html += ".row{margin:10px 0;}";
  html += ".hint{color:#666;font-size:12px;line-height:1.4;}";
  html += ".small{font-size:12px;color:#444;word-break:break-all;}";
  html += "label{display:block;font-weight:600;margin-top:10px;}";
  html += "</style></head><body>";
  html += "<div class='container'><h2>MQTT Configuration</h2>";
  html += "<div class='hint'>Set broker host/port for MQTT control and realtime status. Leave Host empty to disable MQTT.</div>";
  html += "<div class='hint' style='margin-top:6px;'>Naming for your 5 units: use <b>birdnest_1</b> .. <b>birdnest_5</b>. This name is used for OTA hostname and appears in MQTT status/info.</div>";
  html += "<div class='row small'><b>Client ID:</b> " + mqttClientId + "</div>";
  html += "<div class='row small'><b>Base Topic:</b> " + mqttBaseTopic + "</div>";
  html += "<form method='POST' action='/mqtt'>";
  html += "<label>Unit Name</label>";
  html += "<input type='text' name='unit_name' placeholder='birdnest_1' value='" + currentUnit + "'>";
  html += "<label>Broker Host</label>";
  html += "<input type='text' name='host' placeholder='e.g. 192.168.1.10' value='" + currentHost + "'>";
  html += "<label>Broker Port</label>";
  html += "<input type='number' name='port' placeholder='1883' min='1' max='65535' value='" + String(currentPort) + "'>";
  html += "<label>Username</label>";
  html += "<input type='text' name='user' placeholder='(optional)' value='" + currentUser + "'>";
  html += "<label>Password</label>";
  html += "<input type='password' name='pass' placeholder='Leave blank to keep current'>";
  html += "<div class='row' style='display:flex;align-items:center;gap:10px;'>";
  html += "<input type='checkbox' id='clear_password' name='clear_password' style='width:auto;margin:0;'>";
  html += "<label for='clear_password' style='margin:0;font-weight:400;'>Clear saved password</label>";
  html += "</div>";
  html += "<label>OTA Password</label>";
  html += "<input type='password' name='ota_pass' placeholder='(recommended) Leave blank to keep current'>";
  html += "<div class='row' style='display:flex;align-items:center;gap:10px;'>";
  html += "<input type='checkbox' id='clear_ota_password' name='clear_ota_password' style='width:auto;margin:0;'>";
  html += "<label for='clear_ota_password' style='margin:0;font-weight:400;'>Clear saved OTA password</label>";
  html += "</div>";
  html += "<div class='row' style='display:flex;align-items:center;gap:10px;margin-top:10px;'>";
  html += String("<input type='checkbox' id='relay_active_low' name='relay_active_low' style='width:auto;margin:0;' ") + (currentActiveLow ? "checked" : "") + ">";
  html += "<label for='relay_active_low' style='margin:0;font-weight:400;'>Relay Active LOW</label>";
  html += "</div>";
  html += "<button type='submit'>Save & Restart</button>";
  html += "<button type='button' onclick='window.location.href=\"/\"' style='background:#6c757d;'>Back</button>";
  html += "</form>";
  html += "</div></body></html>";
  return html;
}

uint8_t relayWriteLevel(bool on) {
  return relayActiveLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW);
}

bool relayStateFromPin(int pin) {
  int v = digitalRead(pin);
  return relayActiveLow ? (v == LOW) : (v == HIGH);
}

void setupOTA() {
  otaReady = false;
  if (isAPMode) return;
  if (WiFi.status() != WL_CONNECTED) return;

  String hostname = unitName.length() > 0 ? unitName : mqttClientId;
  ArduinoOTA.setHostname(hostname.c_str());
  if (otaPassword.length() > 0) ArduinoOTA.setPassword(otaPassword.c_str());

  ArduinoOTA.onStart([]() {
    Serial.println("OTA update start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA update end");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned long lastLogMs = 0;
    unsigned long now = millis();
    if (now - lastLogMs < 1000) return;
    lastLogMs = now;
    uint32_t pct = total == 0 ? 0 : (progress * 100U) / total;
    Serial.println(String("OTA progress: ") + String(pct) + "%");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.println(String("OTA error: ") + String((int)error));
  });

  ArduinoOTA.begin();
  otaReady = true;
  Serial.println(String("OTA ready. Hostname: ") + hostname);
}

void otaLoop() {
  if (!otaReady) return;
  ArduinoOTA.handle();
}
