<img width="739" height="1600" alt="12" src="https://github.com/user-attachments/assets/1403a747-8d20-4ca9-923e-6ef1423780be" />
<img width="1672" height="941" alt="Kalanadam2" src="https://github.com/user-attachments/assets/222ee320-17c4-4c81-847c-df386217200e" />
<img width="1536" height="1024" alt="Kalanadam3" src="https://github.com/user-attachments/assets/28e72ccc-6af4-4cc9-9ca3-c40a962b229e" />
<img width="739" height="1600" alt="8" src="https://github.com/user-attachments/assets/4e240937-9132-4793-b574-84035307ef05" />
# KĀLANĀDAM (Formerly Smart Bell) 🔔
### End-to-End ESP32-Based Automatic Institutional Bell & PA System

KĀLANĀDAM is a professional-grade, web-controlled automatic bell system designed for schools, hospitals, factories, and temples. It replaces manual timekeeping with a high-precision, battery-backed automation engine.

---

## 🖥️ Desktop Companion App
For easier management of audio files and SD card synchronization, use the **KĀLANĀDAM Manager**. It automates the 4-digit file naming and folder structure required by the hardware.

👉 **[Download KĀLANĀDAM Manager Here](https://github.com/Smru21/K-LAN-DAM-Desktop-Manager-v1.0)**

---

## 🚀 Key Features
- **Smart Scheduling:** 100+ events, holiday overrides, and interval scheduling.
- **Web-First Control:** Responsive PWA interface served directly from the ESP32.
- **Zero-Config Access:** Access via `http://kaalanadham.local` using mDNS.
- **Pro Audio:** I2S digital audio path with PCM5100A DAC for high-quality bells and Live PA.
- **Visual Interface:** 2.4" TFT color display with dynamic themes and status updates.

## 🛠️ Hardware Stack
- **MCU:** ESP32 (NodeMCU-32S)
- **Display:** 2.4" ILI9341 TFT (SPI)
- **Audio:** DFPlayer Mini + PCM5100A (I2S)
- **RTC:** DS1307 (I2C)

## 💻 Tech Stack
- **Firmware:** C++/Arduino (PlatformIO)
- **Web UI:** HTML5, CSS3, JavaScript (ES6)
- **Systems:** LittleFS, mDNS, NTP, REST API

## 📜 License
MIT License
