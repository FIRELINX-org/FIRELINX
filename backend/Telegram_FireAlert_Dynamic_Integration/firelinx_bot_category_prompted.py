import os
import re
import requests
from flask import Flask, request
from paddleocr import PaddleOCR
from paho.mqtt import client as mqtt_client
from dotenv import load_dotenv
from PIL import Image
from io import BytesIO

# Load .env
load_dotenv()
BOT_TOKEN = os.getenv("BOT_TOKEN")
MQTT_BROKER = os.getenv("MQTT_BROKER")
MQTT_PORT = int(os.getenv("MQTT_PORT"))
MQTT_TOPIC = os.getenv("MQTT_TOPIC")
MQTT_CLIENT_ID = os.getenv("MQTT_CLIENT_ID")

app = Flask(__name__)
ocr = PaddleOCR(use_angle_cls=True, lang='en')

mqtt_client = mqtt_client.Client(MQTT_CLIENT_ID)
mqtt_client.connect(MQTT_BROKER, MQTT_PORT)

def extract_lat_long_from_text(text):
    lat_match = re.search(r'Lat(?:itude)?:?\s*([0-9]+\.[0-9]+)', text, re.I)
    long_match = re.search(r'Long(?:itude)?:?\s*([0-9]+\.[0-9]+)', text, re.I)
    return (lat_match.group(1), long_match.group(1)) if lat_match and long_match else (None, None)

def send_mqtt_alert(fire_type, intensity, user, lat, lon):
    import datetime
    now = datetime.datetime.now()
    msg = {
        "command": "fire_alert",
        "payload": {
            "fireType": fire_type,
            "fireIntensity": intensity,
            "verified": True,
            "user": user,
            "userID": "TG",
            "stnID": "TG",
            "latitude": f"{lat}°N",
            "longitude": f"{lon}°E",
            "date": now.strftime("%d/%m"),
            "time": now.strftime("%H:%M:%S")
        }
    }
    mqtt_client.publish(MQTT_TOPIC, str(msg))

@app.route(f"/webhook/{BOT_TOKEN}", methods=["POST"])
def webhook():
    data = request.get_json()
    print("📩 Telegram Data:", data)

    message = data.get("message", {})
    chat_id = message.get("chat", {}).get("id")

    if 'text' in message and message['text'].startswith("/fire"):
        parts = message['text'].split()
        if len(parts) == 5:
            _, ftype, intensity, lat, lon = parts
            send_mqtt_alert(ftype.upper(), intensity, message['from']['first_name'], lat, lon)
            return send_reply(chat_id, f"🔥 Fire alert sent!\nType: {ftype}, Intensity: {intensity}, Location: {lat}, {lon}")
        else:
            return send_reply(chat_id, "⚠️ Format: `/fire B 3 22.57 88.36`", "Markdown")

    elif 'photo' in message:
        file_id = message['photo'][-1]['file_id']
        file_path = get_file_path(file_id)
        image_data = download_image(file_path)
        lat, lon = ocr_lat_lon(image_data)

        if lat and lon:
            return send_reply(chat_id, f"📷 Location extracted!\nLat: {lat}, Lon: {lon}\n\nNow use `/fire B 3 {lat} {lon}` to report fire.")
        else:
            return send_reply(chat_id, "⚠️ Couldn't extract Lat/Long from image. Use `/fire` manually.")

    elif 'text' in message and message['text'] == "/start":
        return send_reply(chat_id, "👋 *Welcome to FireLinx!*\n\n📷 Send a GPS-tagged image with `Lat` and `Long`\nOr use `/fire B 3 22.5726 88.3639`", "Markdown")

    return "ok"

def get_file_path(file_id):
    res = requests.get(f"https://api.telegram.org/bot{BOT_TOKEN}/getFile?file_id={file_id}").json()
    return res["result"]["file_path"]

def download_image(file_path):
    url = f"https://api.telegram.org/file/bot{BOT_TOKEN}/{file_path}"
    response = requests.get(url)
    return Image.open(BytesIO(response.content)).convert("RGB")

def ocr_lat_lon(img):
    result = ocr.ocr(np.array(img), cls=True)
    text = " ".join([line[1][0] for block in result for line in block])
    print("📜 OCR Text:", text)
    return extract_lat_long_from_text(text)

def send_reply(chat_id, text, mode=None):
    payload = {
        "chat_id": chat_id,
        "text": text,
    }
    if mode:
        payload["parse_mode"] = mode
    response = requests.post(f"https://api.telegram.org/bot{BOT_TOKEN}/sendMessage", json=payload)
    print("📬 Telegram API response:", response.text)
    return "ok"

@app.route("/ping", methods=["GET"])
def ping():
    return "pong", 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
