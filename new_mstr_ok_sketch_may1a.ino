#include <WiFi.h>           // For WiFi connection
#include <WiFiUdp.h>        // For UDP communication
#include <esp_now.h>        // For ESP-NOW communication
#include <WiFiManager.h>    // For easy WiFi setup
#include <Wire.h>           // For I2C (OLED, Mega)
#include <Adafruit_GFX.h>   // For OLED
#include <Adafruit_SH110X.h> // For OLED Driver SH1106/SH1107
#include <queue>            // For buffering incoming messages
// --- NEW INCLUDES for MQTT ---
#include <WiFiClient.h>     // Base client for MQTT
#include <PubSubClient.h>   // For MQTT communication
#include <ArduinoJson.h>    // For parsing JSON from MQTT
// --- END NEW INCLUDES ---

// --- Pin Definitions ---
#define BUZZER_PIN 25 // Buzzer pin (Adjust if different)

// --- OLED Display Config ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define OLED_I2C_ADDRESS 0x3C // Default I2C address for SH1106/SSD1306

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Communication Config ---
#define UDP_PORT 12345          // Port to listen for UDP packets
#define MEGA2560_I2C_ADDRESS 0x08 // I2C Address of the Arduino Mega slave
#define ACK_MESSAGE "ACK"       // Acknowledgement message content
#define I2C_TIMEOUT_MS 50       // Timeout for I2C communication

WiFiUDP udp;

// --- NEW MQTT Configuration ---
const char* mqtt_server = "YOUR_MQTT_BROKER_IP_OR_HOSTNAME"; // <<-- REPLACE
const int mqtt_port = 1883;                                  // Default MQTT port
const char* mqtt_topic_subscribe = "staferb/web_alerts"; //"firelinx/master/alerts";  // <<-- REPLACE with your topic
const char* mqtt_client_id = "FireLinxMasterB1";             // Unique client ID (change if needed)
// Add user/pass if your broker requires authentication
// const char* mqtt_user = "your_mqtt_username";
// const char* mqtt_password = "your_mqtt_password";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttReconnectAttempt = 0;
const long mqttReconnectInterval = 5000; // Try reconnecting every 5 seconds
// --- END NEW MQTT Configuration ---


// --- Data Structure (SINGLE STRUCTURE USED FOR ALL INPUTS) ---
typedef struct struct_message {
    char fireType;        // A, B, C, D or 'A' for Auto/MQTT Unknown
    char fireIntensity;   // 1-4
    bool verified;        // True/False
    char user[32];        // Name of user or source ("Auto Mode", "MQTT")
    char userID[10];      // User ID or source ID ("AUTO", "MQTT")
    char stnID[10];       // Station ID (e.g., "D/4", "W/D" from MQTT)
    char latitude[16];    // DDM format string
    char longitude[16];   // DDM format string
    char date[10];        // DD/MM string (or other format from MQTT)
    char time[10];        // HH:MM string (or other format from MQTT)
} struct_message;

// --- Message Queue ---
std::queue<struct_message> fireAlertQueue;
portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED; // Mutex for thread-safe queue access

// --- State Variables ---
unsigned long lastStatusUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 10000; // Update status display every 10 seconds

// --- Function Prototypes ---
// Initialization
void initWiFi();
void initUDP();
void initESP_NOW();
bool checkMegaConnection();
// NEW MQTT Init prototypes
void initMQTT();
bool reconnectMQTT();

// Communication Callbacks & Processing
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len); // ESP-NOW Callback
void processUDPPackets();
void processQueue();
// NEW MQTT Callback & Parsers prototypes
void mqttCallback(char* topic, byte* payload, unsigned int length); // MQTT Callback
void parseAndEnqueueMqttMessage(String jsonInput); // JSON Parser
void parsePayloadAndEnqueue(const char* payloadData); // Payload CSV Parser


// Alert Handling
bool validateAlert(const struct_message& data); // Corrected version below
void handleAlert(const struct_message& alertData);
void logAlert(const struct_message& alertData);
void displayAlert(const struct_message& alertData);
void triggerBuzzer();
void sendToMega2560(const struct_message& alertData);
float convertCoordinate(const char* ddm); // Helper for Mega I2C

// Display & Utility
void displaySystemStatus(); // Updated version below to show MQTT status
void displayError(const char* errorMsg, bool halt = true);


// ======================= SETUP =======================
void setup() {
    Serial.begin(115200);
    Wire.begin(); // Initialize I2C

    Serial.println("\n\n--- FireLinx Master Node Booting ---");

    // Initialize OLED Display
    if (!display.begin(OLED_I2C_ADDRESS, true)) { // Address, init i2c bus
        Serial.println(F("!! OLED Init Failed !!"));
        while(1) delay(1000);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("FireLinx Master B/1"); // Adjust Master ID if needed
    display.println("Initializing...");
    display.display();
    Serial.println("OLED Initialized.");

    // Initialize Buzzer
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("Buzzer Initialized.");

    // Initialize WiFi (Does not halt on failure)
    initWiFi(); // Uses WiFiManager

    // Initialize UDP Listening (Requires WiFi)
    initUDP();

    // Initialize ESP-NOW Receiving (Works even if WiFi fails)
    initESP_NOW();

    // Initialize MQTT Client (Requires WiFi)
    initMQTT(); // Sets up server and callback

    // Check connection to Arduino Mega via I2C
    if (!checkMegaConnection()) {
         Serial.println("Warning: Mega2560 not detected on I2C!");
         display.setCursor(0, 40);
         display.println("WARN: MEGA N/A");
         display.display();
         delay(2000);
    } else {
         Serial.println("Mega2560 Detected on I2C.");
    }

    Serial.println("--- Master System Ready ---");
    displaySystemStatus(); // Show initial status
}
// ======================= END SETUP =======================


// ======================= MAIN LOOP =======================
void loop() {
    // 1. Maintain MQTT Connection & Process Incoming MQTT Messages
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqttClient.connected()) {
            // Non-blocking reconnect attempt
            unsigned long now = millis();
            if (now - lastMqttReconnectAttempt > mqttReconnectInterval) {
                lastMqttReconnectAttempt = now;
                if (reconnectMQTT()) {
                    lastMqttReconnectAttempt = 0; // Reset timer after successful connect
                }
            }
        } else {
            // If connected, process MQTT messages
            mqttClient.loop(); // MUST be called regularly
        }
    }

    // 2. Check for incoming UDP packets and enqueue them
    processUDPPackets();

    // 3. Process one message from the queue (if any) - handles MQTT, UDP, ESPNOW
    processQueue();

    // 4. Periodically update the status display on OLED
    if (millis() - lastStatusUpdate > STATUS_UPDATE_INTERVAL) {
        lastStatusUpdate = millis();
        displaySystemStatus();
    }

    // Small delay to prevent high CPU usage if loops are very fast
    delay(10);
}
// ======================= END MAIN LOOP =======================


// ======================= INITIALIZATION FUNCTIONS =======================

void initWiFi() {
    WiFi.mode(WIFI_STA); // Set ESP32 to Station mode

    WiFiManager wifiManager;
    // Set timeout for config portal (e.g., 180 seconds = 3 minutes)
    // You can adjust this value. Shorter means faster fallback if portal unused.
    wifiManager.setConfigPortalTimeout(180);

    display.setCursor(0, 20);
    display.println("Connecting WiFi...");
    display.println("(AP: FireLinx-Master)"); // Show AP name for WiFiManager
    display.display();
    Serial.println("Starting WiFiManager...");

    // Blocking until connected or timeout
    // **MODIFIED** from original code to prevent halting on failure
    if (!wifiManager.autoConnect("FireLinx-Master")) {
        Serial.println("WiFi Connection Failed via WiFiManager!");
        // *** CHANGED HERE: Don't halt, just warn ***
        displayError("WiFi Failed!", false); // false = DO NOT HALT
        // *** END CHANGE ***
    } else {
        // WiFi is connected (original code block)
        Serial.println("\nWiFi Connected!");
        Serial.print("SSID: "); Serial.println(WiFi.SSID());
        Serial.print("IP Address: "); Serial.println(WiFi.localIP());
        Serial.print("MAC Address: "); Serial.println(WiFi.macAddress());
        Serial.print("Channel: "); Serial.println(WiFi.channel());

        display.setCursor(0, 20); // Update display
        display.clearDisplay();
        display.setCursor(0,0);
        display.println("FireLinx Master B/1");
        display.println("WiFi Connected!");
        display.println(WiFi.SSID());
        display.println(WiFi.localIP());
        display.display();
        delay(1000);
    }
}

// initUDP remains the same as the original code
void initUDP() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Starting UDP listener on port %d\n", UDP_PORT);
        if (udp.begin(UDP_PORT) == 1) { // 1 indicates success
            Serial.println("UDP Listening Started.");
        } else {
            Serial.println("!! UDP Initialization Failed !!");
            displayError("UDP Init Fail", false); // Warn but don't halt necessarily
        }
    } else {
        Serial.println("Skipping UDP Init (WiFi not connected).");
    }
}

// initESP_NOW remains the same as the original code
void initESP_NOW() {
     if (WiFi.status() == WL_CONNECTED) {
         // Set channel reliably based on WiFi connection
         esp_err_t channel_set_result = esp_wifi_set_channel(WiFi.channel(), WIFI_SECOND_CHAN_NONE);
         Serial.printf("Setting ESP-NOW channel to %d... %s\n", WiFi.channel(), (channel_set_result == ESP_OK) ? "OK" : "Failed");
     } else {
         Serial.println("Warning: WiFi not connected. ESP-NOW using default channel.");
         esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // Use a default channel (e.g., 1)
     }

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("!! ESP-NOW Initialization Failed !!");
        displayError("ESPNOW Init Fail", true); // Halt on critical failure
        return;
    } else {
         Serial.println("ESP-NOW Initialized.");
    }

    // Register the receive callback function
    if (esp_now_register_recv_cb(OnDataRecv) != ESP_OK) {
         Serial.println("!! Failed to register ESP-NOW Receive Callback !!");
         displayError("ESPNOW CB Fail", true);
    } else {
         Serial.println("ESP-NOW Receive Callback Registered.");
    }
}

// NEW: Initialize MQTT client settings
void initMQTT() {
    if (WiFi.status() == WL_CONNECTED) {
        mqttClient.setServer(mqtt_server, mqtt_port);
        mqttClient.setCallback(mqttCallback);
        Serial.println("MQTT Client Configured.");
        // Initial connection attempt will happen in the first loop iteration
    } else {
        Serial.println("Skipping MQTT Init (WiFi not connected).");
    }
}

// NEW: Attempt to reconnect to MQTT broker
bool reconnectMQTT() {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // Add user/pass here if required: mqttClient.connect(mqtt_client_id, mqtt_user, mqtt_password)
    if (mqttClient.connect(mqtt_client_id)) {
        Serial.println("connected");
        // Subscribe to the topic upon connection/reconnection
        if (mqttClient.subscribe(mqtt_topic_subscribe)) {
            Serial.print("Successfully Subscribed to: "); Serial.println(mqtt_topic_subscribe);
            return true; // Connection and subscription successful
        } else {
            Serial.print("ERROR: Failed to subscribe to: "); Serial.println(mqtt_topic_subscribe);
            return false; // Connected but failed to subscribe
        }
    } else {
        Serial.print("failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" try again later");
        return false; // Connection failed
    }
}

// checkMegaConnection remains the same as the original code
bool checkMegaConnection() {
    Wire.beginTransmission(MEGA2560_I2C_ADDRESS);
    byte error = Wire.endTransmission();
    if (error == 0) {
        return true; // Device acknowledged
    } else {
        // Serial.printf("I2C Check Error for Mega (Addr 0x%02X): %d\n", MEGA2560_I2C_ADDRESS, error);
        // Minimize serial flooding on repeated checks
        return false;
    }
}

// ======================= COMMUNICATION CALLBACKS & QUEUE PROCESSING =======================

// OnDataRecv (ESP-NOW Callback) remains the same as the original code
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
    if (len == sizeof(struct_message)) {
        struct_message receivedData;
        memcpy(&receivedData, incomingData, sizeof(receivedData));
        Serial.printf("ESP-NOW Data Received from: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      info->src_addr[0], info->src_addr[1], info->src_addr[2], info->src_addr[3], info->src_addr[4], info->src_addr[5]);
        Serial.printf("  -> StnID: %s, Type: %c, Intensity: %c\n", receivedData.stnID, receivedData.fireType, receivedData.fireIntensity);

        esp_now_send(info->src_addr, (uint8_t *)ACK_MESSAGE, strlen(ACK_MESSAGE)); // Attempt ACK

        portENTER_CRITICAL(&queueMux);
        fireAlertQueue.push(receivedData);
        portEXIT_CRITICAL(&queueMux);
        Serial.println("   Added to queue (ESP-NOW).");
    } else {
        Serial.printf("ESP-NOW Received data of incorrect length (%d bytes, expected %d). Ignoring.\n", len, sizeof(struct_message));
    }
}

// NEW: MQTT Message Receive Callback Function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.print("MQTT Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    String messageTemp;
    messageTemp.reserve(length);
    for (int i = 0; i < length; i++) {
        messageTemp += (char)payload[i];
    }
    Serial.println(messageTemp);

    // Parse the JSON content of the message
    parseAndEnqueueMqttMessage(messageTemp);
}


// processUDPPackets remains the same as the original code
void processUDPPackets() {
    if (WiFi.status() != WL_CONNECTED) return;

    int packetSize = udp.parsePacket();
    if (packetSize == sizeof(struct_message)) {
        struct_message receivedData;
        IPAddress remoteIp = udp.remoteIP();
        uint16_t remotePort = udp.remotePort();
        int len = udp.read((uint8_t *)&receivedData, sizeof(receivedData));

        Serial.printf("UDP Data Received from: %s:%d\n", remoteIp.toString().c_str(), remotePort);
        Serial.printf("  -> StnID: %s, Type: %c, Intensity: %c\n", receivedData.stnID, receivedData.fireType, receivedData.fireIntensity);

        udp.beginPacket(remoteIp, remotePort);
        udp.write((uint8_t *)ACK_MESSAGE, strlen(ACK_MESSAGE));
        udp.endPacket(); // Send ACK

        portENTER_CRITICAL(&queueMux);
        fireAlertQueue.push(receivedData);
        portEXIT_CRITICAL(&queueMux);
        Serial.println("   Added to queue (UDP).");

    } else if (packetSize > 0) {
        Serial.printf("UDP Received packet of incorrect length (%d bytes, expected %d). Flushing.\n", packetSize, sizeof(struct_message));
        udp.flush();
    }
}

// processQueue remains the same as the original code
void processQueue() {
    struct_message alertToProcess;
    bool dataAvailable = false;

    portENTER_CRITICAL(&queueMux);
    if (!fireAlertQueue.empty()) {
        alertToProcess = fireAlertQueue.front();
        fireAlertQueue.pop();
        dataAvailable = true;
    }
    portEXIT_CRITICAL(&queueMux);

    if (dataAvailable) {
        Serial.println("--- Processing Alert from Queue ---");
        if (validateAlert(alertToProcess)) { // Use corrected validateAlert
            handleAlert(alertToProcess);
        } else {
            Serial.println("!! Invalid Alert Data in Queue. Discarding. !!");
            logAlert(alertToProcess);
        }
    }
}

// NEW: Parse JSON received via MQTT
void parseAndEnqueueMqttMessage(String jsonInput) {
    StaticJsonDocument<384> doc; // Adjust size if needed
    DeserializationError error = deserializeJson(doc, jsonInput);

    if (error) {
        Serial.print(F("MQTT JSON deserializeJson() failed: ")); Serial.println(error.f_str());
        return;
    }
    if (!doc.is<JsonArray>() || doc.size() == 0) {
         Serial.println(F("MQTT JSON Error: Expected an array '[{...}]'")); return;
    }

    JsonObject alertObject = doc[0];
    const char* command = alertObject["command"];
    const char* payloadStr = alertObject["payload"];

    if (command && payloadStr && strcmp(command, "fire_alert") == 0) {
        Serial.println("Received fire_alert command via MQTT.");
        parsePayloadAndEnqueue(payloadStr);
    } else {
        Serial.println("Received MQTT message, but not a valid fire_alert command or payload missing.");
    }
}

// NEW: Parse the comma-separated payload string from MQTT
void parsePayloadAndEnqueue(const char* payloadData) {
    struct_message tempData = {0}; // Initialize struct
    char buffer[256]; // Modifiable buffer for strtok
    strncpy(buffer, payloadData, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token;
    int fieldIndex = 0;
    token = strtok(buffer, ",");

    while (token != NULL && fieldIndex < 10) { // Expect 10 fields
        // Trim whitespace
        while (isspace((unsigned char)*token)) token++;
        char* end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';

        switch (fieldIndex) {
            case 0: // Fire Type
                if (strlen(token) == 1 && ((token[0] >= 'A' && token[0] <= 'D') || token[0] == 'A')) tempData.fireType = token[0];
                else { tempData.fireType = '?'; Serial.println("MQTT Parse Warn: Invalid Type"); } break;
            case 1: // Intensity
                if (strlen(token) == 1 && token[0] >= '1' && token[0] <= '4') tempData.fireIntensity = token[0];
                else { tempData.fireIntensity = '?'; Serial.println("MQTT Parse Warn: Invalid Intensity"); } break;
            case 2: // Verified
                tempData.verified = (strcmp(token, "true") == 0); break;
            case 3: // User Name
                strncpy(tempData.user, token, sizeof(tempData.user) - 1); tempData.user[sizeof(tempData.user) - 1] = '\0'; break;
            case 4: // User ID
                strncpy(tempData.userID, token, sizeof(tempData.userID) - 1); tempData.userID[sizeof(tempData.userID) - 1] = '\0'; break;
            case 5: // Station ID
                strncpy(tempData.stnID, token, sizeof(tempData.stnID) - 1); tempData.stnID[sizeof(tempData.stnID) - 1] = '\0'; break;
            case 6: // Latitude
                strncpy(tempData.latitude, token, sizeof(tempData.latitude) - 1); tempData.latitude[sizeof(tempData.latitude) - 1] = '\0'; break;
            case 7: // Longitude
                strncpy(tempData.longitude, token, sizeof(tempData.longitude) - 1); tempData.longitude[sizeof(tempData.longitude) - 1] = '\0'; break;
            case 8: // Date
                strncpy(tempData.date, token, sizeof(tempData.date) - 1); tempData.date[sizeof(tempData.date) - 1] = '\0'; break;
            case 9: // Time
                strncpy(tempData.time, token, sizeof(tempData.time) - 1); tempData.time[sizeof(tempData.time) - 1] = '\0'; break;
        }
        fieldIndex++;
        token = strtok(NULL, ","); // Get next token
    }

    if (fieldIndex == 10) {
        Serial.println("Parsed 10 fields from MQTT payload.");
        portENTER_CRITICAL(&queueMux);
        fireAlertQueue.push(tempData); // Enqueue the SINGLE struct type
        portEXIT_CRITICAL(&queueMux);
        Serial.println("   Added to queue (MQTT).");
    } else {
        Serial.printf("ERROR: Incorrect fields parsed from MQTT payload (%d != 10).\n", fieldIndex);
    }
}


// ======================= ALERT HANDLING =======================

// CORRECTED: Validate the received data structure contents
bool validateAlert(const struct_message& data) {
    bool typeValid = (data.fireType >= 'A' && data.fireType <= 'D');
                     // Allow 'A' for auto mode originating from child nodes if needed:
                     // bool typeValid = (data.fireType >= 'A' && data.fireType <= 'D') || (strcmp(data.userID, "AUTO") == 0 && data.fireType == 'A');
                     // The stricter check (A-D only) is used for now.
    bool intensityValid = (data.fireIntensity >= '1' && data.fireIntensity <= '4');
    bool stnIdValid = (strlen(data.stnID) > 0 && strlen(data.stnID) < sizeof(data.stnID));

    // Print specific validation failures using the ACTUAL data
    if (!typeValid) {
        Serial.printf("Validation Failed: Invalid Fire Type '%c'\n", data.fireType);
    }
    if (!intensityValid) {
        Serial.printf("Validation Failed: Invalid Intensity '%c'\n", data.fireIntensity);
    }
    if (!stnIdValid) {
        Serial.printf("Validation Failed: Invalid Station ID '%s'\n", data.stnID);
    }

    return typeValid && intensityValid && stnIdValid;
}


// handleAlert remains the same as the original code
void handleAlert(const struct_message& alertData) {
    Serial.println("Handling Valid Alert:");
    logAlert(alertData);     // Log details
    displayAlert(alertData); // Show on OLED
    triggerBuzzer();         // Activate buzzer
    sendToMega2560(alertData); // Forward to Mega
}

// logAlert remains the same as the original code
void logAlert(const struct_message& data) {
    Serial.println("--------------------------------");
    Serial.printf(" Station ID: %s\n", data.stnID);
    Serial.printf(" Fire Type: %c\n", data.fireType);
    Serial.printf(" Intensity: %c\n", data.fireIntensity);
    Serial.printf(" Verified: %s\n", data.verified ? "Yes" : "No");
    Serial.printf(" User: %s (%s)\n", data.user, data.userID);
    Serial.printf(" Location: %s, %s\n", data.latitude, data.longitude);
    Serial.printf(" Timestamp: %s %s\n", data.date, data.time);
    Serial.println("--------------------------------");
}

// displayAlert remains the same as the original code
void displayAlert(const struct_message& msg) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.setCursor(15, 0); display.println("ALERT!");
    display.setTextSize(1);
    display.drawFastHLine(0, 17, SCREEN_WIDTH, SH110X_WHITE);
    display.setCursor(0, 20);
    display.printf("Stn: %s Typ:%c Int:%c %s\n",
                   msg.stnID, msg.fireType, msg.fireIntensity, msg.verified ? "(V)" : "");
    display.setCursor(0, 30);
    display.printf("User: %.18s\n", msg.user); // Limit display length
    display.setCursor(0, 40);
    display.printf("Lat: %s\n", msg.latitude);
    display.setCursor(0, 50);
    display.printf("Lon: %s\n", msg.longitude);
    display.setCursor(0, 60);
    display.printf("%s %s", msg.date, msg.time);
    display.display();
}

// triggerBuzzer remains the same as the original code
void triggerBuzzer() {
    Serial.println("Triggering Buzzer...");
    for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH); delay(150);
        digitalWrite(BUZZER_PIN, LOW); delay(150);
    }
}

// sendToMega2560 remains the same as the original code
void sendToMega2560(const struct_message& data) {
    uint8_t packet[12]; // 11 data bytes + checksum

    // Map Fire Type (A=1, B=2, C=3, D=4, Auto 'A'=5, Others/Invalid = 0)
    if (data.fireType >= 'A' && data.fireType <= 'D') packet[0] = data.fireType - 'A' + 1;
    else if (data.fireType == 'A' && strcmp(data.userID,"AUTO")==0) packet[0] = 5; // Distinguish Auto 'A'
    else packet[0] = 0;

    // Map Intensity (1-4, Others/Invalid = 0)
    if (data.fireIntensity >= '1' && data.fireIntensity <= '4') packet[1] = data.fireIntensity - '0';
    else packet[1] = 0;

    // Map Zone from Station ID (First char A-Z -> 1-26, Others/Invalid = 0)
    if (strlen(data.stnID) > 0 && data.stnID[0] >= 'A' && data.stnID[0] <= 'Z') packet[2] = data.stnID[0] - 'A' + 1;
    else packet[2] = 0;

    // Convert Coordinates
    float lat = convertCoordinate(data.latitude);
    float lon = convertCoordinate(data.longitude);
    memcpy(&packet[3], &lat, 4);
    memcpy(&packet[7], &lon, 4);

    // Calculate Checksum (XOR bytes 0-10)
    packet[11] = 0;
    for (int i = 0; i < 11; i++) packet[11] ^= packet[i];

    // Send via I2C
    Serial.print("Sending I2C Packet to Mega: ");
    for(int i=0; i<12; i++) Serial.printf("%02X ", packet[i]);
    Serial.println();
    Wire.beginTransmission(MEGA2560_I2C_ADDRESS);
    size_t bytesWritten = Wire.write(packet, sizeof(packet));
    byte error = Wire.endTransmission();

    if (error != 0) Serial.printf("!! I2C Send Error to Mega: %d (Bytes Written: %d) !!\n", error, bytesWritten);
    else if (bytesWritten != sizeof(packet)) Serial.printf("!! I2C Send Warning: Incomplete write (%d / %d bytes) !!\n", bytesWritten, sizeof(packet));
    else Serial.println("I2C Packet Sent Successfully to Mega.");
}

// convertCoordinate remains the same as the original code
float convertCoordinate(const char* ddm) {
    if (ddm == nullptr || strlen(ddm) < 5 || strcmp(ddm, "N/A") == 0) return 0.0f;
    int degrees = 0; float minutes = 0.0f; char direction = ' ';
    int scanResult = sscanf(ddm, "%d*%f'%c", &degrees, &minutes, &direction);
    if (scanResult < 2) {
        // Serial.printf("!! Coordinate Conversion Error: Failed to parse '%s' (Result: %d) !!\n", ddm, scanResult);
        return 0.0f; // Minimize serial flooding
    }
    float decimal = degrees + (minutes / 60.0f);
    if (scanResult == 3 && (direction == 'S' || direction == 'W')) decimal *= -1.0f;
    return decimal;
}

// ======================= DISPLAY & UTILITY =======================

// UPDATED: Display current system status on OLED (includes MQTT status)
void displaySystemStatus() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("FireLinx Master B/1");
    display.drawFastHLine(0, 9, SCREEN_WIDTH, SH110X_WHITE);
    display.setCursor(0, 12);
    if (WiFi.status() == WL_CONNECTED) {
        display.printf("WiFi: %.15s\n", WiFi.SSID().c_str()); // Limit SSID length
        display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        // Show MQTT Connection Status
        display.printf("MQTT: %s", mqttClient.connected() ? "Connected" : "DISCONNECTED");
    } else {
        display.println("WiFi: DISCONNECTED");
        display.println("IP: ---.---.---.---");
        display.println("MQTT: DISCONNECTED"); // MQTT can't be connected if WiFi isn't
    }
    display.setCursor(0, 44); // Position adjusted for MQTT line
    int queueSize;
    portENTER_CRITICAL(&queueMux); queueSize = fireAlertQueue.size(); portEXIT_CRITICAL(&queueMux);
    display.printf("Queued:%d MEGA:%s\n", queueSize, checkMegaConnection() ? "OK" : "ERR");
    display.setCursor(0, 54);
    display.printf("Heap: %d KB", ESP.getFreeHeap() / 1024);
    display.display();
}


// displayError remains the same as the original code (but call in initWiFi was changed)
void displayError(const char* errorMsg, bool halt) {
    Serial.printf("\n\n!!! SYSTEM ERROR: %s !!!\n", errorMsg);
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("--- SYSTEM ERROR ---");
    display.drawFastHLine(0, 9, SCREEN_WIDTH, SH110X_WHITE);
    display.setCursor(0, 15);
    display.println(errorMsg);
    display.display();
    for (int i = 0; i < 5; i++) { // Error buzzer pattern
         digitalWrite(BUZZER_PIN, HIGH); delay(500);
         digitalWrite(BUZZER_PIN, LOW); delay(300);
    }
    if (halt) {
        Serial.println("System Halted.");
        while(1) { delay(1000); } // Infinite loop
    } else {
        Serial.println("Continuing operation despite error...");
        delay(3000); // Display error briefly
    }
}