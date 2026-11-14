#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESP32Servo.h>

// Pins
#define SDA_PIN 0
#define SCL_PIN 14
#define SERVO_LEFT_PIN 2   // Left fin
#define SERVO_RIGHT_PIN 3  // Right fin

Adafruit_MPU6050 mpu;
Servo servoLeft;
Servo servoRight;

// Attitude
float pitch = 0.0, roll = 0.0, yaw = 0.0;
unsigned long lastTime = 0;
const float alpha = 0.98;

// PID
float Kp = 1.5, Ki = 0.05, Kd = 0.3;
float pitchIntegral = 0, rollIntegral = 0, yawIntegral = 0;
float lastPitchError = 0, lastRollError = 0, lastYawError = 0;

// States
bool armed = false;
bool zeroing = false;
bool collecting = false;
bool descentMode = false;   // false = Ascent, true = Descent
float pitchOffset = 0, rollOffset = 0, yawOffset = 0;
unsigned long zeroStart = 0;
const unsigned long zeroDuration = 2000;

// Servo
const float servoNeutral = 90.0;
const float maxDeflection = 30.0;
const float yawMixGain = 0.5;   // equal weight into pitch and roll

// Serial command buffer
String serialCmd = "";

void setServosNeutral() {
  servoLeft.write(servoNeutral);
  servoRight.write(servoNeutral);
}

void processCommand(String cmd) {
  cmd.trim();
  if (cmd == "arm") {
    armed = true;
    collecting = true;
    zeroing = true;
    zeroStart = millis();
    pitchIntegral = rollIntegral = yawIntegral = 0;
    lastPitchError = lastRollError = lastYawError = 0;
    setServosNeutral();
    Serial.println("{\"status\":\"armed\",\"zeroing\":true}");
  } else if (cmd == "disarm" || cmd == "stop") {
    armed = false;
    collecting = false;
    zeroing = false;
    setServosNeutral();
    Serial.println("{\"status\":\"disarmed\"}");
  } else if (cmd == "ascent") {
    descentMode = false;
    Serial.println("{\"status\":\"mode\",\"mode\":\"ascent\"}");
  } else if (cmd == "descent") {
    descentMode = true;
    Serial.println("{\"status\":\"mode\",\"mode\":\"descent\"}");
  } else if (cmd.startsWith("gains:")) {
    int c1 = cmd.indexOf(',');
    int c2 = cmd.indexOf(',', c1 + 1);
    if (c1 > 0 && c2 > c1) {
      Kp = cmd.substring(6, c1).toFloat();
      Ki = cmd.substring(c1 + 1, c2).toFloat();
      Kd = cmd.substring(c2 + 1).toFloat();
      Serial.printf("{\"status\":\"gains\",\"kp\":%.2f,\"ki\":%.3f,\"kd\":%.2f}\n", Kp, Ki, Kd);
    }
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000)) delay(10);
  delay(300);
  Serial.println("{\"status\":\"ready\",\"msg\":\"ESP32-C6 Avionics serial ready\"}");

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!mpu.begin(0x68, &Wire)) {
    Serial.println("{\"status\":\"warn\",\"msg\":\"MPU6050 not found\"}");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servoLeft.setPeriodHertz(50);
  servoRight.setPeriodHertz(50);
  servoLeft.attach(SERVO_LEFT_PIN, 500, 2400);
  servoRight.attach(SERVO_RIGHT_PIN, 500, 2400);
  setServosNeutral();

  lastTime = micros();
}

void loop() {
  // LED heartbeat
  static unsigned long lastBlink = 0;
  static bool ledOn = false;
  if (millis() - lastBlink >= 1000) {
    ledOn = !ledOn;
    rgbLedWrite(RGB_BUILTIN, 0, ledOn ? 64 : 0, 0);
    lastBlink = millis();
  }

  // Read serial commands
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialCmd.length() > 0) {
        processCommand(serialCmd);
        serialCmd = "";
      }
    } else {
      serialCmd += c;
    }
  }

  sensors_event_t a, g, temp;
  bool gotData = mpu.getEvent(&a, &g, &temp);

  unsigned long now = micros();
  float dt = (now - lastTime) / 1000000.0f;
  if (dt > 0.1f) dt = 0.02f;
  lastTime = now;

  if (gotData) {
    float accelPitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
    float accelRoll = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
    float gyroPitch = g.gyro.y * 180.0 / PI;
    float gyroRoll = g.gyro.x * 180.0 / PI;
    float gyroYaw = g.gyro.z * 180.0 / PI;

    pitch = alpha * (pitch + gyroPitch * dt) + (1 - alpha) * accelPitch;
    roll = alpha * (roll + gyroRoll * dt) + (1 - alpha) * accelRoll;
    yaw += gyroYaw * dt;

    if (zeroing && millis() - zeroStart < zeroDuration) {
      pitchOffset = pitch;
      rollOffset = roll;
      yawOffset = yaw;
    } else if (zeroing) {
      zeroing = false;
      Serial.println("{\"status\":\"zeroed\"}");
    }

    float p = pitch - pitchOffset;
    float r = roll - rollOffset;
    float y = yaw - yawOffset;

    float pitchError = -p;
    float rollError = -r;
    float yawError = -y;
    pitchIntegral += pitchError * dt;
    rollIntegral += rollError * dt;
    yawIntegral += yawError * dt;
    float pitchDeriv = (pitchError - lastPitchError) / dt;
    float rollDeriv = (rollError - lastRollError) / dt;
    float yawDeriv = (yawError - lastYawError) / dt;
    lastPitchError = pitchError;
    lastRollError = rollError;
    lastYawError = yawError;

    float pitchOut = Kp * pitchError + Ki * pitchIntegral + Kd * pitchDeriv;
    float rollOut  = Kp * rollError  + Ki * rollIntegral  + Kd * rollDeriv;
    float yawOut   = Kp * yawError   + Ki * yawIntegral   + Kd * yawDeriv;

    // Mix yaw equally into pitch and roll
    float pitchCmd = pitchOut + yawOut * yawMixGain;
    float rollCmd  = rollOut  + yawOut * yawMixGain;

    // Ascent/Descent mirror: Descent inverts both
    if (descentMode) {
      pitchCmd = -pitchCmd;
      rollCmd  = -rollCmd;
    }

    // Map to servo angles (pitch and roll were swapped; corrected)
    // Positive pitch → both outer edges up → both angles decrease below 90 (common mode)
    // Positive roll  → differential
    float leftAngle  = servoNeutral - constrain(rollCmd + pitchCmd, -maxDeflection, maxDeflection);
    float rightAngle = servoNeutral - constrain(rollCmd - pitchCmd, -maxDeflection, maxDeflection);

    if (armed && !zeroing) {
      servoLeft.write(leftAngle);
      servoRight.write(rightAngle);
    } else {
      setServosNeutral();
    }

    if (collecting) {
      Serial.printf("{\"t\":%lu,\"p\":%.2f,\"y\":%.2f,\"r\":%.2f,\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"lc\":%.1f,\"rc\":%.1f,\"zeroing\":%s,\"armed\":%s,\"descent\":%s}\n",
                    millis(), p, y, r,
                    a.acceleration.x / 9.81, a.acceleration.y / 9.81, a.acceleration.z / 9.81,
                    leftAngle, rightAngle,
                    zeroing ? "true" : "false",
                    armed ? "true" : "false",
                    descentMode ? "true" : "false");
    }
  }

  delay(20);
}
