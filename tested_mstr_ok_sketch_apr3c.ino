#include <WiFi.h>           // For WiFi connection
#include <WiFiUdp.h>        // For UDP communication
#include <esp_now.h>        // For ESP-NOW communication
#include <WiFiManager.h>    // For easy WiFi setup
#include <Wire.h>           // For I2C (OLED, Mega)
#include <Adafruit_GFX.h>   // For OLED
#include <Adafruit_SH110X.h> // For OLED Driver SH1106/SH1107
#include <queue>            // For buffering incoming messages

// --- Pin Definitions ---
#define BUZZER_PIN 25 // Buzzer pin (Adjust if different)

// --- OLED Display Config ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define OLED_I2C_ADDRESS 0x3C // Default I2C address for SH1106/SSD1306

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Communication Config ---
#define UDP_PORT 12345      // Port to listen for UDP packets
#define MEGA2560_I2C_ADDRESS 0x08 // I2C Address of the Arduino Mega slave
#define ACK_MESSAGE "ACK"   // Acknowledgement message content
#define I2C_TIMEOUT_MS 50   // Timeout for I2C communication

WiFiUDP udp;

// --- Data Structure (MUST MATCH CHILD NODE) ---
typedef struct struct_message {
    char fireType;        // A, B, C, D or 'A' for Auto
    char fireIntensity;   // 1-4 (Manual) or calculated severity (Auto)
    bool verified;        // True if manual alert verified with '*'
    char user[32];        // Name of user (Manual) or "Auto Mode"
    char userID[10];      // User ID (Manual) or "AUTO"
    char stnID[10];       // Station ID (e.g., "D/4") from EEPROM
    char latitude[16];    // DDM format string
    char longitude[16];   // DDM format string
    char date[10];        // DD/MM string
    char time[10];        // HH:MM string (adjusted)
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

// Communication Callbacks & Processing
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len);
void processUDPPackets();
void processQueue();

// Alert Handling
bool validateAlert(const struct_message& data);
void handleAlert(const struct_message& alertData);
void logAlert(const struct_message& alertData);
void displayAlert(const struct_message& alertData);
void triggerBuzzer();
void sendToMega2560(const struct_message& alertData);
float convertCoordinate(const char* ddm); // Helper for Mega I2C

// Display & Utility
void displaySystemStatus();
void displayError(const char* errorMsg, bool halt = true);


// ======================= SETUP =======================
void setup() {
    Serial.begin(115200);
    Wire.begin(); // Initialize I2C

    Serial.println("\n\n--- FireLinx Master Node Booting ---");

    // Initialize OLED Display
    if (!display.begin(OLED_I2C_ADDRESS, true)) { // Address, init i2c bus
        Serial.println(F("!! OLED Init Failed !!"));
        // Display might be critical, consider halting or visual error
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

    // Initialize WiFi
    initWiFi(); // Uses WiFiManager

    // Initialize UDP Listening (Requires WiFi)
    initUDP();

    // Initialize ESP-NOW Receiving (Best after WiFi for channel sync)
    initESP_NOW();

    // Check connection to Arduino Mega via I2C
    if (!checkMegaConnection()) {
         Serial.println("Warning: Mega2560 not detected on I2C!");
         // Display warning but continue operation
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
    // 1. Check for incoming UDP packets and enqueue them
    processUDPPackets();

    // 2. Process one message from the queue (if any)
    processQueue();

    // 3. Periodically update the status display on OLED
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
    // Optional: Set timeout for config portal
    wifiManager.setConfigPortalTimeout(20); // 3 minutes
    // Optional: Set custom AP name/password for config portal
    // wifiManager.autoConnect("FireLinx-Master-Setup", "password123");

    display.setCursor(0, 20);
    display.println("Connecting WiFi...");
    display.println("(AP: FireLinx-Master)"); // Show AP name for WiFiManager
    display.display();
    Serial.println("Starting WiFiManager...");

    // This is blocking until connected or timeout
    if (!wifiManager.autoConnect("FireLinx-Master")) {
        Serial.println("WiFi Connection Failed via WiFiManager!");
        displayError("WiFi Failed!", true); // Halt on critical failure
    }

    // WiFi is connected
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

bool checkMegaConnection() {
    Wire.beginTransmission(MEGA2560_I2C_ADDRESS);
    byte error = Wire.endTransmission();
    if (error == 0) {
        return true; // Device acknowledged
    } else {
        Serial.printf("I2C Check Error for Mega (Addr 0x%02X): %d\n", MEGA2560_I2C_ADDRESS, error);
        // 2 = addr NACK, 3 = data NACK, 4 = other error
        return false;
    }
}

// ======================= COMMUNICATION & QUEUE PROCESSING =======================

// ESP-NOW Data Receive Callback Function (Runs in WiFi task context)
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
    // Check if data length matches expected structure size
    if (len == sizeof(struct_message)) {
        struct_message receivedData;
        memcpy(&receivedData, incomingData, sizeof(receivedData));

        Serial.printf("ESP-NOW Data Received from: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      info->src_addr[0], info->src_addr[1], info->src_addr[2], info->src_addr[3], info->src_addr[4], info->src_addr[5]);
        Serial.printf("  -> StnID: %s, Type: %c, Intensity: %c\n", receivedData.stnID, receivedData.fireType, receivedData.fireIntensity);

        // --- Send ESP-NOW ACK ---
        esp_err_t ackResult = esp_now_send(info->src_addr, (uint8_t *)ACK_MESSAGE, strlen(ACK_MESSAGE));
        if (ackResult != ESP_OK) {
             Serial.printf("!! ESP-NOW ACK Send Failed to %02X... Error: %s\n", info->src_addr[0], esp_err_to_name(ackResult));
        } else {
             // Serial.println("   ESP-NOW ACK Sent."); // Potentially floods monitor
        }

        // --- Add to Queue (Thread Safe) ---
        portENTER_CRITICAL(&queueMux);
        fireAlertQueue.push(receivedData);
        portEXIT_CRITICAL(&queueMux);
        Serial.println("   Added to queue (ESP-NOW).");

    } else {
        Serial.printf("ESP-NOW Received data of incorrect length (%d bytes, expected %d). Ignoring.\n", len, sizeof(struct_message));
    }
}

// Check for and process incoming UDP packets (Runs in main loop)
void processUDPPackets() {
    if (WiFi.status() != WL_CONNECTED) return; // Can't process UDP without WiFi

    int packetSize = udp.parsePacket();
    if (packetSize == sizeof(struct_message)) {
        struct_message receivedData;
        IPAddress remoteIp = udp.remoteIP();
        uint16_t remotePort = udp.remotePort();

        int len = udp.read((uint8_t *)&receivedData, sizeof(receivedData));

        Serial.printf("UDP Data Received from: %s:%d\n", remoteIp.toString().c_str(), remotePort);
        Serial.printf("  -> StnID: %s, Type: %c, Intensity: %c\n", receivedData.stnID, receivedData.fireType, receivedData.fireIntensity);

        // --- Send UDP ACK ---
        udp.beginPacket(remoteIp, remotePort);
        udp.write((uint8_t *)ACK_MESSAGE, strlen(ACK_MESSAGE));
        if (udp.endPacket()) {
            // Serial.println("   UDP ACK Sent."); // Potentially floods monitor
        } else {
            Serial.println("!! UDP ACK Send Failed !!");
        }

        // --- Add to Queue (Thread Safe) ---
        portENTER_CRITICAL(&queueMux);
        fireAlertQueue.push(receivedData);
        portEXIT_CRITICAL(&queueMux);
        Serial.println("   Added to queue (UDP).");

    } else if (packetSize > 0) {
        // Received a packet, but wrong size
        Serial.printf("UDP Received packet of incorrect length (%d bytes, expected %d). Flushing.\n", packetSize, sizeof(struct_message));
        udp.flush(); // Discard the packet data
    }
    // No packet available or handled correct/incorrect size packet
}

// Process one message from the queue (Runs in main loop)
void processQueue() {
    struct_message alertToProcess;
    bool dataAvailable = false;

    // --- Dequeue (Thread Safe) ---
    portENTER_CRITICAL(&queueMux);
    if (!fireAlertQueue.empty()) {
        alertToProcess = fireAlertQueue.front();
        fireAlertQueue.pop();
        dataAvailable = true;
    }
    portEXIT_CRITICAL(&queueMux);

    // --- Process if data was available ---
    if (dataAvailable) {
        Serial.println("--- Processing Alert from Queue ---");
        if (validateAlert(alertToProcess)) {
            handleAlert(alertToProcess);
        } else {
            Serial.println("!! Invalid Alert Data in Queue. Discarding. !!");
            logAlert(alertToProcess); // Log even invalid data for debugging
        }
    }
}


// ======================= ALERT HANDLING =======================

// Validate the received data structure contents
bool validateAlert(const struct_message& data) {
    // Basic checks - adjust thresholds/logic as needed
    bool typeValid = (data.fireType >= 'A' && data.fireType <= 'D') || data.fireType == 'A'; // Allow 'A' for Auto
    bool intensityValid = (data.fireIntensity >= '1' && data.fireIntensity <= '4');
    bool stnIdValid = (strlen(data.stnID) > 0 && strlen(data.stnID) < sizeof(data.stnID));
    // Could add basic Lat/Lon format check if needed (e.g., presence of '*' and ''')
    // bool gpsFormatSeemsValid = (strstr(data.latitude, "*") != NULL && strstr(data.latitude, "'") != NULL);

    return typeValid && intensityValid && stnIdValid;
}

// Main handler for a validated alert
void handleAlert(const struct_message& alertData) {
    Serial.println("Handling Valid Alert:");
    logAlert(alertData);     // Log details to Serial Monitor
    displayAlert(alertData); // Show on OLED
    triggerBuzzer();         // Activate buzzer pattern
    sendToMega2560(alertData); // Forward data to Mega via I2C
    // After handling, wait for OLED to be cleared by status update or next alert
}

// Print alert details to Serial Monitor
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

// Display alert details on the OLED
void displayAlert(const struct_message& msg) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);

    // Header - Make it stand out
    display.setTextSize(2);
    display.setCursor(15, 0); // Centered-ish
    display.println("ALERT!");
    display.setTextSize(1);
    display.drawFastHLine(0, 17, SCREEN_WIDTH, SH110X_WHITE);

    // Alert Details
    display.setCursor(0, 20);
    display.printf("Stn: %s Typ:%c Int:%c %s\n",
                   msg.stnID, msg.fireType, msg.fireIntensity, msg.verified ? "(V)" : "");

    display.setCursor(0, 30);
    display.printf("User: %.18s\n", msg.user); // Limit user name length display

    display.setCursor(0, 40);
    display.printf("Lat: %s\n", msg.latitude);
    display.setCursor(0, 50);
    display.printf("Lon: %s\n", msg.longitude);

    display.setCursor(0, 60); // Bottom line might get cut off slightly on 64px
    display.printf("%s %s", msg.date, msg.time);

    display.display();
    // Note: This display will persist until the next status update or alert
}

// Trigger a simple buzzer pattern
void triggerBuzzer() {
    Serial.println("Triggering Buzzer...");
    for (int i = 0; i < 3; i++) { // Example: 3 short beeps
        digitalWrite(BUZZER_PIN, HIGH);
        delay(150);
        digitalWrite(BUZZER_PIN, LOW);
        delay(150);
    }
}

// Format data and send it to the Arduino Mega via I2C
void sendToMega2560(const struct_message& data) {
    // Packet structure (11 bytes): Based on previous Master code
    // [0] Fire type (1-4 for A-D, maybe 5 for Auto 'A'?)
    // [1] Intensity (1-4)
    // [2] Zone ('A'=1, 'B'=2, 'C'=3, 'D'=4 etc. based on first char of stnID)
    // [3-6] Latitude (float, converted from DDM)
    // [7-10] Longitude (float, converted from DDM)
    // [11] Checksum (XOR of bytes 0-10) - Corrected index to 11 bytes total

    uint8_t packet[12]; // Increased size to 12 for 11 data bytes + checksum byte

    // Map Fire Type
    if (data.fireType >= 'A' && data.fireType <= 'D') {
        packet[0] = data.fireType - 'A' + 1; // A=1, B=2, C=3, D=4
    } else if (data.fireType == 'A') { // Handle Auto mode 'A'
        packet[0] = 5; // Use 5 to represent Auto - Mega needs to know this convention
    } else {
        packet[0] = 0; // Unknown/Invalid
    }

    // Map Intensity
    if (data.fireIntensity >= '1' && data.fireIntensity <= '4') {
        packet[1] = data.fireIntensity - '0'; // 1-4
    } else {
        packet[1] = 0; // Unknown/Invalid
    }

    // Map Zone from Station ID (assuming format like "D/4")
    if (strlen(data.stnID) > 0 && data.stnID[0] >= 'A' && data.stnID[0] <= 'Z') {
         packet[2] = data.stnID[0] - 'A' + 1; // A=1, B=2, ...
         // Add more robust mapping if station IDs can be different formats
    } else {
         packet[2] = 0; // Unknown Zone
    }


    // Convert Coordinates
    float lat = convertCoordinate(data.latitude);
    float lon = convertCoordinate(data.longitude);
    memcpy(&packet[3], &lat, 4); // Copy float bytes
    memcpy(&packet[7], &lon, 4); // Copy float bytes

    // Calculate Checksum (XOR of bytes 0 to 10)
    packet[11] = 0; // Initialize checksum byte
    for (int i = 0; i < 11; i++) {
        packet[11] ^= packet[i];
    }

    // Send via I2C
    Serial.print("Sending I2C Packet to Mega: ");
    for(int i=0; i<12; i++) Serial.printf("%02X ", packet[i]);
    Serial.println();

    Wire.beginTransmission(MEGA2560_I2C_ADDRESS);
    size_t bytesWritten = Wire.write(packet, sizeof(packet));
    byte error = Wire.endTransmission();

    if (error != 0) {
        Serial.printf("!! I2C Send Error to Mega: %d (Bytes Written: %d) !!\n", error, bytesWritten);
        // Add display feedback for I2C error if needed
    } else if (bytesWritten != sizeof(packet)) {
        Serial.printf("!! I2C Send Warning: Incomplete write (%d / %d bytes) !!\n", bytesWritten, sizeof(packet));
    } else {
        Serial.println("I2C Packet Sent Successfully to Mega.");
    }
}

// Helper to convert DDM string (e.g., "DD*MM.MMMM'D") to float
float convertCoordinate(const char* ddm) {
    if (ddm == nullptr || strlen(ddm) < 5 || strcmp(ddm, "N/A") == 0) {
        return 0.0f; // Invalid input
    }

    int degrees = 0;
    float minutes = 0.0f;
    char direction = ' ';
    int scanResult = sscanf(ddm, "%d*%f'%c", &degrees, &minutes, &direction);

    if (scanResult < 2) { // Check if at least degrees and minutes were scanned
        Serial.printf("!! Coordinate Conversion Error: Failed to parse '%s' (Result: %d) !!\n", ddm, scanResult);
        return 0.0f;
    }

    float decimal = degrees + (minutes / 60.0f);

    // Apply direction sign if scanned successfully
    if (scanResult == 3 && (direction == 'S' || direction == 'W')) {
        decimal *= -1.0f;
    }

    return decimal;
}

// ======================= DISPLAY & UTILITY =======================

// Display current system status on OLED
void displaySystemStatus() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);

    // Header
    display.println("FireLinx Master B/1");
    display.drawFastHLine(0, 9, SCREEN_WIDTH, SH110X_WHITE);

    // WiFi Status
    display.setCursor(0, 12);
    if (WiFi.status() == WL_CONNECTED) {
        display.printf("WiFi: %s\n", WiFi.SSID().c_str());
        display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        display.println("WiFi: DISCONNECTED");
        display.println("IP: ---.---.---.---");
    }

    // Queue Status
    display.setCursor(0, 34);
    int queueSize;
    portENTER_CRITICAL(&queueMux);
    queueSize = fireAlertQueue.size();
    portEXIT_CRITICAL(&queueMux);
    display.printf("Alerts Queued: %d\n", queueSize);

    // Mega Connection Status
    display.setCursor(0, 44);
    display.printf("MEGA (I2C): %s\n", checkMegaConnection() ? "Connected" : "ERROR");

    // Heap Memory (Useful for debugging)
    display.setCursor(0, 54);
    display.printf("Heap Free: %d KB", ESP.getFreeHeap() / 1024);

    display.display();
}

// Display a critical error message and optionally halt
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

    // Simple error buzzer pattern (long beeps)
    for (int i = 0; i < 5; i++) {
         digitalWrite(BUZZER_PIN, HIGH); delay(500);
         digitalWrite(BUZZER_PIN, LOW); delay(300);
    }


    if (halt) {
        Serial.println("System Halted.");
        while(1) { delay(1000); } // Infinite loop
    } else {
        Serial.println("Continuing operation despite error...");
        delay(3000); // Display error for a few seconds before continuing
    }
}