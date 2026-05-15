#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include <SD.h>
#include <string.h>

#if __has_include("spotify_config.h")
#include "spotify_config.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "UMBC Visitor"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef SPOTIFY_CLIENT_ID
#define SPOTIFY_CLIENT_ID "your_spotify_client_id"
#endif

#ifndef SPOTIFY_REFRESH_TOKEN
#define SPOTIFY_REFRESH_TOKEN "your_spotify_refresh_token"
#endif

#ifndef SPOTIFY_POLL_MS
#define SPOTIFY_POLL_MS 5000
#endif

#ifndef SD_CS
#define SD_CS 5
#endif

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite infoSprite = TFT_eSprite(&tft);
TFT_eSprite statusSprite = TFT_eSprite(&tft);

const int SCREEN_W = 480;
const int SCREEN_H = 320;
const int ART_X = 10;
const int ART_Y = 10;
const int ART_SIZE = 300;
const int INFO_X = 325;
const int INFO_Y = 10;
const int INFO_W = 145;
const int INFO_H = 275;
const int STATUS_Y = 294;
const int STATUS_H = 26;

// UI theme and layout knobs. Customize these first if you want a different look.
const uint16_t UI_BG = 0x0841;
const uint16_t UI_PANEL = 0x1082;
const uint16_t UI_PANEL_DARK = 0x0861;
const uint16_t UI_BORDER = 0x39E7;
const uint16_t UI_TEXT = TFT_WHITE;
const uint16_t UI_MUTED = 0xA534;
const uint16_t UI_ACCENT = 0x1DB9;
const uint16_t UI_ACCENT_DIM = 0x0B4D;
const uint16_t UI_WARN = 0xFDC0;
const uint16_t UI_ERROR = TFT_RED;

const unsigned long WIFI_TIMEOUT_MS = 30000;
const char *TOKEN_URL = "https://accounts.spotify.com/api/token";
const char *CURRENTLY_PLAYING_URL = "https://api.spotify.com/v1/me/player/currently-playing";
const char *ART_PATH = "/spotify_art.jpg";

String accessToken = "";
String currentTrackId = "";
String currentArtUrl = "";
String lastTitle = "";
String lastArtist = "";
String lastAlbum = "";
bool lastIsPlaying = false;
int lastProgressMs = -1;
int lastDurationMs = -1;
unsigned long tokenExpiresAt = 0;
unsigned long lastPoll = 0;
unsigned long nextPollDelay = SPOTIFY_POLL_MS;
bool sdReady = false;
bool uiSpritesReady = false;
bool showingNothing = false;
String lastStatusMessage = "";
uint16_t lastStatusColor = 0;

struct NowPlaying {
  bool hasTrack;
  bool isPlaying;
  String trackId;
  String title;
  String artists;
  String album;
  String artUrl;
  int progressMs;
  int durationMs;
};

String urlEncode(const String &value) {
  const char *hex = "0123456789ABCDEF";
  String encoded = "";

  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = (uint8_t)value[i];
    bool safe = isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      encoded += (char)c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

template <typename DisplayT>
String fitTextFor(DisplayT &display, String text, int maxWidth, uint8_t font) {
  text.trim();
  if (display.textWidth(text, font) <= maxWidth) {
    return text;
  }

  while (text.length() > 0 && display.textWidth(text + "...", font) > maxWidth) {
    text.remove(text.length() - 1);
  }

  text.trim();
  return text + "...";
}

String fitText(String text, int maxWidth, uint8_t font) {
  return fitTextFor(tft, text, maxWidth, font);
}

String formatTime(int ms) {
  if (ms < 0) {
    return "--:--";
  }

  int totalSeconds = ms / 1000;
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, seconds);
  return String(buffer);
}

void initUiSprites() {
  infoSprite.setColorDepth(16);
  statusSprite.setColorDepth(16);
  uiSpritesReady = infoSprite.createSprite(INFO_W, INFO_H) != nullptr;
  uiSpritesReady = (statusSprite.createSprite(SCREEN_W, STATUS_H) != nullptr) && uiSpritesReady;
  if (!uiSpritesReady) {
    Serial.println("Could not allocate UI sprites. Falling back to direct drawing.");
  }
}

void drawAppBackground() {
  tft.fillScreen(UI_BG);
  tft.drawFastVLine(318, 12, 266, UI_BORDER);
  tft.drawFastHLine(10, 288, 460, UI_BORDER);
}

void drawStatus(const String &message, uint16_t color = TFT_LIGHTGREY) {
  if (message == lastStatusMessage && color == lastStatusColor) {
    return;
  }

  lastStatusMessage = message;
  lastStatusColor = color;

  if (uiSpritesReady) {
    statusSprite.fillSprite(UI_BG);
    statusSprite.setTextDatum(TL_DATUM);
    statusSprite.setTextColor(color, UI_BG);
    statusSprite.drawString(fitTextFor(statusSprite, message, SCREEN_W - 20, 2), 10, 8, 2);
    statusSprite.pushSprite(0, STATUS_Y);
    return;
  }

  tft.fillRect(0, STATUS_Y, SCREEN_W, SCREEN_H - STATUS_Y, UI_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, UI_BG);
  tft.drawString(fitText(message, SCREEN_W - 20, 2), 10, 302, 2);
}

void showStatusScreen(const String &title, const String &detail = "") {
  lastStatusMessage = "";
  tft.fillScreen(UI_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_ACCENT, UI_BG);
  tft.drawString(title, SCREEN_W / 2, 125, 4);

  if (detail.length() > 0) {
    tft.setTextColor(UI_TEXT, UI_BG);
    tft.drawString(fitText(detail, SCREEN_W - 40, 2), SCREEN_W / 2, 175, 2);
  }
}

bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= SCREEN_H) {
    return false;
  }

  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

bool connectToWiFi() {
  showStatusScreen("Connecting to WiFi", WIFI_SSID);

  Serial.println();
  Serial.println("ESP32 Spotify Now Playing Display");
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
    tft.drawString(fitText(detail, SCREEN_W - 40, 2), SCREEN_W / 2, 175, 2);
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    showStatusScreen("WiFi failed", "Check signal or SSID");
    Serial.println("WiFi connection failed.");
    return false;
  }

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  showStatusScreen("WiFi connected", WiFi.localIP().toString());
  delay(1000);
  return true;
}

bool refreshAccessToken() {
  showStatusScreen("Refreshing Spotify", "Getting access token");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(15000);

  if (!http.begin(client, TOKEN_URL)) {
    Serial.println("Could not start token request.");
    return false;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "grant_type=refresh_token";
  body += "&refresh_token=" + urlEncode(SPOTIFY_REFRESH_TOKEN);
  body += "&client_id=" + urlEncode(SPOTIFY_CLIENT_ID);

  int statusCode = http.POST(body);
  String payload = http.getString();
  http.end();

  if (statusCode != HTTP_CODE_OK) {
    Serial.print("Token refresh failed. HTTP status: ");
    Serial.println(statusCode);
    Serial.println(payload);
    drawStatus("Spotify token refresh failed", TFT_RED);
    return false;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("Could not parse token response: ");
    Serial.println(error.c_str());
    drawStatus("Token JSON parse failed", TFT_RED);
    return false;
  }

  accessToken = doc["access_token"].as<String>();
  int expiresIn = doc["expires_in"] | 3600;
  tokenExpiresAt = millis() + ((unsigned long)expiresIn - 60UL) * 1000UL;

  Serial.println("Spotify access token refreshed.");
  return accessToken.length() > 0;
}

bool ensureAccessToken() {
  if (accessToken.length() == 0 || millis() > tokenExpiresAt) {
    return refreshAccessToken();
  }

  return true;
}

String artistsToString(JsonArray artists) {
  String result = "";
  for (JsonVariant artist : artists) {
    if (result.length() > 0) {
      result += ", ";
    }
    result += artist["name"].as<String>();
  }
  return result;
}

String pickAlbumArt(JsonArray images) {
  String fallback = "";
  for (JsonVariant image : images) {
    String url = image["url"].as<String>();
    int width = image["width"] | 0;
    if (url.length() == 0) {
      continue;
    }
    fallback = url;
    if (width > 0 && width <= ART_SIZE) {
      return url;
    }
  }
  return fallback;
}

bool fetchNowPlaying(NowPlaying &playing, bool retriedAfterRefresh = false) {
  playing.hasTrack = false;

  if (!ensureAccessToken()) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(15000);

  if (!http.begin(client, CURRENTLY_PLAYING_URL)) {
    Serial.println("Could not start currently-playing request.");
    return false;
  }

  const char *rateLimitHeaders[] = {"Retry-After"};
  http.collectHeaders(rateLimitHeaders, 1);
  http.addHeader("Authorization", "Bearer " + accessToken);

  int statusCode = http.GET();

  if (statusCode == HTTP_CODE_NO_CONTENT) {
    http.end();
    playing.hasTrack = false;
    nextPollDelay = SPOTIFY_POLL_MS;
    return true;
  }

  if (statusCode == HTTP_CODE_UNAUTHORIZED) {
    http.end();
    if (retriedAfterRefresh) {
      drawStatus("Spotify authorization failed", TFT_RED);
      Serial.println("Spotify authorization failed after refreshing token.");
      return false;
    }
    accessToken = "";
    if (!refreshAccessToken()) {
      return false;
    }
    return fetchNowPlaying(playing, true);
  }

  if (statusCode == 429) {
    String retryAfter = http.header("Retry-After");
    http.end();
    unsigned long retryDelay = (unsigned long)retryAfter.toInt() * 1000UL;
    nextPollDelay = retryDelay > (unsigned long)SPOTIFY_POLL_MS ? retryDelay : (unsigned long)SPOTIFY_POLL_MS;
    drawStatus("Spotify rate limited; waiting", TFT_YELLOW);
    Serial.print("Spotify rate limited. Retry after seconds: ");
    Serial.println(retryAfter);
    return false;
  }

  String payload = http.getString();
  http.end();

  if (statusCode != HTTP_CODE_OK) {
    Serial.print("Currently-playing failed. HTTP status: ");
    Serial.println(statusCode);
    Serial.println(payload);
    drawStatus("Spotify request failed", TFT_RED);
    nextPollDelay = SPOTIFY_POLL_MS;
    return false;
  }

  DynamicJsonDocument doc(49152);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("Could not parse currently-playing response: ");
    Serial.println(error.c_str());
    drawStatus("Spotify JSON parse failed", TFT_RED);
    return false;
  }

  const char *type = doc["currently_playing_type"] | "";
  JsonObject item = doc["item"];
  if (strcmp(type, "track") != 0 || item.isNull()) {
    playing.hasTrack = false;
    nextPollDelay = SPOTIFY_POLL_MS;
    return true;
  }

  playing.hasTrack = true;
  playing.isPlaying = doc["is_playing"] | false;
  playing.progressMs = doc["progress_ms"] | 0;
  playing.durationMs = item["duration_ms"] | 0;
  playing.trackId = item["id"].as<String>();
  playing.title = item["name"].as<String>();
  playing.album = item["album"]["name"].as<String>();
  playing.artists = artistsToString(item["artists"].as<JsonArray>());
  playing.artUrl = pickAlbumArt(item["album"]["images"].as<JsonArray>());

  nextPollDelay = SPOTIFY_POLL_MS;
  return true;
}

bool downloadAlbumArt(const String &url) {
  if (!sdReady || url.length() == 0) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, url)) {
    Serial.println("Could not start album art request.");
    return false;
  }

  int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
    Serial.print("Album art download failed. HTTP status: ");
    Serial.println(statusCode);
    http.end();
    return false;
  }

  SD.remove(ART_PATH);
  File artFile = SD.open(ART_PATH, FILE_WRITE);
  if (!artFile) {
    Serial.println("Could not open SD art cache for writing.");
    http.end();
    return false;
  }

  int written = http.writeToStream(&artFile);
  artFile.close();
  http.end();

  if (written <= 0) {
    Serial.println("No album art bytes written.");
    SD.remove(ART_PATH);
    return false;
  }

  Serial.print("Album art cached bytes: ");
  Serial.println(written);
  return true;
}

void drawAlbumPlaceholder(const String &label) {
  tft.fillRoundRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, 8, UI_PANEL);
  tft.drawRoundRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, 8, UI_BORDER);
  tft.drawRoundRect(ART_X + 8, ART_Y + 8, ART_SIZE - 16, ART_SIZE - 16, 6, UI_PANEL_DARK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_MUTED, UI_PANEL);
  tft.drawString(label, ART_X + ART_SIZE / 2, ART_Y + ART_SIZE / 2, 4);
}

void drawAlbumArt(const NowPlaying &playing) {
  if (playing.artUrl != currentArtUrl) {
    if (downloadAlbumArt(playing.artUrl)) {
      currentArtUrl = playing.artUrl;
    } else {
      currentArtUrl = "";
      drawAlbumPlaceholder("No Art");
      return;
    }
  }

  tft.fillRoundRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, 8, UI_PANEL);
  TJpgDec.setJpgScale(1);
  JRESULT result = TJpgDec.drawSdJpg(ART_X, ART_Y, ART_PATH);
  if (result != JDR_OK) {
    Serial.print("JPEG render failed: ");
    Serial.println(result);
    drawAlbumPlaceholder("Art Error");
  }
}

void drawProgressOnSprite(int progressMs, int durationMs) {
  int barX = 0;
  int barY = 236;
  int barW = INFO_W;
  int barH = 10;
  int filled = 0;

  if (durationMs > 0 && progressMs >= 0) {
    filled = ((long long)progressMs * barW) / durationMs;
  }

  filled = constrain(filled, 0, barW);
  infoSprite.fillRoundRect(barX, barY, barW, barH, 5, UI_PANEL_DARK);
  if (filled > 2) {
    infoSprite.fillRoundRect(barX, barY, filled, barH, 5, UI_ACCENT);
  }

  infoSprite.setTextDatum(TL_DATUM);
  infoSprite.setTextColor(UI_MUTED, UI_PANEL);
  String timing = formatTime(progressMs) + " / " + formatTime(durationMs);
  infoSprite.drawString(fitTextFor(infoSprite, timing, INFO_W, 2), 0, 254, 2);
}

void drawProgressDirect(int progressMs, int durationMs) {
  int barX = INFO_X;
  int barY = 246;
  int barW = INFO_W;
  int barH = 10;
  int filled = 0;

  if (durationMs > 0 && progressMs >= 0) {
    filled = ((long long)progressMs * barW) / durationMs;
  }

  filled = constrain(filled, 0, barW);
  tft.fillRoundRect(barX, barY, barW, barH, 5, UI_PANEL_DARK);
  if (filled > 2) {
    tft.fillRoundRect(barX, barY, filled, barH, 5, UI_ACCENT);
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_MUTED, UI_PANEL);
  String timing = formatTime(progressMs) + " / " + formatTime(durationMs);
  tft.drawString(fitText(timing, INFO_W, 2), INFO_X, 264, 2);
}

void drawTrackInfo(const NowPlaying &playing) {
  if (uiSpritesReady) {
    infoSprite.fillSprite(UI_PANEL);
    infoSprite.setTextDatum(TL_DATUM);

    infoSprite.setTextColor(UI_MUTED, UI_PANEL);
    infoSprite.drawString("NOW PLAYING", 0, 0, 2);

    infoSprite.setTextColor(UI_TEXT, UI_PANEL);
    infoSprite.drawString(fitTextFor(infoSprite, playing.title, INFO_W, 4), 0, 28, 4);

    infoSprite.setTextColor(UI_ACCENT, UI_PANEL);
    infoSprite.drawString(fitTextFor(infoSprite, playing.artists, INFO_W, 2), 0, 82, 2);

    infoSprite.setTextColor(UI_MUTED, UI_PANEL);
    infoSprite.drawString(fitTextFor(infoSprite, playing.album, INFO_W, 2), 0, 108, 2);

    uint16_t stateColor = playing.isPlaying ? UI_ACCENT : UI_WARN;
    infoSprite.fillRoundRect(0, 158, INFO_W, 38, 6, UI_PANEL_DARK);
    infoSprite.setTextColor(stateColor, UI_PANEL_DARK);
    infoSprite.drawString(playing.isPlaying ? "Playing" : "Paused", 12, 165, 4);

    drawProgressOnSprite(playing.progressMs, playing.durationMs);
    infoSprite.pushSprite(INFO_X, INFO_Y);
    return;
  }

  tft.fillRect(INFO_X, INFO_Y, INFO_W, INFO_H, UI_PANEL);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_TEXT, UI_PANEL);
  tft.drawString(fitText(playing.title, INFO_W, 4), INFO_X, 38, 4);
  tft.setTextColor(UI_ACCENT, UI_PANEL);
  tft.drawString(fitText(playing.artists, INFO_W, 2), INFO_X, 92, 2);
  tft.setTextColor(UI_MUTED, UI_PANEL);
  tft.drawString(fitText(playing.album, INFO_W, 2), INFO_X, 118, 2);
  tft.setTextColor(playing.isPlaying ? UI_ACCENT : UI_WARN, UI_PANEL);
  tft.drawString(playing.isPlaying ? "Playing" : "Paused", INFO_X, 175, 4);
  drawProgressDirect(playing.progressMs, playing.durationMs);
}

void drawNowPlaying(const NowPlaying &playing) {
  bool trackChanged = playing.trackId != currentTrackId;
  bool textChanged = playing.title != lastTitle || playing.artists != lastArtist || playing.album != lastAlbum;
  bool stateChanged = playing.isPlaying != lastIsPlaying;
  bool progressChanged = abs(playing.progressMs - lastProgressMs) > 2500 || playing.durationMs != lastDurationMs;

  if (showingNothing) {
    drawAppBackground();
    showingNothing = false;
    lastStatusMessage = "";
  }

  if (trackChanged) {
    currentTrackId = playing.trackId;
    drawAlbumArt(playing);
  }

  if (trackChanged || textChanged || stateChanged || progressChanged) {
    drawTrackInfo(playing);
  }

  lastTitle = playing.title;
  lastArtist = playing.artists;
  lastAlbum = playing.album;
  lastIsPlaying = playing.isPlaying;
  lastProgressMs = playing.progressMs;
  lastDurationMs = playing.durationMs;

  drawStatus("WiFi: " + WiFi.SSID() + "  IP: " + WiFi.localIP().toString());
}

void drawNothingPlaying() {
  if (showingNothing) {
    drawStatus("Waiting for Spotify playback");
    return;
  }

  showingNothing = true;
  currentTrackId = "";
  currentArtUrl = "";
  lastTitle = "";
  lastArtist = "";
  lastAlbum = "";
  lastProgressMs = -1;
  lastDurationMs = -1;
  lastStatusMessage = "";

  drawAppBackground();
  drawAlbumPlaceholder("Spotify");

  if (uiSpritesReady) {
    infoSprite.fillSprite(UI_PANEL);
    infoSprite.setTextDatum(TL_DATUM);
    infoSprite.setTextColor(UI_MUTED, UI_PANEL);
    infoSprite.drawString("SPOTIFY", 0, 0, 2);
    infoSprite.setTextColor(UI_TEXT, UI_PANEL);
    infoSprite.drawString("Nothing", 0, 62, 4);
    infoSprite.drawString("playing", 0, 98, 4);
    infoSprite.setTextColor(UI_MUTED, UI_PANEL);
    infoSprite.drawString("Start music", 0, 168, 2);
    infoSprite.drawString("on your phone.", 0, 192, 2);
    infoSprite.pushSprite(INFO_X, INFO_Y);
  } else {
    tft.fillRect(INFO_X, INFO_Y, INFO_W, INFO_H, UI_PANEL);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(UI_TEXT, UI_PANEL);
    tft.drawString("Nothing", INFO_X, 70, 4);
    tft.drawString("playing", INFO_X, 105, 4);
    tft.setTextColor(UI_MUTED, UI_PANEL);
    tft.drawString("Start Spotify", INFO_X, 170, 2);
    tft.drawString("on your phone.", INFO_X, 194, 2);
  }

  drawStatus("Waiting for Spotify playback");
}

void setup() {
  Serial.begin(115200);
  delay(250);

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(UI_BG);
  initUiSprites();
  TJpgDec.setCallback(tftJpgOutput);

  sdReady = SD.begin(SD_CS);
  if (!sdReady) {
    Serial.println("SD init failed. Album art will show a placeholder.");
  }

  if (!connectToWiFi()) {
    return;
  }

  if (!refreshAccessToken()) {
    showStatusScreen("Spotify setup needed", "Check spotify_config.h");
    return;
  }

  drawAppBackground();
  drawAlbumPlaceholder("Spotify");
  drawStatus("Polling Spotify");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    showStatusScreen("WiFi disconnected", "Reconnecting...");
    accessToken = "";
    if (!connectToWiFi()) {
      delay(5000);
      return;
    }
  }

  if (millis() - lastPoll < nextPollDelay) {
    return;
  }

  lastPoll = millis();

  NowPlaying playing;
  if (!fetchNowPlaying(playing)) {
    return;
  }

  if (!playing.hasTrack) {
    drawNothingPlaying();
    return;
  }

  drawNowPlaying(playing);
}
