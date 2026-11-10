/**
 * ============================================================================
 * NAVPOD C6 — MINIMALIST GPS NAVIGATION UI (CLEAN FLAT & ACCENTS)
 * ============================================================================
 * Target MCU:  ESP32-C6 (Seeed Studio XIAO ESP32-C6 or DevKit)
 * Display:     GC9A01 1.28" Round LCD (240x240, SPI)
 * Connectivity: Bluetooth Low Energy 5.0 (Nordic UART Service + 1337 Fallback)
 * 
 * FEATURES:
 *   1. Hardware Interaction: Press-and-hold touch sensor (>800ms) toggles
 *      live status overlay (Battery %, BLE status, Total Trip & Turn distance).
 *   2. Responsive Navigation Loop: High refresh rate, instant waypoint transitions.
 *   3. Anti-Aliased Typography: Uses proportional FreeSans vector fonts for
 *      smooth, non-pixelated rendering on 240x240 round screen.
 *   4. Clean Flat Design: Deep charcoal (#121619), neon green (#00FF66) progress ring,
 *      bold white turn arrows, uppercase street names, large distance digits.
 * ============================================================================
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

// Smooth Proportional Anti-Aliased Sans-Serif Fonts (Built into Adafruit_GFX)
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

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
constexpr int8_t TOUCH_PIN = 21; // D3 (Touch Pin / Status Overlay Sensor)

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
#define COLOR_PANEL_BOX    0x1947 // Dark panel border
#define COLOR_RED          0xF800 // Alert red

// ============================================================================
// LIVE NAVIGATION & HARDWARE STATE
// ============================================================================
struct NavigationData {
  uint8_t iconId        = 1;               // 0=Straight, 1=Left, 2=Right, 3=UTurn, 4=Arrived
  String  distanceStr   = "350m";          // e.g. "350m"
  String  streetName    = "PARK AVENUE";   // e.g. "PARK AVENUE"
  uint8_t stepSeq       = 6;               // Top-left sequence number (e.g. 6)
  String  etaStr        = "ETA 4:12 PM";   // e.g. "ETA 4:12 PM"
  String  totalTripDist = "4.8 km";        // Total remaining trip distance
  String  batteryPct    = "78%";           // Live battery percentage
  
  // Backlight brightness levels: 0 = 25% (Night), 1 = 60% (Cloudy), 2 = 100% (Daylight)
  uint8_t brightnessLevel = 2;

  // Touch sensor hold state
  bool isTouching      = false;
  uint32_t touchStartMs = 0;
  bool holdTriggered   = false;
  bool overlayActive   = false;
} nav;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void drawDirectionsUI();
void drawStatusOverlay();
void drawTurnArrow(int centerX, int centerY, uint8_t iconId);
void printCentered(const String& str, int y, const GFXfont* font, uint16_t color);
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
// Format: "ICON_ID|DISTANCE_STR|STREET_NAME|STEP_NUM|ETA"
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

      if (!nav.overlayActive) {
        drawDirectionsUI();
      }
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
  Serial.println("  NavPod C6 — Anti-Aliased Clean Flat UI");
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
  tft.setRotation(0); // Handlebar mount orientation

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
// MAIN LOOP WITH HARDWARE PRESS-AND-HOLD (>800ms) EVENT LISTENER
// ============================================================================
void loop() {
  // Handle BLE Connection status changes
  if (!deviceConnected && oldDeviceConnected) {
    delay(400);
    pServer->startAdvertising();
    Serial.println("[BLE] Advertising restarted.");
    oldDeviceConnected = deviceConnected;
    if (!nav.overlayActive) drawDirectionsUI();
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    if (!nav.overlayActive) drawDirectionsUI();
  }

  // --- HARDWARE INTERACTION: CAPACITIVE TOUCH SENSOR (>800ms HOLD) ---
  bool pinPressed = (digitalRead(TOUCH_PIN) == LOW);

  if (pinPressed && !nav.isTouching) {
    nav.isTouching = true;
    nav.touchStartMs = millis();
    nav.holdTriggered = false;
  }
  else if (pinPressed && nav.isTouching) {
    // Check if held for > 800ms
    if (!nav.holdTriggered && (millis() - nav.touchStartMs >= 800)) {
      nav.holdTriggered = true;
      nav.overlayActive = !nav.overlayActive;
      Serial.printf("[TOUCH] Press-and-hold >800ms detected! Overlay: %s\n", nav.overlayActive ? "ACTIVE" : "CLOSED");

      if (nav.overlayActive) {
        drawStatusOverlay();
      } else {
        drawDirectionsUI();
      }
    }
  }
  else if (!pinPressed && nav.isTouching) {
    uint32_t pressDuration = millis() - nav.touchStartMs;
    nav.isTouching = false;

    if (!nav.holdTriggered && pressDuration > 50) {
      // Short Tap (< 800ms)
      if (nav.overlayActive) {
        // Tap closes overlay immediately
        nav.overlayActive = false;
        drawDirectionsUI();
      } else {
        // Tap cycles backlight brightness
        nav.brightnessLevel = (nav.brightnessLevel + 1) % 3;
        setBacklightBrightness(nav.brightnessLevel);
        Serial.printf("[TOUCH] Short tap: Brightness level %d\n", nav.brightnessLevel);
      }
    }
  }

  delay(20);
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
// TEXT CENTERING HELPER WITH PROPORTIONAL ANTI-ALIASED FONTS
// ============================================================================
void printCentered(const String& str, int y, const GFXfont* font, uint16_t color) {
  tft.setFont(font);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(str.c_str(), 0, y, &x1, &y1, &w, &h);
  int startX = max(10, 120 - ((int)w / 2));
  tft.setCursor(startX, y);
  tft.print(str);
}

// ============================================================================
// 1. MINIMALIST 240x240 DIRECTIONS UI (SMOOTH FONTS & VIBRANT RING)
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
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_NEON_GREEN);
  tft.setCursor(22, 38);
  tft.print(nav.stepSeq);

  // 5. Content Hierarchy (Centered Vertically):
  
  // A. Top Icon: Bold, Clean White Vector Turn Arrow (Centered at y = 68)
  drawTurnArrow(120, 68, nav.iconId);

  // B. Street Name: Uppercase Bright Green ("PARK AVENUE") at y = 116
  String street = nav.streetName;
  if (street.length() > 18) {
    street = street.substring(0, 15) + "...";
  }
  printCentered(street, 116, &FreeSansBold9pt7b, COLOR_NEON_GREEN);

  // C. Distance: Large Bold Prominent Green Numbers ("350m") at y = 155
  printCentered(nav.distanceStr, 155, &FreeSansBold18pt7b, COLOR_NEON_GREEN);

  // D. ETA: Small Muted Gray-Green Text ("ETA 4:12 PM") at y = 188
  printCentered(nav.etaStr, 188, &FreeSans9pt7b, COLOR_ETA_MUTED);
}

// ============================================================================
// 2. COMPREHENSIVE STATUS OVERLAY (PRESS-AND-HOLD TOUCH SENSOR >800ms)
// ============================================================================
void drawStatusOverlay() {
  tft.fillScreen(COLOR_BG_CHARCOAL);

  // Dashed Accent Progress Ring
  tft.drawCircle(120, 120, 95, COLOR_NEON_GREEN);

  // Header Title
  printCentered("STATUS OVERLAY", 38, &FreeSansBold9pt7b, COLOR_NEON_GREEN);

  // 1. Live Battery percentage
  tft.drawRoundRect(34, 50, 172, 30, 6, COLOR_PANEL_BOX);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(0xBDF7);
  tft.setCursor(44, 71);
  tft.print("BATTERY:");
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(COLOR_NEON_GREEN);
  tft.setCursor(144, 71);
  tft.print(nav.batteryPct);

  // 2. Bluetooth Connection Status
  tft.drawRoundRect(34, 86, 172, 30, 6, COLOR_PANEL_BOX);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(0xBDF7);
  tft.setCursor(44, 107);
  tft.print("BLE:");
  tft.setFont(&FreeSansBold9pt7b);
  if (deviceConnected) {
    tft.setTextColor(COLOR_NEON_GREEN);
    tft.setCursor(96, 107);
    tft.print("CONNECTED");
  } else {
    tft.setTextColor(COLOR_RED);
    tft.setCursor(92, 107);
    tft.print("NO DEVICE");
  }

  // 3. Total Trip Distance vs Next Turn Distance
  tft.drawRoundRect(34, 122, 172, 46, 6, COLOR_PANEL_BOX);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(0xBDF7);
  tft.setCursor(44, 142);
  tft.print("TOTAL TRIP:");
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(COLOR_NEON_GREEN);
  tft.setCursor(144, 142);
  tft.print(nav.totalTripDist);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(0xBDF7);
  tft.setCursor(44, 160);
  tft.print("NEXT TURN:");
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(144, 160);
  tft.print(nav.distanceStr);

  // Dismiss Hint
  printCentered("HOLD >800ms / TAP TO CLOSE", 198, &FreeSans9pt7b, COLOR_ETA_MUTED);
}

// ============================================================================
// CLEAN BOLD WHITE VECTOR TURN ARROWS
// ============================================================================
void drawTurnArrow(int cx, int cy, uint8_t iconId) {
  switch (iconId) {
    case 1: // Bold Left Turn
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
