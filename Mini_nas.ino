#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>

#define SD_CS 4

#if __has_include("config.h")
#include "config.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "ꕷɪꫝϻㅤᏀꫝϻɪɴɢ"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "l!ghtning6T"
#endif
#ifndef AP_SSID
#define AP_SSID "ESP32-Cloud"
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
// ================= WIFI =================

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const char* apSSID = AP_SSID;
const char* apPassword = AP_PASSWORD;

// AP MODE IP
IPAddress ap_local_IP(192,168,4,1);
IPAddress ap_gateway(192,168,4,1);
IPAddress ap_subnet(255,255,255,0);

// STA MODE STATIC IP
IPAddress sta_local_IP(192,168,10,200);
IPAddress sta_gateway(192,168,10,1);
IPAddress sta_subnet(255,255,255,0);

IPAddress primaryDNS(192,168,10,1);
IPAddress secondaryDNS(8,8,8,8);

// ================= AUTH =================

const char* authUser = AUTH_USER;
const char* authPass = AUTH_PASS;

// ================= SERVER =================

AsyncWebServer server(81);
bool sdReady = false;

// ================= FILE =================

File uploadFile;
const char* TRASH_DIR = "/.trash";

// ================= UTIL =================

String getParentPath(String path);
bool hasRequestParam(AsyncWebServerRequest *request, const char *name);
String requestParam(AsyncWebServerRequest *request, const char *name);

bool isSafeName(String name) {
  name.trim();
  if (name.length() == 0 || name == "." || name == "..") return false;
  const char* invalid = "/\\:*?\"<>|";
  for (size_t i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    if (c < 32 || strchr(invalid, c)) return false;
  }
  return name.indexOf("..") == -1;
}

String normalizePath(String path) {
  path.trim();
  path.replace("\\", "/");
  if (path.length() == 0) return "/";
  if (!path.startsWith("/")) path = "/" + path;
  while (path.indexOf("//") != -1) path.replace("//", "/");
  if (path.indexOf("..") != -1) return "";
  if (path.length() > 1 && path.endsWith("/")) path = path.substring(0, path.length() - 1);
  return path;
}

String joinPath(String dir, String name) {
  dir = normalizePath(dir);
  if (dir == "" || !isSafeName(name)) return "";
  return dir == "/" ? "/" + name : dir + "/" + name;
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

String jsEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\r", "\\r");
  s.replace("\n", "\\n");
  s.replace("'", "\\'");
  s.replace("\"", "\\\"");
  s.replace("</", "<\\/");
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
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
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
    String candidate = parent == "/" ? "/" : parent + "/";
    candidate += base + "_copy" + String(i) + ext;
    if (!SD.exists(candidate)) return candidate;
  }
  return "";
}

String getRequestDir(AsyncWebServerRequest *request) {
  String dir = hasRequestParam(request, "dir") ? requestParam(request, "dir") : "/";
  dir = normalizePath(dir);
  if (dir == "" || !SD.exists(dir)) return "/";
  File root = SD.open(dir);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return "/";
  }
  root.close();
  return dir;
}

bool canEditFile(String path) {
  String lower = path;
  lower.toLowerCase();
  return lower.endsWith(".txt") || lower.endsWith(".html");
}

bool hasRequestParam(AsyncWebServerRequest *request, const char *name) {
  return request->hasParam(name, true) || request->hasParam(name);
}

String requestParam(AsyncWebServerRequest *request, const char *name) {
  if (request->hasParam(name, true)) return request->getParam(name, true)->value();
  if (request->hasParam(name)) return request->getParam(name)->value();
  return "";
}

bool copyFile(String src, String dst) {
  File source = SD.open(src);
  if (!source || source.isDirectory()) return false;
  
  File dest = SD.open(dst, FILE_WRITE);
  if (!dest) { source.close(); return false; }

  static uint8_t buf[2048]; // Increased buffer for speed
  while (source.available()) {
    size_t len = source.read(buf, sizeof(buf));
    if (len > 0) dest.write(buf, len);
    else break;
  }
  source.close();
  dest.close();
  return true;
}

void copyRecursive(String src, String dst) {
  if (!SD.exists(dst)) SD.mkdir(dst);
  File root = SD.open(src);
  if (!root) return;
  
  File file = root.openNextFile();
  while (file) {
    String name = String(file.name());
    // In some SD versions file.name() is full path, in others just name.
    // We'll normalize to just the name.
    if (name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
    
    String srcPath = src; if(!srcPath.endsWith("/")) srcPath += "/"; srcPath += name;
    String dstPath = dst; if(!dstPath.endsWith("/")) dstPath += "/"; dstPath += name;
    
    bool isDir = file.isDirectory();
    file.close(); // Close before recursion to avoid too many open files
    
    if (isDir) copyRecursive(srcPath, dstPath);
    else copyFile(srcPath, dstPath);
    
    file = root.openNextFile();
  }
  root.close();
}

void deleteRecursive(String path) {
  File root = SD.open(path);
  if (!root) return;
  if (!root.isDirectory()) { 
    root.close(); 
    SD.remove(path); 
    return; 
  }
  
  File file = root.openNextFile();
  while (file) {
    String name = String(file.name());
    if (name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
    
    String filePath = path; if(!filePath.endsWith("/")) filePath += "/"; filePath += name;
    
    bool isDir = file.isDirectory();
    file.close();
    
    if (isDir) deleteRecursive(filePath);
    else SD.remove(filePath);
    
    file = root.openNextFile();
  }
  root.close();
  SD.rmdir(path);
}


String getParentPath(String path)
{
  if (path == "/" || path.length() <= 1) return "/";
  if (path.endsWith("/")) path = path.substring(0, path.length() - 1);
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash <= 0) return "/";
  return path.substring(0, lastSlash);
}

String formatBytes(size_t bytes)
{
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1024 * 1024) return String(bytes / 1024.0) + " KB";
  return String(bytes / 1024.0 / 1024.0) + " MB";
}

String esc(String s) {
  return jsEscape(s);
}

String getContentType(String filename)
{
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".txt")) return "text/plain";
  if (filename.endsWith(".jpg")) return "image/jpeg";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".json")) return "application/json";
  if (filename.endsWith(".mp3")) return "audio/mpeg";
  if (filename.endsWith(".wav")) return "audio/wav";
  if (filename.endsWith(".mp4")) return "video/mp4";
  if (filename.endsWith(".webm")) return "video/webm";
  if (filename.endsWith(".ogg")) return "video/ogg";
  if (filename.endsWith(".mov")) return "video/quicktime";
  return "application/octet-stream";
}

// ================= UI =================

String htmlHeader(String path)
{
  bool inTrash = path.startsWith("/.trash");
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>Cloud NAS</title>
<style>
:root { --bg: #101418; --surface: #161c20; --card-bg: #1b2328; --header-bg: rgba(19, 25, 29, 0.94); --accent: #43c6b4; --accent-2: #f0b35a; --text: #eef3f0; --text-dim: #9aa9a6; --border: #2c393d; --danger: #ff6b6b; --success: #61d394; --shadow: 0 14px 34px rgba(0,0,0,0.32); }
* { box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding-top: 142px; -webkit-tap-highlight-color: transparent; }
.header { position: fixed; top: 0; left: 0; right: 0; background: var(--header-bg); backdrop-filter: blur(18px); -webkit-backdrop-filter: blur(18px); border-bottom: 1px solid rgba(67,198,180,0.18); padding: 14px 16px 12px; z-index: 1000; box-shadow: 0 10px 26px rgba(0,0,0,0.28); }
.brand { display: flex; align-items: center; gap: 10px; font-size: 1.08rem; font-weight: 760; margin-bottom: 10px; color: #fff; }
.brand-mark { width: 28px; height: 28px; border-radius: 9px; display: inline-flex; align-items: center; justify-content: center; background: #203336; color: var(--accent); border: 1px solid rgba(67,198,180,0.35); font-size: 13px; font-weight: 800; }
.path-bar { background: #0d1113; padding: 8px 11px; border-radius: 9px; font-size: 12px; color: var(--text-dim); font-family: "SF Mono", Consolas, monospace; margin-bottom: 10px; border: 1px solid var(--border); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.path-bar span { color: var(--accent); }
.action-bar { display: flex; gap: 8px; overflow-x: auto; padding-bottom: 4px; align-items: stretch; scrollbar-width: none; -ms-overflow-style: none; }
.action-bar::-webkit-scrollbar { display: none; }
.action-bar a { display: flex; text-decoration: none; }
button { background: #202a2f; color: var(--text); border: 1px solid var(--border); padding: 10px 15px; border-radius: 11px; cursor: pointer; font-size: 14px; display: flex; align-items: center; gap: 7px; justify-content: center; transition: transform 0.16s, border-color 0.16s, background 0.16s; font-weight: 680; white-space: nowrap; }
button:hover { border-color: rgba(67,198,180,0.55); background: #243136; }
.action-bar button { min-width: 104px; height: 40px; flex-shrink: 0; }
button:active { transform: translateY(1px) scale(0.98); opacity: 0.92; }
.btn-primary { background: var(--accent); color: #08211f; border-color: transparent; box-shadow: 0 8px 18px rgba(67,198,180,0.2); }
.btn-danger { color: #ffd4d4; border-color: rgba(255,107,107,0.38); background: #2a2022; }
.btn-muted { background: #24252a; border-color: #3a3a42; }
.search-container { position: relative; margin: 18px 0 14px; }
.search-box { width: 100%; background: #0d1113; border: 1px solid var(--border); color: var(--text); padding: 13px 42px 13px 15px; border-radius: 13px; outline: none; transition: border-color 0.18s, box-shadow 0.18s; font-size: 16px; box-shadow: inset 0 1px 0 rgba(255,255,255,0.03); }
.search-box:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(67,198,180,0.14); }
.search-clear { position: absolute; right: 12px; top: 50%; transform: translateY(-50%); cursor: pointer; color: var(--text-dim); display: none; font-size: 18px; width: 26px; height: 26px; line-height: 24px; text-align: center; border-radius: 50%; background: #20282c; border: 1px solid var(--border); }
.container { max-width: 860px; margin: 0 auto; padding: 0 16px 42px; }
.card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 14px; padding: 14px; margin-bottom: 12px; transition: transform 0.16s, border-color 0.16s, background 0.16s; box-shadow: 0 8px 22px rgba(0,0,0,0.18); }
.card:hover { transform: translateY(-1px); border-color: rgba(67,198,180,0.34); background: #202a2f; }
.card-main { display: flex; align-items: center; gap: 13px; min-height: 42px; }
.icon { font-size: 23px; min-width: 42px; width: 42px; height: 42px; display: flex; align-items: center; justify-content: center; text-align: center; border-radius: 12px; background: #12181b; border: 1px solid var(--border); }
.info { flex-grow: 1; overflow: hidden; }
.name { font-weight: 720; font-size: 15px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.meta { font-size: 12px; color: var(--text-dim); margin-top: 4px; }
.actions { display: flex; gap: 8px; margin-top: 13px; padding-top: 12px; border-top: 1px solid var(--border); flex-wrap: wrap; }
.actions a { font-size: 13px; color: var(--accent); text-decoration: none; font-weight: 720; padding: 7px 10px; border-radius: 999px; background: #142226; border: 1px solid rgba(67,198,180,0.18); }
.actions a:hover { border-color: rgba(67,198,180,0.5); background: #173036; }
.actions a.download { color: #ffe1ad; background: #2b2518; border-color: rgba(240,179,90,0.26); }
.actions a.edit { color: #cfe5ff; background: #172434; border-color: rgba(109,170,235,0.24); }
.actions a.copy, .actions a.cut, .actions a.rename { color: #dffcf7; }
.actions a.del { color: #ffd5d5; background: #2b1d20; border-color: rgba(255,107,107,0.24); }
.actions a.restore { color: #d6ffe7; background: #173025; border-color: rgba(97,211,148,0.28); }
.preview-img { width: 100%; max-height: 450px; object-fit: contain; background: #080b0c; border-radius: 12px; margin-top: 12px; border: 1px solid var(--border); }
audio { width: 100%; height: 36px; margin-top: 12px; filter: invert(100%) hue-rotate(180deg) brightness(1.5); }
.modal-form { background: var(--card-bg); border: 1px solid rgba(67,198,180,0.24); padding: 18px; border-radius: 14px; margin-bottom: 18px; box-shadow: var(--shadow); border-left: 4px solid var(--accent-2); }
.modal-form strong { display: block; margin-bottom: 4px; font-size: 15px; }
.input-group { display: flex; flex-direction: column; gap: 10px; margin-top: 12px; }
input[type="text"], input[type="file"] { background: #0d1113; border: 1px solid var(--border); color: var(--text); padding: 12px; border-radius: 10px; width: 100%; outline: none; font-size: 16px; }
.input-group button { width: 100%; }
.progress-container { width: 100%; background: #0d1113; border-radius: 999px; height: 10px; margin-top: 14px; display: none; overflow: hidden; border: 1px solid var(--border); }
.progress-bar { width: 0%; height: 100%; background: var(--accent); border-radius: 999px; transition: width 0.1s; }
.progress-text { font-size: 12px; color: var(--text-dim); margin-top: 6px; text-align: center; display: none; font-weight: 600; }
.main-layout { display: flex; width: 100%; transition: all 0.3s; }
.content-area { flex: 1; min-width: 0; transition: margin-right 0.3s; }
.side-player { position: fixed; right: -350px; top: 128px; bottom: 20px; width: 320px; background: var(--card-bg); border: 1px solid var(--border); border-radius: 16px 0 0 16px; box-shadow: -10px 0 30px rgba(0,0,0,0.48); transition: all 0.3s ease; z-index: 1100; display: flex; flex-direction: column; padding: 20px; backdrop-filter: blur(20px); }
.side-player.active { right: 0; }
.player-header { display: flex; justify-content: flex-end; margin-bottom: 10px; }
.player-close { cursor: pointer; font-size: 18px; color: var(--text-dim); }
.player-info { text-align: center; margin-bottom: 20px; flex: 1; display: flex; flex-direction: column; justify-content: center; }
.player-art { width: 80px; height: 80px; background: #142226; border-radius: 50%; margin: 0 auto 16px; display: flex; align-items: center; justify-content: center; font-size: 32px; border: 1px solid rgba(67,198,180,0.32); color: var(--accent); }
.player-name { font-weight: 600; font-size: 14px; margin-bottom: 4px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.player-meta { font-size: 11px; color: var(--text-dim); text-transform: uppercase; letter-spacing: 1px; }
.player-controls { display: flex; align-items: center; justify-content: center; gap: 24px; margin-top: 10px; }
.ctrl-btn { cursor: pointer; font-size: 20px; color: var(--text); transition: all 0.2s; display: flex; align-items: center; justify-content: center; }
.ctrl-btn:hover { color: var(--accent); transform: scale(1.1); }
.ctrl-btn.play-pause { font-size: 32px; color: var(--accent); }
.player-progress { width: 100%; margin-top: 20px; }
input[type="range"].seek-bar { width: 100%; height: 4px; background: var(--border); border-radius: 2px; outline: none; -webkit-appearance: none; cursor: pointer; }
input[type="range"].seek-bar::-webkit-slider-thumb { -webkit-appearance: none; width: 12px; height: 12px; background: var(--accent); border-radius: 50%; box-shadow: 0 0 10px rgba(67,198,180,0.48); }
.time-info { display: flex; justify-content: space-between; font-size: 10px; color: var(--text-dim); margin-top: 6px; font-family: monospace; }
.video-placeholder { width: 100%; height: 180px; background: #080b0c; border-radius: 12px; margin-top: 12px; display: flex; flex-direction: column; align-items: center; justify-content: center; cursor: pointer; border: 1px solid var(--border); transition: all 0.2s; position: relative; overflow: hidden; }
.video-placeholder:hover { border-color: var(--accent); background: #101819; }
.play-btn { color: var(--accent); display: flex; align-items: center; justify-content: center; font-size: 40px; margin-bottom: 8px; z-index: 2; }
.video-placeholder span { font-size: 13px; color: var(--text-dim); z-index: 2; }
.audio-placeholder { width: 100%; background: #0d1113; border-radius: 12px; margin-top: 12px; display: flex; align-items: center; padding: 11px 15px; cursor: pointer; border: 1px solid var(--border); transition: all 0.2s; gap: 12px; }
.audio-placeholder:hover { border-color: var(--accent); background: #142226; }
.play-btn-sm { color: var(--accent); display: flex; align-items: center; justify-content: center; font-size: 20px; flex-shrink: 0; }
.audio-placeholder span { font-size: 14px; font-weight: 500; }
.video-loader { position: absolute; top: 0; left: 0; right: 0; bottom: 0; background: rgba(0,0,0,0.8); display: flex; flex-direction: column; align-items: center; justify-content: center; z-index: 5; border-radius: 10px; }
.loader-pct { display: none; }
.loader-bar-bg { display: none; }
.loader-bar-fill { display: none; }
.empty-state { text-align: center; padding: 46px 18px; color: var(--text-dim); border: 1px dashed var(--border); border-radius: 16px; background: rgba(27,35,40,0.52); }
.empty-state h3 { margin: 10px 0 5px; color: var(--text); font-size: 18px; }
.empty-state p { margin: 0; font-size: 14px; }
.empty-icon { font-size: 44px; }
@media (max-width: 768px) {
  .side-player { width: 100%; right: 0; top: auto; bottom: -100%; border-radius: 20px 20px 0 0; border-left: none; border-top: 1px solid var(--border); height: auto; max-height: 80vh; }
  .side-player.active { bottom: 0; }
  .content-area.active-player { margin-right: 0; padding-bottom: 100px; }
}
@media (min-width: 769px) {
  .content-area.active-player { margin-right: 320px; }
}
@media (max-width: 480px) {
  body { padding-top: 145px; }
  .header { padding: 10px 12px; }
  .brand { font-size: 1rem; margin-bottom: 6px; }
  .action-bar { gap: 6px; }
  .action-bar button { min-width: 96px; padding: 0 11px; font-size: 13px; height: 36px; }
  .card { padding: 12px; }
  .icon { font-size: 21px; min-width: 38px; width: 38px; height: 38px; }
}
</style>
<script src="/player.js"></script>
<script>
const currentDir = ")rawliteral";
  html += jsEscape(path);
  html += R"rawliteral(";
function search() {
  let q = document.getElementById('q').value.toLowerCase();
  let cards = document.getElementsByClassName('card');
  let clearBtn = document.getElementById('searchClear');
  if(clearBtn) clearBtn.style.display = q ? "block" : "none";
  for(let i=0; i<cards.length; i++) {
    let nameElement = cards[i].querySelector('.name');
    if (nameElement) {
      let text = nameElement.innerText.toLowerCase();
      cards[i].style.display = text.includes(q) ? "" : "none";
    }
  }
}
function clearSearch() {
  document.getElementById('q').value = '';
  search();
  document.getElementById('q').focus();
}
function toggleForm(id) {
  let f = document.getElementById(id);
  f.style.display = f.style.display === 'none' ? 'block' : 'none';
}
async function uploadFiles() {
  const fileInput = document.getElementById('fileInput');
  const files = fileInput.files;
  if (files.length === 0) return;
  document.getElementById('progressContainer').style.display = 'block';
  document.getElementById('progressText').style.display = 'block';
  for (let i = 0; i < files.length; i++) {
    const formData = new FormData();
    formData.append("upload", files[i]);
    await new Promise((resolve) => {
      const xhr = new XMLHttpRequest();
      xhr.upload.addEventListener("progress", (e) => {
        if (e.lengthComputable) {
          const filePercent = Math.round((e.loaded / e.total) * 100);
          const totalPercent = Math.round(((i + (e.loaded / e.total)) / files.length) * 100);
          document.getElementById('progressBar').style.width = totalPercent + '%';
          document.getElementById('progressText').innerText = `File ${i+1}/${files.length}: ${filePercent}%`;
        }
      });
      xhr.onreadystatechange = () => { if (xhr.readyState === 4) resolve(); };
      xhr.open("POST", "/upload?dir=" + encodeURIComponent(currentDir), true);
      xhr.send(formData);
    });
  }
  window.location.reload();
}
function stopAllMedia() {
  document.querySelectorAll('video, audio').forEach(p => { p.pause(); p.src = ""; p.load(); });
}
function skipVideo(id, sec) {
  const v = document.getElementById(id).querySelector('video');
  if(v) v.currentTime += sec;
}
function playVideo(path, id, type) {
  stopAllMedia();
  const container = document.getElementById(id);
  container.innerHTML = `
    <div class="video-loader" id="loader_${id}" style="display:none;background:rgba(0,0,0,0.5)">
      <div class="loader-pct">0%</div>
      <div class="loader-bar-bg"><div class="loader-bar-fill"></div></div>
    </div>
    <video controls autoplay class='preview-img' style='margin-top:0' preload="auto"><source src='/download?file=${encodeURIComponent(path)}' type='${type}'>Your browser does not support the video tag.</video>
    <div style="display:flex;justify-content:center;gap:12px;margin-top:10px">
      <button onclick="skipVideo('${id}',-10)" style="height:34px;min-width:70px;padding:0;font-size:13px">&#171; 10s</button>
      <button onclick="skipVideo('${id}',10)" style="height:34px;min-width:70px;padding:0;font-size:13px">10s &#187;</button>
    </div>`;
  const video = container.querySelector('video');
  const loader = container.querySelector('.video-loader');
  video.onwaiting = () => { loader.style.display = 'flex'; };
  video.onplaying = () => { loader.style.display = 'none'; };
  video.oncanplay = () => { video.play(); loader.style.display = 'none'; };
  container.onclick = null;
  container.style.height = 'auto';
}
</script>
</head>
<body>
<div class="header">
  <div class="brand"><span class="brand-mark">N</span><span>Cloud NAS</span></div>
  <div class="path-bar">Path: <span id="path-bar-text">)rawliteral";
  html += htmlEscape(path);
  html += R"rawliteral(</span></div>
  <div class="action-bar">
    <a href="/?dir=/" style="text-decoration:none"><button>&#8962; Home</button></a>
    <div id="controlsContainer" style="display:flex;gap:8px">)rawliteral";

  if(!inTrash) {
    html += R"rawliteral(<button onclick="toggleForm('uploadForm')">Upload</button><button onclick="toggleForm('folderForm')">Folder +</button><button onclick="toggleForm('fileForm')">File +</button><a href="/?dir=/.trash/" style="text-decoration:none"><button class="btn-muted">Recycle Bin</button></a>)rawliteral";
  } else {
    html += R"rawliteral(<button class="btn-danger" onclick="confirmEmpty()">Empty Bin</button>)rawliteral";
  }

  html += R"rawliteral(</div>
    <button id="pasteBtn" class="btn-primary" style="display:none;" onclick="paste()">Paste</button>
    <button id="cancelPasteBtn" class="btn-danger" style="display:none;margin-left:4px;" onclick="clearClipboard()">Cancel</button>
  </div>
</div>
<div class="container">
  <div class="search-container">
    <input type="text" id="q" class="search-box" placeholder="Search this folder" onkeyup="search()">
    <span id="searchClear" class="search-clear" onclick="clearSearch()">x</span>
  </div>)rawliteral";

  if(!inTrash) {
    html += R"rawliteral(
      <div id="uploadForm" class="modal-form" style="display:none"><strong>Upload Files</strong><div class="input-group"><input type="file" id="fileInput" multiple><button type="button" class="btn-primary" onclick="uploadFiles()">Upload</button><button type="button" class="btn-danger" onclick="toggleForm('uploadForm')">Cancel</button></div><div id="progressContainer" class="progress-container"><div id="progressBar" class="progress-bar"></div></div><div id="progressText" class="progress-text"></div></div>
      <div id="folderForm" class="modal-form" style="display:none"><strong>New Folder</strong><form method="POST" action="/mkdir" class="input-group"><input type="hidden" name="dir" value="%DIR_ATTR%"><input type="text" name="name" placeholder="Name"><button type="submit" class="btn-primary">Create</button><button type="button" class="btn-danger" onclick="toggleForm('folderForm')">Cancel</button></form></div>
      <div id="fileForm" class="modal-form" style="display:none"><strong>New File</strong><form method="POST" action="/mkfile" class="input-group"><input type="hidden" name="dir" value="%DIR_ATTR%"><input type="text" name="name" placeholder="Name.txt"><button type="submit" class="btn-primary">Create</button><button type="button" class="btn-danger" onclick="toggleForm('fileForm')">Cancel</button></form></div>
    )rawliteral";
  }
  
  html += R"rawliteral(<div class="main-layout"><div class="content-area"><div id="fileList">)rawliteral";
  html.replace("%DIR_ATTR%", htmlEscape(path));
  return html;
}

String htmlFooter()
{
  return R"rawliteral(</div></div><div id="sidePlayer" class="side-player"><div class="player-header"><span class="player-close" onclick="closePlayer()">&#10005;</span></div><div class="player-info"><div class="player-art">&#9835;</div><div id="pName" class="player-name"></div><div class="player-meta">Audio Player</div><div id="playerContent"></div></div></div></div></div></body></html>)rawliteral";
}


// ================= FILE LIST =================

void handleFileList(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  String currentPath = getRequestDir(request);
  bool inTrash = currentPath.startsWith(TRASH_DIR);
  File root = SD.open(currentPath);
  if(!root || !root.isDirectory()) { currentPath = "/"; root = SD.open("/"); }

  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->print(htmlHeader(currentPath));

  if(currentPath != "/") {
    response->print("<div class='card' onclick=\"location.href='/?dir=" + urlEncode(getParentPath(currentPath)) + "'\" style='cursor:pointer'><div class='card-main'><div class='icon'>&#8592;</div><div class='info'><div class='name'>Back</div><div class='meta'>Parent folder</div></div></div></div>");
  }

  int count = 0;
  File file = root.openNextFile();
  while(file) {
    String name = String(file.name());
    if(name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
    if(file.isDirectory() && name != ".trash") {
      String fullPath = currentPath; if(!fullPath.endsWith("/")) fullPath += "/"; fullPath += name;
      String encodedPath = urlEncode(fullPath);
      String jsPath = jsEscape(fullPath);
      String card = "<div class='card'><div class='card-main' onclick=\"location.href='/?dir=" + encodedPath + "'\" style='cursor:pointer'><div class='icon'>&#128193;</div><div class='info'><div class='name'>" + htmlEscape(name) + "</div><div class='meta'>Folder</div></div></div>";
      card += "<div class='actions'>";
      if(!inTrash) {
        card += "<a class='copy' href='#' onclick=\"setClipboard('" + jsPath + "', 'copy')\">Copy</a>";
        card += "<a class='cut' href='#' onclick=\"setClipboard('" + jsPath + "', 'cut')\">Cut</a>";
        card += "<a class='rename' href='#' onclick=\"renameItem('" + jsPath + "')\">Rename</a>";
      }
      if(inTrash) card += "<a href='#' onclick=\"postTo('/restore?path=" + encodedPath + "');return false;\" class='restore'>Restore</a>";
      card += "<a href='#' onclick=\"confirmDelete('/rmdir?dir=" + encodedPath + "', " + (inTrash ? "true" : "false") + ")\" class='del'>Delete</a></div></div>";
      response->print(card);
      count++;
    }
    file = root.openNextFile();
  }
  
  root.rewindDirectory(); 
  file = root.openNextFile();
  while(file) {
    String name = String(file.name());
    if(name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
    if(!file.isDirectory()) {
      String fullPath = currentPath; if(!fullPath.endsWith("/")) fullPath += "/"; fullPath += name;
      String nameLower = name; nameLower.toLowerCase();
      String encodedPath = urlEncode(fullPath);
      String jsPath = jsEscape(fullPath);
      String icon = "&#128196;";
      if (nameLower.endsWith(".mp3") || nameLower.endsWith(".wav")) icon = "&#127925;";
      else if (nameLower.endsWith(".jpg") || nameLower.endsWith(".png") || nameLower.endsWith(".gif")) icon = "&#128444;";
      else if (nameLower.endsWith(".mp4") || nameLower.endsWith(".webm") || nameLower.endsWith(".ogg") || nameLower.endsWith(".mov")) icon = "&#127916;";

      String card = "<div class='card'><div class='card-main'><div class='icon'>" + icon + "</div><div class='info'><div class='name'>" + htmlEscape(name) + "</div><div class='meta'>" + formatBytes(file.size()) + "</div></div></div>";
      if(!inTrash) {
        if(nameLower.endsWith(".jpg") || nameLower.endsWith(".png") || nameLower.endsWith(".gif")) card += "<img src='/download?file=" + encodedPath + "' class='preview-img' loading='lazy'>";
        else if(nameLower.endsWith(".mp3") || nameLower.endsWith(".wav")) {
          String audId = "aud_" + String(random(100000));
          card += "<div id='" + audId + "' class='audio-placeholder' data-path='" + htmlEscape(fullPath) + "' data-type='" + getContentType(name) + "' onclick=\"playAudio('" + jsPath + "', '" + audId + "', '" + getContentType(name) + "')\">";
          card += "<div class='play-btn-sm'>&#9654;</div><span>Tap to Play Audio</span></div>";
        }
        else if(nameLower.endsWith(".mp4") || nameLower.endsWith(".webm") || nameLower.endsWith(".ogg") || nameLower.endsWith(".mov")) {
          String vidId = "vid_" + String(random(100000));
          card += "<div id='" + vidId + "' class='video-placeholder' data-path='" + htmlEscape(fullPath) + "' data-type='" + getContentType(name) + "' onclick=\"playVideo('" + jsPath + "', '" + vidId + "', '" + getContentType(name) + "')\">";
          card += "<div class='play-btn'>&#9654;</div><span>Tap to Play Video</span></div>";
        }
      }
      card += "<div class='actions'>";
      if(!inTrash) {
        card += "<a class='download' href='/download?file=" + encodedPath + "&dl=1'>Download</a>";
        if(canEditFile(fullPath)) card += "<a class='edit' href='/edit?file=" + encodedPath + "'>Edit</a>";
        card += "<a class='copy' href='#' onclick=\"setClipboard('" + jsPath + "', 'copy')\">Copy</a>";
        card += "<a class='cut' href='#' onclick=\"setClipboard('" + jsPath + "', 'cut')\">Cut</a>";
        card += "<a class='rename' href='#' onclick=\"renameItem('" + jsPath + "')\">Rename</a>";
      } else {
        card += "<a href='#' onclick=\"postTo('/restore?path=" + encodedPath + "');return false;\" class='restore'>Restore</a>";
      }
      card += "<a href='#' onclick=\"confirmDelete('/delete?file=" + encodedPath + "', " + (inTrash ? "true" : "false") + ")\" class='del'>Delete</a></div></div>";
      response->print(card);
      count++;
    }
    file = root.openNextFile();
  }
  
  if (count == 0) {
    response->print("<div class='empty-state'><div class='empty-icon'>&#128194;</div><h3>Empty Folder</h3><p>No files found here.</p></div>");
  }
  
  response->print(htmlFooter());
  request->send(response);
}


// ================= UPLOAD =================

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if(!request->authenticate(authUser, authPass)) return;
  if(index == 0) {
    String dir = getRequestDir(request);
    String path = joinPath(dir, filename);
    if(path == "") {
      Serial.println(F("Upload rejected: unsafe filename"));
      return;
    }
    Serial.printf("Uploading: %s\n", path.c_str());
    uploadFile = SD.open(path, FILE_WRITE);
    if (!uploadFile) Serial.println(F("Upload failed to open file"));
  }
  if(uploadFile) uploadFile.write(data, len);
  if(final) { 
    if (uploadFile) {
      uploadFile.close(); 
      Serial.println(F("Upload finished"));
    }
  }
}


void handleCreateFolder(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!hasRequestParam(request, "name")) return request->send(400);
  String dir = getRequestDir(request);
  String name = requestParam(request, "name");
  String path = joinPath(dir, name);
  if(path == "") return request->send(400, "text/plain", "Invalid folder name");
  if(!SD.exists(path)) SD.mkdir(path);
  request->redirect("/?dir=" + urlEncode(dir));
}

void handleFileDelete(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!hasRequestParam(request, "file")) return request->send(400);
  String path = normalizePath(requestParam(request, "file"));
  if(path == "" || path == "/" || path == String(TRASH_DIR)) return request->send(400, "text/plain", "Invalid path");
  String redirectDir = path.startsWith(TRASH_DIR) ? String(TRASH_DIR) : getParentPath(path);
  if (path.startsWith(TRASH_DIR)) deleteRecursive(path);
  else {
    if(SD.exists(path)) {
      String filename = path.substring(path.lastIndexOf('/') + 1);
      String trashPath = String(TRASH_DIR) + "/" + String(millis()) + "_" + filename;
      SD.rename(path, trashPath);
    }
  }
  request->redirect("/?dir=" + urlEncode(redirectDir));
}

void handleDeleteFolder(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!hasRequestParam(request, "dir")) return request->send(400);
  String path = normalizePath(requestParam(request, "dir"));
  if(path == "" || path == "/" || path == String(TRASH_DIR)) return request->send(400, "text/plain", "Invalid path");
  String redirectDir = path.startsWith(TRASH_DIR) ? String(TRASH_DIR) : getParentPath(path);
  if (path.startsWith(TRASH_DIR)) deleteRecursive(path);
  else {
    if(SD.exists(path)) {
      String foldername = path.substring(path.lastIndexOf('/') + 1);
      String trashPath = String(TRASH_DIR) + "/" + String(millis()) + "_" + foldername;
      SD.rename(path, trashPath);
    }
  }
  request->redirect("/?dir=" + urlEncode(redirectDir));
}

void handleEmptyTrash(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  File root = SD.open(TRASH_DIR);
  if(root && root.isDirectory()) {
    File file = root.openNextFile();
    while(file) {
      String filePath = String(file.name());
      if (!filePath.startsWith("/")) {
        filePath = String(TRASH_DIR) + "/" + filePath;
      }
      bool isDir = file.isDirectory();
      file.close();
      if(isDir) deleteRecursive(filePath);
      else SD.remove(filePath);
      file = root.openNextFile();
    }
    root.close();
  }
  request->redirect("/?dir=/");
}

void handleFileEdit(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!hasRequestParam(request, "file")) return request->send(400);
  String path = normalizePath(requestParam(request, "file"));
  if(path == "" || !canEditFile(path)) return request->send(400, "text/plain", "Invalid editable file");
  File file = SD.open(path);
  String content = "";
  if(file) { while(file.available()) content += (char)file.read(); file.close(); }
  String html = R"rawliteral(<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Edit</title><style>:root{--bg:#101418;--card:#1b2328;--line:#2c393d;--accent:#43c6b4;--danger:#ff6b6b;--text:#eef3f0;--dim:#9aa9a6}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;padding:18px}.wrap{max-width:920px;margin:0 auto}.top{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:12px}.path{min-width:0;color:var(--accent);font-family:Consolas,monospace;font-size:13px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;background:#0d1113;border:1px solid var(--line);border-radius:11px;padding:10px 12px}.editor{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:12px;box-shadow:0 14px 34px rgba(0,0,0,.32)}textarea{width:100%;height:72vh;background:#0d1113;color:#d9fff5;font-family:Consolas,monospace;font-size:14px;line-height:1.45;padding:14px;border:1px solid var(--line);border-radius:12px;outline:none;resize:vertical}textarea:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(67,198,180,.14)}.actions{display:flex;gap:10px;justify-content:flex-end;margin-top:12px}button{border:none;border-radius:11px;padding:11px 18px;font-weight:700;cursor:pointer}.save{background:var(--accent);color:#08211f}.cancel{background:#2a2022;color:#ffd4d4;border:1px solid rgba(255,107,107,.38)}a{text-decoration:none}@media(max-width:640px){body{padding:12px}.top{display:block}.path{margin-bottom:10px}.actions{flex-direction:column}button{width:100%}}</style></head><body><div class="wrap"><div class="top"><div class="path">%PATH%</div></div><form method="POST" action="/save" class="editor"><input type="hidden" name="file" value="%PATH%"><textarea name="content">%CONTENT%</textarea><div class="actions"><a href="/"><button type="button" class="cancel">Cancel</button></a><button type="submit" class="save">Save</button></div></form></div></body></html>)rawliteral";
  html.replace("%PATH%", htmlEscape(path)); html.replace("%CONTENT%", htmlEscape(content));
  request->send(200, "text/html", html);
}

void handleFileSave(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("file", true) || !request->hasParam("content", true)) return request->send(400);
  String path = normalizePath(request->getParam("file", true)->value());
  if(path == "" || !canEditFile(path)) return request->send(400, "text/plain", "Invalid editable file");
  String content = request->getParam("content", true)->value();
  if(SD.exists(path)) SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if(file) { file.print(content); file.close(); }
  request->redirect("/?dir=" + urlEncode(getParentPath(path)));
}

void handleCreateFile(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!hasRequestParam(request, "name")) return request->send(400);
  String dir = getRequestDir(request);
  String path = joinPath(dir, requestParam(request, "name"));
  if(path == "") return request->send(400, "text/plain", "Invalid file name");
  if(!SD.exists(path)) { File file = SD.open(path, FILE_WRITE); if(file) file.close(); }
  request->redirect("/?dir=" + urlEncode(dir));
}

void handleFileRestore(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!hasRequestParam(request, "path")) return request->send(400);
  String path = normalizePath(requestParam(request, "path"));
  if(path == "" || !path.startsWith(String(TRASH_DIR) + "/")) return request->send(400, "text/plain", "Invalid restore path");
  String filename = path.substring(path.lastIndexOf('/') + 1);
  int uIdx = filename.indexOf('_'); if(uIdx != -1) filename = filename.substring(uIdx + 1);
  String restorePath = uniquePath("/" + filename);
  if(restorePath != "" && SD.exists(path)) SD.rename(path, restorePath);
  request->redirect("/?dir=" + urlEncode(String(TRASH_DIR)));
}

void handleFileRead(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!hasRequestParam(request, "file")) return request->send(400);
  String path = normalizePath(requestParam(request, "file"));
  if(path == "" || path == "/") return request->send(400, "text/plain", "Invalid file path");
  bool download = request->hasParam("dl");
  if (SD.exists(path)) {
    AsyncWebServerResponse *response = request->beginResponse(SD, path, getContentType(path), download);
    response->addHeader("Accept-Ranges", "bytes");
    request->send(response);
  }
  else request->send(404);
}

void handleFilePaste(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!hasRequestParam(request, "from") || !hasRequestParam(request, "to") || !hasRequestParam(request, "action")) return request->send(400);
  String from = normalizePath(requestParam(request, "from"));
  String toDir = normalizePath(requestParam(request, "to"));
  String action = requestParam(request, "action");
  if(from == "" || from == "/" || toDir == "" || !SD.exists(from) || !SD.exists(toDir)) return request->send(400, "text/plain", "Invalid paste path");
  if(action != "copy" && action != "cut") return request->send(400, "text/plain", "Invalid paste action");
  String filename = from.substring(from.lastIndexOf('/') + 1);
  if(!toDir.endsWith("/")) toDir += "/";
  String dest = uniquePath(toDir + filename);
  if(dest == "" || dest.startsWith(from + "/")) return request->send(400, "text/plain", "Invalid destination");
  if (action == "cut") { if(SD.exists(from)) SD.rename(from, dest); }
  else {
    File srcFile = SD.open(from);
    if(srcFile) { bool isDir = srcFile.isDirectory(); srcFile.close(); if(isDir) copyRecursive(from, dest); else copyFile(from, dest); }
  }
  request->redirect("/?dir=" + urlEncode(normalizePath(toDir)));
}

void handleFileRename(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!hasRequestParam(request, "path") || !hasRequestParam(request, "new")) return request->send(400);
  String oldPath = normalizePath(requestParam(request, "path"));
  String newName = requestParam(request, "new");
  if(oldPath == "" || oldPath == "/" || !isSafeName(newName)) return request->send(400, "text/plain", "Invalid rename");
  String parent = getParentPath(oldPath);
  if(!parent.endsWith("/")) parent += "/";
  String newPath = parent + newName;
  if(SD.exists(oldPath) && !SD.exists(newPath)) SD.rename(oldPath, newPath);
  request->redirect("/?dir=" + urlEncode(normalizePath(parent)));
}

void setupWiFi()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.config(sta_local_IP, sta_gateway, sta_subnet, primaryDNS, secondaryDNS);
  WiFi.begin(ssid, password);
  
  Serial.print(F("Connecting to WiFi"));
  int t = 0; 
  while(WiFi.status() != WL_CONNECTED && t < 20) { 
    delay(500); 
    Serial.print("."); 
    t++; 
  }
  Serial.println();

  if(WiFi.status() == WL_CONNECTED) {
    Serial.print(F("STA IP: ")); Serial.println(WiFi.localIP());
  } else {
    WiFi.softAPConfig(ap_local_IP, ap_gateway, ap_subnet);
    WiFi.softAP(apSSID, apPassword);
    Serial.print(F("AP IP: ")); Serial.println(WiFi.softAPIP());
  }
}

void printServiceUrls()
{
  IPAddress ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP();
  Serial.print(F("Web: http://"));
  Serial.println(ip);
}


void handlePlayerJS(AsyncWebServerRequest *request) {
  String js = R"js(
    let currentPlaylist = [];
    let currentIndex = -1;

    function savePlayerState() {
      const audio = document.getElementById('mainAudio');
      if (currentIndex < 0 || currentIndex >= currentPlaylist.length) return;
      const time = audio && !Number.isNaN(audio.currentTime) ? audio.currentTime : 0;
      const playing = audio ? !audio.paused : false;
      sessionStorage.setItem('player_state', JSON.stringify({
        index: currentIndex,
        playlist: currentPlaylist,
        time,
        playing
      }));
    }

    function postTo(url) {
      const form = document.createElement('form');
      form.method = 'POST';
      form.action = url;
      document.body.appendChild(form);
      form.submit();
    }

    function confirmDelete(url, isPermanent) {
      if(confirm(isPermanent ? "PERMANENTLY delete?" : "Move to Recycle Bin?")) postTo(url);
    }

    function confirmEmpty() {
      if(confirm("Empty Recycle Bin?")) postTo("/empty_trash");
    }

    function setClipboard(path, action) {
      savePlayerState();
      sessionStorage.setItem('nas_clipboard', JSON.stringify({path: path, action: action}));
      window.location.reload();
    }

    function clearClipboard() {
      sessionStorage.removeItem('nas_clipboard');
      window.location.reload();
    }

    function updateControls() {
      const pasteBtn = document.getElementById('pasteBtn');
      const cancelBtn = document.getElementById('cancelPasteBtn');
      const hasClip = !!sessionStorage.getItem('nas_clipboard');
      if(pasteBtn) pasteBtn.style.display = hasClip ? 'flex' : 'none';
      if(cancelBtn) cancelBtn.style.display = hasClip ? 'flex' : 'none';
    }

    function paste() {
      savePlayerState();
      let clip = JSON.parse(sessionStorage.getItem('nas_clipboard'));
      if(!clip) return;
      let btn = document.getElementById('pasteBtn');
      if(btn) { btn.innerHTML = 'Pasting...'; btn.disabled = true; }
      let dest = new URLSearchParams(window.location.search).get('dir') || '/';
      postTo(`/paste?from=${encodeURIComponent(clip.path)}&to=${encodeURIComponent(dest)}&action=${clip.action}`);
      sessionStorage.removeItem('nas_clipboard');
    }

    function renameItem(path) {
      const oldName = path.split('/').pop();
      const newName = prompt('Enter new name:', oldName);
      if (newName && newName !== oldName) {
        postTo(`/rename?path=${encodeURIComponent(path)}&new=${encodeURIComponent(newName)}`);
      }
    }

    function playAudio(path, id, type) {
      const audioPlaceholders = Array.from(document.querySelectorAll('.audio-placeholder'));
      currentPlaylist = audioPlaceholders.map(el => ({
        path: el.getAttribute('data-path'),
        name: el.closest('.card').querySelector('.name').innerText,
        type: el.getAttribute('data-type') || 'audio/mpeg'
      }));
      currentIndex = currentPlaylist.findIndex(item => item.path === path);
      loadTrack(currentIndex, 0, true);
    }

    function loadTrack(index, savedTime = 0, shouldAutoplay = true) {
      if (index < 0 || index >= currentPlaylist.length) return;
      currentIndex = index;
      const track = currentPlaylist[currentIndex];
      
      const player = document.getElementById('sidePlayer');
      const content = document.getElementById('playerContent');
      document.getElementById('pName').innerText = track.name;
      
      content.innerHTML = `
        <audio id="mainAudio" style="display:none"><source src="/download?file=${encodeURIComponent(track.path)}" type="${track.type || 'audio/mpeg'}"></audio>
        <div class="player-controls">
          <div class="ctrl-btn" onclick="prevTrack()">&#9198;</div>
          <div class="ctrl-btn play-pause" id="playPauseBtn" onclick="togglePlay()">&#9208;</div>
          <div class="ctrl-btn" onclick="nextTrack()">&#9197;</div>
        </div>
        <div class="player-progress">
          <input type="range" class="seek-bar" id="seekBar" value="0" step="0.1">
          <div class="time-info"><span id="curTime">0:00</span><span id="durTime">0:00</span></div>
        </div>
      `;
      
      const audio = document.getElementById('mainAudio');
      const seek = document.getElementById('seekBar');
      const playBtn = document.getElementById('playPauseBtn');
      const resumeTime = Number(savedTime) || 0;
      
      playBtn.innerText = shouldAutoplay ? '\u23f8' : '\u25b6';
      audio.onloadedmetadata = () => {
        if(resumeTime > 0 && audio.duration && resumeTime < audio.duration) audio.currentTime = resumeTime;
        document.getElementById('curTime').innerText = formatTime(audio.currentTime);
        document.getElementById('durTime').innerText = formatTime(audio.duration || 0);
      };

      if(shouldAutoplay) {
        audio.play().then(savePlayerState).catch(e => {
          if(playBtn) playBtn.innerText = '\u25b6';
          savePlayerState();
        });
      } else {
        savePlayerState();
      }
      
      audio.ontimeupdate = () => {
        if(!audio.duration) return;
        const pct = (audio.currentTime / audio.duration) * 100;
        seek.value = pct;
        document.getElementById('curTime').innerText = formatTime(audio.currentTime);
        document.getElementById('durTime').innerText = formatTime(audio.duration);
        savePlayerState();
      };
      
      seek.oninput = () => { audio.currentTime = (seek.value / 100) * audio.duration; };
      audio.onplay = () => { playBtn.innerText = '\u23f8'; savePlayerState(); };
      audio.onpause = () => { playBtn.innerText = '\u25b6'; savePlayerState(); };
      audio.onended = () => nextTrack();
      
      player.classList.add('active');
      document.querySelector('.content-area').classList.add('active-player');
    }

    function formatTime(sec) {
      let m = Math.floor(sec / 60);
      let s = Math.floor(sec % 60);
      return m + ":" + (s < 10 ? "0" + s : s);
    }

    function togglePlay() {
      const audio = document.getElementById('mainAudio');
      const btn = document.getElementById('playPauseBtn');
      if(!audio || !btn) return;
      if (audio.paused) { audio.play(); btn.innerText = '\u23f8'; }
      else { audio.pause(); btn.innerText = '\u25b6'; }
      savePlayerState();
    }

    function nextTrack() { if (currentIndex < currentPlaylist.length - 1) loadTrack(currentIndex + 1, 0, true); }
    function prevTrack() { if (currentIndex > 0) loadTrack(currentIndex - 1, 0, true); }

    function closePlayer() {
      const player = document.getElementById('sidePlayer');
      document.getElementById('playerContent').innerHTML = '';
      player.classList.remove('active');
      document.querySelector('.content-area').classList.remove('active-player');
      sessionStorage.removeItem('player_state');
    }

    window.addEventListener('load', () => {
      updateControls();
      try {
        const raw = sessionStorage.getItem('player_state');
        const state = raw ? JSON.parse(raw) : null;
        if(state && state.playlist && state.playlist.length) {
          currentPlaylist = state.playlist;
          currentIndex = state.index;
          loadTrack(currentIndex, state.time, false);
        }
      } catch(e) {
        sessionStorage.removeItem('player_state');
      }
    });
    window.addEventListener('beforeunload', savePlayerState);
  )js";
  request->send(200, "application/javascript", js);
}

void setup()
{
  Serial.begin(115200); delay(1000);
  Serial.println(F("\n--- ESP32 NAS STARTING ---"));

  if(!SD.begin(SD_CS)) {
    Serial.println(F("SD Card Mount Failed!"));
  } else {
    sdReady = true;
    Serial.println(F("SD Card Mounted."));
    if(!SD.exists(TRASH_DIR)) SD.mkdir(TRASH_DIR);

    // Create Player Assets on SD if missing
    if(!SD.exists("/player.css")) {
      File f = SD.open("/player.css", FILE_WRITE);
      if(f) {
        f.print(R"css(
          .side-player { position: fixed; right: -350px; top: 120px; bottom: 20px; width: 320px; background: var(--card-bg); border: 1px solid var(--border); border-radius: 16px 0 0 16px; box-shadow: -10px 0 30px rgba(0,0,0,0.5); transition: all 0.3s ease; z-index: 1100; display: flex; flex-direction: column; padding: 20px; box-sizing: border-box; backdrop-filter: blur(20px); }
          .side-player.active { right: 0; }
          .player-header { display: flex; justify-content: flex-end; margin-bottom: 10px; }
          .player-close { cursor: pointer; font-size: 18px; color: var(--text-dim); }
          .player-info { text-align: center; margin-bottom: 20px; flex: 1; display: flex; flex-direction: column; justify-content: center; }
          .player-art { width: 80px; height: 80px; background: rgba(47, 129, 247, 0.1); border-radius: 50%; margin: 0 auto 16px; display: flex; align-items: center; justify-content: center; font-size: 32px; border: 1px solid rgba(47, 129, 247, 0.3); color: var(--accent); }
          .player-name { font-weight: 600; font-size: 14px; margin-bottom: 4px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .player-controls { display: flex; align-items: center; justify-content: center; gap: 24px; margin-top: 10px; }
          .ctrl-btn { cursor: pointer; font-size: 20px; color: var(--text); transition: all 0.2s; display: flex; align-items: center; justify-content: center; }
          .ctrl-btn:hover { color: var(--accent); transform: scale(1.1); }
          .ctrl-btn.play-pause { font-size: 32px; color: var(--accent); }
          .player-progress { width: 100%; margin-top: 20px; }
          input[type="range"].seek-bar { width: 100%; height: 4px; background: var(--border); border-radius: 2px; outline: none; -webkit-appearance: none; cursor: pointer; }
          input[type="range"].seek-bar::-webkit-slider-thumb { -webkit-appearance: none; width: 12px; height: 12px; background: var(--accent); border-radius: 50%; }
          .time-info { display: flex; justify-content: space-between; font-size: 10px; color: var(--text-dim); margin-top: 6px; font-family: monospace; }
          @media (max-width: 768px) {
            .side-player { width: 100%; right: 0; top: auto; bottom: -100%; border-radius: 20px 20px 0 0; border-left: none; border-top: 1px solid var(--border); height: auto; max-height: 80vh; }
            .side-player.active { bottom: 0; }
          }
        )css");
        f.close();
      }
    }
  }

  setupWiFi(); 

  server.on("/", HTTP_GET, handleFileList);
  server.on("/player.js", HTTP_GET, handlePlayerJS);
  server.on("/download", HTTP_GET, handleFileRead);
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
    if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
    request->redirect("/?dir=" + urlEncode(getRequestDir(request)));
  }, handleFileUpload);
  server.on("/delete", HTTP_POST, handleFileDelete);
  server.on("/mkdir", HTTP_POST, handleCreateFolder);
  server.on("/mkfile", HTTP_POST, handleCreateFile);
  server.on("/rmdir", HTTP_POST, handleDeleteFolder);
  server.on("/restore", HTTP_POST, handleFileRestore);
  server.on("/paste", HTTP_POST, handleFilePaste);
  server.on("/edit", HTTP_GET, handleFileEdit);
  server.on("/save", HTTP_POST, handleFileSave);
  server.on("/empty_trash", HTTP_POST, handleEmptyTrash);
  server.on("/rename", HTTP_POST, handleFileRename);

  server.begin();
  Serial.println(F("NAS READY"));
  printServiceUrls();
}

void loop() {
  delay(1);
}
