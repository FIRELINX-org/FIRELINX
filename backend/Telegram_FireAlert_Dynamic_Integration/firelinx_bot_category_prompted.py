from flask import Flask, request
import paho.mqtt.publish as publish
import requests
import json
from datetime import datetime
import ssl
import re

app = Flask(__name__)

TELEGRAM_TOKEN = "8120467792:AAH8MjGR4TQtk0g8--kOJwC7hZEBpi28K8I"
MQTT_BROKER = "259353f6c5704a35aeb3dff107a0ab04.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_TOPIC = "staferb/web_alerts"
MQTT_USERNAME = "Staferb"
MQTT_PASSWORD = "EspWebDash@32"
TELEGRAM_API_URL = f"https://api.telegram.org/bot{TELEGRAM_TOKEN}"

FIRE_TYPES = {
    "A": "🟢 Type A - Ordinary Combustibles",
    "B": "🟡 Type B - Flammable Liquids",
    "C": "🔴 Type C - Electrical Fires",
    "D": "🟠 Type D - Metal Fires"
}
INTENSITIES = ["1", "2", "3", "4"]
user_sessions = {}

def reply(chat_id, text, buttons=None, markdown=False):
    payload = {
        "chat_id": chat_id,
        "text": text,
        "parse_mode": "Markdown" if markdown else None
    }
    if buttons:
        payload["reply_markup"] = json.dumps({"keyboard": [[{"text": b} for b in row] for row in buttons], "resize_keyboard": True, "one_time_keyboard": True})
    requests.post(f"{TELEGRAM_API_URL}/sendMessage", json=payload)

@app.route("/ping", methods=["GET"])
def ping():
    return "Pong!", 200

def format_ddm(coord, is_lat=True):
    direction = 'N' if (coord >= 0 and is_lat) else 'S' if is_lat else 'E' if coord >= 0 else 'W'
    coord = abs(float(coord))
    degrees = int(coord)
    minutes = (coord - degrees) * 60
    return f"{degrees}°{minutes:.4f}'{direction}"

def extract_coords_from_url(url):
    try:
        response = requests.get(url, allow_redirects=True, timeout=10)
        resolved = response.url
        print(f"🔍 [Resolved URL] {resolved}")  # <--- This line logs the final URL

        # Try format 1: ...@lat,lng,...
        match = re.search(r'@([-\\d.]+),([-\\d.]+)', resolved)
        if match:
            return float(match.group(1)), float(match.group(2))

        # Try format 2: ...!3dlat!4dlng
        match = re.search(r'!3d([-\\d.]+)!4d([-\\d.]+)', resolved)
        if match:
            return float(match.group(1)), float(match.group(2))

        # Try format 3: query=lat,lng
        match = re.search(r'query=([-\\d.]+),([-\\d.]+)', resolved)
        if match:
            return float(match.group(1)), float(match.group(2))

        print(f"❌ Could not parse coordinates from resolved URL.")
    except Exception as e:
        print("⚠️ Error resolving URL:", e)
    return None, None

def send_mqtt_alert(chat_id, user_name, user_id, fire_type, intensity, lat, lng):
    latitude = format_ddm(lat, True)
    longitude = format_ddm(lng, False)
    now = datetime.now()
    alert = {
        "command": "fire_alert",
        "payload": {
            "fireType": fire_type,
            "fireIntensity": intensity,
            "verified": True,
            "user": user_name,
            "userID": str(user_id)[-5:],
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
            payload=json.dumps(alert, ensure_ascii=False),
            hostname=MQTT_BROKER,
            port=MQTT_PORT,
            auth={'username': MQTT_USERNAME, 'password': MQTT_PASSWORD},
            tls=ssl.create_default_context()
        )
        reply(chat_id, f"✅ *Fire alert sent!*\n{FIRE_TYPES.get(fire_type)}\n"
                       f"🔥 *Intensity*: {intensity}\n📍 *Location*: {latitude}, {longitude}", markdown=True)
        user_sessions.pop(chat_id, None)
    except Exception as e:
        reply(chat_id, f"❌ Error: {str(e)}")

@app.route(f"/webhook/{TELEGRAM_TOKEN}", methods=["POST"])
def telegram_webhook():
    data = request.get_json()
    message = data.get("message", {})
    text = message.get("text", "").strip()
    chat_id = message.get("chat", {}).get("id")
    user = message.get("from", {})
    user_name = user.get("username") or user.get("first_name", "Unknown")
    user_id = user.get("id", 0)
    session = user_sessions.get(chat_id, {})

    if text.lower() in ["/start", "start"]:
        reply(chat_id, "👋 *Welcome to FireLinx!*\n\nSend a Google Maps location link or use the format:\n"
                      "`/fire B 3 22.5726 88.3639`", markdown=True)
        return "OK", 200

    if text.lower() == "/help":
        reply(chat_id, "📘 *Help Guide*\n\n"
                      "• To report fire:\n  1. Send Google Maps link OR\n  2. Use `/fire <type> <intensity> <lat> <lng>`\n"
                      "• I'll guide you from there.\n\n"
                      "Fire Types:\n" + "\n".join([f"`{k}` - {v}" for k, v in FIRE_TYPES.items()]) +
                      "\nIntensity: `1` (low) to `4` (severe)", markdown=True)
        return "OK", 200

    # Manual full command
    if text.startswith("/fire"):
        try:
            _, fire_type, intensity, raw_lat, raw_lng = text.split()
            if fire_type.upper() not in FIRE_TYPES or intensity not in INTENSITIES:
                raise ValueError
            send_mqtt_alert(chat_id, user_name, user_id, fire_type.upper(), intensity, float(raw_lat), float(raw_lng))
        except Exception:
            reply(chat_id, "❌ Invalid format. Use `/fire B 3 22.5726 88.3639`", markdown=True)
        return "OK", 200

    # Step 1: Google Maps link
    if "maps.app.goo.gl" in text or "google.com/maps" in text:
        lat, lng = extract_coords_from_url(text)
        if lat and lng:
            user_sessions[chat_id] = {"lat": lat, "lng": lng, "step": "type"}
            reply(chat_id, "📍 Location received!\nChoose fire type:", [[list(FIRE_TYPES.keys())]])
        else:
            reply(chat_id, "❌ Could not extract location from link.")
        return "OK", 200

    # Step 2: Fire type
    if session.get("step") == "type" and text.upper() in FIRE_TYPES:
        session["fireType"] = text.upper()
        session["step"] = "intensity"
        user_sessions[chat_id] = session
        reply(chat_id, "🔥 Now choose fire intensity:", [[INTENSITIES]])
        return "OK", 200

    # Step 3: Intensity
    if session.get("step") == "intensity" and text in INTENSITIES:
        session["intensity"] = text
        send_mqtt_alert(chat_id, user_name, user_id, session["fireType"], text, session["lat"], session["lng"])
        return "OK", 200

    reply(chat_id, "🤖 Please send a valid Google Maps link or use `/fire B 3 22.5726 88.3639`", markdown=True)
    return "OK", 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5001)
