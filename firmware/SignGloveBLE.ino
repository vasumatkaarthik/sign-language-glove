/*
  Sign Language Glove BLE Firmware
  
  Complete ESP32 firmware for a smart glove with:
  - 5 flex sensors (one per finger)
  - MPU6050 IMU sensor
  - BLE communication to web browser
  - Real-time gesture recognition
  
  Hardware connections:
  - Flex Sensor 1 (Thumb)  -> GPIO 34 (ADC)
  - Flex Sensor 2 (Index)  -> GPIO 35 (ADC)
  - Flex Sensor 3 (Middle) -> GPIO 32 (ADC)
  - Flex Sensor 4 (Ring)   -> GPIO 33 (ADC)
  - Flex Sensor 5 (Pinky)  -> GPIO 25 (ADC)
  - MPU6050 SDA -> GPIO 21
  - MPU6050 SCL -> GPIO 22
  - MPU6050 VCC -> 3.3V
  - MPU6050 GND -> GND
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ==========================
// BLE Configuration
// ==========================
static const char *DEVICE_NAME = "SignGlove";
static const char *SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char *CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

BLECharacteristic *signCharacteristic = NULL;
bool clientConnected = false;
bool wasClientConnected = false;

class ConnectionCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    clientConnected = true;
    Serial.println("[BLE] Client connected");
  }

  void onDisconnect(BLEServer *pServer) override {
    clientConnected = false;
    Serial.println("[BLE] Client disconnected");
  }
};

class IncomingTextCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    std::string value = pCharacteristic->getValue();
    if (!value.empty()) {
      Serial.print("[BLE] Received command: ");
      Serial.println(value.c_str());
    }
  }
};

// ==========================
// Flex Sensor Configuration
// ==========================
const int FLEX_PIN[5] = {34, 35, 32, 33, 25};
const char* fingerName[5] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};

const int SAMPLE_COUNT = 20;
int baseline[5] = {0, 0, 0, 0, 0};
int rawValue[5] = {0, 0, 0, 0, 0};
int smoothedValue[5] = {0, 0, 0, 0, 0};

// ==========================
// Gesture Detection Settings
// ==========================
const int BEND_THRESHOLD = 80;         // Adjust based on calibration
const int SMOOTHING_FACTOR = 5;        // Smoothing for jitter reduction
const unsigned long DEBOUNCE_TIME = 800; // Debounce time for same gesture

Adafruit_MPU6050 mpu;

// ==========================
// Gesture Enum and State
// ==========================
enum Gesture {
  GESTURE_NONE = 0,
  GESTURE_HELLO = 1,
  GESTURE_YES = 2,
  GESTURE_NO = 3,
  GESTURE_THANK_YOU = 4,
  GESTURE_I_LOVE_YOU = 5,
  GESTURE_PEACE = 6,
  GESTURE_OK = 7,
  GESTURE_THUMBS_UP = 8
};

Gesture previousGesture = GESTURE_NONE;
unsigned long lastGestureTime = 0;

// ==========================
// Function Prototypes
// ==========================
void setupBLE();
void setupSensors();
void calibrateSensors();
void readFlexSensors();
void smoothFlexSensors();
Gesture detectGesture(float ax, float ay, float az);
String getGestureName(Gesture gesture);
void sendSign(const String &sign);
bool isFingerBent(int index);
void printSensorDebug();

// ==========================
// Setup Function
// ==========================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========================================");
  Serial.println("  Sign Language Glove - ESP32 Firmware");
  Serial.println("========================================");

  // Setup analog inputs
  Serial.println("[SETUP] Configuring ADC pins...");
  analogReadResolution(12);
  analogSetPinAttenuation(34, ADC_11db);
  analogSetPinAttenuation(35, ADC_11db);
  analogSetPinAttenuation(32, ADC_11db);
  analogSetPinAttenuation(33, ADC_11db);
  analogSetPinAttenuation(25, ADC_11db);

  // Setup I2C and MPU6050
  Serial.println("[SETUP] Initializing I2C and MPU6050...");
  Wire.begin();
  delay(100);

  if (!mpu.begin()) {
    Serial.println("[ERROR] MPU6050 not found! Check I2C connections.");
    Serial.println("[ERROR] SDA = GPIO 21, SCL = GPIO 22");
    while (1) {
      delay(1000);
      Serial.println("[ERROR] Waiting for MPU6050...");
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("[MPU6050] Configured successfully");

  // Calibrate flex sensors
  Serial.println("\n[CALIBRATION] Starting in 2 seconds...");
  Serial.println("[CALIBRATION] Keep hand relaxed - fingers straight, not bent");
  delay(2000);
  calibrateSensors();

  // Setup BLE
  Serial.println("\n[BLE] Setting up Bluetooth Low Energy...");
  setupBLE();

  Serial.println("\n========================================");
  Serial.println("  Ready! Waiting for BLE connection...");
  Serial.println("  Open signglove.html in Chrome/Edge");
  Serial.println("========================================\n");
}

// ==========================
// Main Loop
// ==========================
void loop() {
  // Read all sensors
  readFlexSensors();
  smoothFlexSensors();

  // Get MPU6050 data
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float ax = a.acceleration.x;
  float ay = a.acceleration.y;
  float az = a.acceleration.z;

  // Detect gesture
  Gesture currentGesture = detectGesture(ax, ay, az);

  // Send gesture if detected and different from previous
  if (currentGesture != GESTURE_NONE && currentGesture != previousGesture) {
    if (millis() - lastGestureTime >= DEBOUNCE_TIME) {
      String gestureName = getGestureName(currentGesture);
      Serial.print("[GESTURE] Detected: ");
      Serial.println(gestureName);
      
      sendSign(gestureName);
      
      previousGesture = currentGesture;
      lastGestureTime = millis();
    }
  }

  // Reset gesture after timeout
  if (millis() - lastGestureTime > 2000) {
    previousGesture = GESTURE_NONE;
  }

  // Print debug info every 500ms
  static unsigned long lastDebugTime = 0;
  if (millis() - lastDebugTime > 500) {
    lastDebugTime = millis();
    printSensorDebug();
  }

  // Handle BLE reconnection
  if (!clientConnected && wasClientConnected) {
    delay(400);
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising restarted");
    wasClientConnected = false;
  }
  if (clientConnected && !wasClientConnected) {
    wasClientConnected = true;
  }

  delay(50); // Main loop delay
}

// ==========================
// BLE Setup
// ==========================
void setupBLE() {
  BLEDevice::init(DEVICE_NAME);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ConnectionCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  signCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  signCharacteristic->addDescriptor(new BLE2902());
  signCharacteristic->setCallbacks(new IncomingTextCallbacks());
  signCharacteristic->setValue("READY");

  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising as 'SignGlove'");
}

// ==========================
// Calibrate Flex Sensors
// ==========================
void calibrateSensors() {
  Serial.println("[CALIBRATION] Reading baseline values from flex sensors...");
  
  for (int i = 0; i < 5; i++) {
    long sum = 0;
    for (int s = 0; s < SAMPLE_COUNT; s++) {
      sum += analogRead(FLEX_PIN[i]);
      delay(20);
    }
    baseline[i] = sum / SAMPLE_COUNT;
    smoothedValue[i] = baseline[i];
    
    Serial.print("[CALIBRATION] ");
    Serial.print(fingerName[i]);
    Serial.print(" baseline = ");
    Serial.println(baseline[i]);
  }
  
  Serial.println("[CALIBRATION] Complete! Baseline values saved.\n");
}

// ==========================
// Read Flex Sensors (Raw)
// ==========================
void readFlexSensors() {
  for (int i = 0; i < 5; i++) {
    rawValue[i] = analogRead(FLEX_PIN[i]);
  }
}

// ==========================
// Smooth Flex Sensor Values
// ==========================
void smoothFlexSensors() {
  for (int i = 0; i < 5; i++) {
    smoothedValue[i] = (smoothedValue[i] * (SMOOTHING_FACTOR - 1) + rawValue[i]) / SMOOTHING_FACTOR;
  }
}

// ==========================
// Check if Finger is Bent
// ==========================
bool isFingerBent(int index) {
  if (index < 0 || index > 4) return false;
  int delta = smoothedValue[index] - baseline[index];
  return delta > BEND_THRESHOLD;
}

// ==========================
// Gesture Detection Logic
// ==========================
Gesture detectGesture(float ax, float ay, float az) {
  bool thumb  = isFingerBent(0);
  bool index  = isFingerBent(1);
  bool middle = isFingerBent(2);
  bool ring   = isFingerBent(3);
  bool pinky  = isFingerBent(4);

  // 1. HELLO - All fingers bent (waving motion)
  if (thumb && index && middle && ring && pinky) {
    return GESTURE_HELLO;
  }

  // 2. YES - Only index bent (nodding motion)
  if (index && !middle && !ring && !pinky && !thumb) {
    return GESTURE_YES;
  }

  // 3. NO - Index, middle, ring bent (shaking motion)
  if (!thumb && index && middle && ring && !pinky) {
    return GESTURE_NO;
  }

  // 4. THANK YOU - Thumb, ring, pinky bent
  if (thumb && !index && !middle && ring && pinky) {
    return GESTURE_THANK_YOU;
  }

  // 5. I LOVE YOU - Thumb, index, pinky bent (sign language)
  if (thumb && index && !middle && !ring && pinky) {
    return GESTURE_I_LOVE_YOU;
  }

  // 6. PEACE - Index and middle bent, others relaxed
  if (!thumb && index && middle && !ring && !pinky) {
    return GESTURE_PEACE;
  }

  // 7. OK - Thumb and index bent (circle), others relaxed
  if (thumb && index && !middle && !ring && !pinky) {
    return GESTURE_OK;
  }

  // 8. THUMBS UP - Only thumb bent
  if (thumb && !index && !middle && !ring && !pinky) {
    return GESTURE_THUMBS_UP;
  }

  return GESTURE_NONE;
}

// ==========================
// Get Gesture Name
// ==========================
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
    case GESTURE_PEACE:
      return "PEACE";
    case GESTURE_OK:
      return "OK";
    case GESTURE_THUMBS_UP:
      return "THUMBS UP";
    default:
      return "NONE";
  }
}

// ==========================
// Send Sign via BLE
// ==========================
void sendSign(const String &sign) {
  String text = sign;
  text.trim();
  
  if (text.length() == 0) return;

  if (!clientConnected) {
    Serial.println("[BLE] No client connected; gesture not sent");
    return;
  }

  signCharacteristic->setValue(text.c_str());
  signCharacteristic->notify();
  
  Serial.print("[BLE] Sent: ");
  Serial.println(text);
}

// ==========================
// Debug Output
// ==========================
void printSensorDebug() {
  Serial.print("[SENSORS] Flex: ");
  for (int i = 0; i < 5; i++) {
    int delta = smoothedValue[i] - baseline[i];
    Serial.print(fingerName[i]);
    Serial.print("=");
    Serial.print(delta);
    Serial.print(" ");
  }
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  Serial.print(" | MPU: ax=");
  Serial.print(a.acceleration.x);
  Serial.print(" ay=");
  Serial.print(a.acceleration.y);
  Serial.print(" az=");
  Serial.print(a.acceleration.z);
  
  Serial.print(" | BLE: ");
  Serial.println(clientConnected ? "Connected" : "Disconnected");
}
