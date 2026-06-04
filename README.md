# ESP NAS

A powerful, minimal ESP32-based network storage project that exposes an SD card through a modern web file manager. It supports advanced folder management, recursive uploads, persistent media playback, and robust file operations.

## Features

### 📁 Advanced File System
- **Recursive Folder Uploads:** Upload entire directory structures directly from your browser with progress tracking.
- **Smart File Operations:** Create, Copy, Cut, Paste, Rename, and Restore files/folders with automatic fallback logic.
- **Robust Deletion:** Improved recursive deletion with "Rewind and Retry" to handle stubborn SD card folders.
- **Recycle Bin:** Deleted items are moved to `/.trash` with timestamped unique names for easy recovery.
- **Text Editor:** Create and edit common text formats (`.txt`, `.md`, `.json`, etc.) directly in the web UI.

### 🎥 Media and UI
- **Persistent Audio Player:** Music keeps playing seamlessly even as you navigate through different folders (powered by `localStorage`).
- **Enhanced Video Player:** Custom controls with 10-second skip, mute/unmute, and state-aware play/pause icons.
- **Inline PDF Viewing:** PDFs open directly in your browser's native viewer in a new tab.
- **Themed UI:** A modern, dark responsive interface with theme-synced minimalist circular controls.
- **Real-time Progress:** Visual progress bars with percentage indicators and "Cancel" support for all uploads.

### 📶 Connectivity
- **Dual Mode:** Station mode with Static IP and automatic Access Point (AP) fallback.
- **Dynamic Config:** Support for a `config.h` file for secure credential management.
- **Optimized Server:** Powered by `ESPAsyncWebServer` with `Accept-Ranges` for smooth media seeking.

## Hardware Requirements
- **ESP32** development board.
- **MicroSD** card module (SPI).
- **FAT32** formatted microSD card.

## Pin Mapping (Standard VSPI)
| ESP32 Pin | SD Module |
|-----------|-----------|
| GPIO 18   | SCK       |
| GPIO 19   | MISO      |
| GPIO 23   | MOSI      |
| GPIO 4    | CS / SS   |
| 3.3V      | VCC       |
| GND       | GND       |

## Installation

1. **Clone the repository:**
   ```bash
   git clone https://github.com/Tauhid3K/Mini_esp32_nas.git
   ```

2. **Configure Credentials:**
   Copy `config.example.h` to `config.h` and update your WiFi and login details. The project will automatically prioritize this file.

3. **Install Libraries:**
   - `WiFi`, `ESPAsyncWebServer`, `AsyncTCP`, `SPI`, `SD`.

4. **Upload:**
   Open `Mini_nas.ino` in Arduino IDE and upload to your ESP32.

5. **Access:**
   Open the IP address printed in the Serial Monitor (Default: `192.168.10.200`).

## License
This project is open-source and available under the MIT License.
