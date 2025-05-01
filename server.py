from flask import Flask, request, jsonify
from flask_cors import CORS
import subprocess

app = Flask(__name__)

# Configure CORS with explicit settings
CORS(app, resources={
    r"/trigger-sos": {
        "origins": ["http://localhost:5173", "*"],
        "methods": ["POST", "OPTIONS"],
        "allow_headers": ["Content-Type"],
        "supports_credentials": True
    },
    r"/recognize": {
        "origins": ["http://localhost:5173"],
        "allow_headers": ["Content-Type"],
        "methods": ["POST", "OPTIONS"],
        "supports_credentials": True
    }
})

@app.route('/trigger-sos', methods=['POST', 'OPTIONS'])
def trigger_sos():
    if request.method == 'OPTIONS':
        # Handle preflight request
        response = jsonify({"status": "preflight"})
        response.headers.add('Access-Control-Allow-Origin', 'http://localhost:5173')
        response.headers.add('Access-Control-Allow-Methods', 'POST, OPTIONS')
        response.headers.add('Access-Control-Allow-Headers', 'Content-Type')
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 200
    
    try:
        result = subprocess.run(['python', 'sos.py'], 
                              check=True, 
                              capture_output=True, 
                              text=True)
        response = jsonify({
            "status": "success", 
            "message": "SOS triggered successfully",
            "output": result.stdout
        })
        response.headers.add('Access-Control-Allow-Origin', 'http://localhost:5173')
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 200
    except subprocess.CalledProcessError as e:
        response = jsonify({
            "status": "error", 
            "message": "Failed to trigger SOS",
            "error": str(e.stderr)
        })
        response.headers.add('Access-Control-Allow-Origin', 'http://localhost:5173')
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 500

@app.route('/recognize', methods=['POST', 'OPTIONS'])
def trigger_recognize():
    if request.method == 'OPTIONS':
        response = jsonify({"status": "preflight"})
        response.headers.add('Access-Control-Allow-Origin', 'http://localhost:5173')
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 200
    
    try:
        result = subprocess.run(['python', 'capture_faces.py', '--recognize'], 
                              check=True, 
                              capture_output=True, 
                              text=True)
        response = jsonify({
            "status": "success", 
            "message": "Face recognition completed",
            "data": result.stdout
        })
        response.headers.add('Access-Control-Allow-Origin', 'http://localhost:5173')
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 200
    except subprocess.CalledProcessError as e:
        response = jsonify({
            "status": "error", 
            "message": str(e.stderr)
        })
        response.headers.add('Access-Control-Allow-Origin', 'http://localhost:5173')
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 500

if __name__ == '__main__':
    app.run(debug=True, port=5000)
