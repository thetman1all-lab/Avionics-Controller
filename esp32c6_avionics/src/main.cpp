// #include <Arduino.h>
// #include <Wire.h>
// #include <Adafruit_MPU6050.h>
// #include <Adafruit_Sensor.h>
// #include <ESP32Servo.h>

// // Pins
// #define SDA_PIN 0
// #define SCL_PIN 14
// #define SERVO1_PIN 2  // Pitch
// #define SERVO2_PIN 3  // Yaw

// Adafruit_MPU6050 mpu;
// Servo servoPitch;
// Servo servoYaw;

// // Attitude
// float pitch = 0.0, roll = 0.0, yaw = 0.0;
// unsigned long lastTime = 0;
// const float alpha = 0.98;

// // PID
// float Kp = 1.5, Ki = 0.05, Kd = 0.3;
// float pitchIntegral = 0, rollIntegral = 0, yawIntegral = 0;
// float lastPitchError = 0, lastRollError = 0, lastYawError = 0;

// // Zeroing & collection
// bool zeroing = false;
// bool collecting = false;
// float pitchOffset = 0, rollOffset = 0, yawOffset = 0;
// unsigned long zeroStart = 0;
// const unsigned long zeroDuration = 2000;

// // Servo
// float servoNeutral = 90.0;
// float maxDeflection = 30.0;
// bool sTurnEnabled = true;
// float sTurnAmplitude = 5.0;
// unsigned long sTurnPeriod = 2000;
// unsigned long sTurnStart = 0;

// // Serial command buffer
// String serialCmd = "";

// void processCommand(String cmd) {
//   cmd.trim();
//   if (cmd == "start") {
//     collecting = true;
//     zeroing = true;
//     zeroStart = millis();
//     pitchIntegral = rollIntegral = yawIntegral = 0;
//     lastPitchError = lastRollError = lastYawError = 0;
//     Serial.println("{\"status\":\"started\",\"zeroing\":true}");
//   } else if (cmd == "stop") {
//     collecting = false;
//     zeroing = false;
//     Serial.println("{\"status\":\"stopped\"}");
//   } else if (cmd.startsWith("gains:")) {
//     int c1 = cmd.indexOf(',');
//     int c2 = cmd.indexOf(',', c1 + 1);
//     if (c1 > 0 && c2 > c1) {
//       Kp = cmd.substring(6, c1).toFloat();
//       Ki = cmd.substring(c1 + 1, c2).toFloat();
//       Kd = cmd.substring(c2 + 1).toFloat();
//       Serial.printf("{\"status\":\"gains\",\"kp\":%.2f,\"ki\":%.3f,\"kd\":%.2f}\n", Kp, Ki, Kd);
//     }
//   }
// }

// void setup() {
//   Serial.begin(115200);
//   unsigned long start = millis();
//   while (!Serial && (millis() - start < 3000)) delay(10);
//   delay(300);
//   Serial.println("{\"status\":\"ready\",\"msg\":\"ESP32-C6 Avionics serial ready\"}");

//   Wire.begin(SDA_PIN, SCL_PIN);
//   if (!mpu.begin(0x68, &Wire)) {
//     Serial.println("{\"status\":\"warn\",\"msg\":\"MPU6050 not found\"}");
//   } else {
//     mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
//     mpu.setGyroRange(MPU6050_RANGE_500_DEG);
//     mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
//   }

//   ESP32PWM::allocateTimer(0);
//   ESP32PWM::allocateTimer(1);
//   ESP32PWM::allocateTimer(2);
//   ESP32PWM::allocateTimer(3);
//   servoPitch.setPeriodHertz(50);
//   servoYaw.setPeriodHertz(50);
//   servoPitch.attach(SERVO1_PIN, 500, 2400);
//   servoYaw.attach(SERVO2_PIN, 500, 2400);
//   servoPitch.write(servoNeutral);
//   servoYaw.write(servoNeutral);

//   lastTime = micros();
//   sTurnStart = millis();
// }

// void loop() {
//   // LED heartbeat
//   static unsigned long lastBlink = 0;
//   static bool ledOn = false;
//   if (millis() - lastBlink >= 1000) {
//     ledOn = !ledOn;
//     rgbLedWrite(RGB_BUILTIN, 0, ledOn ? 64 : 0, 0);
//     lastBlink = millis();
//   }

//   // Read serial commands
//   while (Serial.available()) {
//     char c = Serial.read();
//     if (c == '\n' || c == '\r') {
//       if (serialCmd.length() > 0) {
//         processCommand(serialCmd);
//         serialCmd = "";
//       }
//     } else {
//       serialCmd += c;
//     }
//   }

//   sensors_event_t a, g, temp;
//   bool gotData = mpu.getEvent(&a, &g, &temp);

//   unsigned long now = micros();
//   float dt = (now - lastTime) / 1000000.0f;
//   if (dt > 0.1f) dt = 0.02f;
//   lastTime = now;

//   if (gotData) {
//     float accelPitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
//     float accelRoll = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
//     float gyroPitch = g.gyro.y * 180.0 / PI;
//     float gyroRoll = g.gyro.x * 180.0 / PI;
//     float gyroYaw = g.gyro.z * 180.0 / PI;

//     pitch = alpha * (pitch + gyroPitch * dt) + (1 - alpha) * accelPitch;
//     roll = alpha * (roll + gyroRoll * dt) + (1 - alpha) * accelRoll;
//     yaw += gyroYaw * dt;

//     if (zeroing && millis() - zeroStart < zeroDuration) {
//       pitchOffset = pitch;
//       rollOffset = roll;
//       yawOffset = yaw;
//     } else if (zeroing) {
//       zeroing = false;
//       Serial.println("{\"status\":\"zeroed\"}");
//     }

//     float p = pitch - pitchOffset;
//     float r = roll - rollOffset;
//     float y = yaw - yawOffset;

//     float pitchError = -p;
//     float rollError = -r;
//     float yawError = -y;
//     pitchIntegral += pitchError * dt;
//     rollIntegral += rollError * dt;
//     yawIntegral += yawError * dt;
//     float pitchDeriv = (pitchError - lastPitchError) / dt;
//     float rollDeriv = (rollError - lastRollError) / dt;
//     float yawDeriv = (yawError - lastYawError) / dt;
//     lastPitchError = pitchError;
//     lastRollError = rollError;
//     lastYawError = yawError;

//     float pitchOut = Kp * pitchError + Ki * pitchIntegral + Kd * pitchDeriv;
//     float yawOut = Kp * yawError + Ki * yawIntegral + Kd * yawDeriv;

//     float sTurn = 0;
//     if (sTurnEnabled) {
//       float phase = 2 * PI * (millis() - sTurnStart) / (float)sTurnPeriod;
//       sTurn = sTurnAmplitude * sin(phase);
//     }

//     float pitchCmd = servoNeutral + constrain(pitchOut, -maxDeflection, maxDeflection);
//     float yawCmd = servoNeutral + constrain(yawOut + sTurn, -maxDeflection, maxDeflection);
//     servoPitch.write(pitchCmd);
//     servoYaw.write(yawCmd);

//     if (collecting) {
//       Serial.printf("{\"t\":%lu,\"p\":%.2f,\"y\":%.2f,\"r\":%.2f,\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"pc\":%.1f,\"yc\":%.1f,\"st\":%.2f,\"zeroing\":%s}\n",
//                     millis(), p, y, r,
//                     a.acceleration.x / 9.81, a.acceleration.y / 9.81, a.acceleration.z / 9.81,
//                     pitchCmd, yawCmd, sTurn, zeroing ? "true" : "false");
//     }
//   }

//   delay(20);
// }

#include <Arduino.h>

#define SERVO_PIN 3   // try 2, then 3, then 4 if needed

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000)) delay(10);
  delay(500);
  Serial.println("Corrected servo test");

  ledcAttach(SERVO_PIN, 50, 14);   // 50 Hz, 14-bit
  Serial.println("Attached");
}

void loop() {
  // Center (1500 µs)
  uint32_t duty = (1500UL * 16383UL) / 20000UL;
  ledcWrite(SERVO_PIN, duty);
  Serial.printf("Center  duty=%u\n", duty);
  delay(2000);

  // One end (~1000 µs)
  duty = (1000UL * 16383UL) / 20000UL;
  ledcWrite(SERVO_PIN, duty);
  Serial.printf("Min     duty=%u\n", duty);
  delay(2000);

  // Other end (~2000 µs)
  duty = (2000UL * 16383UL) / 20000UL;
  ledcWrite(SERVO_PIN, duty);
  Serial.printf("Max     duty=%u\n", duty);
  delay(2000);
}