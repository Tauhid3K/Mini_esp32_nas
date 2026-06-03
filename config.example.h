#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

#define AP_SSID "ESP32-Cloud"
#define AP_PASSWORD "change-this-ap-password"

#define AUTH_USER "admin"
#define AUTH_PASS "change-this-web-password"

#define FTP_USER AUTH_USER
#define FTP_PASS AUTH_PASS
// ESP32FtpServer uses port 21 by default. Changing this value only affects
// the Serial Monitor message unless you also modify the library's FTP_CTRL_PORT.
#define FTP_PORT 21
