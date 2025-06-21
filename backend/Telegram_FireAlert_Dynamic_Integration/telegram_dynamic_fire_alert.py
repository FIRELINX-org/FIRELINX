from flask import Flask, request
import paho.mqtt.publish as publish
import requests
import json
from datetime import datetime
import ssl

app = Flask(__name__)

TELEGRAM_TOKEN = "8120467792:AAH8MjGR4TQtk0g8--kOJwC7hZEBpi28K8I"

MQTT_BROKER = "259353f6c5704a35aeb3dff107a0ab04.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_TOPIC = "staferb/web_alerts"
MQTT_USERNAME = "Staferb"
MQTT_PASSWORD = "EspWebDash@32"

TELEGRAM_API_URL = f"https://api.telegram.org/bot{TELEGRAM_TOKEN}/sendMessage"

def reply(chat_id, text):
    requests.post(TELEGRAM_API_URL, json={
        "chat_id": chat_id,
        "text": text
    })

def format_ddm(coord, is_lat=True):
    direction = ''
    if is_lat:
        direction = 'N' if coord >= 0 else 'S'
    else:
        direction = 'E' if coord >= 0 else 'W'

    coord = abs(float(coord))
    degrees = int(coord)
    minutes = (coord - degrees) * 60
    return f"{degrees}°{minutes:.4f}'{direction}"  # keep proper ° symbol

@app.route(f"/webhook/{TELEGRAM_TOKEN}", methods=["POST"])
def telegram_webhook():
    data = request.get_json()

    message = data.get("message", {})
    text = message.get("text", "").strip()
    chat_id = message.get("chat", {}).get("id")
    user = message.get("from", {})

    user_name = user.get("username") or user.get("first_name", "Unknown")
    user_id = str(user.get("id", 0))[-5:]  # Shorten user ID

    if text.startswith("/fire"):
        try:
            parts = text.split()
            fire_type = parts[1].upper()
            fire_intensity = parts[2]
            raw_lat = float(parts[3])
            raw_lng = float(parts[4])
            latitude = format_ddm(raw_lat, is_lat=True)
            longitude = format_ddm(raw_lng, is_lat=False)
        except Exception:
            reply(chat_id, "❌ Invalid format. Use:\n`/fire B 3 22.5726 88.3639`")
            return "Invalid format", 200

        now = datetime.now()
        alert = {
            "command": "fire_alert",
            "payload": {
                "fireType": fire_type,
                "fireIntensity": fire_intensity,
                "verified": True,
                "user": user_name,
                "userID": user_id,
                "stnID": "TG",
                "latitude": latitude,
                "longitude": longitude,
                "date": now.strftime("%d/%m"),
                "time": now.strftime("%H:%M:%S")
            }
        }

        try:
            publish.single(
                MQTT_TOPIC,
                payload=json.dumps(alert, ensure_ascii=False),  # ✅ keep ° symbol
                hostname=MQTT_BROKER,
                port=MQTT_PORT,
                auth={'username': MQTT_USERNAME, 'password': MQTT_PASSWORD},
                tls=ssl.create_default_context()
            )
            reply(chat_id, f"✅ Fire alert sent!\nType: {fire_type}, Intensity: {fire_intensity}\nLocation: {latitude}, {longitude}")
        except Exception as e:
            reply(chat_id, f"❌ Failed to send alert: {str(e)}")

    else:
        reply(chat_id, "Hi! 👋 Use `/fire B 3 22.5726 88.3639` to report a fire alert.")

    return "OK", 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5001)
