# Mini ESP32 NAS

A small ESP32-based network storage project that exposes an SD card through a web file manager. It supports browsing, uploads, downloads, simple file/folder operations, a recycle bin, basic text editing, and media previews.

## Features

### File System

- Async file uploads with progress feedback.
- Create, copy, cut, paste, rename, delete, and restore files/folders.
- Recycle bin stored in `/.trash`.
- Built-in editor for `.txt` and `.html` files.
- Client-side search/filtering.

### Media and UI

- Dark responsive web interface.
- Image previews for `.jpg`, `.png`, and `.gif`.
- Browser playback for common audio/video files.
- **Enhanced Video Controls**: Added 10-second skip forward/backward buttons for better playback control.
- Mobile-friendly layout.

### Connectivity

- Station mode with static IP.
- Access Point fallback mode.
- Web server powered by `ESPAsyncWebServer`.

## Hardware Requirements

- ESP32 development board.
- MicroSD card module using SPI.
- FAT32-formatted microSD card.

## Pin Mapping

| ESP32 Pin | SD Module |
|-----------|-----------|
| GPIO 18   | SCK       |
| GPIO 19   | MISO      |
| GPIO 23   | MOSI      |
| GPIO 4    | CS / SS   |
| 3.3V      | VCC       |
| GND       | GND       |

Standard VSPI pins are usually GPIO 18, 19, and 23, but check your ESP32 board variant.

## Installation

1. Clone the repository:

   ```bash
   git clone https://github.com/Tauhid3K/Mini_esp32_nas.git
   ```

2. Install the Arduino libraries:

   - `WiFi`
   - `ESPAsyncWebServer`
   - `AsyncTCP`
   - `SPI`
   - `SD`

3. Create your private config:

   Copy `config.example.h` to `config.h`, then update the WiFi, web login, and AP password.

4. Upload `Mini_nas.ino` to your ESP32.

5. Open the IP address printed in Serial Monitor.

   Default static IP in the sketch: `192.168.10.200`

## Security Notes

- Do not keep the default passwords in real use.
- `config.h` is ignored by Git so private credentials are not committed.
- Keep this device on a trusted local network unless you add stronger authentication and HTTPS.

## License

This project is open-source and available under the MIT License.
