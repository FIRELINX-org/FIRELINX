
from flask import Flask, request, jsonify
from flask_cors import CORS
import subprocess

app = Flask(__name__) 
CORS(app, 
     resources={
         r"/recognize": {
             "origins": "http://localhost:5173",
             "supports_credentials": True
         },
         r"/train": {
             "origins": "http://localhost:5173",
             "supports_credentials": True
         }
     },
     supports_credentials=True)


# Train Face Recognition Model
@app.route('/recognize', methods=['POST', 'OPTIONS'])
def trigger_recognize():
    if request.method == 'OPTIONS':
        # Handle preflight request
        response = jsonify({"status": "success"})
        response.headers.add("Access-Control-Allow-Headers", "Content-Type")
        response.headers.add("Access-Control-Allow-Methods", "POST")
        return response
    
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
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 200
    except subprocess.CalledProcessError as e:
        response = jsonify({
            "status": "error", 
            "message": str(e.stderr)
        })
        response.headers.add('Access-Control-Allow-Credentials', 'true')
        return response, 500



    
if __name__ == '__main__':
    app.run(debug=True)
