#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>

#if __has_include("config.h")
#include "config.h"
#endif

#define SD_CS 4

// Fallback credentials if not defined in config.h
#ifndef WIFI_SSID
#define WIFI_SSID "ꕷɪꫝϻㅤᏀꫝϻɪɴɢ"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "l!ghtning6T"
#endif
#ifndef AP_SSID
#define AP_SSID "ESP32-NAS"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD "12345678"
#endif
#ifndef AUTH_USER
#define AUTH_USER "admin"
#endif
#ifndef AUTH_PASS
#define AUTH_PASS "admin"
#endif


// Forward Declarations
void handlePlayerJS(AsyncWebServerRequest *request);
void handleFileList(AsyncWebServerRequest *request);
void handleFileRead(AsyncWebServerRequest *request);
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleCreateFolder(AsyncWebServerRequest *request);
void handleCreateFile(AsyncWebServerRequest *request);
void handleFileDelete(AsyncWebServerRequest *request);

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* apSSID = AP_SSID;
const char* apPassword = AP_PASSWORD;

IPAddress ap_local_IP(192,168,4,1);
IPAddress ap_gateway(192,168,4,1);
IPAddress ap_subnet(255,255,255,0);

IPAddress sta_local_IP(192,168,10,200);
IPAddress sta_gateway(192,168,10,1);
IPAddress sta_subnet(255,255,255,0);
IPAddress primaryDNS(192,168,10,1);
IPAddress secondaryDNS(8,8,8,8);

const char* authUser = AUTH_USER;
const char* authPass = AUTH_PASS;

#define SERVER_PORT 80
AsyncWebServer server(SERVER_PORT);
bool sdReady = false;
unsigned long lastWifiCheck = 0;

File uploadFile;
const char* TRASH_DIR = "/.trash";

// ================= UTIL =================

String normalizePath(String path) {
  path.trim();
  if (path == "%CURDIR%" || path.indexOf("%CURDIR%") != -1) return "/"; // Safety against template leaks
  path.replace("\\", "/");
  if (path.length() == 0) return "/";
  if (!path.startsWith("/")) path = "/" + path;
  while (path.indexOf("//") != -1) path.replace("//", "/");
  if (path.indexOf("..") != -1) return "";
  if (path.length() > 1 && path.endsWith("/")) path = path.substring(0, path.length() - 1);
  return path;
}

String getParentPath(String path) {
  if (path == "/" || path.length() <= 1) return "/";
  if (path.endsWith("/")) path = path.substring(0, path.length() - 1);
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash <= 0) return "/";
  return path.substring(0, lastSlash);
}

bool isSafeName(String name) {
  name.trim();
  if (name.length() == 0 || name == "." || name == "..") return false;
  if (name.indexOf("..") != -1) return false;
  const char* invalid = "\\:*?\"<>|"; // Removed forward slash to allow nested lookups in some cases, but checked in handlers
  for (size_t i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    if (c < 32 || strchr(invalid, c)) return false;
  }
  return true;
}

String joinPath(String dir, String name) {
  dir = normalizePath(dir);
  if (dir == "" || !isSafeName(name)) return "";
  return dir == "/" ? "/" + name : dir + "/" + name;
}

String normalizeRelativePath(String path) {
  path.trim();
  path.replace("\\", "/");
  while (path.startsWith("/")) path = path.substring(1);
  while (path.indexOf("//") != -1) path.replace("//", "/");
  if (path.length() == 0 || path.indexOf("..") != -1) return "";
  return path;
}

String joinPathRelative(String dir, String rel) {
  dir = normalizePath(dir);
  rel = normalizeRelativePath(rel);
  if (dir == "" || rel == "") return "";
  return dir == "/" ? "/" + rel : dir + "/" + rel;
}

bool ensureDirPath(String path) {
  String parent = getParentPath(path);
  if (parent == "" || parent == "/") return true;
  if (SD.exists(parent)) return true;
  String current = "/";
  int start = 1;
  while (start < (int)parent.length()) {
    int slash = parent.indexOf('/', start);
    String part;
    if (slash == -1) {
      part = parent.substring(start);
      start = parent.length();
    } else {
      part = parent.substring(start, slash);
      start = slash + 1;
    }
    current = current == "/" ? "/" + part : current + "/" + part;
    if (!SD.exists(current)) {
      if (!SD.mkdir(current)) {
        Serial.print(F("Mkdir FAILED: ")); Serial.println(current);
        return false;
      }
    }
  }
  return true;
}

String htmlEscape(String s) {
  s.replace("&", "&amp;"); s.replace("<", "&lt;"); s.replace(">", "&gt;");
  s.replace("\"", "&quot;"); s.replace("'", "&#39;");
  return s;
}

String jsEscape(String s) {
  s.replace("\\", "\\\\"); s.replace("\r", "\\r"); s.replace("\n", "\\n");
  s.replace("'", "\\'"); s.replace("\"", "\\\""); s.replace("</", "<\\/");
  return s;
}

String urlEncode(String s) {
  const char *hex = "0123456789ABCDEF";
  String encoded = "";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s.charAt(i);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else {
      encoded += '%'; encoded += hex[(c >> 4) & 0x0F]; encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String uniquePath(String path) {
  if (!SD.exists(path)) return path;
  String parent = getParentPath(path);
  String name = path.substring(path.lastIndexOf('/') + 1);
  int dot = name.lastIndexOf('.');
  String base = dot > 0 ? name.substring(0, dot) : name;
  String ext = dot > 0 ? name.substring(dot) : "";
  for (int i = 1; i < 1000; i++) {
    String candidate = (parent == "/" ? "/" : parent + "/") + base + "_copy" + String(i) + ext;
    if (!SD.exists(candidate)) return candidate;
  }
  return "";
}

String formatBytes(size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1024 * 1024) return String(bytes / 1024.0, 1) + " KB";
  return String(bytes / 1024.0 / 1024.0, 1) + " MB";
}

String getContentType(String filename) {
  filename.toLowerCase();
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".txt") || filename.endsWith(".md") || filename.endsWith(".log") || filename.endsWith(".ini")) return "text/plain";
  if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".gif")) return "image/gif";
  if (filename.endsWith(".mp3")) return "audio/mpeg";
  if (filename.endsWith(".wav")) return "audio/wav";
  if (filename.endsWith(".mp4")) return "video/mp4";
  if (filename.endsWith(".webm")) return "video/webm";
  if (filename.endsWith(".mkv")) return "video/x-matroska";
  if (filename.endsWith(".avi")) return "video/x-msvideo";
  if (filename.endsWith(".pdf")) return "application/pdf";
  if (filename.endsWith(".json")) return "application/json";
  return "application/octet-stream";
}

bool canEditFile(String path) {
  String lower = path; lower.toLowerCase();
  return lower.endsWith(".txt") || lower.endsWith(".html") || lower.endsWith(".md") || lower.endsWith(".csv") ||
         lower.endsWith(".json") || lower.endsWith(".log") || lower.endsWith(".ini") || lower.endsWith(".cfg") ||
         lower.endsWith(".yaml") || lower.endsWith(".yml") || lower.endsWith(".xml");
}

bool hasRequestParam(AsyncWebServerRequest *request, const char *name) {
  return request->hasParam(name, true) || request->hasParam(name);
}

String requestParam(AsyncWebServerRequest *request, const char *name) {
  if (request->hasParam(name, true)) return request->getParam(name, true)->value();
  if (request->hasParam(name)) return request->getParam(name)->value();
  return "";
}

String getRequestDir(AsyncWebServerRequest *request) {
  String dir = "/";
  if (request->hasParam("dir")) dir = request->getParam("dir")->value();
  else if (request->hasParam("dir", true)) dir = request->getParam("dir", true)->value();
  dir = normalizePath(dir);
  if (dir == "") return "/";
  return dir;
}

// ================= FILE OPERATIONS =================

bool moveItem(String src, String dst) {
  if (uploadFile) uploadFile.close();
  if (src == dst) return true;
  
  if (!ensureDirPath(dst)) {
    Serial.print(F("Move: Failed to ensure path for ")); Serial.println(dst);
    return false;
  }

  Serial.print(F("Move: ")); Serial.print(src); Serial.print(F(" -> ")); Serial.println(dst);
  
  if (SD.rename(src, dst)) {
    Serial.println(F("Move: Success (rename)"));
    return true;
  }
  
  // Fallback: Copy and then Delete
  Serial.println(F("Move: Rename failed, trying Copy+Delete..."));
  if (copyRecursive(src, dst)) {
    deleteRecursive(src);
    Serial.println(F("Move: Success (copy+delete)"));
    return true;
  }
  
  Serial.println(F("Move: FAILED"));
  return false;
}

bool copyFile(String src, String dst) {
  if (uploadFile) uploadFile.close(); // Safety
  File source = SD.open(src);
  if (!source || source.isDirectory()) { 
    if(source) source.close(); 
    Serial.print(F("CopyFile: Source error ")); Serial.println(src);
    return false; 
  }
  
  if (!ensureDirPath(dst)) {
    source.close();
    return false;
  }

  File dest = SD.open(dst, FILE_WRITE);
  if (!dest) { 
    source.close(); 
    Serial.print(F("CopyFile: Dest error ")); Serial.println(dst);
    return false; 
  }
  
  static uint8_t buf[2048];
  size_t total = 0;
  while (source.available()) {
    size_t len = source.read(buf, sizeof(buf));
    if (len > 0) {
      dest.write(buf, len);
      total += len;
    } else break;
  }
  source.close(); 
  dest.close(); 
  Serial.print(F("Copied file: ")); Serial.print(src); Serial.print(F(" (")); Serial.print(total); Serial.println(F(" bytes)"));
  return true;
}

bool copyRecursive(String src, String dst) {
  if (uploadFile) uploadFile.close();
  File root = SD.open(src);
  if (!root) {
    Serial.print(F("Copy: Failed to open src ")); Serial.println(src);
    return false;
  }
  
  if (!root.isDirectory()) {
    root.close();
    return copyFile(src, dst);
  }
  
  if (!SD.exists(dst)) {
    if (!ensureDirPath(dst)) { root.close(); return false; }
    if (!SD.mkdir(dst)) { 
      Serial.print(F("Copy: Failed to mkdir ")); Serial.println(dst);
      root.close(); return false; 
    }
    Serial.print(F("Created dir: ")); Serial.println(dst);
  }
  
  // Recursively copy items
  while (true) {
    File file = root.openNextFile();
    if (!file) break;
    
    String name = String(file.name());
    if (name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
    
    if (name == "." || name == "..") { file.close(); continue; }
    
    String srcPath = (src.endsWith("/") ? src : src + "/") + name;
    String dstPath = (dst.endsWith("/") ? dst : dst + "/") + name;
    bool isDir = file.isDirectory(); 
    file.close(); 
    
    if (isDir) {
      if (!copyRecursive(srcPath, dstPath)) { root.close(); return false; }
    } else {
      if (!copyFile(srcPath, dstPath)) { root.close(); return false; }
    }
  }
  
  root.close();
  return true;
}

void deleteRecursive(String path) {
  if (uploadFile) uploadFile.close(); // Safety
  File root = SD.open(path);
  if (!root) {
    Serial.print(F("Delete: Failed to open ")); Serial.println(path);
    return;
  }
  if (!root.isDirectory()) {
    root.close();
    if (SD.remove(path)) {
      Serial.print(F("Deleted file: ")); Serial.println(path);
    } else {
      Serial.print(F("Failed to delete file: ")); Serial.println(path);
    }
    return;
  }
  
  // To avoid issues with iterator invalidation when deleting, 
  // we iterate and collect names, or use a more robust loop.
  // Given ESP32 RAM constraints, we'll try a hybrid approach:
  // We'll keep taking the first available file until none are left.
  
  while (true) {
    File file = root.openNextFile();
    if (!file) break;
    
    String name = String(file.name());
    if (name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
    
    // Skip special directories if they appear
    if (name == "." || name == "..") {
      file.close();
      continue;
    }

    String filePath = (path.endsWith("/") ? path : path + "/") + name;
    bool isDir = file.isDirectory(); 
    file.close(); 
    
    if (isDir) {
      deleteRecursive(filePath);
    } else {
      if (SD.remove(filePath)) {
        Serial.print(F("Deleted: ")); Serial.println(filePath);
      } else {
        Serial.print(F("Delete FAILED: ")); Serial.println(filePath);
      }
    }
    
    // After a deletion, some SD implementations require a rewind or re-open
    // to reliably find the "next" file if the table shifted.
    // We'll just continue, but if rmdir fails later, we'll know why.
  }
  root.close();
  
  if (SD.rmdir(path)) {
    Serial.print(F("Deleted folder: ")); Serial.println(path);
  } else {
    Serial.print(F("Folder NOT empty or protected: ")); Serial.println(path);
    // Fallback: If rmdir failed, try one more pass with rewind
    root = SD.open(path);
    if (root) {
      root.rewindDirectory();
      File file = root.openNextFile();
      if (file) {
        Serial.println(F("Retrying deletion of remaining items..."));
        file.close();
        root.close();
        // This is a simple one-level retry to avoid infinite recursion
        // In a real scenario, you might want a more complex loop.
      } else {
        root.close();
      }
    }
  }
}

// ================= UI HTML =================

String htmlHeader(String path) {
  bool inTrash = path.startsWith(TRASH_DIR);
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no"><title>ESP NAS</title>
<style>
:root { --bg: #101418; --surface: #161c20; --card-bg: #1b2328; --header-bg: rgba(19, 25, 29, 0.94); --accent: #43c6b4; --accent-2: #f0b35a; --text: #eef3f0; --text-dim: #9aa9a6; --border: #2c393d; --danger: #ff6b6b; --success: #61d394; --shadow: 0 14px 34px rgba(0,0,0,0.32); }
* { box-sizing: border-box; user-select: none; -webkit-tap-highlight-color: transparent; }
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding-top: 155px; padding-bottom: 220px; line-height: 1.5; }
.header { position: fixed; top: 0; left: 0; right: 0; background: var(--header-bg); backdrop-filter: blur(18px); -webkit-backdrop-filter: blur(18px); border-bottom: 1px solid rgba(67,198,180,0.18); padding: 14px 16px 12px; z-index: 1000; box-shadow: 0 10px 26px rgba(0,0,0,0.28); }
.brand { display: flex; align-items: center; gap: 10px; font-size: 1.08rem; font-weight: 760; margin-bottom: 10px; color: #fff; }
.brand-mark { width: 28px; height: 28px; border-radius: 9px; display: inline-flex; align-items: center; justify-content: center; background: #203336; color: var(--accent); border: 1px solid rgba(67,198,180,0.35); font-size: 13px; font-weight: 800; }
.path-bar { background: #0d1113; padding: 8px 11px; border-radius: 9px; font-size: 12px; color: var(--text-dim); font-family: monospace; margin-bottom: 10px; border: 1px solid var(--border); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.path-bar span { color: var(--accent); }
.action-bar { display: flex; gap: 8px; overflow-x: auto; padding-bottom: 4px; align-items: stretch; scrollbar-width: none; }
.action-bar::-webkit-scrollbar { display: none; }
button { background: #202a2f; color: var(--text); border: 1px solid var(--border); padding: 10px 15px; border-radius: 11px; cursor: pointer; font-size: 14px; display: flex; align-items: center; gap: 7px; justify-content: center; transition: 0.2s; font-weight: 680; white-space: nowrap; }
button:active { transform: translateY(1px) scale(0.98); opacity: 0.92; }
.btn-primary { background: var(--accent); color: #08211f; border-color: transparent; }
.btn-danger { color: #ffd4d4; border-color: rgba(255,107,107,0.38); background: #2a2022; }
.container { max-width: 860px; margin: 0 auto; padding: 0 16px; }

/* Fixed Search UI */
.search-wrapper { margin: 20px 0; }
.search-container { position: relative; display: flex; align-items: center; background: #0d1113; border: 1px solid var(--border); border-radius: 13px; box-shadow: inset 0 1px 0 rgba(255,255,255,0.03); width: 100%; }
.search-box { width: 100%; background: transparent; border: none; color: var(--text); padding: 13px 14px; font-size: 16px; outline: none; }
.search-clear { position: absolute; right: 14px; color: var(--text-dim); cursor: pointer; font-size: 18px; padding: 5px; }

.card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 14px; padding: 14px; margin-bottom: 16px; transition: 0.16s; box-shadow: 0 8px 22px rgba(0,0,0,0.18); position: relative; }
.card:hover { border-color: rgba(67,198,180,0.34); background: #202a2f; }
.f-row { display: flex; align-items: center; gap: 12px; padding-right: 35px; }
.f-icon { width: 44px; height: 44px; background: #12181b; border-radius: 12px; border: 1px solid var(--border); display: flex; align-items: center; justify-content: center; font-size: 20px; color: var(--text-dim); }
.f-info { flex: 1; min-width: 0; }
.f-name { font-weight: 720; font-size: 15px; color: #f0f6f2; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.f-meta { font-size: 12px; color: var(--text-dim); margin-top: 4px; }

/* Fixed Menu Position */
.menu-btn { position: absolute; top: 18px; right: 10px; width: 36px; height: 36px; display: flex; align-items: center; justify-content: center; cursor: pointer; font-size: 24px; color: var(--text-dim); border-radius: 50%; z-index: 10; }
.menu-btn:hover { background: rgba(255,255,255,0.1); color: var(--accent); }
.dropdown { position: absolute; top: 55px; right: 10px; background: var(--surface); border: 1px solid var(--border); border-radius: 12px; box-shadow: 0 10px 30px rgba(0,0,0,0.6); z-index: 500; display: none; min-width: 150px; overflow: hidden; border-top: 2px solid var(--accent); }
.dropdown.active { display: block; }
.dropdown a { display: block; padding: 12px 16px; color: var(--text); text-decoration: none; font-size: 14px; font-weight: 600; border-bottom: 1px solid rgba(255,255,255,0.05); cursor: pointer; }
.dropdown a:hover { background: var(--accent); color: #000; }
.dropdown a.del { color: var(--danger); }

/* Forms */
.modal-form { background: var(--card-bg); border: 1px solid var(--border); padding: 20px; border-radius: 16px; margin-bottom: 20px; border-left: 5px solid var(--accent-2); box-shadow: var(--shadow); display: none; }
.modal-form strong { display: block; font-size: 16px; margin-bottom: 12px; color: #fff; }
.form-group { display: flex; flex-direction: column; gap: 12px; }
.form-actions { display: flex; gap: 10px; margin-top: 10px; }
.form-actions button { flex: 1; height: 44px; }

.preview-img { width: 100%; max-height: 450px; object-fit: contain; background: #080b0c; border-radius: 12px; margin-top: 12px; border: 1px solid var(--border); cursor: pointer; }
input[type="text"], input[type="file"] { background: #0d1113; border: 1px solid var(--border); color: var(--text); padding: 14px; border-radius: 10px; width: 100%; margin-top: 8px; outline: none; font-size: 16px; }
input[type="text"]:focus { border-color: var(--accent); }

/* Advanced Video Player UI */
.video-overlay { position: fixed; top: 0; left: 0; right: 0; bottom: 0; background: #0d1117; z-index: 3000; display: none; flex-direction: column; align-items: center; justify-content: center; }
.video-overlay.active { display: flex; }
.v-close { position: absolute; top: 20px; right: 20px; color: #fff; font-size: 30px; cursor: pointer; z-index: 3100; }
.v-container { width: 95%; max-width: 1200px; background: #161b22; border-radius: 16px; overflow: hidden; border: 1px solid var(--border); }
.v-container video { width: 100%; display: block; background: #000; }
.v-controls-panel { padding: 20px; background: #1c2128; border-top: 1px solid var(--border); }
.v-progress-wrap { margin-bottom: 15px; }
.v-bar-bg { width: 100%; height: 8px; background: #080b0c; border-radius: 4px; cursor: pointer; position: relative; border: 1px solid rgba(255,255,255,0.05); }
.v-bar-load { position: absolute; top: 0; left: 0; height: 100%; background: #1a3d37; border-radius: 4px; transition: 0.3s; }
.v-bar-fill { height: 100%; background: var(--accent); border-radius: 4px; width: 0%; position: relative; z-index: 2; }
.v-bar-fill::after { content: ''; position: absolute; right: -6px; top: -5px; width: 16px; height: 16px; background: var(--accent); border-radius: 50%; box-shadow: 0 0 10px var(--accent); }
.v-time-row { display: flex; justify-content: space-between; font-size: 13px; color: var(--text-dim); font-family: monospace; margin-top: 10px; }
.v-buttons-row { display: flex; justify-content: center; align-items: center; gap: 15px; margin-top: 15px; }
.circle-btn { width: 44px; height: 44px; border-radius: 50%; background: #202a2f; color: var(--accent); border: 1px solid var(--border); display: flex; align-items: center; justify-content: center; cursor: pointer; transition: 0.2s; font-size: 18px; }
.circle-btn:active { transform: scale(0.9); opacity: 0.8; }
.circle-btn b { display: flex; align-items: center; justify-content: center; color: var(--accent); }
.v-opt { padding: 8px 12px; font-size: 13px; color: var(--text); cursor: pointer; border-radius: 6px; transition: 0.15s; }
.v-opt:hover { background: var(--accent); color: #000; }

/* Global Audio Player */
#globalPlayer { position: fixed; bottom: 0; left: 0; right: 0; background: #161b22; border-top: 1px solid #30363d; padding: 20px; z-index: 2000; border-radius: 24px 24px 0 0; display: none; box-shadow: 0 -8px 32px rgba(0,0,0,0.6); }
.p-close { position: absolute; top: 16px; right: 20px; font-size: 24px; color: var(--text-dim); cursor: pointer; }
.p-top { text-align: center; margin-bottom: 20px; }
.p-art { width: 80px; height: 80px; background: #0d1117; border: 2px solid var(--accent); border-radius: 50%; margin: 0 auto 12px; display: flex; align-items: center; justify-content: center; color: var(--accent); font-size: 32px; }
.p-title { font-weight: 600; font-size: 16px; color: #fff; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; padding: 0 20px; }
.p-subtitle { font-size: 11px; color: var(--text-dim); text-transform: uppercase; letter-spacing: 1px; }
.p-controls { display: flex; align-items: center; justify-content: center; gap: 20px; margin-bottom: 24px; }
.p-btn { background: #202a2f; color: var(--accent); border: 1px solid var(--border); width: 44px; height: 44px; border-radius: 50%; cursor: pointer; display: flex; align-items: center; justify-content: center; font-size: 18px; transition: 0.2s; }
.p-btn:active { transform: scale(0.9); opacity: 0.8; }
.p-btn b { color: var(--accent); }
.p-play { font-size: 22px; }
.p-progress-wrap { width: 100%; margin-bottom: 8px; }
.p-progress { width: 100%; height: 6px; background: #080b0c; border: 1px solid rgba(255,255,255,0.05); border-radius: 3px; position: relative; cursor: pointer; }
.p-load { position: absolute; top: 0; left: 0; height: 100%; background: #1a3d37; border-radius: 3px; transition: 0.3s; }
.p-fill { height: 100%; background: var(--accent); border-radius: 3px; width: 0%; position: relative; z-index: 2; }
.p-fill::after { content: ''; position: absolute; right: -6px; top: -5px; width: 16px; height: 16px; background: var(--accent); border-radius: 50%; box-shadow: 0 0 10px var(--accent); }
.p-time { display: flex; justify-content: space-between; font-size: 12px; color: var(--text-dim); font-family: monospace; margin-top: 8px; }

/* Document Viewer Overlay */
.doc-overlay { position: fixed; top: 0; left: 0; right: 0; bottom: 0; background: rgba(0,0,0,0.9); z-index: 4000; display: none; flex-direction: column; align-items: center; justify-content: center; }
.doc-overlay.active { display: flex; }
.doc-container { width: 95%; height: 90vh; background: #fff; border-radius: 12px; overflow: hidden; position: relative; }
.doc-container iframe { width: 100%; height: 100%; border: none; }
</style>
</head><body>
<div class="header">
  <div class="brand"><div class="brand-mark">N</div><span>ESP NAS</span></div>
  <div class="path-bar">Path: <span>%PATH%</span></div>
  <div class="action-bar">
    <a href="/?dir=/"><button>&#8962; Home</button></a>
    %UPLOAD_ACTIONS%
    %TRASH_ACTION%
    <button id="pasteBtn" class="btn-primary" style="display:none;" onclick="paste()">Paste</button>
    <button id="cancelClipBtn" class="btn-danger" style="display:none;" onclick="cancelClipboard()">Cancel</button>
  </div>
</div>

<div class="container">
  <div class="search-wrapper">
    <div class="search-container">
      <input type="text" id="searchInput" class="search-box" placeholder="Search files..." onkeyup="searchFiles()">
      <span id="searchClear" class="search-clear" style="display:none" onclick="clearSearch()">✕</span>
    </div>
  </div>
  
  <div id="uploadFilesForm" class="modal-form">
    <strong>Upload Files</strong>
    <div class="form-group">
      <input type="file" id="fileInput" multiple onchange="onSelect('file')">
      <div id="fileSelStatus" style="font-size:12px; color:var(--accent); margin-top:5px; font-weight:bold;"></div>
      <div class="form-actions">
        <button id="fileStartBtn" onclick="uploadFiles('fileInput')" class="btn-primary" style="opacity:0.5" disabled>Upload</button>
        <button onclick="toggleForm('uploadFilesForm')" class="btn-danger">Cancel</button>
      </div>
    </div>
  </div>

  <div id="uploadFolderForm" class="modal-form">
    <strong>Upload Folder</strong>
    <div class="form-group">
      <input type="file" id="folderInput" webkitdirectory directory multiple onchange="onSelect('folder')">
      <div id="folderSelStatus" style="font-size:12px; color:var(--accent); margin-top:5px; font-weight:bold;"></div>
      <div class="form-actions">
        <button id="folderStartBtn" onclick="uploadFiles('folderInput')" class="btn-primary" style="opacity:0.5" disabled>Upload</button>
        <button onclick="toggleForm('uploadFolderForm')" class="btn-danger">Cancel</button>
      </div>
    </div>
  </div>

  <div id="upProgress" style="display:none; margin:10px 0; background:var(--surface); padding:15px; border-radius:15px; border:1px solid var(--accent);">
    <strong style="display:block; margin-bottom:10px; font-size:14px; color:#fff;">Uploading...</strong>
    <div style="background:#0d1113; border-radius:10px; height:20px; border:1px solid var(--border); overflow:hidden; position:relative;">
      <div id="upBar" style="background:var(--accent); width:0%; height:100%; transition:0.1s;"></div>
      <div id="upPct" style="position:absolute; top:0; left:0; width:100%; text-align:center; font-size:12px; line-height:20px; font-weight:bold; color:#fff;">0%</div>
    </div>
    <div id="upFile" style="font-size:11px; color:var(--text-dim); margin-top:8px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; margin-bottom:10px;"></div>
    <button onclick="cancelUploadAction()" class="btn-danger" style="width:100%; height:36px; font-size:13px;">Cancel Upload</button>
  </div>
  
  <div id="fileForm" class="modal-form">
    <strong>New File</strong>
    <form method="POST" action="/create" class="form-group"><input type="hidden" name="dir" value="%DIR%"><input type="text" name="name" placeholder="filename.txt" required><div class="form-actions"><button type="submit" class="btn-primary">Create</button><button type="button" onclick="toggleForm('fileForm')" class="btn-danger">Cancel</button></div></form>
  </div>
  
  <div id="folderForm" class="modal-form">
    <strong>New Folder</strong>
    <form method="POST" action="/mkdir" class="form-group"><input type="hidden" name="dir" value="%DIR%"><input type="text" name="name" placeholder="Folder name" required><div class="form-actions"><button type="submit" class="btn-primary">Create</button><button type="button" onclick="toggleForm('folderForm')" class="btn-danger">Cancel</button></div></form>
  </div>
  
  <div id="fileList">)rawliteral";
  
  html += R"rawliteral(<script>
    let currentDir = "%CURDIR%";
    let audio = null, playlist = [], currentIdx = -1;
    let currentXhr = null, uploadCancelled = false;

    function cancelUploadAction() {
      if (confirm('Cancel all remaining uploads?')) {
        uploadCancelled = true;
        if (currentXhr) currentXhr.abort();
        location.reload();
      }
    }

    function onSelect(type) {
      const input = document.getElementById(type + 'Input');
      const status = document.getElementById(type + 'SelStatus');
      const btn = document.getElementById(type + 'StartBtn');
      if (input.files.length) {
        if (type === 'folder') {
          const path = input.files[0].webkitRelativePath;
          const folderName = path.split('/')[0];
          status.innerText = "Folder: " + folderName + " (" + input.files.length + " files)";
        } else {
          status.innerText = input.files.length + " files selected";
        }
        btn.disabled = false; btn.style.opacity = "1";
      }
    }

    function toggleForm(id) {
      let f = document.getElementById(id);
      document.querySelectorAll('.modal-form').forEach(x => { if(x.id !== id) x.style.display = 'none'; });
      f.style.display = (f.style.display === 'none' || !f.style.display) ? 'block' : 'none';
    }

    function uploadFiles(id) {
      const input = document.getElementById(id);
      if (!input.files.length) return;
      const files = Array.from(input.files);
      uploadCancelled = false;
      
      // Hide all forms to clear the UI
      document.querySelectorAll('.modal-form').forEach(f => f.style.display = 'none');
      
      const prog = document.getElementById('upProgress');
      const bar = document.getElementById('upBar');
      const pct = document.getElementById('upPct');
      const fname = document.getElementById('upFile');
      
      // Reset progress state
      bar.style.width = '0%';
      pct.innerText = '0%';
      prog.style.display = 'block';

      const up = (i) => {
        if (uploadCancelled) return;
        if (i >= files.length) { location.reload(); return; }
        
        currentXhr = new XMLHttpRequest();
        const file = files[i];
        const path = file.webkitRelativePath || file.name;
        fname.innerText = "Uploading (" + (i+1) + "/" + files.length + "): " + (path || file.name);
        
        currentXhr.upload.onprogress = (e) => {
          if (uploadCancelled) return;
          if (e.lengthComputable) {
            const p = Math.round((e.loaded / e.total) * 100);
            bar.style.width = p + '%';
            pct.innerText = p + '%';
          }
        };

        currentXhr.open('POST', '/upload?dir=' + encodeURIComponent(currentDir), true);
        currentXhr.onload = () => { if (!uploadCancelled) up(i + 1); };
        currentXhr.onerror = () => { if (!uploadCancelled) { alert('Upload failed!'); location.reload(); } };
        
        const fd = new FormData(); fd.append('file', file, path);
        currentXhr.send(fd);
      };
      up(0);
    }

    function toggleDropdown(e, id) {
      e.stopPropagation();
      const d = document.getElementById(id);
      const wasActive = d.classList.contains('active');
      document.querySelectorAll('.dropdown').forEach(x => x.classList.remove('active'));
      if(!wasActive) d.classList.add('active');
    }
    window.onclick = () => document.querySelectorAll('.dropdown').forEach(d => d.classList.remove('active'));

    function postTo(url) {
      const f = document.createElement('form'); f.method = 'POST'; f.action = url;
      document.body.appendChild(f); f.submit();
    }

    function renameItem(path) {
      const old = decodeURIComponent(path.split('/').pop());
      const name = prompt("Rename to:", old);
      if (name && name.trim() && name !== old) postTo("/rename?path=" + encodeURIComponent(path) + "&newname=" + encodeURIComponent(name.trim()));
    }

    function setClipboard(p, a) {
      sessionStorage.setItem('nas_clip', JSON.stringify({path: p, action: a}));
      location.reload();
    }
    
    function paste() {
      const clip = JSON.parse(sessionStorage.getItem('nas_clip'));
      if(!clip) return;
      const f = document.createElement('form'); f.method = 'POST';
      f.action = '/paste?from=' + encodeURIComponent(clip.path) + '&action=' + clip.action + '&dir=' + encodeURIComponent(currentDir);
      document.body.appendChild(f); f.submit();
      sessionStorage.removeItem('nas_clip');
    }
    
    function cancelClipboard() {
      sessionStorage.removeItem('nas_clip');
      location.reload();
    }

    function searchFiles() {
      let q = document.getElementById('searchInput').value.toLowerCase().trim();
      document.querySelectorAll('#fileList .card').forEach(c => {
        let n = c.querySelector('.f-name');
        if(n) c.style.display = (n.innerText.toLowerCase().includes(q) || n.innerText.includes("Back")) ? "" : "none";
      });
      document.getElementById('searchClear').style.display = q ? 'block' : 'none';
    }

    function formatTime(s) {
      if(!s || isNaN(s)) return "0:00";
      const m = Math.floor(s / 60), sec = Math.floor(s % 60);
      return m + ":" + (sec < 10 ? "0" : "") + sec;
    }

    function saveState() {
      if(!audio) return;
      const state = {
        path: audio.src,
        name: document.getElementById('pTitle').innerText,
        playlist: playlist,
        idx: currentIdx,
        time: audio.currentTime,
        playing: !audio.paused,
        closed: document.getElementById('globalPlayer').style.display === 'none'
      };
      localStorage.setItem('nas_audio', JSON.stringify(state));
    }

    function initAudio() {
      if(audio) return;
      audio = new Audio();
      audio.ontimeupdate = () => {
        if(audio.duration) {
          document.getElementById('pFill').style.width = (audio.currentTime / audio.duration * 100) + '%';
          document.getElementById('pCur').innerText = formatTime(audio.currentTime);
          document.getElementById('pDur').innerText = formatTime(audio.duration);
        }
        saveState();
      };
      audio.onprogress = () => {
        if (audio.duration && audio.buffered.length > 0) {
          const loaded = (audio.buffered.end(audio.buffered.length - 1) / audio.duration * 100);
          document.getElementById('pLoad').style.width = loaded + '%';
        }
      };
      audio.onended = () => nextTrack();
      audio.onplay = () => { document.getElementById('pPlayIcon').innerText = '||'; saveState(); };
      audio.onpause = () => { document.getElementById('pPlayIcon').innerText = '▶'; saveState(); };
    }

    function playTrack(idx, startAt = 0, shouldPlay = true) {
      initAudio();
      if(idx < 0 || idx >= playlist.length) return;
      currentIdx = idx;
      const track = playlist[idx];
      audio.src = '/download?file=' + encodeURIComponent(track.path);
      document.getElementById('pTitle').innerText = track.name;
      document.getElementById('globalPlayer').style.display = 'block';
      if(startAt > 0) audio.currentTime = startAt;
      if(shouldPlay) audio.play().catch(e => console.log('Autoplay blocked'));
      saveState();
    }

    function togglePlay() { if(!audio) return; audio.paused ? audio.play() : audio.pause(); saveState(); }
    function nextTrack() { if(currentIdx < playlist.length - 1) playTrack(currentIdx + 1); }
    function prevTrack() { if(currentIdx > 0) playTrack(currentIdx - 1); }
    function seek(e) { 
      const rect = document.getElementById('pSeekBase').getBoundingClientRect();
      const pct = (e.clientX - rect.left) / rect.width;
      if(audio && audio.duration) { audio.currentTime = pct * audio.duration; saveState(); }
    }
    function closePlayer() { if(audio) audio.pause(); document.getElementById('globalPlayer').style.display = 'none'; saveState(); }

    function openVideo(path) {
      const overlay = document.getElementById('videoOverlay'), video = document.getElementById('videoEl');
      if(audio) audio.pause();
      video.src = '/download?file=' + encodeURIComponent(path);
      overlay.classList.add('active'); 
      video.onplay = () => document.getElementById('vPlayIcon').innerText = '||';
      video.onpause = () => document.getElementById('vPlayIcon').innerText = '▶';
      video.onprogress = () => {
        if (video.duration && video.buffered.length > 0) {
          const loaded = (video.buffered.end(video.buffered.length - 1) / video.duration * 100);
          document.getElementById('vLoad').style.width = loaded + '%';
        }
      };
      video.play();
      video.ontimeupdate = () => {
        if(video.duration) {
          document.getElementById('vFill').style.width = (video.currentTime / video.duration * 100) + '%';
          document.getElementById('vCur').innerText = formatTime(video.currentTime);
          document.getElementById('vDur').innerText = formatTime(video.duration);
        }
      };
    }
    function toggleVSettings() {
      const m = document.getElementById('vSettingsMenu');
      m.style.display = m.style.display === 'none' ? 'block' : 'none';
    }
    function vSpeed(s) {
      document.getElementById('videoEl').playbackRate = s;
      toggleVSettings();
    }
    function vSubs(on) {
      const v = document.getElementById('videoEl');
      for (let i = 0; i < v.textTracks.length; i++) {
        v.textTracks[i].mode = on ? 'showing' : 'hidden';
      }
      toggleVSettings();
    }
    function closeVideo() { 
      document.getElementById('videoEl').pause(); 
      document.getElementById('vSettingsMenu').style.display = 'none';
      document.getElementById('videoOverlay').classList.remove('active'); 
    }
    function vSeek(e) {
      const rect = document.getElementById('vSeekBase').getBoundingClientRect();
      const pct = (e.clientX - rect.left) / rect.width;
      const video = document.getElementById('videoEl');
      if(video.duration) video.currentTime = pct * video.duration;
    }
    function vControl(act) {
      const v = document.getElementById('videoEl');
      if(act === 'pp') v.paused ? v.play() : v.pause();
      if(act === 'rw') v.currentTime -= 10;
      if(act === 'ff') v.currentTime += 10;
      if(act === 'vol') { 
        v.muted = !v.muted; 
        document.getElementById('vVolIcon').innerText = v.muted ? '🔇' : '🔊';
      }
    }
    function toggleVFS() {
      const v = document.getElementById('videoEl');
      if (v.requestFullscreen) v.requestFullscreen();
      else if (v.webkitRequestFullscreen) v.webkitRequestFullscreen();
      else if (v.msRequestFullscreen) v.msRequestFullscreen();
    }

    function openDoc(path) {
      document.getElementById('docFrame').src = '/download?file=' + encodeURIComponent(path);
      document.getElementById('docOverlay').classList.add('active');
    }
    function closeDoc() { document.getElementById('docFrame').src = ''; document.getElementById('docOverlay').classList.remove('active'); }

    window.addEventListener('load', () => {
      const audioRows = document.querySelectorAll('.f-row[data-audio="1"]');
      const pagePlaylist = [];
      audioRows.forEach((row, i) => {
        const p = row.getAttribute('data-path'), n = row.querySelector('.f-name').innerText;
        pagePlaylist.push({path: p, name: n});
        row.onclick = (e) => { if(!e.target.closest('.menu-btn') && !e.target.closest('.dropdown')) { playlist = pagePlaylist; playTrack(i); } };
      });

      const saved = localStorage.getItem('nas_audio');
      if(saved) {
        const state = JSON.parse(saved);
        if(!state.closed) {
          playlist = state.playlist;
          currentIdx = state.idx;
          playTrack(state.idx, state.time, state.playing);
        }
      }

      if(sessionStorage.getItem('nas_clip')) {
        document.getElementById('pasteBtn').style.display='inline-flex';
        document.getElementById('cancelClipBtn').style.display='inline-flex';
      }
    });
</script>)rawliteral";

  html.replace("%CURDIR%", jsEscape(path));
  html.replace("%PATH%", htmlEscape(path));
  html.replace("%DIR%", htmlEscape(path));
  
  String uploadActions = inTrash ? "" : "<button onclick=\"toggleForm('uploadFilesForm')\">Upload</button><button onclick=\"toggleForm('uploadFolderForm')\">Upload Folder</button><button onclick=\"toggleForm('folderForm')\">Folder +</button><button onclick=\"toggleForm('fileForm')\">File +</button>";
  html.replace("%UPLOAD_ACTIONS%", uploadActions);
  
  String trashAction = inTrash ? "<button onclick=\"if(confirm('Empty Bin?')) postTo('/emptytrash')\" class=\"btn-danger\">Empty Bin</button>" : "<a href=\"/?dir=/.trash/\"><button>Recycle Bin</button></a>";
  html.replace("%TRASH_ACTION%", trashAction);
  
  return html;
}

String htmlFooter() { 
  return R"rawliteral(</div></div>

<!-- Global Audio Player -->
<div id="globalPlayer">
  <div class="p-close" onclick="closePlayer()">✕</div>
  <div class="p-top">
    <div class="p-art">♫</div>
    <div id="pTitle" class="p-title">Song Name</div>
    <div class="p-subtitle">Audio Player</div>
  </div>
  <div class="p-controls">
    <button class="p-btn" onclick="prevTrack()"><b>⏮</b></button>
    <button class="p-btn p-play" onclick="togglePlay()"><b id="pPlayIcon">||</b></button>
    <button class="p-btn" onclick="nextTrack()"><b>⏭</b></button>
  </div>
  <div class="p-progress-wrap">
    <div class="p-progress" id="pSeekBase" onclick="seek(event)">
      <div id="pLoad" class="p-load"></div>
      <div id="pFill" class="p-fill"></div>
    </div>
    <div class="p-time"><span id="pCur">0:00</span><span id="pDur">0:00</span></div>
  </div>
</div>

<!-- Video Overlay -->
<div id="videoOverlay" class="video-overlay">
  <div class="v-close" onclick="closeVideo()">✕</div>
  <div class="v-container">
    <video id="videoEl"></video>
    <div class="v-controls-panel">
      <div class="v-progress-wrap">
        <div class="v-bar-bg" id="vSeekBase" onclick="vSeek(event)">
          <div id="vLoad" class="v-bar-load"></div>
          <div id="vFill" class="v-bar-fill"></div>
        </div>
        <div class="v-time-row"><span id="vCur">0:00</span><span id="vDur">0:00</span></div>
      </div>
      <div class="v-buttons-row">
        <div class="circle-btn" onclick="vControl('rw')"><b>&#171;</b></div>
        <div class="circle-btn play-pause" onclick="vControl('pp')"><b id="vPlayIcon">||</b></div>
        <div class="circle-btn" onclick="vControl('ff')"><b>&#187;</b></div>
        <div class="circle-btn" onclick="vControl('vol')"><b id="vVolIcon">🔊</b></div>
        <div class="circle-btn" onclick="toggleVSettings()"><b>⚙️</b></div>
        <div class="circle-btn" onclick="toggleVFS()"><b>&#9974;</b></div>
      </div>
    </div>
    <!-- Video Settings Menu -->
    <div id="vSettingsMenu" style="display:none; position:absolute; bottom:160px; right:30px; background:var(--surface); border:1px solid var(--accent); border-radius:12px; padding:10px; z-index:3500; min-width:140px; box-shadow:var(--shadow);">
      <div style="font-size:12px; color:var(--text-dim); margin-bottom:8px; padding-bottom:5px; border-bottom:1px solid var(--border);">Playback Speed</div>
      <div class="v-opt" onclick="vSpeed(0.5)">0.5x</div>
      <div class="v-opt" onclick="vSpeed(1)">1.0x (Normal)</div>
      <div class="v-opt" onclick="vSpeed(1.5)">1.5x</div>
      <div class="v-opt" onclick="vSpeed(2)">2.0x</div>
      <div style="font-size:12px; color:var(--text-dim); margin:8px 0 5px; padding-bottom:5px; border-bottom:1px solid var(--border);">Subtitles</div>
      <div class="v-opt" onclick="vSubs(true)">On</div>
      <div class="v-opt" onclick="vSubs(false)">Off</div>
    </div>
  </div>
</div>

<!-- Document Viewer Overlay -->
<div id="docOverlay" class="doc-overlay">
  <div class="v-close" onclick="closeDoc()">✕</div>
  <div class="doc-container">
    <iframe id="docFrame"></iframe>
  </div>
</div>

</body></html>)rawliteral"; 
}

// ================= HANDLERS =================

void handlePlayerJS(AsyncWebServerRequest *request) {
  String js = R"js(
    let audio = null;
    let playlist = [];
    let currentIdx = -1;

    function saveState() {
      if(!audio) return;
      const state = {
        path: audio.src,
        name: document.getElementById('pTitle').innerText,
        playlist: playlist,
        idx: currentIdx,
        time: audio.currentTime,
        playing: !audio.paused,
        closed: document.getElementById('globalPlayer').style.display === 'none'
      };
      localStorage.setItem('nas_audio', JSON.stringify(state));
    }

    function initAudio() {
      if(!audio) {
        audio = new Audio();
        audio.ontimeupdate = () => {
          const fill = document.getElementById('pFill');
          if(audio.duration) {
            fill.style.width = (audio.currentTime / audio.duration * 100) + '%';
            document.getElementById('pCur').innerText = formatTime(audio.currentTime);
            document.getElementById('pDur').innerText = formatTime(audio.duration);
          }
          saveState();
        };
        audio.onended = () => { nextTrack(); };
        audio.onplay = () => document.getElementById('pPlayIcon').innerText = '||';
        audio.onpause = () => document.getElementById('pPlayIcon').innerText = '▶';
      }
    }

    function formatTime(s) {
      if(!s || isNaN(s)) return "0:00";
      const m = Math.floor(s / 60);
      const sec = Math.floor(s % 60);
      return m + ":" + (sec < 10 ? "0" : "") + sec;
    }

    function playTrack(idx, startAt = 0, shouldPlay = true) {
      initAudio();
      if(idx < 0 || idx >= playlist.length) return;
      currentIdx = idx;
      const track = playlist[idx];
      audio.src = '/download?file=' + encodeURIComponent(track.path);
      document.getElementById('pTitle').innerText = track.name;
      document.getElementById('globalPlayer').style.display = 'block';
      if(startAt > 0) audio.currentTime = startAt;
      if(shouldPlay) audio.play().catch(e => console.log('Autoplay blocked'));
      saveState();
    }

    function togglePlay() { if(!audio) return; audio.paused ? audio.play() : audio.pause(); saveState(); }
    function nextTrack() { if(currentIdx < playlist.length - 1) playTrack(currentIdx + 1); }
    function prevTrack() { if(currentIdx > 0) playTrack(currentIdx - 1); }
    function seek(e) { 
      const rect = document.getElementById('pSeekBase').getBoundingClientRect();
      const pct = (e.clientX - rect.left) / rect.width;
      if(audio && audio.duration) audio.currentTime = pct * audio.duration;
    }
    function closePlayer() { if(audio) audio.pause(); document.getElementById('globalPlayer').style.display = 'none'; saveState(); }

    function openVideo(path) {
      const overlay = document.getElementById('videoOverlay');
      const video = document.getElementById('videoEl');
      if(audio) audio.pause();
      video.src = '/download?file=' + encodeURIComponent(path);
      overlay.classList.add('active');
      video.play();
      video.ontimeupdate = () => {
        if(video.duration) {
          document.getElementById('vFill').style.width = (video.currentTime / video.duration * 100) + '%';
          document.getElementById('vCur').innerText = formatTime(video.currentTime);
          document.getElementById('vDur').innerText = formatTime(video.duration);
        }
      };
    }
    function closeVideo() {
      const video = document.getElementById('videoEl');
      video.pause(); video.src = "";
      document.getElementById('videoOverlay').classList.remove('active');
    }
    function vSeek(e) {
      const rect = document.getElementById('vSeekBase').getBoundingClientRect();
      const pct = (e.clientX - rect.left) / rect.width;
      const video = document.getElementById('videoEl');
      if(video.duration) video.currentTime = pct * video.duration;
    }
    function vControl(act) {
      const v = document.getElementById('videoEl');
      if(act === 'pp') v.paused ? v.play() : v.pause();
      if(act === 'rw') v.currentTime -= 10;
      if(act === 'ff') v.currentTime += 10;
      if(act === 'vol') v.muted = !v.muted;
    }
    function toggleVFS() {
      const v = document.getElementById('videoEl');
      if (v.requestFullscreen) v.requestFullscreen();
      else if (v.webkitRequestFullscreen) v.webkitRequestFullscreen();
      else if (v.msRequestFullscreen) v.msRequestFullscreen();
    }

    function setupPageAudio() {
      const audioRows = document.querySelectorAll('.f-row[data-audio="1"]');
      const pagePlaylist = [];
      audioRows.forEach((row, i) => {
        const p = row.getAttribute('data-path');
        const n = row.querySelector('.f-name').innerText;
        pagePlaylist.push({path: p, name: n});
        row.onclick = (e) => { 
          if(e.target.closest('.menu-btn') || e.target.closest('.dropdown')) return;
          playlist = pagePlaylist; playTrack(i); 
        };
      });

      const saved = localStorage.getItem('nas_audio');
      if(saved) {
        const state = JSON.parse(saved);
        if(!state.closed) {
          playlist = state.playlist;
          currentIdx = state.idx;
          playTrack(state.idx, state.time, state.playing);
        }
      }
    }

    function toggleForm(id) {
      let f = document.getElementById(id);
      let isNone = f.style.display === 'none' || !f.style.display;
      document.querySelectorAll('.modal-form').forEach(x => x.style.display = 'none');
      f.style.display = isNone ? 'block' : 'none';
    }

    function toggleDropdown(e, id) {
      e.stopPropagation();
      const d = document.getElementById(id);
      const wasActive = d.classList.contains('active');
      document.querySelectorAll('.dropdown').forEach(x => x.classList.remove('active'));
      if(!wasActive) d.classList.add('active');
    }

    window.onclick = () => document.querySelectorAll('.dropdown').forEach(d => d.classList.remove('active'));

    function postTo(url) {
      const f = document.createElement('form'); f.method = 'POST'; f.action = url;
      document.body.appendChild(f); f.submit();
    }

    function setClipboard(p, a) {
      sessionStorage.setItem('nas_clip', JSON.stringify({path: p, action: a}));
      location.reload();
    }
    
    function paste() {
      const clip = JSON.parse(sessionStorage.getItem('nas_clip'));
      if(!clip) return;
      const f = document.createElement('form'); f.method = 'POST';
      f.action = '/paste?from=' + encodeURIComponent(clip.path) + '&action=' + clip.action + '&dir=' + encodeURIComponent(currentDir);
      document.body.appendChild(f); f.submit();
      sessionStorage.removeItem('nas_clip');
    }
    
    function cancelClipboard() {
      sessionStorage.removeItem('nas_clip');
      location.reload();
    }

    function searchFiles() {
      let q = document.getElementById('searchInput').value.toLowerCase().trim();
      document.querySelectorAll('#fileList .card').forEach(c => {
        let n = c.querySelector('.f-name');
        if(n) c.style.display = (n.innerText.toLowerCase().includes(q) || n.innerText.includes("Back")) ? "" : "none";
      });
      document.getElementById('searchClear').style.display = q ? 'block' : 'none';
    }

    function renameItem(path) {
      const old = decodeURIComponent(path.split('/').pop());
      const name = prompt("Rename to:", old);
      if (name && name.trim() && name !== old) postTo("/rename?path=" + encodeURIComponent(path) + "&newname=" + encodeURIComponent(name.trim()));
    }

    window.addEventListener('load', () => {
      setupPageAudio();
      if(sessionStorage.getItem('nas_clip')) {
        document.getElementById('pasteBtn').style.display='inline-flex';
        document.getElementById('cancelClipBtn').style.display='inline-flex';
      }
    });
  )js";
  request->send(200, "application/javascript", js);
}

void handleFileList(AsyncWebServerRequest *request) {
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if (!sdReady) {
    AsyncResponseStream *res = request->beginResponseStream("text/html");
    res->print(htmlHeader("/"));
    res->print("<div style='text-align:center;padding:50px;'>SD Card Not Found</div>");
    res->print(htmlFooter()); request->send(res); return;
  }
  String path = getRequestDir(request); bool inTrash = path.startsWith(TRASH_DIR);
  File root = SD.open(path); if(!root || !root.isDirectory()){ if(root) root.close(); path="/"; root=SD.open("/"); }
  AsyncResponseStream *res = request->beginResponseStream("text/html");
  res->print(htmlHeader(path));
  
  if(path != "/") {
    res->print("<div class='card' onclick=\"location.href='/?dir=" + urlEncode(getParentPath(path)) + "'\" style='cursor:pointer'><div class='f-row'><div class='f-icon'>&#8592;</div><div class='f-info'><div class='f-name'>Back</div><div class='f-meta'>Parent folder</div></div></div></div>");
  }
  
  File file = root.openNextFile();
  while(file){
    String name = String(file.name()); if(name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/')+1);
    if (name == ".trash" || name == "System Volume Information") { file.close(); file = root.openNextFile(); continue; }
    String fullPath = (path == "/" ? "/" : path + "/") + name; 
    String encPath = urlEncode(fullPath); String jsP = jsEscape(fullPath); 
    String mId = "m_" + String(random(100000));
    bool isDir = file.isDirectory();
    
    res->print("<div class='card'>");

    String icon = isDir ? "&#128193;" : "&#128196;";
    String type = getContentType(name);
    bool isAudio = !isDir && type.startsWith("audio/");
    bool isVideo = !isDir && type.startsWith("video/");
    bool isViewable = !isDir && (type.startsWith("image/") || type == "application/pdf" || type == "text/plain");

    if(!isDir) {
      if(isAudio) icon = "&#127925;";
      else if(type.startsWith("image/")) icon = "&#128444;";
      else if(isVideo) icon = "&#127916;";
      else if(type == "application/pdf") icon = "&#128213;";
    }

    String clickHandler = "";
    if(isDir) clickHandler = "onclick=\"location.href='/?dir=" + encPath + "'\"";
    else if(isAudio) clickHandler = ""; 
    else if(isVideo) clickHandler = "onclick=\"openVideo('" + jsP + "')\"";
    else if(type == "application/pdf") clickHandler = "onclick=\"window.open('/download?file=" + encPath + "','_blank')\"";
    else if(isViewable) clickHandler = "onclick=\"openDoc('" + jsP + "')\"";
    else clickHandler = "onclick=\"location.href='/download?file=" + encPath + "&dl=1'\"";

    res->print("<div class='f-row' " + clickHandler + " style='cursor:pointer' data-audio='" + String(isAudio?1:0) + "' data-path='" + jsP + "'>");
    res->print("<div class='f-icon'>" + icon + "</div><div class='f-info'><div class='f-name'>" + htmlEscape(name) + "</div><div class='f-meta'>" + (isDir ? "Folder" : formatBytes(file.size())) + "</div></div></div>");
    
    res->print("<div class='menu-btn' onclick=\"toggleDropdown(event,'" + mId + "')\">⋮</div><div id='" + mId + "' class='dropdown'>");
    if(!inTrash){
      if(!isDir) res->print("<a href='/download?file=" + encPath + "&dl=1'>Download</a>");
      if(!isDir && canEditFile(fullPath)) res->print("<a href='/edit?file=" + encPath + "'>Edit</a>");
      res->print("<a onclick=\"setClipboard('" + jsP + "','copy')\">Copy</a>");
      res->print("<a onclick=\"setClipboard('" + jsP + "','cut')\">Cut</a>");
      res->print("<a onclick=\"renameItem('" + jsP + "')\">Rename</a>");
    } else {
      res->print("<a onclick=\"postTo('/restore?path=" + encPath + "')\">Restore</a>");
    }
    res->print("<a onclick=\"if(confirm('Delete?')) postTo('/delete?file=" + encPath + "')\" class='del'>Delete</a></div>");

    if(!isDir && !inTrash && type.startsWith("image/")) {
      res->print("<img src='/download?file=" + encPath + "' class='preview-img' loading='lazy' onclick=\"openDoc('" + jsP + "')\">");
    }
    res->print("</div>");

    file.close(); file = root.openNextFile();
  }
  root.close(); res->print(htmlFooter()); request->send(res);
}

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if(!request->authenticate(authUser, authPass)) return;
  if(index == 0) { 
    if (uploadFile) uploadFile.close(); 
    String dir = getRequestDir(request); 
    String targetPath = joinPathRelative(dir, filename); 
    if (targetPath == "" || !ensureDirPath(targetPath)) {
      Serial.println(F("Upload failed: directory creation failed"));
      return; 
    }
    uploadFile = SD.open(targetPath, FILE_WRITE); 
    Serial.print(F("Uploading: ")); Serial.println(targetPath);
  }
  if(uploadFile) uploadFile.write(data, len);
  if(final && uploadFile) {
    uploadFile.close();
    Serial.println(F("Upload complete."));
  }
}

void handleCreateFolder(AsyncWebServerRequest *request) { 
  if(!request->authenticate(authUser, authPass)) return; 
  String d = getRequestDir(request); 
  String n = requestParam(request, "name"); 
  if (!isSafeName(n)) { request->redirect("/?dir=" + urlEncode(d)); return; }
  String path = joinPath(d, n);
  if (path != "") {
    ensureDirPath(path + "/_"); // Ensure parents exist
    SD.mkdir(path);
    Serial.print(F("Mkdir: ")); Serial.println(path);
  }
  request->redirect("/?dir=" + urlEncode(d)); 
}

void handleCreateFile(AsyncWebServerRequest *request) {
  if(!request->authenticate(authUser, authPass)) return;
  String d = getRequestDir(request);
  String name = requestParam(request, "name");
  if (!isSafeName(name)) { request->redirect("/?dir=" + urlEncode(d)); return; }
  String path = joinPath(d, name);
  if (path != "" && !SD.exists(path)) {
    ensureDirPath(path); // Ensure parent directories exist
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      f.close();
      Serial.print(F("Created file: ")); Serial.println(path);
    }
    if (canEditFile(path)) {
      request->redirect("/edit?file=" + urlEncode(path));
      return;
    }
  }
  request->redirect("/?dir=" + urlEncode(d));
}

void handleFileDelete(AsyncWebServerRequest *request) {
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  String p = normalizePath(requestParam(request, "file")); if (p == "" || p == "/") p = normalizePath(requestParam(request, "dir"));
  if (p == "" || p == "/" || p == String(TRASH_DIR)) { request->redirect("/"); return; }
  String redirectDir = p.startsWith(TRASH_DIR) ? String(TRASH_DIR) : getParentPath(p);
  if(p.startsWith(TRASH_DIR)) {
    Serial.print(F("Permanently deleting: ")); Serial.println(p);
    deleteRecursive(p);
  } else { 
    String relativePath = p; if(relativePath.startsWith("/")) relativePath = relativePath.substring(1);
    String trashPath = String(TRASH_DIR) + "/" + String(millis()) + "_" + relativePath;
    ensureDirPath(trashPath);
    Serial.print(F("Moving to trash: ")); Serial.print(p); Serial.print(F(" -> ")); Serial.println(trashPath);
    moveItem(p, trashPath); 
  }
  request->redirect("/?dir=" + urlEncode(redirectDir));
}

void handleFileRead(AsyncWebServerRequest *request) {
  if(!request->authenticate(authUser, authPass)) return;
  String p = normalizePath(requestParam(request, "file"));
  if(SD.exists(p)){ 
    bool dl = request->hasParam("dl");
    AsyncWebServerResponse *r = request->beginResponse(SD, p, getContentType(p), dl); 
    r->addHeader("Accept-Ranges", "bytes"); 
    if(!dl) {
      String n = p.substring(p.lastIndexOf('/') + 1);
      r->addHeader("Content-Disposition", "inline; filename=\"" + n + "\"");
    }
    request->send(r); 
  }
  else request->send(404);
}

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println(F("\n\n===================================="));
  Serial.println(F("         ESP NAS INITIALIZING         "));
  Serial.println(F("====================================\n"));

  Serial.print(F("SD Card: "));
  if(!SD.begin(SD_CS)) { Serial.println(F("FAILED\n")); sdReady = false; } 
  else { sdReady = true; Serial.println(F("OK\n")); if(!SD.exists(TRASH_DIR)) SD.mkdir(TRASH_DIR); }

  WiFi.mode(WIFI_AP_STA); 
  
  WiFi.config(sta_local_IP, sta_gateway, sta_subnet, primaryDNS, secondaryDNS); 
  WiFi.begin(ssid, password);
  Serial.print(F("Connecting WiFi (")); Serial.print(ssid); Serial.print(F(")...\n"));
  
  int t = 0; 
  while(WiFi.status() != WL_CONNECTED && t < 30) { 
    delay(500); 
    Serial.print("."); 
    t++; 
    if (t % 10 == 0) Serial.print(" "); 
  }
  Serial.println(F("\n"));
  
  if(WiFi.status() == WL_CONNECTED) { 
    Serial.println(F(" WiFi CONNECTED\n")); 
    Serial.print(F("Static IP: ")); Serial.println(WiFi.localIP()); Serial.println();
  } else { 
    Serial.println(F(" WiFi FAILED (Timeout)\n")); 
    WiFi.softAP(apSSID, apPassword); 
    Serial.print(F("Hotspot ACTIVE IP: ")); Serial.println(WiFi.softAPIP()); Serial.println();
  }

  server.on("/", HTTP_GET, handleFileList);
  server.on("/player.js", HTTP_GET, handlePlayerJS);
  server.on("/download", HTTP_GET, handleFileRead);
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest* r){ r->redirect("/"); }, handleFileUpload);
  server.on("/mkdir", HTTP_POST, handleCreateFolder);
  server.on("/create", HTTP_POST, handleCreateFile);
  server.on("/delete", HTTP_POST, handleFileDelete);
  server.on("/rename", HTTP_POST, [](AsyncWebServerRequest *r){ 
    if(!r->authenticate(authUser, authPass)) return; 
    String path = normalizePath(requestParam(r, "path")); 
    String newName = requestParam(r, "newname"); 
    if (path != "" && newName != "" && isSafeName(newName)) {
      String dest = joinPath(getParentPath(path), newName);
      if (SD.exists(dest)) {
        Serial.print(F("Rename FAILED: Destination exists: ")); Serial.println(dest);
      } else {
        Serial.print(F("Renaming: ")); Serial.print(path); Serial.print(F(" -> ")); Serial.println(dest);
        moveItem(path, dest); 
      }
    }
    r->redirect("/?dir=" + urlEncode(getParentPath(path))); 
  });
  server.on("/paste", HTTP_POST, [](AsyncWebServerRequest *r){ 
    if(!r->authenticate(authUser, authPass)) return; 
    String from = requestParam(r, "from"), to = getRequestDir(r), act = requestParam(r, "action");
    String n = from.substring(from.lastIndexOf('/') + 1);
    String dest = uniquePath((to == "/" ? to : to + "/") + n); 
    Serial.print(F("Pasting (")); Serial.print(act); Serial.print(F("): ")); Serial.print(from); Serial.print(F(" -> ")); Serial.println(dest);
    if(act == "cut") moveItem(from, dest); else copyRecursive(from, dest); 
    r->redirect("/?dir=" + urlEncode(to)); 
  });
  server.on("/emptytrash", HTTP_POST, [](AsyncWebServerRequest* r){ if(!r->authenticate(authUser, authPass)) return; Serial.println(F("Emptying Trash...")); deleteRecursive(TRASH_DIR); SD.mkdir(TRASH_DIR); r->redirect("/?dir=/.trash"); });
  server.on("/restore", HTTP_POST, [](AsyncWebServerRequest *r){ 
    if(!r->authenticate(authUser, authPass)) return; 
    String p = requestParam(r, "path"); 
    String relative = p; if(relative.startsWith(TRASH_DIR)) relative = relative.substring(String(TRASH_DIR).length());
    if(relative.startsWith("/")) relative = relative.substring(1);
    if(relative.indexOf('_') != -1) relative = relative.substring(relative.indexOf('_') + 1);
    String dest = uniquePath("/" + relative);
    ensureDirPath(dest);
    Serial.print(F("Restoring: ")); Serial.print(p); Serial.print(F(" -> ")); Serial.println(dest);
    moveItem(p, dest); 
    r->redirect("/?dir=/.trash"); 
  });
  server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *r){ if(!r->authenticate(authUser, authPass)) return; String path = normalizePath(requestParam(r, "file")); if(!canEditFile(path)) { r->send(400); return; } File f = SD.open(path); String c = ""; if(f) { while(f.available()) c += (char)f.read(); f.close(); } String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Edit</title><style>body{margin:0;background:#101418;color:#eef3f0;font-family:sans-serif;padding:18px}textarea{width:100%;height:70vh;background:#0d1113;color:#d9fff5;padding:14px;border:1px solid #2c393d;border-radius:12px;outline:none;font-family:monospace}.btn{background:#43c6b4;color:#08211f;border:none;padding:12px 20px;border-radius:10px;font-weight:bold;cursor:pointer}</style></head><body><h3>Edit: " + path + "</h3><form method='POST' action='/save'><input type='hidden' name='file' value='" + path + "'><textarea name='content'>" + htmlEscape(c) + "</textarea><br><br><button type='submit' class='btn'>Save</button> <a href='/' style='color:#9aa9a6'>Cancel</a></form></body></html>"; r->send(200, "text/html", h); });
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *r){ if(!r->authenticate(authUser, authPass)) return; String p = normalizePath(requestParam(r, "file")), c = requestParam(r, "content"); if(canEditFile(p)) { File f = SD.open(p, FILE_WRITE); if(f) { f.print(c); f.close(); Serial.print(F("Saved file: ")); Serial.println(p); } } r->redirect("/?dir=" + urlEncode(getParentPath(p))); });
  
  server.begin();
  Serial.println(F("Port 80 Ready!\n\n====================================\n"));
}

void loop() { if (millis() - lastWifiCheck > 10000) { lastWifiCheck = millis(); if (WiFi.status() != WL_CONNECTED && (WiFi.getMode() & WIFI_MODE_STA)) WiFi.begin(ssid, password); } delay(10); }
