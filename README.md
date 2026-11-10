# NavPod — 240x240 Smartwatch GPS Navigation & Simulator

A web-based GPS navigation application and real-time simulator modeled after a **240x240 pixel round smartwatch interface**, with live **Google Maps Directions API** routing, dynamic neon progress ring, and **ESP32-C6 Web Bluetooth** companion streaming.

---

## 1. Key Features

### A. 240×240 Circular Smartwatch UI Shell
- **Aesthetic**: Deep dark charcoal (`#121619`) with faint 8-axis radial compass grid lines.
- **Dynamic Neon Green Progress Ring**: Outer `#00FF66` progress circle dynamically fills along the perimeter based on total route completion percentage.
- **Typography**: Anti-aliased proportional **Montserrat** & **Inter** sans-serif font rendering with zero jagged or pixelated edges.
- **Hierarchy**:
  - Top-left sequence step indicator (e.g. `6`).
  - Pure white bold vector turn arrow.
  - Bright green uppercase street name (`PARK AVENUE`).
  - Large bold green distance readout (`350m`).
  - Clean muted ETA indicator (`ETA 4:12 PM`).

### B. Hardware Interaction (>800ms Press & Hold Touch Sensor)
- Clicking and holding down on the watch display or on the interactive **"Hold Touch Sensor (>800ms)"** button triggers a sleek telemetry status overlay:
  - **Live Battery Percentage** (`78%`)
  - **Bluetooth Connection Status** (`CONNECTED` / `OFFLINE`)
  - **Total Remaining Trip Distance** vs **Upcoming Turn Distance**
- Quick tap resumes normal turn view.

### C. Google Maps Integration & Live Route Simulator
- Built on **Google Maps JavaScript API** & **Directions Service** using the provided key (`AIzaSyC4ftTohLewruuuHhPLhhw92yAtRQkF5pw`).
- Click-to-set start & destination points, or search locations directly.
- Travel mode switching: **Bicycle**, **Driving**, and **Walking**.
- Real-time **Ride Simulator**:
  - Play, Pause, and Reset controls.
  - Multi-speed scrubber (**1x, 2x, 5x, 10x**).
  - Smoothly drives/rides a marker along the exact Google Maps polyline coordinates.
  - Decrements distance in real time, automatically advances maneuver steps when within **< 25m**, triggers audio chimes, and streams packets over Web Bluetooth.

### D. Zero-Failure Dual-Engine Navigation Architecture
- **Leaflet + OSRM Dark Map Engine**: Enabled by default for web deployments (e.g. Vercel) with zero API keys required, unrestricted CORS, CartoDB Dark Matter tiles, and instant Nominatim search.
- **Google Maps JavaScript & Directions Engine**: Seamlessly switchable via the **⚙️ API / Engine** settings modal or header toggle button. If Google Maps encounters HTTP Referrer restrictions on your domain (`gm_authFailure`), the dashboard automatically and gracefully falls back to the Leaflet engine so the map never fails.
- In-app **⚙️ API / Engine Manager** allows inspecting or swapping Google Maps keys and viewing domain setup steps on the fly.

---

## 2. ESP32-C6 Hardware Compatibility

The web dashboard is fully compatible with the ESP32-C6 firmware in [`navpod_esp32c6/navpod_esp32c6.ino`](./navpod_esp32c6/navpod_esp32c6.ino):
- Receives navigation packets via Nordic UART Service (`6e400001-...`):
  `ICON_ID|DISTANCE_STR|STREET_NAME|STEP_NUM|ETA`
- Tap the **CONNECT BLE** button on the dashboard to pair with the ESP32-C6 in Chrome/Edge on Android or Bluefy on iOS.
