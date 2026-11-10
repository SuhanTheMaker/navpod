# NavPod C6 — Dedicated Bike Turn-by-Turn Navigation System

A dedicated, bike-mounted turn-by-turn navigation system for the **ESP32-C6** and the **GC9A01 1.28" (240x240) Round LCD Display**, paired with the cross-platform mobile Web Dashboard ([`index.html`](./index.html)).

---

## 1. Hardware Pinout (ESP32-C6)

Matches Seeed Studio XIAO ESP32-C6 and standard ESP32-C6 DevKits:

| Signal / Function | ESP32-C6 GPIO | XIAO Pin Label | Description |
| :--- | :--- | :--- | :--- |
| **TFT_SCLK** | `GPIO 19` | `D8` | Hardware SPI Clock |
| **TFT_MOSI** | `GPIO 18` | `D10` | Hardware SPI Data Out |
| **TFT_CS** | `GPIO 20` | `D9` | Display Chip Select |
| **TFT_DC** | `GPIO 1` | `D1` | Data / Command Control |
| **TFT_RST** | `GPIO 2` | `D2` | Display Hardware Reset |
| **TFT_BL** | `GPIO 0` | `D0` | Backlight LED (PWM Brightness Control) |
| **TOUCH_PIN** | `GPIO 21` | `D3` (or GPIO 6) | Touch Pin / Button (Tap to cycle backlight brightness) |

---

## 2. Firmware Features ([`navpod_esp32c6.ino`](./navpod_esp32c6.ino))

- **Dedicated Turn-by-Turn Directions UI**:
  - High-contrast outdoor vector turn arrows:
    - `0`: Straight Ahead
    - `1`: 90° Turn Left
    - `2`: 90° Turn Right
    - `3`: U-Turn
    - `4`: Checkered Finish Flag (Destination Reached)
  - Large digital distance counter (`250m`, `1.4km`).
  - Speed display in km/h.
  - Centered street name marquee banner.
- **Physical Touch Button (`TOUCH_PIN = 21`)**:
  - **Tap**: Cycles backlight brightness on the fly (**100% Daylight** -> **60% Overcast** -> **25% Night Ride**).
- **Bluetooth Low Energy (NUS)**:
  - Primary Nordic UART Service: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
  - Secondary Fallback: `13370001-8f20-4b61-a043-b11c97c12662`
  - Receives standard packet format: `ICON_ID|DISTANCE_STR|STREET_NAME`

---

## 3. Web Dashboard Features ([`index.html`](./index.html))

- **Ultra-Reliable Enterprise Map Tiles**:
  - Uses **Esri ArcGIS Dark Canvas** (`server.arcgisonline.com`) with zero API key requirement, open CORS, and full compatibility over `file://` or web hosting without 403 referrer blocks.
  - One-click toggle between **Dark Canvas**, **Street Map**, and **Satellite Imagery**.
- **OSRM Cycling Engine**:
  - Turn-by-turn routing with automatic waypoint advancing when within **< 25 meters**.
- **Live 240x240 GC9A01 Cockpit Mirror**:
  - 1:1 physical replica of the round handlebar screen.
- **Live Ride Telemetry**:
  - Real-time speedometer (km/h), compass heading bearing (`345° NNW`), remaining step distance, and total route ETA.
- **Manual Turn Simulator**:
  - Test buttons (`Straight`, `Left`, `Right`, `U-Turn`, `Arrive`) for bench/desk testing.
