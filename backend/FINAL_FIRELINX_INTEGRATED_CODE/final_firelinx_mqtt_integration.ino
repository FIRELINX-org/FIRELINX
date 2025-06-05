#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_now.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <queue>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Pin Definitions ---
#define BUZZER_PIN 25

// --- OLED Display Config ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDRESS 0x3C

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Communication Config ---
#define UDP_PORT 12345
#define MEGA2560_I2C_ADDRESS 0x08
#define ACK_MESSAGE "ACK"
#define I2C_TIMEOUT_MS 50

WiFiUDP udp;

// --- MQTT Configuration ---
const char* mqtt_server = "259353f6c5704a35aeb3dff107a0ab04.s1.eu.hivemq.cloud";
const int mqtt_port = 1883;
const char* mqtt_topic_subscribe = "staferb/web_alerts";
const char* mqtt_client_id = "FireLinxMasterB1";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttReconnectAttempt = 0;
const long mqttReconnectInterval = 5000;

// Optional MQTT credentials
// const char* mqtt_user = "your_username";
// const char* mqtt_password = "your_password";

// --- Data Structure ---
typedef struct struct_message {
    char fireType;
    char fireIntensity;
    bool verified;
    char user[32];
    char userID[10];
    char stnID[10];
    char latitude[16];
    char longitude[16];
    char date[10];
    char time[10];
} struct_message;

std::queue<struct_message> fireAlertQueue;
portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED;

// --- MQTT Callback ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.println("MQTT JSON parse error");
        return;
    }

    JsonObject data = doc["payload"];
    struct_message msg;
    msg.fireType = data["fireType"][0];
    msg.fireIntensity = data["fireIntensity"][0];
    msg.verified = data["verified"];
    strncpy(msg.user, data["user"] | "MQTT", sizeof(msg.user));
    strncpy(msg.userID, data["userID"] | "MQTT", sizeof(msg.userID));
    strncpy(msg.stnID, data["stnID"] | "MQ", sizeof(msg.stnID));
    strncpy(msg.latitude, data["latitude"] | "--", sizeof(msg.latitude));
    strncpy(msg.longitude, data["longitude"] | "--", sizeof(msg.longitude));
    strncpy(msg.date, data["date"] | "--", sizeof(msg.date));
    strncpy(msg.time, data["time"] | "--", sizeof(msg.time));

    portENTER_CRITICAL(&queueMux);
    fireAlertQueue.push(msg);
    portEXIT_CRITICAL(&queueMux);
}

// --- MQTT Reconnect ---
void mqttReconnect() {
    if (!mqttClient.connected()) {
        if (mqttClient.connect(mqtt_client_id)) {
            mqttClient.subscribe(mqtt_topic_subscribe);
        }
    }
}

// --- OLED Display Function ---
void displayMessage(const struct_message& msg) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.printf("FIRE ALERT\nType: %c Intensity: %c\nLoc: %s, %s\nFrom: %s", msg.fireType, msg.fireIntensity, msg.latitude, msg.longitude, msg.user);
    display.display();
}

// --- Send to Mega via I2C ---
void sendToMega(const struct_message& msg) {
    Wire.beginTransmission(MEGA2560_I2C_ADDRESS);
    Wire.write((uint8_t*)&msg, sizeof(msg));
    Wire.endTransmission();
}

// --- Setup ---
void setup() {
    Serial.begin(115200);
    Wire.begin();
    display.begin(OLED_I2C_ADDRESS, true);
    display.display();
    delay(1000);
    display.clearDisplay();

    WiFiManager wifiManager;
    wifiManager.autoConnect("FireLinxMaster");

    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(mqttCallback);
}

// --- Main Loop ---
void loop() {
    if (!mqttClient.connected()) {
        mqttReconnect();
    }
    mqttClient.loop();

    if (!fireAlertQueue.empty()) {
        portENTER_CRITICAL(&queueMux);
        struct_message msg = fireAlertQueue.front();
        fireAlertQueue.pop();
        portEXIT_CRITICAL(&queueMux);

        displayMessage(msg);
        sendToMega(msg);
        tone(BUZZER_PIN, 2000, 1000); // buzz alert
    }

    // Existing ESP-NOW and UDP handling here...
}
