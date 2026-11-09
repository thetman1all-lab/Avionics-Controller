#!/usr/bin/env python3
"""
ESP32-C6 Avionics Ground Station
Reads serial from the board, serves a local dashboard at http://localhost:5000
"""

import serial
import serial.tools.list_ports
import json
import threading
import time
from flask import Flask, render_template_string, jsonify, request
from flask_socketio import SocketIO

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

ser = None
serial_thread = None
running = False
current_port = None
current_baud = 115200

HTML = r'''
<!DOCTYPE html>
<html>
<head>
  <title>Avionics Ground Station</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <script src="https://cdn.socket.io/4.7.2/socket.io.min.js"></script>
  <style>
    body { font-family: Arial, sans-serif; margin: 16px; background: #111; color: #eee; }
    h1 { color: #4CAF50; }
    .panel { background: #222; padding: 12px; margin: 10px 0; border-radius: 8px; }
    .charts { display: flex; flex-wrap: wrap; gap: 12px; }
    .chart-box { background: #1a1a1a; padding: 10px; border-radius: 6px; width: 32%; min-width: 280px; }
    button { padding: 8px 16px; margin: 4px; cursor: pointer; }
    label { margin-right: 8px; }
    input, select { padding: 4px; margin: 4px; }
    .zero { color: #ff9800; font-weight: bold; display: none; }
    #status { font-weight: bold; }
  </style>
</head>
<body>
  <h1>Avionics Ground Station (Serial)</h1>
  <div class="panel">
    <div id="status">Disconnected</div>
    <label>Port:</label>
    <select id="port"></select>
    <button onclick="refreshPorts()">Refresh Ports</button>
    <label>Baud:</label>
    <select id="baud">
      <option value="115200" selected>115200</option>
      <option value="57600">57600</option>
      <option value="38400">38400</option>
      <option value="9600">9600</option>
    </select>
    <button onclick="connect()">Connect</button>
    <button onclick="disconnect()">Disconnect</button>
  </div>
  <div class="panel">
    <div id="zeroMsg" class="zero">Keep the board still while it zeros out</div>
    <button onclick="sendCmd('start')">Start Collection</button>
    <button onclick="sendCmd('stop')">Stop Collection</button>
    <button onclick="downloadCSV()">Download CSV</button>
    <div>
      <label>Kp: <input type="range" id="kp" min="0" max="5" step="0.1" value="1.5" oninput="sendGains()"></label>
      <label>Ki: <input type="range" id="ki" min="0" max="1" step="0.01" value="0.05" oninput="sendGains()"></label>
      <label>Kd: <input type="range" id="kd" min="0" max="2" step="0.05" value="0.3" oninput="sendGains()"></label>
    </div>
  </div>
  <div class="panel" id="readouts">
    Accel X: <span id="ax">—</span> g (forward/back)<br>
    Accel Y: <span id="ay">—</span> g (left/right)<br>
    Accel Z: <span id="az">—</span> g (up/down)
  </div>
  <div class="charts">
    <div class="chart-box"><canvas id="pitchChart"></canvas></div>
    <div class="chart-box"><canvas id="yawChart"></canvas></div>
    <div class="chart-box"><canvas id="rollChart"></canvas></div>
  </div>
  <script>
    const socket = io();
    const maxPoints = 120;
    let csvRows = ["Time(ms),Pitch(deg),Yaw(deg),Roll(deg),AccelX(g),AccelY(g),AccelZ(g),PitchCmd,YawCmd,STurn"];

    function makeChart(id, label, color) {
      const ctx = document.getElementById(id).getContext('2d');
      return new Chart(ctx, {
        type: 'line',
        data: { labels: [], datasets: [{ label: label, data: [], borderColor: color, tension: 0.1, fill: false, pointRadius: 0 }] },
        options: {
          scales: { x: { title: { display: true, text: 'Samples' }, ticks: { display: false } }, y: { title: { display: true, text: 'Degrees' } } },
          animation: false,
          plugins: { legend: { labels: { color: '#eee' } } }
        }
      });
    }
    const pitchChart = makeChart('pitchChart', 'Pitch Angle (deg)', '#4CAF50');
    const yawChart = makeChart('yawChart', 'Yaw Angle (deg)', '#2196F3');
    const rollChart = makeChart('rollChart', 'Roll Angle (deg)', '#FF9800');

    function addPoint(chart, val) {
      chart.data.labels.push('');
      chart.data.datasets[0].data.push(val);
      if (chart.data.labels.length > maxPoints) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
      }
      chart.update('none');
    }

    socket.on('ports', ports => {
      const sel = document.getElementById('port');
      sel.innerHTML = '';
      ports.forEach(p => {
        const opt = document.createElement('option');
        opt.value = p;
        opt.text = p;
        sel.appendChild(opt);
      });
    });

    socket.on('status', msg => { document.getElementById('status').textContent = msg; });

    socket.on('data', d => {
      document.getElementById('zeroMsg').style.display = d.zeroing ? 'block' : 'none';
      addPoint(pitchChart, d.p);
      addPoint(yawChart, d.y);
      addPoint(rollChart, d.r);
      document.getElementById('ax').textContent = d.ax.toFixed(3);
      document.getElementById('ay').textContent = d.ay.toFixed(3);
      document.getElementById('az').textContent = d.az.toFixed(3);
      csvRows.push([d.t, d.p.toFixed(2), d.y.toFixed(2), d.r.toFixed(2), d.ax.toFixed(3), d.ay.toFixed(3), d.az.toFixed(3), d.pc.toFixed(1), d.yc.toFixed(1), d.st.toFixed(2)].join(','));
    });

    function refreshPorts() { socket.emit('list_ports'); }
    function connect() {
      const port = document.getElementById('port').value;
      const baud = parseInt(document.getElementById('baud').value);
      socket.emit('connect_serial', {port, baud});
    }
    function disconnect() { socket.emit('disconnect_serial'); }
    function sendCmd(cmd) { socket.emit('command', cmd); }
    function sendGains() {
      const kp = document.getElementById('kp').value;
      const ki = document.getElementById('ki').value;
      const kd = document.getElementById('kd').value;
      socket.emit('command', `gains:${kp},${ki},${kd}`);
    }
    function downloadCSV() {
      const blob = new Blob([csvRows.join('\\n')], {type: 'text/csv'});
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'avionics_log.csv';
      a.click();
    }
    refreshPorts();
  </script>
</body>
</html>
'''

@app.route('/')
def index():
    return render_template_string(HTML)

def serial_reader():
    global ser, running
    while running and ser and ser.is_open:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line.startswith('{'):
                try:
                    data = json.loads(line)
                    if 'p' in data:
                        socketio.emit('data', data)
                    else:
                        socketio.emit('status', json.dumps(data))
                except json.JSONDecodeError:
                    pass
        except Exception as e:
            socketio.emit('status', f'Serial error: {e}')
            break
        time.sleep(0.01)

@socketio.on('list_ports')
def list_ports():
    ports = [p.device for p in serial.tools.list_ports.comports()]
    socketio.emit('ports', ports)

@socketio.on('connect_serial')
def connect_serial(data):
    global ser, running, serial_thread, current_port, current_baud
    try:
        if ser and ser.is_open:
            ser.close()
        current_port = data['port']
        current_baud = int(data['baud'])
        ser = serial.Serial(current_port, current_baud, timeout=1)
        running = True
        serial_thread = threading.Thread(target=serial_reader, daemon=True)
        serial_thread.start()
        socketio.emit('status', f'Connected to {current_port} @ {current_baud}')
    except Exception as e:
        socketio.emit('status', f'Connect failed: {e}')

@socketio.on('disconnect_serial')
def disconnect_serial():
    global ser, running
    running = False
    if ser and ser.is_open:
        ser.close()
    socketio.emit('status', 'Disconnected')

@socketio.on('command')
def send_command(cmd):
    global ser
    if ser and ser.is_open:
        ser.write((cmd + '\n').encode())
        socketio.emit('status', f'Sent: {cmd}')

if __name__ == '__main__':
    print("Avionics Ground Station")
    print("Open http://localhost:5001 in your browser")
    try:
        socketio.run(app, host='0.0.0.0', port=5001, allow_unsafe_werkzeug=True)
    except OSError as e:
        if e.errno == 48:
            print("Port 5001 is also in use. Trying 5002...")
            socketio.run(app, host='0.0.0.0', port=5002, allow_unsafe_werkzeug=True)
        else:
            raise
