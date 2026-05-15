#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <string.h>
#include <time.h>

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "UMBC Visitor"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef TIMEZONE
#define TIMEZONE "EST5EDT,M3.2.0/2,M11.1.0/2"
#endif

#ifndef WEATHER_LATITUDE
#define WEATHER_LATITUDE 0.0000
#endif

#ifndef WEATHER_LONGITUDE
#define WEATHER_LONGITUDE 0.0000
#endif

#ifndef WEATHER_LOCATION_NAME
#define WEATHER_LOCATION_NAME "My Location"
#endif

#ifndef WEATHER_USE_FAHRENHEIT
#define WEATHER_USE_FAHRENHEIT true
#endif

TFT_eSPI tft = TFT_eSPI();

const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.nist.gov";

const int SCREEN_W = 480;
const int SCREEN_H = 320;

const unsigned long WIFI_TIMEOUT_MS = 30000;
const unsigned long NTP_TIMEOUT_MS = 20000;
const unsigned long CLOCK_REFRESH_MS = 1000;
const unsigned long WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL;

String lastTimeText = "";
String lastDateText = "";
String lastWeatherSummary = "";
String lastWeatherDetails = "";
String lastWeatherUpdated = "";
unsigned long lastClockRefresh = 0;
unsigned long lastWeatherRefresh = 0;
unsigned long lastSyncRetry = 0;
bool clockReady = false;
bool weatherReady = false;

struct WeatherData {
  float temperature;
  float feelsLike;
  float humidity;
  float windSpeed;
  float precipitation;
  int weatherCode;
};

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
  tft.fillRect(0, 294, SCREEN_W, 26, TFT_BLACK);
  tft.drawString("WiFi: " + WiFi.SSID() + "  IP: " + WiFi.localIP().toString(), 10, 300, 2);
}

bool connectToWiFi() {
  showStatus("Connecting to WiFi", WIFI_SSID);

  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("ESP32 Time + Weather Display");
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

bool extractJsonNumber(const String &json, const String &key, float &value) {
  int searchFrom = 0;

  while (searchFrom < json.length()) {
    int keyIndex = json.indexOf("\"" + key + "\":", searchFrom);
    if (keyIndex < 0) {
      return false;
    }

    int valueStart = keyIndex + key.length() + 3;
    while (valueStart < json.length() && json[valueStart] == ' ') {
      valueStart++;
    }

    char firstChar = json[valueStart];
    if (isDigit(firstChar) || firstChar == '-' || firstChar == '.') {
      int valueEnd = valueStart;
      while (valueEnd < json.length()) {
        char c = json[valueEnd];
        if (!isDigit(c) && c != '-' && c != '.') {
          break;
        }
        valueEnd++;
      }

      value = json.substring(valueStart, valueEnd).toFloat();
      return true;
    }

    searchFrom = valueStart + 1;
  }

  return false;
}

String weatherDescription(int code) {
  if (code == 0) return "Clear";
  if (code == 1) return "Mostly clear";
  if (code == 2) return "Partly cloudy";
  if (code == 3) return "Cloudy";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code >= 85 && code <= 86) return "Snow showers";
  if (code >= 95 && code <= 99) return "Thunderstorm";
  return "Weather";
}

String buildWeatherUrl() {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(WEATHER_LATITUDE, 4);
  url += "&longitude=";
  url += String(WEATHER_LONGITUDE, 4);
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m";
  url += "&wind_speed_unit=mph";

  if (WEATHER_USE_FAHRENHEIT) {
    url += "&temperature_unit=fahrenheit&precipitation_unit=inch";
  }

  return url;
}

bool fetchWeather(WeatherData &weather) {
  showStatus("Updating weather", WEATHER_LOCATION_NAME);
  Serial.println("Fetching weather from Open-Meteo...");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(15000);
  String url = buildWeatherUrl();

  if (!http.begin(client, url)) {
    Serial.println("Could not start weather request.");
    return false;
  }

  int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
    Serial.print("Weather request failed. HTTP status: ");
    Serial.println(statusCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  float weatherCode = 0;
  bool ok = extractJsonNumber(payload, "temperature_2m", weather.temperature);
  ok = extractJsonNumber(payload, "apparent_temperature", weather.feelsLike) && ok;
  ok = extractJsonNumber(payload, "relative_humidity_2m", weather.humidity) && ok;
  ok = extractJsonNumber(payload, "wind_speed_10m", weather.windSpeed) && ok;
  ok = extractJsonNumber(payload, "precipitation", weather.precipitation) && ok;
  ok = extractJsonNumber(payload, "weather_code", weatherCode) && ok;
  weather.weatherCode = (int)weatherCode;

  if (!ok) {
    Serial.println("Could not parse weather response.");
    return false;
  }

  Serial.println("Weather updated.");
  return true;
}

String currentTimeLabel() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    return "Updated just now";
  }

  char timeBuffer[16];
  strftime(timeBuffer, sizeof(timeBuffer), "Updated %I:%M %p", &timeInfo);
  return String(timeBuffer);
}

void drawDashboardFrame() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Time + Weather", SCREEN_W / 2, 24, 4);

  tft.drawFastHLine(35, 48, SCREEN_W - 70, TFT_DARKGREY);
  tft.drawFastHLine(35, 166, SCREEN_W - 70, TFT_DARKGREY);
  showNetworkInfo();
}

void updateClock() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    clockReady = false;
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.fillRect(0, 62, SCREEN_W, 80, TFT_BLACK);
    tft.drawString("Time lost", SCREEN_W / 2, 102, 4);
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
    tft.fillRect(0, 58, SCREEN_W, 62, TFT_BLACK);
    tft.drawString(timeText, SCREEN_W / 2, 91, 6);
    lastTimeText = timeText;
  }

  if (dateText != lastDateText) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.fillRect(0, 125, SCREEN_W, 34, TFT_BLACK);
    tft.drawString(dateText, SCREEN_W / 2, 140, 4);
    lastDateText = dateText;
  }
}

void drawWeather(const WeatherData &weather) {
  String tempUnit = WEATHER_USE_FAHRENHEIT ? "F" : "C";
  String rainUnit = WEATHER_USE_FAHRENHEIT ? "in" : "mm";

  String summary = String((int)round(weather.temperature)) + " " + tempUnit + "  " + weatherDescription(weather.weatherCode);
  String details = "Feels " + String((int)round(weather.feelsLike)) + " " + tempUnit;
  details += "  Humidity " + String((int)round(weather.humidity)) + "%";
  details += "  Wind " + String((int)round(weather.windSpeed)) + " mph";
  String precip = "Rain " + String(weather.precipitation, WEATHER_USE_FAHRENHEIT ? 2 : 1) + " " + rainUnit;
  String updated = currentTimeLabel();

  tft.setTextDatum(MC_DATUM);

  if (summary != lastWeatherSummary) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.fillRect(0, 178, SCREEN_W, 42, TFT_BLACK);
    tft.drawString(summary, SCREEN_W / 2, 198, 4);
    lastWeatherSummary = summary;
  }

  if (details != lastWeatherDetails) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.fillRect(0, 224, SCREEN_W, 28, TFT_BLACK);
    tft.drawString(details, SCREEN_W / 2, 237, 2);
    lastWeatherDetails = details;
  }

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.fillRect(0, 258, SCREEN_W, 28, TFT_BLACK);
  tft.drawString(String(WEATHER_LOCATION_NAME) + "  " + precip + "  " + updated, SCREEN_W / 2, 271, 2);
  lastWeatherUpdated = updated;
}

void updateWeather() {
  WeatherData weather;
  if (fetchWeather(weather)) {
    weatherReady = true;
    lastWeatherRefresh = millis();
    drawDashboardFrame();
    lastTimeText = "";
    lastDateText = "";
    updateClock();
    drawWeather(weather);
    return;
  }

  weatherReady = false;
  lastWeatherRefresh = millis();
  drawDashboardFrame();
  updateClock();
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.fillRect(0, 185, SCREEN_W, 80, TFT_BLACK);
  tft.drawString("Weather unavailable", SCREEN_W / 2, 205, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Check WiFi or captive portal access", SCREEN_W / 2, 245, 2);
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
  drawDashboardFrame();
  updateClock();
  updateWeather();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi disconnected", "Restarting connection...");
    lastTimeText = "";
    lastDateText = "";
    lastWeatherSummary = "";
    lastWeatherDetails = "";
    connectToWiFi();
    if (WiFi.status() == WL_CONNECTED && syncTime()) {
      clockReady = true;
      drawDashboardFrame();
      updateClock();
      updateWeather();
    }
  }

  if (!clockReady) {
    if (millis() - lastSyncRetry >= 60000) {
      lastSyncRetry = millis();
      if (syncTime()) {
        clockReady = true;
        drawDashboardFrame();
      }
    }
    return;
  }

  if (millis() - lastClockRefresh >= CLOCK_REFRESH_MS) {
    lastClockRefresh = millis();
    updateClock();
  }

  if (millis() - lastWeatherRefresh >= WEATHER_REFRESH_MS) {
    updateWeather();
  }
}
