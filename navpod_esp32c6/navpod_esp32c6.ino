/**
 * ============================================================================
 * NAVPOD C6 — DEDICATED BIKE TURN-BY-TURN NAVIGATION FIRMWARE
 * ============================================================================
 * Target MCU:  ESP32-C6 (Seeed Studio XIAO ESP32-C6 or DevKit)
 * Display:     GC9A01 1.28" Round LCD (240x240, SPI)
 * Connectivity: Bluetooth Low Energy 5.0 (Nordic UART Service + 1337 Fallback)
 * 
 * REQUIRED LIBRARIES (Install via Arduino Library Manager):
 *   1. "Adafruit GC9A01A" by Adafruit
 *   2. "Adafruit GFX Library" by Adafruit
 * ============================================================================
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ============================================================================
// HARDWARE PIN DEFINITIONS (ESP32-C6)
// ============================================================================
constexpr int8_t TFT_SCLK  = 19; // D8 (Hardware SCK)
constexpr int8_t TFT_MOSI  = 18; // D10 (Hardware MOSI)
constexpr int8_t TFT_CS    = 20; // D9 (Chip Select)
constexpr int8_t TFT_DC    = 1;  // D1 (Data / Command)
constexpr int8_t TFT_RST   = 2;  // D2 (Reset)
constexpr int8_t TFT_BL    = 0;  // D0 (Backlight LED)
constexpr int8_t TOUCH_PIN = 21; // D3 (Touch Pin / Brightness Toggle)

// Display instance using Hardware SPI
Adafruit_GC9A01A tft(&SPI, TFT_DC, TFT_CS, TFT_RST);

// ============================================================================
// BLE SPECIFICATIONS & UUIDs
// ============================================================================
#define DEVICE_NAME "NavPod-C6"

// Primary Nordic UART Service (NUS)
#define NUS_SERVICE_UUID  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_CHAR_UUID  "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // Mobile writes here
#define NUS_TX_CHAR_UUID  "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // ESP32 notifies mobile

// Secondary Fallback Service (BikeNav 1337)
#define FALLBACK_SERVICE_UUID "13370001-8f20-4b61-a043-b11c97c12662"
#define FALLBACK_CHAR_UUID    "13370002-8f20-4b61-a043-b11c97c12662"

BLEServer*         pServer        = nullptr;
BLECharacteristic* pTxChar        = nullptr;
BLECharacteristic* pFallbackChar  = nullptr;
bool               deviceConnected = false;
bool               oldDeviceConnected = false;

// ============================================================================
// COLOR PALETTE (RGB565 High-Contrast Outdoor Daylight)
// ============================================================================
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_DARK_GRAY   0x18E3
#define COLOR_MID_GRAY    0x39E7
#define COLOR_LIGHT_GRAY  0xBDF7
#define COLOR_EMERALD     0x07E0 // Bright Neon Green
#define COLOR_CYAN        0x07FF // Bright Cyan
#define COLOR_AMBER       0xFD20 // Warning Amber / Orange
#define COLOR_RED         0xF800 // Alert Red

// ============================================================================
// LIVE NAVIGATION STATE
// ============================================================================
struct NavigationData {
  uint8_t iconId      = 0;               // 0=Straight, 1=Left, 2=Right, 3=UTurn, 4=Arrived
  String  distanceStr = "---";           // e.g. "250m", "1.4km"
  String  streetName  = "Ready to Ride"; // e.g. "MG Road"
  String  speedStr    = "0 KM/H";        // e.g. "24 KM/H"
  
  // Backlight brightness levels: 0 = 25% (Night), 1 = 60% (Cloudy), 2 = 100% (Daylight)
  uint8_t brightnessLevel = 2;

  // Touch button debounce
  uint32_t lastTouchMs = 0;
} nav;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void drawDirectionsUI();
void drawTurnArrow(int centerX, int centerY, uint8_t iconId, uint16_t color);
void setBacklightBrightness(uint8_t level);
void parseIncomingPacket(const String& pkt);

// ============================================================================
// BLE SERVER CALLBACKS
// ============================================================================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("[BLE] Mobile navigation companion connected!");
    if (pTxChar) {
      pTxChar->setValue("STATUS:CONNECTED\n");
      pTxChar->notify();
    }
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("[BLE] Disconnected. Ready for reconnection...");
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String value = pCharacteristic->getValue().c_str();
    value.trim();
    if (value.length() > 0) {
      Serial.print("[BLE RX] ");
      Serial.println(value);
      parseIncomingPacket(value);
    }
  }
};

// ============================================================================
// NAVIGATION PACKET PARSER
// Format: "ICON_ID|DISTANCE_STR|STREET_NAME" (e.g. "1|250m|MG Road")
// ============================================================================
void parseIncomingPacket(const String& pkt) {
  // Remote Backlight Command: "BL:HIGH", "BL:MED", "BL:LOW"
  if (pkt.startsWith("BL:")) {
    String bl = pkt.substring(3);
    if (bl == "HIGH") setBacklightBrightness(2);
    else if (bl == "MED") setBacklightBrightness(1);
    else if (bl == "LOW") setBacklightBrightness(0);
    return;
  }

  // Turn-By-Turn Navigation Packet
  int firstPipe = pkt.indexOf('|');
  if (firstPipe != -1) {
    int secondPipe = pkt.indexOf('|', firstPipe + 1);
    if (secondPipe != -1) {
      String iconStr = pkt.substring(0, firstPipe);
      String distStr = pkt.substring(firstPipe + 1, secondPipe);
      
      // Check for optional 3rd pipe: ICON|DIST|STREET|SPEED
      int thirdPipe = pkt.indexOf('|', secondPipe + 1);
      String street;
      if (thirdPipe != -1) {
        street = pkt.substring(secondPipe + 1, thirdPipe);
        nav.speedStr = pkt.substring(thirdPipe + 1) + " KM/H";
      } else {
        street = pkt.substring(secondPipe + 1);
      }

      nav.iconId      = iconStr.toInt();
      nav.distanceStr = distStr;
      nav.streetName  = street;

      Serial.printf("[NAV UPDATE] Maneuver: %d, Distance: %s, Street: %s\n", 
                    nav.iconId, distStr.c_str(), street.c_str());

      drawDirectionsUI();
      return;
    }
  }

  Serial.println("[WARN] Unrecognized packet format.");
}

// ============================================================================
// HARDWARE INITIALIZATION
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==========================================");
  Serial.println("  NavPod C6 — Dedicated Bike Directions UI");
  Serial.println("==========================================");

  // 1. Backlight Setup
  pinMode(TFT_BL, OUTPUT);
  setBacklightBrightness(2); // Start at 100% Daylight brightness

  // 2. Touch Mode Button Setup
  pinMode(TOUCH_PIN, INPUT_PULLUP);

  // 3. Hardware SPI for ESP32-C6
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  // 4. Initialize GC9A01 Round Display (240x240)
  tft.begin();
  tft.setRotation(0); // 0 or 2 for handlebar mount orientation
  tft.fillScreen(COLOR_BLACK);

  // Initial Boot Splash
  tft.drawCircle(120, 120, 118, COLOR_CYAN);
  tft.setTextColor(COLOR_CYAN);
  tft.setTextSize(2);
  tft.setCursor(65, 85);
  tft.println("NavPod");
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(1);
  tft.setCursor(85, 115);
  tft.println("ESP32-C6");
  tft.setTextColor(COLOR_EMERALD);
  tft.setCursor(55, 145);
  tft.println("Bike Directions UI");

  // 5. Initialize Bluetooth Low Energy Stack
  BLEDevice::init(DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Nordic UART Service (NUS)
  BLEService* pNusService = pServer->createService(NUS_SERVICE_UUID);

  // RX Characteristic (Mobile writes here)
  BLECharacteristic* pRxChar = pNusService->createCharacteristic(
    NUS_RX_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxChar->setCallbacks(new RxCallbacks());

  // TX Characteristic (ESP32 notifies mobile here)
  pTxChar = pNusService->createCharacteristic(
    NUS_TX_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );
  pTxChar->addDescriptor(new BLE2902());

  pNusService->start();

  // Secondary Fallback Service (1337)
  BLEService* pFallbackService = pServer->createService(FALLBACK_SERVICE_UUID);
  pFallbackChar = pFallbackService->createCharacteristic(
    FALLBACK_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_READ
  );
  pFallbackChar->setCallbacks(new RxCallbacks());
  pFallbackService->start();

  // Start BLE Advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(NUS_SERVICE_UUID);
  pAdvertising->addServiceUUID(FALLBACK_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising started. Ready for navigation packets!");

  delay(600);
  drawDirectionsUI();
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  // Handle BLE Connection status changes
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("[BLE] Advertising restarted.");
    oldDeviceConnected = deviceConnected;
    drawDirectionsUI();
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    drawDirectionsUI();
  }

  // Handle Touch Pin (Tap cycles screen brightness)
  if (digitalRead(TOUCH_PIN) == LOW) {
    uint32_t now = millis();
    if (now - nav.lastTouchMs > 350) { // 350ms Debounce
      nav.lastTouchMs = now;
      nav.brightnessLevel = (nav.brightnessLevel + 1) % 3;
      setBacklightBrightness(nav.brightnessLevel);
      Serial.printf("[TOUCH] Brightness level changed to: %d\n", nav.brightnessLevel);
    }
  }

  delay(25);
}

// ============================================================================
// BACKLIGHT BRIGHTNESS CONTROL
// ============================================================================
void setBacklightBrightness(uint8_t level) {
  nav.brightnessLevel = level;
  switch (level) {
    case 0: // Low / Night mode (25%)
      analogWrite(TFT_BL, 64);
      break;
    case 1: // Medium (60%)
      analogWrite(TFT_BL, 150);
      break;
    case 2: // High / Daylight (100%)
    default:
      digitalWrite(TFT_BL, HIGH);
      break;
  }
}

// ============================================================================
// PURE TURN-BY-TURN DIRECTIONS UI
// ============================================================================
void drawDirectionsUI() {
  tft.fillScreen(COLOR_BLACK);

  // Outer Circular Tachometer / Bezel Ring
  tft.drawCircle(120, 120, 118, COLOR_MID_GRAY);
  tft.drawCircle(120, 120, 117, COLOR_MID_GRAY);

  // Top Status Bar: BLE Indicator & Speed
  tft.setTextSize(1);
  if (deviceConnected) {
    tft.setTextColor(COLOR_EMERALD);
    tft.setCursor(45, 18);
    tft.print("BLE OK");
  } else {
    tft.setTextColor(COLOR_AMBER);
    tft.setCursor(35, 18);
    tft.print("WAIT BLE");
  }

  tft.setTextColor(COLOR_CYAN);
  tft.setCursor(145, 18);
  tft.print(nav.speedStr);

  // Center High-Contrast Vector Maneuver Arrow
  uint16_t arrowColor = (nav.iconId == 4) ? COLOR_EMERALD : ((nav.iconId == 3) ? COLOR_AMBER : COLOR_CYAN);
  drawTurnArrow(120, 95, nav.iconId, arrowColor);

  // Large Digital Distance Counter (High-contrast outdoor daylight font)
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(3);
  int distWidth = nav.distanceStr.length() * 18;
  int distStartX = max(10, 120 - (distWidth / 2));
  tft.setCursor(distStartX, 150);
  tft.print(nav.distanceStr);

  // Horizontal Divider Line
  tft.drawFastHLine(35, 185, 170, COLOR_DARK_GRAY);

  // Street Name Banner (Centered and cleanly truncated)
  tft.setTextSize(1);
  tft.setTextColor(COLOR_CYAN);
  String street = nav.streetName;
  if (street.length() > 22) {
    street = street.substring(0, 19) + "...";
  }
  int streetWidth = street.length() * 6;
  int streetStartX = max(15, 120 - (streetWidth / 2));
  tft.setCursor(streetStartX, 198);
  tft.print(street);
}

// ============================================================================
// OUTDOOR HIGH-CONTRAST VECTOR TURN ARROWS
// ============================================================================
void drawTurnArrow(int cx, int cy, uint8_t iconId, uint16_t color) {
  switch (iconId) {
    case 0: // Straight Ahead
      tft.fillTriangle(cx, cy - 36, cx - 24, cy - 8, cx + 24, cy - 8, color);
      tft.fillRect(cx - 8, cy - 8, 16, 44, color);
      break;

    case 1: // Turn Left (90°)
      // Arrowhead pointing left
      tft.fillTriangle(cx - 38, cy - 14, cx - 14, cy - 34, cx - 14, cy + 6, color);
      // Horizontal bar
      tft.fillRect(cx - 14, cy - 22, 34, 16, color);
      // Vertical trunk
      tft.fillRect(cx + 6, cy - 6, 16, 42, color);
      break;

    case 2: // Turn Right (90°)
      // Arrowhead pointing right
      tft.fillTriangle(cx + 38, cy - 14, cx + 14, cy - 34, cx + 14, cy + 6, color);
      // Horizontal bar
      tft.fillRect(cx - 20, cy - 22, 34, 16, color);
      // Vertical trunk
      tft.fillRect(cx - 22, cy - 6, 16, 42, color);
      break;

    case 3: // U-Turn
      // Inverted U shape + downward arrow
      tft.fillRoundRect(cx - 28, cy - 34, 56, 24, 12, color);
      tft.fillRect(cx - 28, cy - 14, 16, 30, color);
      tft.fillRect(cx + 12, cy - 14, 16, 30, color);
      tft.fillCircle(cx, cy - 10, 10, COLOR_BLACK); // Hollow center
      // Downward arrowhead on left branch
      tft.fillTriangle(cx - 20, cy + 34, cx - 36, cy + 14, cx - 4, cy + 14, color);
      break;

    case 4: // Destination Reached / Checkered Flag
      tft.fillRect(cx - 25, cy - 32, 6, 68, COLOR_WHITE);
      tft.fillTriangle(cx - 19, cy - 32, cx + 26, cy - 14, cx - 19, cy + 4, color);
      tft.fillCircle(cx - 22, cy - 34, 4, COLOR_AMBER);
      break;

    default:
      tft.fillCircle(cx, cy, 22, color);
      break;
  }
}
