#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ESP32FtpServer.h>

#define SD_CS 4

// ================= WIFI =================

const char* ssid = "SQL_SLAMMER☢️🔥";
const char* password = "error404";

const char* apSSID = "ESP32-Cloud";
const char* apPassword = "12345678";

// AP MODE IP
IPAddress ap_local_IP(192,168,4,1);
IPAddress ap_gateway(192,168,4,1);
IPAddress ap_subnet(255,255,255,0);

// STA MODE STATIC IP
IPAddress sta_local_IP(192,168,110,200);
IPAddress sta_gateway(192,168,110,1);
IPAddress sta_subnet(255,255,255,0);

IPAddress primaryDNS(192,168,110,1);
IPAddress secondaryDNS(8,8,8,8);

// ================= AUTH =================

const char* authUser = "admin";
const char* authPass = "admin";

// ================= SERVER =================

AsyncWebServer server(80);
FtpServer ftpSrv;

// ================= FILE =================

File uploadFile;
String currentPath = "/";
const char* TRASH_DIR = "/.trash";

// ================= UTIL =================

bool copyFile(String src, String dst) {
  File source = SD.open(src);
  if (!source || source.isDirectory()) return false;
  
  File dest = SD.open(dst, FILE_WRITE);
  if (!dest) { source.close(); return false; }

  static uint8_t buf[1024]; 
  while (source.available()) {
    size_t len = source.read(buf, sizeof(buf));
    dest.write(buf, len);
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
    if (name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
    String srcPath = src + "/" + name;
    String dstPath = dst + "/" + name;
    if (file.isDirectory()) { file.close(); copyRecursive(srcPath, dstPath); }
    else { file.close(); copyFile(srcPath, dstPath); }
    file = root.openNextFile();
  }
  root.close();
}

void deleteRecursive(String path) {
  File root = SD.open(path);
  if (!root) return;
  if (!root.isDirectory()) { root.close(); SD.remove(path); return; }
  File file = root.openNextFile();
  while (file) {
    String name = String(file.name());
    String fullPath = name;
    if (!name.startsWith("/")) {
      fullPath = path;
      if (!fullPath.endsWith("/")) fullPath += "/";
      fullPath += name;
    }
    if (file.isDirectory()) { file.close(); deleteRecursive(fullPath); }
    else { file.close(); SD.remove(fullPath); }
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

String getContentType(String filename)
{
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".txt")) return "text/plain";
  if (filename.endsWith(".jpg")) return "image/jpeg";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".json")) return "application/json";
  return "application/octet-stream";
}

// ================= UI =================

String htmlPage(String path, String files)
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
:root { --bg: #0d1117; --card-bg: #161b22; --header-bg: rgba(22, 27, 34, 0.8); --accent: #2f81f7; --text: #c9d1d9; --text-dim: #8b949e; --border: #30363d; --danger: #f85149; --success: #238636; }
body { font-family: -apple-system, system-ui, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding-top: 140px; }
.header { position: fixed; top: 0; left: 0; right: 0; background: var(--header-bg); backdrop-filter: blur(12px); -webkit-backdrop-filter: blur(12px); border-bottom: 1px solid var(--border); padding: 16px; z-index: 1000; }
.brand { display: flex; align-items: center; gap: 10px; font-size: 1.2rem; font-weight: 600; margin-bottom: 12px; }
.path-bar { background: rgba(0,0,0,0.2); padding: 6px 12px; border-radius: 6px; font-size: 13px; color: var(--text-dim); font-family: monospace; margin-bottom: 12px; border: 1px solid var(--border); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.action-bar { display: flex; gap: 8px; overflow-x: auto; padding-bottom: 4px; }
.action-bar::-webkit-scrollbar { display: none; }
button { background: var(--card-bg); color: var(--text); border: 1px solid var(--border); padding: 8px 14px; border-radius: 6px; cursor: pointer; font-size: 14px; display: flex; align-items: center; gap: 6px; }
.btn-primary { background: var(--accent); color: white; border: none; }
.btn-danger { color: var(--danger); }
.search-box { width: 100%; background: var(--bg); border: 1px solid var(--border); color: var(--text); padding: 10px 14px; border-radius: 6px; margin: 12px 0; box-sizing: border-box; }
.container { max-width: 800px; margin: 0 auto; padding: 16px; }
.card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 8px; padding: 12px; margin-bottom: 10px; transition: border-color 0.2s; }
.card:hover { border-color: var(--text-dim); }
.card-main { display: flex; align-items: center; gap: 12px; }
.icon { font-size: 24px; min-width: 32px; text-align: center; }
.info { flex-grow: 1; overflow: hidden; }
.name { font-weight: 500; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.meta { font-size: 12px; color: var(--text-dim); margin-top: 2px; }
.actions { display: flex; gap: 12px; margin-top: 12px; padding-top: 10px; border-top: 1px solid var(--border); flex-wrap: wrap; }
.actions a { font-size: 13px; color: var(--accent); text-decoration: none; font-weight: 500; }
.actions a.del { color: var(--danger); }
.actions a.restore { color: var(--success); }
.preview-img { width: 100%; max-height: 250px; object-fit: contain; background: #000; border-radius: 6px; margin-top: 10px; }
audio { width: 100%; height: 32px; margin-top: 10px; }
.modal-form { background: var(--card-bg); border: 1px solid var(--border); padding: 15px; border-radius: 8px; margin-bottom: 15px; }
.input-group { display: flex; gap: 8px; margin-top: 8px; }
input[type="text"], input[type="file"] { background: var(--bg); border: 1px solid var(--border); color: var(--text); padding: 8px; border-radius: 4px; flex-grow: 1; }
.progress-container { width: 100%; background: var(--bg); border-radius: 4px; height: 12px; margin-top: 12px; display: none; overflow: hidden; border: 1px solid var(--border); }
.progress-bar { width: 0%; height: 100%; background: var(--accent); transition: width 0.1s; }
.progress-text { font-size: 11px; color: var(--text-dim); margin-top: 4px; text-align: right; display: none; }
</style>
<script>
function confirmDelete(url, isPermanent) {
  if(confirm(isPermanent ? "PERMANENTLY delete?" : "Move to Recycle Bin?")) window.location.href = url;
}
function confirmEmpty() {
  if(confirm("Empty Recycle Bin?")) window.location.href = "/empty_trash";
}
function setClipboard(path, action) {
  sessionStorage.setItem('nas_clipboard', JSON.stringify({path: path, action: action}));
  window.location.reload();
}
function paste() {
  let clip = JSON.parse(sessionStorage.getItem('nas_clipboard'));
  if(!clip) return;
  let dest = new URLSearchParams(window.location.search).get('dir') || '/';
  window.location.href = `/paste?from=${encodeURIComponent(clip.path)}&to=${encodeURIComponent(dest)}&action=${clip.action}`;
  sessionStorage.removeItem('nas_clipboard');
}
function search() {
  let q = document.getElementById('q').value.toLowerCase();
  let cards = document.getElementsByClassName('card');
  for(let i=0; i<cards.length; i++) {
    let text = cards[i].innerText.toLowerCase();
    cards[i].style.display = text.includes(q) ? "" : "none";
  }
}
function toggleForm(id) {
  let f = document.getElementById(id);
  f.style.display = f.style.display === 'none' ? 'block' : 'none';
}
function uploadFile() {
  const fileInput = document.getElementById('fileInput');
  if (fileInput.files.length === 0) return;
  const formData = new FormData();
  formData.append("upload", fileInput.files[0]);
  const xhr = new XMLHttpRequest();
  document.getElementById('progressContainer').style.display = 'block';
  document.getElementById('progressText').style.display = 'block';
  xhr.upload.addEventListener("progress", (e) => {
    if (e.lengthComputable) {
      const percent = Math.round((e.loaded / e.total) * 100);
      document.getElementById('progressBar').style.width = percent + '%';
      document.getElementById('progressText').innerText = `Uploading: ${percent}%`;
    }
  });
  xhr.onreadystatechange = () => { if (xhr.readyState === 4) window.location.reload(); };
  xhr.open("POST", "/upload", true);
  xhr.send(formData);
}
window.onload = () => {
  if(sessionStorage.getItem('nas_clipboard')) document.getElementById('pasteBtn').style.display = 'flex';
}
</script>
</head>
<body>
<div class="header">
  <div class="brand">🚀 <span>Cloud NAS</span></div>
  <div class="path-bar">Location: %PATH%</div>
  <div class="action-bar">
    <a href="/?dir=/" style="text-decoration:none"><button>🏠 Home</button></a>
    %CONTROLS%
    <button id="pasteBtn" class="btn-primary" style="display:none;background:#238636;" onclick="paste()">📋 Paste</button>
  </div>
</div>
<div class="container">
  <input type="text" id="q" class="search-box" placeholder="Search..." onkeyup="search()">
  %FORMS%
  <div id="fileList">%FILES%</div>
</div>
</body>
</html>
)rawliteral";

  String controls = ""; String forms = "";
  if(!inTrash) {
    controls = R"rawliteral(<button onclick="toggleForm('uploadForm')">📤 Upload</button><button onclick="toggleForm('folderForm')">📁 Folder+</button><button onclick="toggleForm('fileForm')">📄 File+</button><a href="/?dir=/.trash/" style="text-decoration:none"><button style="background:#444;">♻ Recycle Bin</button></a>)rawliteral";
    forms = R"rawliteral(
      <div id="uploadForm" class="modal-form" style="display:none"><strong>Upload</strong><div class="input-group"><input type="file" id="fileInput"><button type="button" class="btn-primary" onclick="uploadFile()">Upload</button><button type="button" class="btn-danger" onclick="toggleForm('uploadForm')">Cancel</button></div><div id="progressContainer" class="progress-container"><div id="progressBar" class="progress-bar"></div></div><div id="progressText" class="progress-text"></div></div>
      <div id="folderForm" class="modal-form" style="display:none"><strong>New Folder</strong><form method="GET" action="/mkdir" class="input-group"><input type="text" name="name" placeholder="Name"><button type="submit" class="btn-primary">Create</button><button type="button" class="btn-danger" onclick="toggleForm('folderForm')">Cancel</button></form></div>
      <div id="fileForm" class="modal-form" style="display:none"><strong>New File</strong><form method="GET" action="/mkfile" class="input-group"><input type="text" name="name" placeholder="Name.txt"><button type="submit" class="btn-primary">Create</button><button type="button" class="btn-danger" onclick="toggleForm('fileForm')">Cancel</button></form></div>
    )rawliteral";
  } else {
    controls = R"rawliteral(<button class="btn-danger" onclick="confirmEmpty()">🗑 Empty Bin</button>)rawliteral";
  }
  html.replace("%CONTROLS%", controls); html.replace("%FORMS%", forms); html.replace("%FILES%", files); html.replace("%PATH%", path);
  return html;
}

// ================= FILE LIST =================

void handleFileList(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(request->hasParam("dir")) {
    currentPath = request->getParam("dir")->value();
    if(currentPath.length() > 1 && currentPath.endsWith("/")) currentPath = currentPath.substring(0, currentPath.length() - 1);
  }
  if(!SD.exists(currentPath)) currentPath = "/";
  bool inTrash = currentPath.startsWith(TRASH_DIR);
  File root = SD.open(currentPath);
  if(!root || !root.isDirectory()) { currentPath = "/"; root = SD.open("/"); }
  String files = "";
  if(currentPath != "/") {
    files += "<div class='card' onclick=\"location.href='/?dir=" + getParentPath(currentPath) + "'\" style='cursor:pointer'><div class='card-main'><div class='icon'>⬅</div><div class='info'><div class='name'>Go Back</div><div class='meta'>Parent Directory</div></div></div></div>";
  }
  File file = root.openNextFile();
  while(file) {
    String name = String(file.name());
    if(name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
    if(file.isDirectory() && name != ".trash") {
      String fullPath = currentPath; if(!fullPath.endsWith("/")) fullPath += "/"; fullPath += name;
      files += "<div class='card'><div class='card-main' onclick=\"location.href='/?dir=" + fullPath + "'\" style='cursor:pointer'><div class='icon'>📁</div><div class='info'><div class='name'>" + name + "</div><div class='meta'>Folder</div></div></div>";
      files += "<div class='actions'>";
      if(!inTrash) {
        files += "<a href='#' onclick=\"setClipboard('" + fullPath + "', 'copy')\">Copy</a> | ";
        files += "<a href='#' onclick=\"setClipboard('" + fullPath + "', 'cut')\">Cut</a> | ";
      }
      if(inTrash) files += "<a href='/restore?path=" + fullPath + "' class='restore'>Restore</a> | ";
      files += "<a href='#' onclick=\"confirmDelete('/rmdir?dir=" + fullPath + "', " + (inTrash ? "true" : "false") + ")\" class='del'>Delete</a></div></div>";
    }
    file = root.openNextFile();
  }
  root.rewindDirectory(); file = root.openNextFile();
  while(file) {
    String name = String(file.name());
    if(name.lastIndexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
    if(!file.isDirectory()) {
      String fullPath = currentPath; if(!fullPath.endsWith("/")) fullPath += "/"; fullPath += name;
      String nameLower = name; nameLower.toLowerCase();
      files += "<div class='card'><div class='card-main'><div class='icon'>" + String(nameLower.endsWith(".mp3") ? "🎵" : nameLower.endsWith(".jpg") || nameLower.endsWith(".png") ? "🖼" : "📄") + "</div><div class='info'><div class='name'>" + name + "</div><div class='meta'>" + formatBytes(file.size()) + "</div></div></div>";
      if(!inTrash) {
        if(nameLower.endsWith(".jpg") || nameLower.endsWith(".png") || nameLower.endsWith(".gif")) files += "<img src='/download?file=" + fullPath + "' class='preview-img' loading='lazy'>";
        if(nameLower.endsWith(".mp3") || nameLower.endsWith(".wav")) files += "<audio controls><source src='/download?file=" + fullPath + "' type='audio/mpeg'></audio>";
      }
      files += "<div class='actions'>";
      if(!inTrash) {
        files += "<a href='/download?file=" + fullPath + "'>Download</a> | ";
        if(nameLower.endsWith(".txt") || nameLower.endsWith(".html")) files += "<a href='/edit?file=" + fullPath + "'>Edit</a> | ";
        files += "<a href='#' onclick=\"setClipboard('" + fullPath + "', 'copy')\">Copy</a> | ";
        files += "<a href='#' onclick=\"setClipboard('" + fullPath + "', 'cut')\">Cut</a> | ";
      } else {
        files += "<a href='/restore?path=" + fullPath + "' class='restore'>Restore</a> | ";
      }
      files += " | <a href='#' onclick=\"confirmDelete('/delete?file=" + fullPath + "', " + (inTrash ? "true" : "false") + ")\" class='del'>Delete</a></div></div>";
    }
    file = root.openNextFile();
  }
  if (files == "" || (currentPath != "/" && files.indexOf("Go Back") != -1 && files.indexOf("Folder") == -1 && files.indexOf("Download") == -1)) {
    if(files == "" || files.indexOf("meta'>Folder") == -1) files += "<div style='text-align:center;padding:40px;color:var(--text-dim);'><div style='font-size:48px'>📂</div><h3>Empty Folder</h3>No files found here.</div>";
  }
  request->send(200, "text/html", htmlPage(currentPath, files));
}

// ================= UPLOAD =================

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if(index == 0) {
    String path = currentPath; if(!path.endsWith("/")) path += "/"; path += filename;
    uploadFile = SD.open(path, FILE_WRITE);
  }
  if(uploadFile) uploadFile.write(data, len);
  if(final) { uploadFile.close(); request->redirect("/?dir=" + currentPath); }
}

void handleCreateFolder(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("name")) return request->send(400);
  String name = request->getParam("name")->value();
  String path = currentPath; if(!path.endsWith("/")) path += "/"; path += name;
  if(!SD.exists(path)) SD.mkdir(path);
  request->redirect("/?dir=" + currentPath);
}

void handleFileDelete(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("file")) return request->send(400);
  String path = request->getParam("file")->value();
  if (path.startsWith(TRASH_DIR)) SD.remove(path);
  else {
    String filename = path.substring(path.lastIndexOf('/') + 1);
    String trashPath = String(TRASH_DIR) + "/" + String(millis()) + "_" + filename;
    if(SD.exists(path)) SD.rename(path, trashPath);
  }
  request->redirect("/?dir=" + currentPath);
}

void handleDeleteFolder(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("dir")) return request->send(400);
  String path = request->getParam("dir")->value();
  if(path.endsWith("/")) path = path.substring(0, path.length() - 1);
  if (path.startsWith(TRASH_DIR)) deleteRecursive(path);
  else {
    String foldername = path.substring(path.lastIndexOf('/') + 1);
    String trashPath = String(TRASH_DIR) + "/" + String(millis()) + "_" + foldername;
    if(SD.exists(path)) SD.rename(path, trashPath);
  }
  request->redirect("/?dir=" + currentPath);
}

void handleEmptyTrash(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  deleteRecursive(TRASH_DIR); SD.mkdir(TRASH_DIR);
  request->redirect("/?dir=/");
}

void handleFileEdit(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("file")) return request->send(400);
  String path = request->getParam("file")->value();
  File file = SD.open(path);
  String content = "";
  if(file) { while(file.available()) content += (char)file.read(); file.close(); }
  String html = R"rawliteral(<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Edit</title><style>body{background:#0f0f0f;color:white;padding:20px;}textarea{width:100%;height:70vh;background:#1e1e1e;color:#0f0;font-family:monospace;padding:10px;border:1px solid #333;}button{background:#007bff;border:none;padding:10px 20px;color:white;margin-top:10px;}</style></head><body><h3>%PATH%</h3><form method="POST" action="/save"><input type="hidden" name="file" value="%PATH%"><textarea name="content">%CONTENT%</textarea><br><button type="submit">💾 Save</button><a href="/"><button type="button" style="background:#dc3545">Cancel</button></a></form></body></html>)rawliteral";
  html.replace("%PATH%", path); html.replace("%CONTENT%", content);
  request->send(200, "text/html", html);
}

void handleFileSave(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("file", true) || !request->hasParam("content", true)) return request->send(400);
  String path = request->getParam("file", true)->value();
  String content = request->getParam("content", true)->value();
  File file = SD.open(path, FILE_WRITE);
  if(file) { file.print(content); file.close(); }
  request->redirect("/?dir=" + getParentPath(path));
}

void handleCreateFile(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("name")) return request->send(400);
  String path = currentPath; if(!path.endsWith("/")) path += "/"; path += request->getParam("name")->value();
  if(!SD.exists(path)) { File file = SD.open(path, FILE_WRITE); if(file) file.close(); }
  request->redirect("/?dir=" + currentPath);
}

void handleFileRestore(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("path")) return request->send(400);
  String path = request->getParam("path")->value();
  String filename = path.substring(path.lastIndexOf('/') + 1);
  int uIdx = filename.indexOf('_'); if(uIdx != -1) filename = filename.substring(uIdx + 1);
  if(SD.exists(path)) SD.rename(path, "/" + filename);
  request->redirect("/?dir=" + currentPath);
}

void handleFileRead(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("file")) return request->send(400);
  String path = request->getParam("file")->value();
  if (SD.exists(path)) request->send(SD, path, getContentType(path), true);
  else request->send(404);
}

void handleFilePaste(AsyncWebServerRequest *request)
{
  if(!request->authenticate(authUser, authPass)) return request->requestAuthentication();
  if(!request->hasParam("from") || !request->hasParam("to") || !request->hasParam("action")) return request->send(400);
  String from = request->getParam("from")->value();
  String toDir = request->getParam("to")->value();
  String action = request->getParam("action")->value();
  String filename = from.substring(from.lastIndexOf('/') + 1);
  if(!toDir.endsWith("/")) toDir += "/";
  String dest = toDir + filename;
  if (action == "cut") { if(SD.exists(from)) SD.rename(from, dest); }
  else {
    File srcFile = SD.open(from);
    if(srcFile) { bool isDir = srcFile.isDirectory(); srcFile.close(); if(isDir) copyRecursive(from, dest); else copyFile(from, dest); }
  }
  request->redirect("/?dir=" + toDir);
}

void setupWiFi()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.config(sta_local_IP, sta_gateway, sta_subnet, primaryDNS, secondaryDNS);
  WiFi.begin(ssid, password);
  int t = 0; while(WiFi.status() != 3 && t < 20) { delay(500); t++; }
  if(WiFi.status() == 3) Serial.println(WiFi.localIP());
  else { WiFi.softAPConfig(ap_local_IP, ap_gateway, ap_subnet); WiFi.softAP(apSSID, apPassword); Serial.println(WiFi.softAPIP()); }
}

void setup()
{
  Serial.begin(115200); delay(1000);
  if(!SD.begin(SD_CS)) return;
  File vFile = SD.open("/FTP_CHECK.txt", FILE_WRITE); if(vFile) { vFile.println("FTP OK"); vFile.close(); }
  if(!SD.exists(TRASH_DIR)) SD.mkdir(TRASH_DIR);
  setupWiFi(); ftpSrv.begin("esp32","esp32");
  server.on("/", HTTP_GET, handleFileList);
  server.on("/download", HTTP_GET, handleFileRead);
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){}, handleFileUpload);
  server.on("/delete", HTTP_GET, handleFileDelete);
  server.on("/mkdir", HTTP_GET, handleCreateFolder);
  server.on("/mkfile", HTTP_GET, handleCreateFile);
  server.on("/rmdir", HTTP_GET, handleDeleteFolder);
  server.on("/restore", HTTP_GET, handleFileRestore);
  server.on("/paste", HTTP_GET, handleFilePaste);
  server.on("/edit", HTTP_GET, handleFileEdit);
  server.on("/save", HTTP_POST, handleFileSave);
  server.on("/empty_trash", HTTP_GET, handleEmptyTrash);
  server.begin();
  Serial.println("NAS READY");
}

void loop() { ftpSrv.handleFTP(); }
