import os
import json
import sqlite3
import serial
import time
import threading
from flask import Flask, render_template, request
from flask_socketio import SocketIO

app = Flask(__name__)
app.config['SECRET_KEY'] = 'secret!'
socketio = SocketIO(app, async_mode=None)

# Automatické vytvorenie priečinka pre súbor, aby to nepadlo
os.makedirs('static/files', exist_ok=True)

# Inicializácia databázy
def init_db():
    conn = sqlite3.connect('merania.db')
    c = conn.cursor()
    # Vytvorí tabuľku, ak neexistuje
    c.execute('''CREATE TABLE IF NOT EXISTS zaznamy (id INTEGER PRIMARY KEY AUTOINCREMENT, data TEXT)''')
    conn.commit()
    conn.close()

init_db()

# Globálne premenné
thread = None
thread_lock = threading.Lock()
is_running = False
ser = None

# Regulačné parametre (predvolené)
SETPOINT = 20.0
Kp = 50.0
data_log = []

def background_thread():
    global is_running, ser, data_log, SETPOINT, Kp
   
    while True:
        if ser is not None and ser.is_open and ser.in_waiting > 0:
            try:
                line = ser.readline().decode('utf-8').strip()
                if "," in line:
                    parts = line.split(",")
                    temp = float(parts[0])-5.0
                    rpm = int(parts[1])
                   
                    pwm_out = 0
                    if is_running:
                        odchylka = temp - SETPOINT
                        if odchylka > 0:
                            pwm_out = int(odchylka * Kp)
                            if pwm_out > 255:
                                pwm_out = 255
                        else:
                            pwm_out = 0
                           
                        # Ukladanie do poľa len ak beží regulácia
                        data_log.append({"temp": temp, "rpm": rpm, "pwm": int((pwm_out/255)*100)})
                   
                    # Odoslanie do Arduina (výkon 0-255)
                    ser.write(f"{pwm_out}\n".encode('utf-8'))
                   
                    # Odoslanie na Web (výkon v %)
                    socketio.emit('my_response', {
                        'temp': temp,
                        'rpm': rpm,
                        'pwm': int((pwm_out/255)*100)
                    }, namespace='/test')
                   
            except Exception as e:
                print(f"Chyba sériového portu: {e}")
               
        socketio.sleep(0.1)

@app.route('/')
def index():
    return render_template('tabs.html')

# Cesta pre čítanie z databázy (Bod 7)
@app.route('/read_db/<int:zaznam_id>')
def read_db(zaznam_id):
    conn = sqlite3.connect('merania.db')
    c = conn.cursor()
    c.execute("SELECT data FROM zaznamy WHERE id=?", (zaznam_id,))
    rv = c.fetchone()
    conn.close()
    if rv:
        return rv[0]
    return "[]"

# Cesta pre čítanie zo súboru (Bod 8)
@app.route('/read_file/<int:line_num>')
def read_file(line_num):
    try:
        with open('static/files/namerane_hodnoty.txt', 'r') as f:
            lines = f.readlines()
            if 0 <= line_num - 1 < len(lines):
                return lines[line_num - 1].strip()
    except:
        pass
    return "[]"

# --- SOCKET IO EVENTY Z WEBU ---

@socketio.on('open', namespace='/test')
def open_connection():
    global ser, thread
    print("Pripájam Arduino...")
    try:
        # Vyskúša USB0, ak nefunguje, skúsi ACM0
        ser = serial.Serial('/dev/ttyUSB0', 9600, timeout=1)
    except:
        try:
            ser = serial.Serial('/dev/ttyACM0', 9600, timeout=1)
        except Exception as e:
            print(f"Nepodarilo sa pripojiť Arduino: {e}")
            return
           
    time.sleep(2) # Čakanie na reštart Arduina po pripojení
    with thread_lock:
        if thread is None:
            thread = socketio.start_background_task(background_thread)

@socketio.on('close', namespace='/test')
def close_connection():
    global ser
    print("Odpájam Arduino...")
    if ser is not None and ser.is_open:
        ser.write(b"0\n") # Bezpečné vypnutie ventilátora pred zatvorením
        ser.close()

@socketio.on('set_params', namespace='/test')
def set_params(msg):
    global SETPOINT, Kp
    SETPOINT = float(msg['setpoint'])
    Kp = float(msg['kp'])
    print(f"Parametre zmenené: Žiadaná teplota={SETPOINT}°C, Kp={Kp}")

@socketio.on('toggle_state', namespace='/test')
def toggle_state(msg):
    global is_running, data_log, ser
   
    if msg['state']: # ŠTART
        is_running = True
        data_log = []
        print("Regulácia odštartovaná.")
    else:            # STOP
        is_running = False
        if ser is not None and ser.is_open:
            ser.write(b"0\n")
           
        if len(data_log) > 0:
            json_data = json.dumps(data_log)
            # Uloženie do Databázy
            conn = sqlite3.connect('merania.db')
            c = conn.cursor()
            c.execute("INSERT INTO zaznamy (data) VALUES (?)", (json_data,))
            conn.commit()
            conn.close()
            # Uloženie do textového súboru
            with open('static/files/namerane_hodnoty.txt', 'a') as f:
                f.write(json_data + '\n')
            print("Záznam úspešne uložený do DB aj súboru.")

if __name__ == '__main__':
    socketio.run(app, host="0.0.0.0", port=80, debug=True)