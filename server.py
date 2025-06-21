import os
from flask import Flask, request, jsonify
from flask_cors import CORS
import subprocess

app = Flask(__name__)

# Get environment variables
PORT = int(os.environ.get('PORT', 5000))
DEBUG = os.environ.get('FLASK_ENV') == 'development'

# Production-ready CORS configuration
allowed_origins = [
    "http://localhost:5173",  # Development
    "http://localhost:3000",  # Alternative dev port
    os.environ.get('FRONTEND_URL', '')  # Production frontend URL
]

# Filter out empty strings
allowed_origins = [origin for origin in allowed_origins if origin]

CORS(app, resources={
    r"/trigger-sos": {
        "origins": allowed_origins,
        "methods": ["POST", "OPTIONS"],
        "allow_headers": ["Content-Type"],
        "supports_credentials": True
    },
    r"/recognize": {
        "origins": allowed_origins,
        "allow_headers": ["Content-Type"],
        "methods": ["POST", "OPTIONS"],
        "supports_credentials": True
    }
})

def get_allowed_origin():
    """Get the allowed origin for the current request"""
    origin = request.headers.get('Origin')
    if origin in allowed_origins:
        return origin
    return allowed_origins[0] if allowed_origins else '*'

@app.route('/', methods=['GET'])
def root():
    """Root endpoint for health checks"""
    return jsonify({"status": "FIRELINX API is running", "service": "FIRELINX API"}), 200

@app.route('/health', methods=['GET'])
def health_check():
    """Health check endpoint for deployment"""
    return jsonify({"status": "healthy", "service": "FIRELINX API"}), 200

@app.route('/trigger-sos', methods=['POST', 'OPTIONS'])
def trigger_sos():
    allowed_origin = get_allowed_origin()
    
    if request.method == 'OPTIONS':
        response = jsonify({"status": "preflight"})
        response.headers.add('Access-Control-Allow-Origin', allowed_origin)
        response.headers.add('Access-Control-Allow-Methods', 'POST, OPTIONS')
        response.headers.add('Access-Control-Allow-Headers', 'Content-Type')
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 200
    
    try:
        result = subprocess.run(['python', 'sos.py'], 
                              check=True, 
                              capture_output=True, 
                              text=True,
                              timeout=30)  # Add timeout for production
        response = jsonify({
            "status": "success", 
            "message": "SOS triggered successfully",
            "output": result.stdout
        })
        response.headers.add('Access-Control-Allow-Origin', allowed_origin)
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 200
    except subprocess.TimeoutExpired:
        response = jsonify({
            "status": "error", 
            "message": "SOS request timed out"
        })
        response.headers.add('Access-Control-Allow-Origin', allowed_origin)
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 408
    except subprocess.CalledProcessError as e:
        response = jsonify({
            "status": "error", 
            "message": "Failed to trigger SOS",
            "error": str(e.stderr)
        })
        response.headers.add('Access-Control-Allow-Origin', allowed_origin)
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 500

@app.route('/recognize', methods=['POST', 'OPTIONS'])
def trigger_recognize():
    allowed_origin = get_allowed_origin()
    
    if request.method == 'OPTIONS':
        response = jsonify({"status": "preflight"})
        response.headers.add('Access-Control-Allow-Origin', allowed_origin)
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 200
    
    try:
        result = subprocess.run(['python', 'capture_faces.py', '--recognize'], 
                              check=True, 
                              capture_output=True, 
                              text=True,
                              timeout=60)  # Add timeout for face recognition
        response = jsonify({
            "status": "success", 
            "message": "Face recognition completed",
            "data": result.stdout
        })
        response.headers.add('Access-Control-Allow-Origin', allowed_origin)
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 200
    except subprocess.TimeoutExpired:
        response = jsonify({
            "status": "error", 
            "message": "Face recognition timed out"
        })
        response.headers.add('Access-Control-Allow-Origin', allowed_origin)
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 408
    except subprocess.CalledProcessError as e:
        response = jsonify({
            "status": "error", 
            "message": str(e.stderr)
        })
        response.headers.add('Access-Control-Allow-Origin', allowed_origin)
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 500

if __name__ == '__main__':
    app.run(debug=DEBUG, host='0.0.0.0', port=PORT)
