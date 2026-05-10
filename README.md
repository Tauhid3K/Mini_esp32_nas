# 🚀 Mini ESP32 NAS (Cloud Storage)

A full-featured, high-performance Network Attached Storage (NAS) and Cloud Storage solution built for the ESP32. This project transforms a simple ESP32 and an SD card into a powerful, web-based file management system with a modern "Glassmorphism" UI.

## ⚠️ Current Status
> **Note:** The FTP server functionality is currently not working correctly. I am aware of this issue and will be fixing it in a future update. For now, please use the Web Interface for file management.

## ✨ Features

### 📁 File System
- **Real-time Upload**: Asynchronous uploads with a live progress bar.
- **File Management**: Create, Copy, Cut, Paste, and Delete files and folders.
- **Recycle Bin**: Safety-first deletion system with **Restore** and **Empty Bin** capabilities (stored in `/.trash`).
- **Web Editor**: Built-in editor for `.txt` and `.html` files.
- **Search**: Instant, real-time file searching/filtering.

### 📸 Media & UI
- **Modern Dark UI**: Professional "GitHub-inspired" dark theme.
- **Image Previews**: Lazy-loaded thumbnails for `.jpg`, `.png`, and `.gif`.
- **MP3 Player**: Stream and play music (`.mp3`, `.wav`) directly from the browser.
- **Mobile Optimized**: Fully responsive UI for smartphones and tablets.

### 🌐 Connectivity
- **Dual WiFi Mode**: Supports both Station (STA) and Access Point (AP) modes.
- **Web Server**: High-speed file browsing via `ESPAsyncWebServer`.
- **Static IP**: Configurable static IP for easy network access.

## 🛠️ Hardware Requirements
- ESP32 Development Board.
- MicroSD Card Module (SPI).
- MicroSD Card (Formatted to FAT32).

## 🔌 Pin Mapping (Default)
| ESP32 Pin | SD Module |
|-----------|-----------|
| GPIO 18   | SCK       |
| GPIO 19   | MISO      |
| GPIO 23   | MOSI      |
| GPIO 4    | CS (SS)   |
| 3.3V      | VCC       |
| GND       | GND       |

*Note: SPI pins may vary depending on your ESP32 board variant. Standard VSPI pins are usually 18, 19, 23.*

## 🚀 Installation

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/Tauhid3K/Mini_esp32_nas.git
    ```
2.  **Install Libraries**: Ensure you have the following libraries installed in your Arduino IDE:
    - `WiFi`
    - `ESPAsyncWebServer`
    - `AsyncTCP`
    - `SPI`
    - `SD`
    - `ESP32FtpServer`
3.  **Configure WiFi**: Edit the `ssid` and `password` variables in `Mini_nas.ino`.
4.  **Upload**: Select your ESP32 board and click upload.
5.  **Access**: Open your browser and navigate to the IP address printed in the Serial Monitor (Default Static: `192.168.110.200`).
    - **Username:** `admin`
    - **Password:** `admin`

## 🛡️ License
This project is open-source and available under the MIT License.
