#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

TFT_eSPI tft = TFT_eSPI();

const char *WIFI_SSID = "UMBC Visitor";
const char *WIFI_PASSWORD = ""; // Leave empty for open Wi-Fi.

const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.nist.gov";
const char *TIMEZONE = "EST5EDT,M3.2.0/2,M11.1.0/2"; // US Eastern time with DST.

const int SCREEN_W = 480;
const int SCREEN_H = 320;

const unsigned long WIFI_TIMEOUT_MS = 30000;
const unsigned long NTP_TIMEOUT_MS = 20000;
const unsigned long CLOCK_REFRESH_MS = 1000;

String lastTimeText = "";
String lastDateText = "";
unsigned long lastClockRefresh = 0;
unsigned long lastSyncRetry = 0;
bool clockReady = false;

void showStatus(const String &title, const String &detail = "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(title, SCREEN_W / 2, 125, 4);

  if (detail.length() > 0) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(detail, SCREEN_W / 2, 175, 2);
  }
}

void showNetworkInfo() {
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.println();

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.fillRect(0, 292, SCREEN_W, 28, TFT_BLACK);
  tft.drawString("WiFi: " + WiFi.SSID() + "  IP: " + WiFi.localIP().toString(), 10, 298, 2);
}

bool connectToWiFi() {
  showStatus("Connecting to WiFi", WIFI_SSID);

  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("ESP32 UMBC Visitor Time Display");
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Connecting to SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(500);

  if (strlen(WIFI_PASSWORD) == 0) {
    WiFi.begin(WIFI_SSID);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  unsigned long startAttempt = millis();
  int dots = 0;

  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");

    dots = (dots + 1) % 4;
    String detail = WIFI_SSID;
    for (int i = 0; i < dots; i++) {
      detail += ".";
    }
    tft.fillRect(0, 160, SCREEN_W, 40, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(detail, SCREEN_W / 2, 175, 2);
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi failed", "Check signal or SSID");
    Serial.println("WiFi connection failed.");
    return false;
  }

  showStatus("WiFi connected", WiFi.localIP().toString());
  showNetworkInfo();
  delay(1500);
  return true;
}

bool syncTime() {
  showStatus("Syncing time", "Using NTP");
  Serial.println("Syncing time with NTP...");

  configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);

  struct tm timeInfo;
  unsigned long startAttempt = millis();
  int dots = 0;

  while (!getLocalTime(&timeInfo) && millis() - startAttempt < NTP_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");

    dots = (dots + 1) % 4;
    String detail = "Using NTP";
    for (int i = 0; i < dots; i++) {
      detail += ".";
    }
    tft.fillRect(0, 160, SCREEN_W, 40, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(detail, SCREEN_W / 2, 175, 2);
  }

  Serial.println();

  if (!getLocalTime(&timeInfo)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Time sync failed", SCREEN_W / 2, 95, 4);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("WiFi connected, but NTP did not respond.", SCREEN_W / 2, 145, 2);
    tft.drawString("UMBC Visitor may need a browser login", SCREEN_W / 2, 175, 2);
    tft.drawString("or device/MAC approval from DoIT.", SCREEN_W / 2, 200, 2);

    Serial.println("NTP sync failed.");
    Serial.println("If UMBC Visitor has a captive portal, the ESP32 may need device/MAC approval.");
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
    return false;
  }

  Serial.println("Time synced.");
  return true;
}

void drawClockFrame() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("UMBC Visitor Clock", SCREEN_W / 2, 35, 4);

  tft.drawFastHLine(40, 65, SCREEN_W - 80, TFT_DARKGREY);
  showNetworkInfo();
}

void updateClock() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    clockReady = false;
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.fillRect(0, 115, SCREEN_W, 90, TFT_BLACK);
    tft.drawString("Time lost", SCREEN_W / 2, 155, 4);
    return;
  }

  char timeBuffer[16];
  char dateBuffer[32];
  strftime(timeBuffer, sizeof(timeBuffer), "%I:%M:%S %p", &timeInfo);
  strftime(dateBuffer, sizeof(dateBuffer), "%A, %B %d", &timeInfo);

  String timeText = String(timeBuffer);
  String dateText = String(dateBuffer);

  tft.setTextDatum(MC_DATUM);

  if (timeText != lastTimeText) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.fillRect(0, 105, SCREEN_W, 80, TFT_BLACK);
    tft.drawString(timeText, SCREEN_W / 2, 145, 7);
    lastTimeText = timeText;
  }

  if (dateText != lastDateText) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.fillRect(0, 205, SCREEN_W, 38, TFT_BLACK);
    tft.drawString(dateText, SCREEN_W / 2, 220, 4);
    lastDateText = dateText;
  }
}

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  if (!connectToWiFi()) {
    return;
  }

  if (!syncTime()) {
    return;
  }

  clockReady = true;
  drawClockFrame();
  updateClock();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi disconnected", "Restarting connection...");
    lastTimeText = "";
    lastDateText = "";
    connectToWiFi();
    if (WiFi.status() == WL_CONNECTED && syncTime()) {
      clockReady = true;
      drawClockFrame();
    }
  }

  if (!clockReady) {
    if (millis() - lastSyncRetry >= 60000) {
      lastSyncRetry = millis();
      if (syncTime()) {
        clockReady = true;
        drawClockFrame();
      }
    }
    return;
  }

  if (millis() - lastClockRefresh >= CLOCK_REFRESH_MS) {
    lastClockRefresh = millis();
    updateClock();
  }
}
