import os
import io
import numpy as np
import tensorflow as tf
from PIL import Image
from flask import Flask, request, jsonify
import logging
import socket

# --- HIDE FLASK REQUEST LOGS ---
log = logging.getLogger('werkzeug')
log.setLevel(logging.ERROR)

# --- CONFIGURATION ---
MODEL_NAME = 'model_unquant_03.tflite'
CONFIDENCE_THRESHOLD = 60.0
class_names = ['onion', 'potato', 'no item'] 

app = Flask(__name__)

# GLOBAL STATE (The "Bank")
current_bill_amount = 0.0

# --- HELPER: GET IP ---
def get_ip_address():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

# --- LOAD MODEL ---
print("-" * 50)
print(f"🔄 Loading TFLite model: {MODEL_NAME}...")
try:
    current_folder = os.path.dirname(os.path.abspath(__file__))
    model_path = os.path.join(current_folder, MODEL_NAME)
    interpreter = tf.lite.Interpreter(model_path=model_path)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    input_shape = input_details[0]['shape']
    HEIGHT = input_shape[1]
    WIDTH = input_shape[2]
    print(f"✅ Model loaded! Expects input: {WIDTH}x{HEIGHT}")
except Exception as e:
    print(f"❌ FATAL ERROR: {e}")
    exit()

# --- ROUTE 1: DETECT OBJECT ---
@app.route('/detect', methods=['POST'])
def detect_object():
    try:
        if not request.data: return jsonify({"error": "No data"}), 400
        
        img = Image.open(io.BytesIO(request.data)).resize((WIDTH, HEIGHT))
        img_array = np.expand_dims(np.array(img, dtype=np.float32) / 255.0, axis=0)
        interpreter.set_tensor(input_details[0]['index'], img_array)
        interpreter.invoke()
        predictions = interpreter.get_tensor(output_details[0]['index'])[0]
        
        idx = np.argmax(predictions)
        conf = (np.max(predictions) if np.max(predictions) <= 1.0 else (np.exp(predictions)/np.sum(np.exp(predictions)))[idx]) * 100
        item = class_names[idx] if conf >= CONFIDENCE_THRESHOLD and idx < len(class_names) else "None"
        
        # Don't print detection log here to keep terminal clean
        return jsonify({"detected_item": item, "confidence": float(conf)})
    except Exception as e:
        return jsonify({"detected_item": "None", "error": str(e)}), 500

# --- ROUTE 2: ADD PRICE TO BILL (Correct Logic) ---
@app.route('/submit-price', methods=['POST'])
def receive_price():
    global current_bill_amount
    try:
        data = request.json
        item = data.get('item', 'Unknown')
        weight = data.get('weight', 0.0)
        
        # ESP32 sends the cost of THIS item
        item_price = data.get('price', 0.0) 

        # Server Adds it to the Total
        current_bill_amount += item_price 

        print("\n" + "="*35)
        print("          🧾 BILLING UPDATE          ")
        print("="*35)
        print(f" 📦 Item Added:  {item.upper()}")
        print(f" ⚖️  Weight:      {weight} g")
        print(f" 💵 Item Cost:   {item_price:.2f} Rs")
        print("-" * 35)
        print(f" 💰 TOTAL BILL:  {current_bill_amount:.2f} Rs")
        print("="*35 + "\n")
        
        # SEND NEW TOTAL BACK TO ESP32
        return jsonify({
            "status": "success", 
            "new_total_bill": current_bill_amount
        })

    except Exception as e:
        print(f"Billing Error: {e}")
        return jsonify({"status": "error"}), 500

# --- ROUTE 3: GET BILL (For ESP8266) ---
@app.route('/get-bill', methods=['GET'])
def send_bill_to_esp8266():
    global current_bill_amount
    return jsonify({"total_bill": current_bill_amount})

# --- ROUTE 4: PAY BILL (RFID) ---
@app.route('/process-payment', methods=['POST'])
def process_payment():
    global current_bill_amount
    try:
        data = request.json
        rfid_tag = data.get('rfid_tag', 'Unknown')
        
        if current_bill_amount > 0.01:
            print("\n" + "$"*35)
            print("        ✅ PAYMENT SUCCESSFUL        ")
            print("$"*35)
            print(f" 💳 Card ID:     {rfid_tag}")
            print(f" 💵 Amount Paid: {current_bill_amount:.2f} Rs")
            print(f" 🧾 Bill Status: CLEARED (0.00)")
            print("$"*35 + "\n")
            
            current_bill_amount = 0.0 # RESET BILL
            return jsonify({"status": "success", "message": "Payment Accepted"})
        else:
            return jsonify({"status": "error", "message": "No Bill"})
            
    except Exception as e:
        return jsonify({"status": "error"}), 500

if __name__ == '__main__':
    my_ip = get_ip_address()
    print(f"🚀 SERVER STARTED at http://{my_ip}:5000")
    print("   (Update your ESP32 and ESP8266 with this IP)")
    app.run(host='0.0.0.0', port=5000, debug=False)