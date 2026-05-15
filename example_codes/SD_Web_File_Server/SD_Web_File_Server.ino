#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ctype.h>
#include <string.h>

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "UMBC Visitor"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef FILE_SERVER_USERNAME
#define FILE_SERVER_USERNAME "admin"
#endif

#ifndef FILE_SERVER_PASSWORD
#define FILE_SERVER_PASSWORD "esp32"
#endif

// ====== USER SETTINGS ======
#define SD_CS 5
// ===========================

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
File uploadFile;
String uploadDir = "/";
String sessionToken;

const int SCREEN_W = 480;
const int SCREEN_H = 320;
const unsigned long WIFI_TIMEOUT_MS = 30000;
const char *HEADER_KEYS[] = { "Cookie" };

String htmlEscape(const String &value) {
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

String urlEncode(const String &value) {
  const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = value[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String cleanPath(String path) {
  path.trim();
  path.replace("\\", "/");
  if (!path.startsWith("/")) path = "/" + path;
  while (path.indexOf("//") >= 0) path.replace("//", "/");
  while (path.indexOf("/../") >= 0) path.replace("/../", "/");
  path.replace("/./", "/");
  if (path.endsWith("/..") || path.endsWith("/.")) path = "/";
  if (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
  return path;
}

String baseName(String path) {
  path.replace("\\", "/");
  int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

String parentPath(const String &path) {
  if (path == "/") return "/";
  int slash = path.lastIndexOf('/');
  return slash <= 0 ? "/" : path.substring(0, slash);
}

String joinPath(const String &dir, const String &name) {
  String cleanName = name;
  cleanName.replace("\\", "/");
  while (cleanName.startsWith("/")) cleanName.remove(0, 1);
  if (cleanName.indexOf("..") >= 0) return dir;
  return cleanPath(dir == "/" ? "/" + cleanName : dir + "/" + cleanName);
}

String formatBytes(uint64_t bytes) {
  char buffer[24];
  if (bytes < 1024) {
    snprintf(buffer, sizeof(buffer), "%llu B", (unsigned long long)bytes);
  } else if (bytes < 1024ULL * 1024ULL) {
    snprintf(buffer, sizeof(buffer), "%.1f KB", bytes / 1024.0);
  } else {
    snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / 1048576.0);
  }
  return String(buffer);
}

String contentTypeFor(const String &path) {
  String lower = path;
  lower.toLowerCase();
  if (lower.endsWith(".html") || lower.endsWith(".htm")) return "text/html";
  if (lower.endsWith(".txt") || lower.endsWith(".csv") || lower.endsWith(".log")) return "text/plain";
  if (lower.endsWith(".css")) return "text/css";
  if (lower.endsWith(".js")) return "application/javascript";
  if (lower.endsWith(".json")) return "application/json";
  if (lower.endsWith(".png")) return "image/png";
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
  if (lower.endsWith(".gif")) return "image/gif";
  if (lower.endsWith(".bmp")) return "image/bmp";
  if (lower.endsWith(".pdf")) return "application/pdf";
  return "application/octet-stream";
}

void showStatus(const String &title, const String &detail = "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(title, SCREEN_W / 2, 95, 4);
  if (detail.length() > 0) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(detail, SCREEN_W / 2, 145, 2);
  }
}

void showReadyScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("SD Web Server", SCREEN_W / 2, 65, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Open this in your browser:", SCREEN_W / 2, 120, 2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("http://" + WiFi.localIP().toString(), SCREEN_W / 2, 155, 4);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("SSID: " + WiFi.SSID(), SCREEN_W / 2, 225, 2);
  tft.drawString("MAC: " + WiFi.macAddress(), SCREEN_W / 2, 250, 2);
}

bool connectToWiFi() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("ESP32 SD Web File Server");
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Connecting to SSID: ");
  Serial.println(WIFI_SSID);

  showStatus("Connecting to WiFi", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(500);

  if (strlen(WIFI_PASSWORD) == 0) {
    WiFi.begin(WIFI_SSID);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  unsigned long started = millis();
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
    dots = (dots + 1) % 4;
    String detail = WIFI_SSID;
    for (int i = 0; i < dots; i++) detail += ".";
    tft.fillRect(0, 135, SCREEN_W, 28, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(detail, SCREEN_W / 2, 145, 2);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi failed", "Check signal, SSID, or portal");
    Serial.println("WiFi connection failed.");
    return false;
  }

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  return true;
}

String pageHeader(const String &title) {
  String html = F("<!doctype html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += "<title>" + htmlEscape(title) + "</title>";
  html += F("<style>"
            "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:#f6f7f9;color:#171a1f}"
            "header{background:#16202a;color:white;padding:16px 20px}"
            "main{max-width:920px;margin:0 auto;padding:18px}"
            "a{color:#0f5c9c;text-decoration:none}a:hover{text-decoration:underline}"
            "table{width:100%;border-collapse:collapse;background:white;border:1px solid #d9dee7}"
            "th,td{padding:10px;border-bottom:1px solid #e6e9ef;text-align:left}"
            "th{background:#edf1f6;color:#303946;font-size:14px}"
            ".actions{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:14px 0}"
            ".login{display:block;max-width:360px;background:white;border:1px solid #d9dee7;padding:20px;margin:34px auto}"
            ".login label{display:block;margin:12px 0 6px;font-weight:600}"
            ".login input{box-sizing:border-box;width:100%}"
            "input,button{font:inherit;padding:8px;border:1px solid #b8c0cc;border-radius:6px;background:white}"
            "button{background:#174f7d;color:white;border-color:#174f7d;cursor:pointer}"
            ".danger{background:#8d1f1f;border-color:#8d1f1f}"
            ".muted{color:#667080;font-size:14px}"
            ".error{color:#8d1f1f;font-weight:600}"
            "form{display:inline}"
            "</style></head><body><header><h1>");
  html += htmlEscape(title);
  html += F("</h1></header><main>");
  return html;
}

String pageFooter() {
  return F("</main></body></html>");
}

String currentUrl() {
  String url = server.uri();
  if (server.args() > 0) {
    url += "?";
    for (int i = 0; i < server.args(); i++) {
      if (i > 0) url += "&";
      url += urlEncode(server.argName(i));
      url += "=";
      url += urlEncode(server.arg(i));
    }
  }
  return url;
}

bool isAuthenticated() {
  String cookie = server.header("Cookie");
  return sessionToken.length() > 0 && cookie.indexOf("sd_session=" + sessionToken) >= 0;
}

void redirectToLogin() {
  server.sendHeader("Location", "/login?next=" + urlEncode(currentUrl()));
  server.send(303, "text/plain", "");
}

bool requireAuth() {
  if (isAuthenticated()) return true;
  redirectToLogin();
  return false;
}

void redirectToDir(const String &dir) {
  server.sendHeader("Location", "/?dir=" + urlEncode(cleanPath(dir)));
  server.send(303, "text/plain", "");
}

void handleLoginPage(bool failed = false) {
  String next = server.hasArg("next") ? server.arg("next") : "/";
  String html = pageHeader("Login");
  html += F("<form class='login' method='POST' action='/login'>");
  if (failed) html += F("<p class='error'>Wrong username or password.</p>");
  html += F("<input type='hidden' name='next' value='");
  html += htmlEscape(next);
  html += F("'><label>Username</label><input name='username' autocomplete='username'>"
            "<label>Password</label><input name='password' type='password' autocomplete='current-password'>"
            "<p><button type='submit'>Login</button></p></form>");
  html += pageFooter();
  server.send(200, "text/html", html);
}

void handleLoginGet() {
  if (isAuthenticated()) {
    redirectToDir("/");
    return;
  }
  handleLoginPage(false);
}

void handleLoginPost() {
  String username = server.hasArg("username") ? server.arg("username") : "";
  String password = server.hasArg("password") ? server.arg("password") : "";
  String next = server.hasArg("next") ? server.arg("next") : "/";
  if (!next.startsWith("/")) next = "/";

  if (username == FILE_SERVER_USERNAME && password == FILE_SERVER_PASSWORD) {
    server.sendHeader("Set-Cookie", "sd_session=" + sessionToken + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400");
    server.sendHeader("Location", next.length() > 0 ? next : "/");
    server.send(303, "text/plain", "");
    return;
  }

  handleLoginPage(true);
}

void handleLogout() {
  server.sendHeader("Set-Cookie", "sd_session=deleted; Path=/; Max-Age=0");
  server.sendHeader("Location", "/login");
  server.send(303, "text/plain", "");
}

void handleBrowse() {
  if (!requireAuth()) return;

  String dirPath = cleanPath(server.hasArg("dir") ? server.arg("dir") : "/");
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    server.send(404, "text/plain", "Directory not found: " + dirPath);
    return;
  }

  String html = pageHeader("SD Card: " + dirPath);
  html += "<p class='muted'>ESP32 at " + WiFi.localIP().toString() + "</p>";
  html += F("<div class='actions'>");
  html += F("<a href='/logout'>Logout</a>");
  if (dirPath != "/") {
    html += "<a href='/?dir=" + urlEncode(parentPath(dirPath)) + "'>Up one folder</a>";
  }
  html += F("<form method='POST' action='/mkdir'><input type='hidden' name='dir' value='");
  html += htmlEscape(dirPath);
  html += F("'><input name='name' placeholder='New folder'><button type='submit'>Create folder</button></form>");
  html += "<form method='POST' action='/upload?dir=" + urlEncode(dirPath) + "' enctype='multipart/form-data'><input type='hidden' name='dir' value='";
  html += htmlEscape(dirPath);
  html += F("'><input type='file' name='file'><button type='submit'>Upload</button></form></div>");

  html += F("<table><thead><tr><th>Name</th><th>Type</th><th>Size</th><th>Actions</th></tr></thead><tbody>");
  File entry = dir.openNextFile();
  while (entry) {
    String name = baseName(entry.name());
    String fullPath = joinPath(dirPath, name);
    bool isDir = entry.isDirectory();
    html += F("<tr><td>");
    if (isDir) {
      html += "<a href='/?dir=" + urlEncode(fullPath) + "'>" + htmlEscape(name) + "/</a>";
    } else {
      html += "<a href='/download?path=" + urlEncode(fullPath) + "'>" + htmlEscape(name) + "</a>";
    }
    html += F("</td><td>");
    html += isDir ? "Folder" : "File";
    html += F("</td><td>");
    html += isDir ? "-" : formatBytes(entry.size());
    html += F("</td><td>");
    if (!isDir) {
      html += "<a href='/download?path=" + urlEncode(fullPath) + "'>Download</a> ";
    }
    html += F("<form method='POST' action='/delete' onsubmit=\"return confirm('Delete this item?')\">"
              "<input type='hidden' name='path' value='");
    html += htmlEscape(fullPath);
    html += F("'><input type='hidden' name='dir' value='");
    html += htmlEscape(dirPath);
    html += F("'><button class='danger' type='submit'>Delete</button></form>");
    html += F("</td></tr>");
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  html += F("</tbody></table>");
  html += pageFooter();
  server.send(200, "text/html", html);
}

void handleDownload() {
  if (!requireAuth()) return;

  if (!server.hasArg("path")) {
    server.send(400, "text/plain", "Missing path");
    return;
  }

  String path = cleanPath(server.arg("path"));
  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    server.send(404, "text/plain", "File not found: " + path);
    return;
  }

  server.sendHeader("Content-Disposition", "attachment; filename=\"" + baseName(path) + "\"");
  server.streamFile(file, contentTypeFor(path));
  file.close();
}

void handleMkdir() {
  if (!requireAuth()) return;

  String dir = cleanPath(server.hasArg("dir") ? server.arg("dir") : "/");
  String name = server.hasArg("name") ? server.arg("name") : "";
  name.trim();
  if (name.length() > 0) {
    String path = joinPath(dir, name);
    SD.mkdir(path);
  }
  redirectToDir(dir);
}

void deletePath(const String &path) {
  File item = SD.open(path);
  if (!item) return;
  bool isDir = item.isDirectory();
  item.close();

  if (isDir) {
    SD.rmdir(path);
  } else {
    SD.remove(path);
  }
}

void handleDelete() {
  if (!requireAuth()) return;

  String dir = cleanPath(server.hasArg("dir") ? server.arg("dir") : "/");
  if (server.hasArg("path")) {
    String path = cleanPath(server.arg("path"));
    if (path != "/") deletePath(path);
  }
  redirectToDir(dir);
}

void handleUploadDone() {
  if (!requireAuth()) return;
  redirectToDir(uploadDir);
}

void handleUploadFile() {
  if (!isAuthenticated()) return;

  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadDir = cleanPath(server.hasArg("dir") ? server.arg("dir") : "/");
    String filename = baseName(upload.filename);
    String path = joinPath(uploadDir, filename);
    Serial.print("Upload start: ");
    Serial.println(path);
    SD.remove(path);
    uploadFile = SD.open(path, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    Serial.print("Upload complete, bytes: ");
    Serial.println(upload.totalSize);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    Serial.println("Upload aborted");
  }
}

void handleNotFound() {
  if (!requireAuth()) return;

  String path = cleanPath(server.uri());
  File file = SD.open(path, FILE_READ);
  if (file && !file.isDirectory()) {
    server.streamFile(file, contentTypeFor(path));
    file.close();
    return;
  }
  if (file) file.close();
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  showStatus("Mounting SD...");
  if (!SD.begin(SD_CS)) {
    showStatus("SD init failed", "Card Mount Failed");
    Serial.println("Card Mount Failed");
    while (true) delay(100);
  }

  if (!connectToWiFi()) {
    return;
  }

  sessionToken = String((uint32_t)ESP.getEfuseMac(), HEX) + String(millis(), HEX);
  server.collectHeaders(HEADER_KEYS, 1);
  server.on("/", HTTP_GET, handleBrowse);
  server.on("/login", HTTP_GET, handleLoginGet);
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/mkdir", HTTP_POST, handleMkdir);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadFile);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.print("Open http://");
  Serial.println(WiFi.localIP());
  showReadyScreen();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi disconnected", "Reconnecting...");
    connectToWiFi();
    if (WiFi.status() == WL_CONNECTED) showReadyScreen();
  }

  server.handleClient();
}
