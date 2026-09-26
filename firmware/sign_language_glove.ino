#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------------------------
// Pins for 5 flex sensors
// ---------------------------
const int FLEX_PIN[5] = {34, 35, 32, 33, 25};

// ---------------------------
// Sensor calibration values
// ---------------------------
const int SAMPLE_COUNT = 20;
int baseline[5] = {0, 0, 0, 0};
int rawValue[5] = {0, 0, 0, 0, 0};

// ---------------------------
// Gesture detection settings
// ---------------------------
const int BEND_THRESHOLD = 100;       // adjust after calibration
const float TILT_THRESHOLD = 0.8f;    // yaw/roll threshold for hand orientation

Adafruit_MPU6050 mpu;

enum Gesture {
  GESTURE_NONE,
  GESTURE_HELLO,
  GESTURE_YES,
  GESTURE_NO,
  GESTURE_THANK_YOU,
  GESTURE_I_LOVE_YOU
};

Gesture previousGesture = GESTURE_NONE;
unsigned long lastGestureTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // ADC setup for ESP32
  analogReadResolution(12);          // 0..4095
  analogSetPinAttenuation(34, ADC_11db);
  analogSetPinAttenuation(35, ADC_11db);
  analogSetPinAttenuation(32, ADC_11db);
  analogSetPinAttenuation(33, ADC_11db);
  analogSetPinAttenuation(25, ADC_11db);

  Wire.begin();

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Check wiring!");
    while (1) {
      delay(10);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("Calibrating glove...");
  calibrateSensors();
  Serial.println("Calibration complete.");
  Serial.println("Ready to detect gestures");
}

void loop() {
  readFlexSensors();

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float ax = a.acceleration.x;
  float ay = a.acceleration.y;
  float az = a.acceleration.z;

  Gesture currentGesture = detectGesture(az, ay);

  if (currentGesture != GESTURE_NONE && currentGesture != previousGesture) {
    String gestureName = getGestureName(currentGesture);
    Serial.print("Detected: ");
    Serial.println(gestureName);

    // Optional: send data to PC/phone or a serial-based speech module
    // Example: Serial2.println(gestureName);

    previousGesture = currentGesture;
    lastGestureTime = millis();
  }

  if (millis() - lastGestureTime > 1000) {
    previousGesture = GESTURE_NONE;
  }

  delay(100);
}

void calibrateSensors() {
  for (int i = 0; i < 5; i++) {
    long sum = 0;
    for (int s = 0; s < SAMPLE_COUNT; s++) {
      sum += analogRead(FLEX_PIN[i]);
      delay(10);
    }
    baseline[i] = sum / SAMPLE_COUNT;
    Serial.print("Baseline[");
    Serial.print(i);
    Serial.print("] = ");
    Serial.println(baseline[i]);
  }
}

void readFlexSensors() {
  for (int i = 0; i < 5; i++) {
    rawValue[i] = analogRead(FLEX_PIN[i]);
  }
}

bool isFingerBent(int index) {
  int delta = rawValue[index] - baseline[index];
  return delta > BEND_THRESHOLD;
}

Gesture detectGesture(float az, float ay) {
  bool thumb  = isFingerBent(0);
  bool index  = isFingerBent(1);
  bool middle = isFingerBent(2);
  bool ring   = isFingerBent(3);
  bool pinky  = isFingerBent(4);

  // Demo pattern recognizer
  // Gesture 1: HELLO -> all fingers bent moderately, hand level
  if (thumb && index && middle && ring && pinky) {
    return GESTURE_HELLO;
  }

  // Gesture 2: YES -> index bent, others relaxed
  if (index && !middle && !ring && !pinky && !thumb) {
    return GESTURE_YES;
  }

  // Gesture 3: NO -> all fingers bent except thumb and pinky relaxed
  if (!thumb && index && middle && ring && !pinky) {
    return GESTURE_NO;
  }

  // Gesture 4: THANK YOU -> thumb + ring + pinky bent
  if (thumb && !index && !middle && ring && pinky) {
    return GESTURE_THANK_YOU;
  }

  // Gesture 5: I LOVE YOU -> index + pinky bent, middle/ring relaxed
  if (thumb && index && !middle && !ring && pinky) {
    return GESTURE_I_LOVE_YOU;
  }

  // Tilt-based variation: if hand is tilted, ignore false detections
  if (az < -TILT_THRESHOLD) {
    return GESTURE_NONE;
  }

  return GESTURE_NONE;
}

String getGestureName(Gesture gesture) {
  switch (gesture) {
    case GESTURE_HELLO:
      return "HELLO";
    case GESTURE_YES:
      return "YES";
    case GESTURE_NO:
      return "NO";
    case GESTURE_THANK_YOU:
      return "THANK YOU";
    case GESTURE_I_LOVE_YOU:
      return "I LOVE YOU";
    default:
      return "NONE";
  }
}
