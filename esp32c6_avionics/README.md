# ESP32-C6 Avionics – Serial Ground Station

## On the board (ESP32-C6)
Upload the PlatformIO project. The board:
- Reads the MPU-6050
- Runs complementary filter + full PID
- Drives two servos (pitch + yaw/S-turn)
- Streams compact JSON over USB serial at 115200 when collecting
- Accepts commands: `start`, `stop`, `gains:Kp,Ki,Kd`
- Blinks the onboard green LED once per second

## On the laptop (ground station)
```bash
cd esp32c6_avionics
pip install -r requirements.txt
python ground_station.py
```
Then open **http://localhost:5001** in a browser.

### Usage
1. Refresh Ports and select the ESP serial port (usually something like `/dev/cu.usbmodem*` or `COM3`).
2. Confirm baud rate (default 115200) and click **Connect**.
3. Adjust PID sliders if desired.
4. Click **Start Collection** → the page shows “Keep the board still while it zeros out” for ~2 s, then live graphs and numeric accel begin.
5. **Stop Collection** any time (connection stays up).
6. **Download CSV** at any point (labeled columns, data accumulated in the browser).

The CSV and graphs are handled entirely on the laptop, so the flight computer only does control and a light serial stream.
