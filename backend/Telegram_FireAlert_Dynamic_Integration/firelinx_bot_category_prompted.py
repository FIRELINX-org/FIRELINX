import os
from flask import Flask, request
import paho.mqtt.publish as publish
import requests
import json
from datetime import datetime
import ssl
import re
import easyocr
from PIL import Image
from PIL.ExifTags import TAGS, GPSTAGS
import io
from dotenv import load_dotenv
load_dotenv()

app = Flask(__name__)

# Load environment variables
TELEGRAM_TOKEN = os.environ.get("TELEGRAM_TOKEN")
print("🔑 Loaded TELEGRAM_TOKEN:", TELEGRAM_TOKEN)
MQTT_BROKER = os.environ.get("MQTT_BROKER")
MQTT_PORT = int(os.environ.get("MQTT_PORT", 8883))
MQTT_TOPIC = os.environ.get("MQTT_TOPIC")
MQTT_USERNAME = os.environ.get("MQTT_USERNAME")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD")
TELEGRAM_API_URL = f"https://api.telegram.org/bot{TELEGRAM_TOKEN}"

FIRE_TYPES = {
    "A": "🟢 Type A - Combustibles",
    "B": "🟡 Type B - Flammable Liquids",
    "C": "🔴 Type C - Electrical Fires",
    "D": "🟠 Type D - Metal Fires"
}
INTENSITIES = ["1", "2", "3", "4"]
user_sessions = {}
reader = easyocr.Reader(['en'], gpu=False)

# === HELPERS ===
def reply(chat_id, text, buttons=None, markdown=False):
    payload = {
        "chat_id": chat_id,
        "text": text
    }
    if markdown:
        payload["parse_mode"] = "Markdown"

    if buttons:
        payload["reply_markup"] = json.dumps({
            "keyboard": [[{"text": b} for b in row] for row in buttons],  # fixed: correct button structure
            "resize_keyboard": True,
            "one_time_keyboard": True
        })

    try:
        print("📤 Reply payload:", payload)
        response = requests.post(f"{TELEGRAM_API_URL}/sendMessage", json=payload)
        print("📬 Telegram API response:", response.status_code, response.text)
    except Exception as e:
        print("❌ Telegram reply error:", e)

def format_ddm(coord, is_lat=True):
    direction = 'N' if (coord >= 0 and is_lat) else 'S' if is_lat else 'E' if coord >= 0 else 'W'
    coord = abs(float(coord))
    degrees = int(coord)
    minutes = (coord - degrees) * 60
    return f"{degrees}°{minutes:.4f}'{direction}"

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
            payload=json.dumps(alert, separators=(",", ":"), ensure_ascii=False),
            hostname=MQTT_BROKER,
            port=MQTT_PORT,
            auth={'username': MQTT_USERNAME, 'password': MQTT_PASSWORD},
            tls=ssl.create_default_context()
        )
        reply(chat_id, f"✅ *Fire alert sent!*\n{FIRE_TYPES.get(fire_type)}\n"
                       f"🔥 *Intensity*: {intensity}\n📍 *Location*: {latitude}, {longitude}", markdown=True)
        user_sessions.pop(chat_id, None)
    except Exception as e:
        reply(chat_id, f"❌ MQTT Error: {str(e)}")

def extract_lat_lng_from_image(image_bytes):
    try:
        image = Image.open(io.BytesIO(image_bytes)).convert("RGB")
        image.save("temp_ocr.jpg")
        results = reader.readtext("temp_ocr.jpg", detail=0)
        text = " ".join(results)
        print("📜 OCR Text:\n", text)

        lat_match = re.search(r"Lat[:\s]*([0-9.]+)", text, re.IGNORECASE)
        lng_match = re.search(r"Long[:\s]*([0-9.]+)", text, re.IGNORECASE)

        if lat_match and lng_match:
            return float(lat_match.group(1)), float(lng_match.group(1))
    except Exception as e:
        print("❌ OCR extract error:", e)
    return None, None

@app.route("/ping", methods=["GET"])
def ping():
    return "Pong!", 200
@app.route(f"/webhook/{TELEGRAM_TOKEN}", methods=["POST"])
def telegram_webhook():
    data = request.get_json()
    print("📩 Telegram Data:", data)

    message = data.get("message", {})
    chat_id = message.get("chat", {}).get("id")
    user = message.get("from", {})
    user_name = user.get("username") or user.get("first_name", "Unknown")
    user_id = user.get("id", 0)
    session = user_sessions.get(chat_id, {})

    # 🖼 Handle photo upload
    if "photo" in message:
        try:
            file_id = message["photo"][-1]["file_id"]
            file_info = requests.get(f"{TELEGRAM_API_URL}/getFile?file_id={file_id}").json()
            file_path = file_info["result"]["file_path"]
            image_bytes = requests.get(f"https://api.telegram.org/file/bot{TELEGRAM_TOKEN}/{file_path}").content

            lat, lng = extract_lat_lng_from_image(image_bytes)
            print(f"🧠 Extracted Lat: {lat}, Lng: {lng}")

            if lat and lng:
                user_sessions[chat_id] = {"lat": lat, "lng": lng, "step": "type"}
                reply(chat_id, "📷 Location extracted from image!\nChoose fire type:", [[k] for k in FIRE_TYPES])
            else:
                reply(chat_id, "⚠️ Couldn't extract Lat/Long. Please try again or use `/fire` manually.")
        except Exception as e:
            print("❌ Image error:", e)
            reply(chat_id, "❌ Error reading image.")
        return "OK", 200

    # Handle text commands
    text = message.get("text", "").strip()

    if text.lower() in ["/start", "start"]:
        reply(chat_id, "👋 *Welcome to FireLinx!*\n\n📷 Send a GPS-tagged image with `Lat` and `Long`\n"
                      "Or use `/fire B 3 22.5726 88.3639`", markdown=True)
        return "OK", 200

    if text.lower() == "/help":
        reply(chat_id, "📘 *Help*\nSend image with `Lat` & `Long`, or use `/fire B 3 <lat> <lng>`", markdown=True)
        return "OK", 200

    if text.startswith("/fire"):
        try:
            _, fire_type, intensity, raw_lat, raw_lng = text.split()
            if fire_type.upper() not in FIRE_TYPES or intensity not in INTENSITIES:
                raise ValueError
            send_mqtt_alert(chat_id, user_name, user_id, fire_type.upper(), intensity, float(raw_lat), float(raw_lng))
        except Exception:
            reply(chat_id, "❌ Invalid format. Use `/fire B 3 22.5726 88.3639`", markdown=True)
        return "OK", 200

    if session.get("step") == "type" and text.upper() in FIRE_TYPES:
        session["fireType"] = text.upper()
        session["step"] = "intensity"
        user_sessions[chat_id] = session
        reply(chat_id, "🔥 Choose fire intensity:", [[i] for i in INTENSITIES])
        return "OK", 200

    if session.get("step") == "intensity" and text in INTENSITIES:
        session["intensity"] = text
        send_mqtt_alert(chat_id, user_name, user_id, session["fireType"], text, session["lat"], session["lng"])
        return "OK", 200

    reply(chat_id, "🤖 Send a GPS-tagged photo or use `/fire`.", markdown=True)
    return "OK", 200

# === RUN ===
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=int(os.environ.get("PORT", 5000)))
