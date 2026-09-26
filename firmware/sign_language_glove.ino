#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------------------
// BLE Configuration
// ---------------------------
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789012"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-210987654321"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// ---------------------------
// Pins for 5 flex sensors
// ---------------------------
const int FLEX_PIN[5] = {34, 35, 32, 33, 25};
const char* fingerName[5] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};

// ---------------------------
// Sensor calibration values
// ---------------------------
const int SAMPLE_COUNT = 20;
int baseline[5] = {0, 0, 0, 0, 0};
int rawValue[5] = {0, 0, 0, 0, 0};

// ---------------------------
// Gesture detection settings
// ---------------------------
const int BEND_THRESHOLD = 100;       // adjust after calibration
const float TILT_THRESHOLD = 0.8f;    // tilt limit

Adafruit_MPU6050 mpu;

// ---------------------------
// Gesture Enum
// ---------------------------
enum Gesture {
  GESTURE_NONE = 0,
  GESTURE_HELLO = 1,
  GESTURE_YES = 2,
  GESTURE_NO = 3,
  GESTURE_THANK_YOU = 4,
  GESTURE_I_LOVE_YOU = 5
};

Gesture previousGesture = GESTURE_NONE;
unsigned long lastGestureTime = 0;

// ---------------------------
// BLE Server Callback
// ---------------------------
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Client connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Client disconnected");
    }
};

// ---------------------------
// Setup
// ---------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // ADC setup for ESP32
  analogReadResolution(12);
  analogSetPinAttenuation(34, ADC_11db);
  analogSetPinAttenuation(35, ADC_11db);
  analogSetPinAttenuation(32, ADC_11db);
  analogSetPinAttenuation(33, ADC_11db);
  analogSetPinAttenuation(25, ADC_11db);

  // I2C setup for MPU6050
  Wire.begin();

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Check wiring!");
    while (1) delay(10);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Calibrate flex sensors
  Serial.println("Calibrating flex sensors...");
  calibrateSensors();
  Serial.println("Calibration complete.");

  // BLE Setup
  Serial.println("Starting BLE...");
  setupBLE();
  Serial.println("BLE started. Waiting for connection...");
}

// ---------------------------
// Main Loop
// ---------------------------
void loop() {
  // Read flex sensors
  readFlexSensors();

  // Read MPU6050
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float az = a.acceleration.z;
  float ay = a.acceleration.y;

  // Detect gesture
  Gesture currentGesture = detectGesture(az, ay);

  // Send data if gesture detected or if connected to device
  if (currentGesture != GESTURE_NONE && currentGesture != previousGesture) {
    previousGesture = currentGesture;
    lastGestureTime = millis();
    sendGestureData(currentGesture);
  }

  // Reset gesture after timeout
  if (millis() - lastGestureTime > 1000) {
    previousGesture = GESTURE_NONE;
  }

  // BLE connection handling
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Start advertising");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  delay(100);
}

// ---------------------------
// BLE Setup Function
// ---------------------------
void setupBLE() {
  BLEDevice::init("SignLanguageGlove");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->addDescriptor(new BLE2902());

  pCharacteristic->setValue("Gesture: NONE");
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  pAdvertising->start();
}

// ---------------------------
// Calibrate Sensors
// ---------------------------
void calibrateSensors() {
  for (int i = 0; i < 5; i++) {
    long sum = 0;
    for (int s = 0; s < SAMPLE_COUNT; s++) {
      sum += analogRead(FLEX_PIN[i]);
      delay(10);
    }
    baseline[i] = sum / SAMPLE_COUNT;
    Serial.print("Baseline[");
    Serial.print(fingerName[i]);
    Serial.print("] = ");
    Serial.println(baseline[i]);
  }
}

// ---------------------------
// Read Flex Sensors
// ---------------------------
void readFlexSensors() {
  for (int i = 0; i < 5; i++) {
    rawValue[i] = analogRead(FLEX_PIN[i]);
  }
}

// ---------------------------
// Check if Finger is Bent
// ---------------------------
bool isFingerBent(int index) {
  int delta = rawValue[index] - baseline[index];
  return delta > BEND_THRESHOLD;
}

// ---------------------------
// Detect Gesture
// ---------------------------
Gesture detectGesture(float az, float ay) {
  bool thumb  = isFingerBent(0);
  bool index  = isFingerBent(1);
  bool middle = isFingerBent(2);
  bool ring   = isFingerBent(3);
  bool pinky  = isFingerBent(4);

  // HELLO: all fingers bent
  if (thumb && index && middle && ring && pinky) {
    return GESTURE_HELLO;
  }

  // YES: index bent only
  if (index && !middle && !ring && !pinky && !thumb) {
    return GESTURE_YES;
  }

  // NO: index + middle + ring bent, thumb and pinky relaxed
  if (!thumb && index && middle && ring && !pinky) {
    return GESTURE_NO;
  }

  // THANK YOU: thumb + ring + pinky bent
  if (thumb && !index && !middle && ring && pinky) {
    return GESTURE_THANK_YOU;
  }

  // I LOVE YOU: thumb + index + pinky bent
  if (thumb && index && !middle && !ring && pinky) {
    return GESTURE_I_LOVE_YOU;
  }

  return GESTURE_NONE;
}

// ---------------------------
// Get Gesture Name
// ---------------------------
String getGestureName(Gesture gesture) {
  switch (gesture) {
    case GESTURE_HELLO:
      return "HELLO";
    case GESTURE_YES:
      return "YES";
    case GESTURE_NO:
      return "NO";
    case GESTURE_THANK_YOU:
      return "THANK_YOU";
    case GESTURE_I_LOVE_YOU:
      return "I_LOVE_YOU";
    default:
      return "NONE";
  }
}

// ---------------------------
// Send Gesture Data via BLE
// ---------------------------
void sendGestureData(Gesture gesture) {
  String gestureName = getGestureName(gesture);
  
  Serial.print("Detected: ");
  Serial.println(gestureName);

  if (deviceConnected) {
    // Create JSON-like string with sensor data
    String sensorData = "{\"gesture\":\"" + gestureName + "\",\"sensors\":[";
    for (int i = 0; i < 5; i++) {
      int delta = rawValue[i] - baseline[i];
      sensorData += String(delta);
      if (i < 4) sensorData += ",";
    }
    sensorData += "]}";

    pCharacteristic->setValue(sensorData.c_str());
    pCharacteristic->notify();
  }
}

// ---------------------------
// Print Sensor Debug Info
// ---------------------------
void printSensorDebug() {
  Serial.print("Flex values: ");
  for (int i = 0; i < 5; i++) {
    int delta = rawValue[i] - baseline[i];
    Serial.print(fingerName[i]);
    Serial.print("=");
    Serial.print(delta);
    Serial.print(" ");
  }
  Serial.println();
}
