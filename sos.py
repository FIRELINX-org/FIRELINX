from twilio.rest import Client
import requests
import webbrowser
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from datetime import datetime

# Twilio credentials
TWILIO_ACCOUNT_SID = 'AC77ed9afe70adaea1e2240186e4318422'
TWILIO_AUTH_TOKEN = 'c34ecb8eec88f7997bd3e204540934ee'
TWILIO_PHONE_NUMBER = '+15632782223'
RECIPIENT_PHONE_NUMBER = '+917384228365'

# Email credentials
EMAIL_SENDER = 'firelinx33@gmail.com'
EMAIL_PASSWORD = 'yqkq zgho yowt ijln'
EMAIL_RECIPIENTS = [
    'swarajit19082003@gmail.com',
    'adrijamethodist@gmail.com',
    'koyelisha7@gmail.com',
    'shyantan5@gmail.com'
]

# Function to get current timestamp
def get_current_timestamp():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

def generate_sos_message():
    return f"""
Fire Alert!
Timestamp: {get_current_timestamp()}
Fire Intensity: Medium
Fire Cause: Electricity
"""

def get_gps_coordinates():
    try:
        response = requests.get('https://ipinfo.io')
        if response.status_code == 200:
            data = response.json()
            location = data.get('loc', '').split(',')
            if len(location) == 2:
                return float(location[0]), float(location[1])
        return None, None
    except Exception as e:
        print(f"Failed to get GPS coordinates: {e}")
        return None, None

def generate_google_maps_link(latitude, longitude):
    if latitude and longitude:
        return f"https://www.google.com/maps?q={latitude},{longitude}&z=15"
    return None

def send_sms(latitude=None, longitude=None, manual_location=None):
    try:
        base_message = generate_sos_message()
        
        if latitude and longitude:
            location_info = f"Fire location: Latitude: {latitude}, Longitude: {longitude}"
            maps_link = generate_google_maps_link(latitude, longitude)
            full_message = f"{base_message}{location_info}\nGoogle Maps: {maps_link}"
        elif manual_location:
            full_message = f"{base_message}Fire location: {manual_location}"
        else:
            full_message = f"{base_message}Fire location: Location not available"

        client = Client(TWILIO_ACCOUNT_SID, TWILIO_AUTH_TOKEN)
        message = client.messages.create(
            body=full_message,
            from_=TWILIO_PHONE_NUMBER,
            to=RECIPIENT_PHONE_NUMBER
        )
        print(f"SMS sent: {message.sid}")
        return {"status": "success", "message": "SMS alert sent successfully!"}
    except Exception as e:
        print(f"Failed to send SMS: {e}")
        return {"status": "error", "message": f"Failed to send SMS alert: {str(e)}"}

def send_email(latitude=None, longitude=None, manual_location=None):
    try:
        subject = f"FIRE Alert - {get_current_timestamp()}"
        base_message = generate_sos_message()
        
        if latitude and longitude:
            location_info = f"Fire location: Latitude: {latitude}, Longitude: {longitude}"
            maps_link = generate_google_maps_link(latitude, longitude)
            body = f"{base_message}{location_info}\nGoogle Maps: {maps_link}"
        elif manual_location:
            body = f"{base_message}Fire location: {manual_location}"
        else:
            body = f"{base_message}Fire location: Location not available"

        msg = MIMEMultipart()
        msg['From'] = EMAIL_SENDER
        msg['To'] = ", ".join(EMAIL_RECIPIENTS)  # Show all recipients in To field
        msg['Subject'] = subject
        msg.attach(MIMEText(body, 'plain'))

        with smtplib.SMTP('smtp.gmail.com', 587) as server:
            server.starttls()
            server.login(EMAIL_SENDER, EMAIL_PASSWORD)
            server.sendmail(EMAIL_SENDER, EMAIL_RECIPIENTS, msg.as_string())

        print(f"Email sent successfully to {len(EMAIL_RECIPIENTS)} recipients!")
        return {"status": "success", "message": f"Email alert sent to {len(EMAIL_RECIPIENTS)} recipients"}
    except Exception as e:
        print(f"Failed to send email: {e}")
        return {"status": "error", "message": f"Failed to send email alerts: {str(e)}"}

def sos_button_click():
    latitude, longitude = get_gps_coordinates()

    if latitude and longitude:
        sms_result = send_sms(latitude, longitude)
        email_result = send_email(latitude, longitude)
        maps_link = generate_google_maps_link(latitude, longitude)
        if maps_link:
            webbrowser.open(maps_link)
        else:
            print("Failed to generate Google Maps link.")
        return sms_result, email_result
    else:
        manual_location = input("Unable to fetch location. Enter location manually: ")
        if manual_location:
            sms_result = send_sms(manual_location=manual_location)
            email_result = send_email(manual_location=manual_location)
        else:
            sms_result = send_sms()
            email_result = send_email()
        return sms_result, email_result

if __name__ == "__main__":
    sos_button_click()