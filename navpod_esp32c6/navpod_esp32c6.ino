/**
 * ============================================================================
 * NAVPOD C6 — MINIMALIST GPS NAVIGATION UI (CLEAN FLAT & ACCENTS)
 * ============================================================================
 * Target MCU:  ESP32-C6 (Seeed Studio XIAO ESP32-C6 or DevKit)
 * Display:     GC9A01 1.28" Round LCD (240x240, SPI)
 * Connectivity: Bluetooth Low Energy 5.0 (Nordic UART Service + 1337 Fallback)
 * 
 * DESIGN SPECIFICATIONS:
 *   1. Screen Dimensions: 240x240 circular display.
 *   2. Background: Deep dark charcoal (#121619) with faint radiating compass lines.
 *   3. Outer Ring: Vibrant neon green inset progress circle (#00FF66).
 *   4. Top Left Indicator: Bold neon green step sequence number (e.g. "6").
 *   5. Content Hierarchy (Centered Vertically):
 *      - Top Icon: Clean, bold white turn arrow.
 *      - Street Name: Bright neon green uppercase text ("PARK AVENUE").
 *      - Distance: Large bold prominent green numbers ("350m").
 *      - ETA: Small muted gray-green text ("ETA 4:12 PM").
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

// Nordic UART Service (NUS)
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
// COLOR PALETTE (RGB565 Tailored to Design Specs)
// ============================================================================
#define COLOR_BG_CHARCOAL  0x10C3 // #121619 Deep dark charcoal
#define COLOR_NEON_GREEN   0x07EC // #00FF66 Vibrant neon green
#define COLOR_WHITE        0xFFFF // #FFFFFF Pure white arrow
#define COLOR_ETA_MUTED    0x746D // #708A79 Muted gray-green ETA text
#define COLOR_RADIAL_LINE  0x1905 // Faint radiating compass lines

// ============================================================================
// LIVE NAVIGATION STATE
// ============================================================================
struct NavigationData {
  uint8_t iconId      = 1;               // 0=Straight, 1=Left, 2=Right, 3=UTurn, 4=Arrived
  String  distanceStr = "350m";          // e.g. "350m"
  String  streetName  = "PARK AVENUE";   // e.g. "PARK AVENUE"
  uint8_t stepSeq     = 6;               // Top-left sequence number (e.g. 6)
  String  etaStr      = "ETA 4:12 PM";   // e.g. "ETA 4:12 PM"
  
  // Backlight brightness levels: 0 = 25% (Night), 1 = 60% (Cloudy), 2 = 100% (Daylight)
  uint8_t brightnessLevel = 2;

  // Touch button debounce
  uint32_t lastTouchMs = 0;
} nav;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void drawDirectionsUI();
void drawTurnArrow(int centerX, int centerY, uint8_t iconId);
void setBacklightBrightness(uint8_t level);
void parseIncomingPacket(const String& pkt);

// ============================================================================
// BLE SERVER CALLBACKS
// ============================================================================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("[BLE] Mobile companion connected!");
    if (pTxChar) {
      pTxChar->setValue("STATUS:CONNECTED\n");
      pTxChar->notify();
    }
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("[BLE] Disconnected. Waiting for reconnection...");
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
// Format: "ICON_ID|DISTANCE_STR|STREET_NAME" or "ICON_ID|DISTANCE_STR|STREET_NAME|STEP_NUM|ETA"
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
      
      int thirdPipe = pkt.indexOf('|', secondPipe + 1);
      if (thirdPipe != -1) {
        nav.streetName = pkt.substring(secondPipe + 1, thirdPipe);
        int fourthPipe = pkt.indexOf('|', thirdPipe + 1);
        if (fourthPipe != -1) {
          nav.stepSeq = pkt.substring(thirdPipe + 1, fourthPipe).toInt();
          nav.etaStr  = pkt.substring(fourthPipe + 1);
        } else {
          nav.stepSeq = pkt.substring(thirdPipe + 1).toInt();
        }
      } else {
        nav.streetName = pkt.substring(secondPipe + 1);
      }

      nav.iconId      = iconStr.toInt();
      nav.distanceStr = distStr;
      nav.streetName.toUpperCase();

      Serial.printf("[NAV UPDATE] Maneuver: %d, Dist: %s, Street: %s\n", 
                    nav.iconId, nav.distanceStr.c_str(), nav.streetName.c_str());

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
  Serial.println("  NavPod C6 — Minimalist Clean Flat UI");
  Serial.println("==========================================");

  // 1. Backlight Setup
  pinMode(TFT_BL, OUTPUT);
  setBacklightBrightness(2); // 100% Daylight brightness

  // 2. Touch Mode Button Setup
  pinMode(TOUCH_PIN, INPUT_PULLUP);

  // 3. Hardware SPI for ESP32-C6
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  // 4. Initialize GC9A01 Round Display (240x240)
  tft.begin();
  tft.setRotation(0); // 0 or 2 for handlebar mount orientation

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

  delay(200);
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
// MINIMALIST 240x240 DIRECTIONS UI (MATCHING DESIGN SPECS)
// ============================================================================
void drawDirectionsUI() {
  // 1. Background: Deep dark charcoal (#121619)
  tft.fillScreen(COLOR_BG_CHARCOAL);

  // 2. Faint 8-Axis Radiating Compass Grid Lines
  tft.drawLine(25, 120, 215, 120, COLOR_RADIAL_LINE); // Horizontal
  tft.drawLine(120, 25, 120, 215, COLOR_RADIAL_LINE); // Vertical
  tft.drawLine(53, 53, 187, 187, COLOR_RADIAL_LINE);   // 45 deg diagonal
  tft.drawLine(187, 53, 53, 187, COLOR_RADIAL_LINE);   // 135 deg diagonal

  // 3. Inset Vibrant Neon Green Progress Ring (#00FF66)
  tft.drawCircle(120, 120, 95, COLOR_NEON_GREEN);
  tft.drawCircle(120, 120, 96, COLOR_NEON_GREEN);
  tft.drawCircle(120, 120, 97, COLOR_NEON_GREEN);

  // 4. Top Left Indicator: Bold Neon Green Step Number (e.g. "6")
  tft.setTextSize(3);
  tft.setTextColor(COLOR_NEON_GREEN);
  tft.setCursor(24, 20);
  tft.print(nav.stepSeq);

  // 5. Content Hierarchy (Centered Vertically):
  
  // A. Top Icon: Bold, Clean White Vector Turn Arrow (Centered at y = 68)
  drawTurnArrow(120, 68, nav.iconId);

  // B. Street Name: Uppercase Bright Green ("PARK AVENUE") at y = 114
  tft.setTextSize(1);
  tft.setTextColor(COLOR_NEON_GREEN);
  String street = nav.streetName;
  if (street.length() > 20) {
    street = street.substring(0, 17) + "...";
  }
  int streetWidth = street.length() * 6;
  int streetStartX = max(20, 120 - (streetWidth / 2));
  tft.setCursor(streetStartX, 114);
  tft.print(street);

  // C. Distance: Large Bold Prominent Green Numbers ("350m") at y = 142
  tft.setTextSize(4);
  tft.setTextColor(COLOR_NEON_GREEN);
  int distWidth = nav.distanceStr.length() * 24;
  int distStartX = max(20, 120 - (distWidth / 2));
  tft.setCursor(distStartX, 138);
  tft.print(nav.distanceStr);

  // D. ETA: Small Muted Gray-Green Text ("ETA 4:12 PM") at y = 188
  tft.setTextSize(1);
  tft.setTextColor(COLOR_ETA_MUTED);
  int etaWidth = nav.etaStr.length() * 6;
  int etaStartX = max(20, 120 - (etaWidth / 2));
  tft.setCursor(etaStartX, 188);
  tft.print(nav.etaStr);
}

// ============================================================================
// CLEAN BOLD WHITE VECTOR TURN ARROWS
// ============================================================================
void drawTurnArrow(int cx, int cy, uint8_t iconId) {
  switch (iconId) {
    case 1: // Bold Left Turn (Matching Mockup)
      // Vertical trunk
      tft.fillRect(cx + 4, cy, 8, 22, COLOR_WHITE);
      // Smooth rounded top corner & horizontal bar
      tft.fillRoundRect(cx - 14, cy - 14, 26, 16, 6, COLOR_WHITE);
      // Left-pointing arrowhead
      tft.fillTriangle(cx - 24, cy - 6, cx - 10, cy - 20, cx - 10, cy + 8, COLOR_WHITE);
      break;

    case 2: // Bold Right Turn
      // Vertical trunk
      tft.fillRect(cx - 12, cy, 8, 22, COLOR_WHITE);
      // Smooth rounded top corner & horizontal bar
      tft.fillRoundRect(cx - 12, cy - 14, 26, 16, 6, COLOR_WHITE);
      // Right-pointing arrowhead
      tft.fillTriangle(cx + 24, cy - 6, cx + 10, cy - 20, cx + 10, cy + 8, COLOR_WHITE);
      break;

    case 0: // Straight Ahead
      // Vertical trunk
      tft.fillRect(cx - 4, cy - 6, 8, 28, COLOR_WHITE);
      // Upward arrowhead
      tft.fillTriangle(cx, cy - 24, cx - 16, cy - 4, cx + 16, cy - 4, COLOR_WHITE);
      break;

    case 3: // U-Turn
      // Inverted U shape
      tft.fillRoundRect(cx - 16, cy - 18, 32, 20, 8, COLOR_WHITE);
      tft.fillRect(cx - 16, cy - 4, 8, 16, COLOR_WHITE);
      tft.fillRect(cx + 8, cy - 4, 8, 16, COLOR_WHITE);
      tft.fillCircle(cx, cy - 4, 6, COLOR_BG_CHARCOAL); // Center cutout
      // Downward arrowhead on left branch
      tft.fillTriangle(cx - 12, cy + 22, cx - 22, cy + 8, cx - 2, cy + 8, COLOR_WHITE);
      break;

    case 4: // Destination Reached / Flag
      tft.fillRect(cx - 16, cy - 20, 4, 40, COLOR_WHITE);
      tft.fillTriangle(cx - 12, cy - 20, cx + 16, cy - 8, cx - 12, cy + 4, COLOR_WHITE);
      tft.fillCircle(cx - 14, cy - 22, 3, COLOR_NEON_GREEN);
      break;

    default:
      tft.fillCircle(cx, cy, 14, COLOR_WHITE);
      break;
  }
}
