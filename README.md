# KĀLANĀDAM (Formerly Smart Bell) 🔔
### End-to-End ESP32-Based Automatic Institutional Bell & PA System

KĀLANĀDAM is a professional-grade, web-controlled automatic bell system designed for schools, hospitals, factories, and temples. It replaces manual timekeeping with a high-precision, battery-backed automation engine.

## 🚀 Key Features
- **Smart Scheduling:** Supports 100+ events with holiday overrides and interval scheduling.
- **Web-First Control:** Built-in web server with a Responsive PWA (Progressive Web App) interface. No app store download required.
- **Zero-Config Access:** Access via `http://kaalanadham.local` using mDNS—no IP address memorization needed.
- **Pro Audio:** I2S digital audio path with PCM5100A DAC and MEMS microphone for Live PA Announcements.
- **Visual Interface:** 2.4" TFT color display with dynamic themes and real-time status updates.
- **Institutional Ready:** 24/7 uptime design, battery-backed RTC (DS1307), and robust fail-safe boot sequence.

## 🛠️ Hardware Stack
- **MCU:** ESP32 (NodeMCU-32S)
- **Display:** 2.4" ILI9341 TFT (SPI)
- **Audio:** DFPlayer Mini (UART) + PCM5100A (I2S)
- **RTC:** DS1307 (I2C)
- **Input:** 4x Physical Buttons + MEMS Microphone

## 💻 Tech Stack
- **Firmware:** C++/Arduino (PlatformIO)
- **Web UI:** HTML5, CSS3, JavaScript (ES6), Async Web Server
- **Systems:** LittleFS, mDNS, NTP, REST API

## 📸 Screenshots
*(Coming Soon: Add photos of the device and Web UI here)*

## ⚡ Quick Start
1. Clone this repo.
2. Upload the `data/` folder to LittleFS using `PIO: Upload Filesystem Image`.
3. Flash the firmware using PlatformIO.
4. Connect to WiFi: `KĀLANĀDAM` (Pass: 1234).
5. Open `http://kaalanadham.local` to start the Setup Wizard.

## 📜 License
This project is licensed under the MIT License.