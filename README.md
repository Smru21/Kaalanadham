<img width="739" height="1600" alt="12" src="https://github.com/user-attachments/assets/1403a747-8d20-4ca9-923e-6ef1423780be" />
<img width="1672" height="941" alt="Kalanadam2" src="https://github.com/user-attachments/assets/222ee320-17c4-4c81-847c-df386217200e" />
<img width="1536" height="1024" alt="Kalanadam3" src="https://github.com/user-attachments/assets/28e72ccc-6af4-4cc9-9ca3-c40a962b229e" />
<img width="739" height="1600" alt="8" src="https://github.com/user-attachments/assets/4e240937-9132-4793-b574-84035307ef05" />
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
