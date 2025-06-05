from flask import Flask, request
import paho.mqtt.publish as publish
import json

app = Flask(__name__)

# Telegram Bot Token
TELEGRAM_TOKEN = "8120467792:AAH8MjGR4TQtk0g8--kOJwC7hZEBpi28K8I"

# HiveMQ MQTT Config
MQTT_BROKER = "259353f6c5704a35aeb3dff107a0ab04.s1.eu.hivemq.cloud"
MQTT_PORT = 1883
MQTT_TOPIC = "staferb/web_alerts"
MQTT_USERNAME = "Staferb"
MQTT_PASSWORD = "EspWebDash@32"

# Root endpoint for webhook
@app.route(f"/webhook/{TELEGRAM_TOKEN}", methods=["POST"])
def telegram_webhook():
    data = request.get_json()

    # Basic command processing
    message = data.get("message", {})
    chat_id = message.get("chat", {}).get("id")
    text = message.get("text", "").strip().lower()

    if "/fire" in text or "/sos" in text:
        alert = {
            "command": "fire_alert",
            "payload": {
                "fireType": "A",
                "fireIntensity": "2",
                "verified": True,
                "user": "Telegram",
                "userID": str(chat_id),
                "stnID": "TG",
                "latitude": "00.0000N",
                "longitude": "00.0000E",
                "date": "01/05",
                "time": "12:00"
            }
        }

        publish.single(
            MQTT_TOPIC,
            payload=json.dumps(alert),
            hostname=MQTT_BROKER,
            port=MQTT_PORT,
            auth={'username': MQTT_USERNAME, 'password': MQTT_PASSWORD}
        )

    return "OK", 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5001)
