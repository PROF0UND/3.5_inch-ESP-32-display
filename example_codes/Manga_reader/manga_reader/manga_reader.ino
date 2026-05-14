#include "FS.h"
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <SPIFFS.h>

// ====== USER SETTINGS ======
#define SD_CS 5
// Paste your calibration from Touch_calibrate:
uint16_t calData[5] = { 300, 3800, 320, 3650, 1 }; // <-- REPLACE WITH YOUR VALUES
#define CALIBRATION_FILE "/calibrationData"
// ===========================

TFT_eSPI tft = TFT_eSPI();

// Screen layout (rotation = 1 -> 480x320)
const int SCREEN_W = 480;
const int SCREEN_H = 320;
const int TOPBAR_H = 28;
const int ROW_H    = 24;
const int MARGIN_X = 8;

// Tap zones
const int BACK_X1 = 0, BACK_Y1 = 0, BACK_X2 = 90, BACK_Y2 = TOPBAR_H; // "< Back"

// Reader state
enum Mode { MODE_LIST, MODE_PAGE };
Mode mode = MODE_LIST;

const int MAX_BOOKS = 64;
String books[MAX_BOOKS];
int bookCount = 0;
int listPage = 0;

String currentBookName;
String currentBookPath;
int currentPage = 1;
int maxPage = 1;

// ---------- BMP helpers (from image_display.ino) ----------
static uint16_t read16(File &f) {
  uint16_t result;
  result  = (uint16_t)f.read();
  result |= (uint16_t)f.read() << 8;
  return result;
}

static uint32_t read32(File &f) {
  uint32_t result;
  result  = (uint32_t)f.read();
  result |= (uint32_t)f.read() << 8;
  result |= (uint32_t)f.read() << 16;
  result |= (uint32_t)f.read() << 24;
  return result;
}

static uint8_t countBits(uint32_t mask) {
  uint8_t bits = 0;
  while (mask) {
    bits += (mask & 1U) ? 1 : 0;
    mask >>= 1U;
  }
  return bits;
}

static uint8_t maskShift(uint32_t mask) {
  if (mask == 0) return 0;
  uint8_t shift = 0;
  while ((mask & 1U) == 0) {
    mask >>= 1U;
    shift++;
  }
  return shift;
}

static uint8_t scaleTo8(uint32_t value, uint8_t bits) {
  if (bits == 0) return 0;
  if (bits >= 8) return (uint8_t)(value >> (bits - 8));
  return (uint8_t)((value * 255U) / ((1U << bits) - 1U));
}

static void showError(const char *msg) {
  tft.fillRect(0, TOPBAR_H, SCREEN_W, SCREEN_H - TOPBAR_H, TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(MARGIN_X, TOPBAR_H + 8);
  tft.println(msg);
  Serial.println(msg);
}

static bool drawBMP(const char *path, int16_t x, int16_t y) {
  File bmpFile = SD.open(path);
  if (!bmpFile) {
    showError("BMP open failed");
    return false;
  }

  if (read16(bmpFile) != 0x4D42) {
    bmpFile.close();
    showError("Not a BMP");
    return false;
  }

  (void)read32(bmpFile); // fileSize
  (void)read32(bmpFile); // creator bytes
  uint32_t imageOffset = read32(bmpFile);
  uint32_t headerSize = read32(bmpFile);
  if (headerSize < 40) {
    bmpFile.close();
    showError("BMP header");
    return false;
  }

  int32_t bmpWidth  = (int32_t)read32(bmpFile);
  int32_t bmpHeight = (int32_t)read32(bmpFile);
  uint16_t planes = read16(bmpFile);
  uint16_t depth  = read16(bmpFile);
  uint32_t compression = read32(bmpFile);

  uint32_t rMask = 0x00FF0000;
  uint32_t gMask = 0x0000FF00;
  uint32_t bMask = 0x000000FF;
  uint32_t aMask = 0xFF000000;

  (void)read32(bmpFile); // imageSize
  (void)read32(bmpFile); // xPixelsPerMeter
  (void)read32(bmpFile); // yPixelsPerMeter
  (void)read32(bmpFile); // colorsUsed
  (void)read32(bmpFile); // importantColors
  if (compression == 3) {
    const uint32_t dibStart = 14;
    uint32_t maskPos = dibStart + 40;
    bmpFile.seek(maskPos);
    rMask = read32(bmpFile);
    gMask = read32(bmpFile);
    bMask = read32(bmpFile);
    if (headerSize >= 56) {
      aMask = read32(bmpFile);
    } else {
      aMask = 0;
    }
  }

  bool compressionSupported =
      (compression == 0) ||
      ((depth == 16 || depth == 32) && compression == 3);
  if (planes != 1 || (depth != 16 && depth != 24 && depth != 32) || !compressionSupported) {
    bmpFile.close();
    showError("BMP unsupported");
    return false;
  }

  bool flip = true;
  if (bmpHeight < 0) {
    bmpHeight = -bmpHeight;
    flip = false;
  }

  int32_t w = bmpWidth;
  int32_t h = bmpHeight;
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (y + h > SCREEN_H) h = SCREEN_H - y;

  uint8_t bytesPerPixel = (depth == 32) ? 4 : (depth == 16 ? 2 : 3);
  uint32_t rowSize = (bmpWidth * bytesPerPixel + 3) & ~3;
  uint8_t  sdbuffer[4 * 80];
  uint16_t lcdbuffer[80];

  if (depth == 16 && compression == 0) {
    rMask = 0xF800;
    gMask = 0x07E0;
    bMask = 0x001F;
  }

  uint8_t rShift = maskShift(rMask);
  uint8_t gShift = maskShift(gMask);
  uint8_t bShift = maskShift(bMask);
  uint8_t rBits = countBits(rMask);
  uint8_t gBits = countBits(gMask);
  uint8_t bBits = countBits(bMask);

  for (int32_t row = 0; row < h; row++) {
    uint32_t pos = imageOffset + (flip ? (bmpHeight - 1 - row) : row) * rowSize;
    if (bmpFile.position() != pos) {
      bmpFile.seek(pos);
    }

    int32_t col = 0;
    while (col < w) {
      int32_t chunk = w - col;
      if (chunk > 80) chunk = 80;

      int32_t bytesToRead = chunk * bytesPerPixel;
      int32_t bytesRead = bmpFile.read(sdbuffer, bytesToRead);
      if (bytesRead != bytesToRead) {
        bmpFile.close();
        showError("BMP read err");
        return false;
      }

      for (int32_t i = 0; i < chunk; i++) {
        uint8_t r, g, b;
        if (bytesPerPixel == 2) {
          uint16_t px =  (uint16_t)sdbuffer[i * bytesPerPixel + 0]
                       | ((uint16_t)sdbuffer[i * bytesPerPixel + 1] << 8);
          r = scaleTo8((px & rMask) >> rShift, rBits);
          g = scaleTo8((px & gMask) >> gShift, gBits);
          b = scaleTo8((px & bMask) >> bShift, bBits);
        } else if (bytesPerPixel == 3) {
          b = sdbuffer[i * bytesPerPixel + 0];
          g = sdbuffer[i * bytesPerPixel + 1];
          r = sdbuffer[i * bytesPerPixel + 2];
        } else {
          uint32_t px =  (uint32_t)sdbuffer[i * bytesPerPixel + 0]
                       | ((uint32_t)sdbuffer[i * bytesPerPixel + 1] << 8)
                       | ((uint32_t)sdbuffer[i * bytesPerPixel + 2] << 16)
                       | ((uint32_t)sdbuffer[i * bytesPerPixel + 3] << 24);
          r = scaleTo8((px & rMask) >> rShift, rBits);
          g = scaleTo8((px & gMask) >> gShift, gBits);
          b = scaleTo8((px & bMask) >> bShift, bBits);
        }
        lcdbuffer[i] = tft.color565(r, g, b);
      }

      tft.pushImage(x + col, y + row, chunk, 1, lcdbuffer);
      col += chunk;
    }
  }

  bmpFile.close();
  return true;
}

// ---------- Touch utils ----------
bool getTouch(uint16_t &x, uint16_t &y) {
  static uint32_t last = 0;
  if (millis() - last < 20) return false;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) last = millis();
  return pressed;
}
void waitRelease() {
  uint16_t x, y;
  while (tft.getTouch(&x, &y)) delay(10);
}

// ---------- Touch calibration ----------
void loadTouchCalibration() {
  uint16_t calibrationData[5];
  uint8_t calDataOK = 0;

  if (!SPIFFS.begin()) {
    Serial.println("formatting file system");
    SPIFFS.format();
    SPIFFS.begin();
  }

  if (SPIFFS.exists(CALIBRATION_FILE)) {
    File f = SPIFFS.open(CALIBRATION_FILE, "r");
    if (f) {
      if (f.readBytes((char *)calibrationData, 14) == 14) {
        calDataOK = 1;
      }
      f.close();
    }
  }

  if (calDataOK) {
    tft.setTouch(calibrationData);
  } else {
    tft.calibrateTouch(calibrationData, TFT_WHITE, TFT_RED, 15);
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const unsigned char *)calibrationData, 14);
      f.close();
    }
    tft.setTouch(calibrationData);
  }
}

// ---------- UI ----------
void drawTopBar(const String& title, bool showBack) {
  tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, TFT_DARKGREY);
  tft.drawFastHLine(0, TOPBAR_H - 1, SCREEN_W, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  if (showBack) {
    tft.setCursor(6, 5);
    tft.print("< Back");
  }

  String t = title;
  if (t.length() > 30) t = t.substring(0, 27) + "...";
  int cx = (SCREEN_W / 2) - (t.length() * 6);
  if (cx < 100) cx = 100;
  tft.setCursor(cx, 5);
  tft.print(t);
}

void drawTouchDebug(uint16_t x, uint16_t y) {
  Serial.print("Touch: ");
  Serial.print(x);
  Serial.print(", ");
  Serial.println(y);

  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK, TFT_DARKGREY);
  tft.setCursor(SCREEN_W - 140, 5);
  tft.printf("%3u,%3u", x, y);
  tft.drawPixel(x, y, TFT_RED);
}

void drawTouchRawDebug(uint16_t rx, uint16_t ry, uint16_t rz) {
  Serial.print("Raw: ");
  Serial.print(rx);
  Serial.print(", ");
  Serial.print(ry);
  Serial.print(" z=");
  Serial.println(rz);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(SCREEN_W - 200, 5);
  tft.printf("R%3u,%3u", rx, ry);
}

// ---------- Book list ----------
void listBooks() {
  bookCount = 0;
  File dir = SD.open("/books");
  if (!dir || !dir.isDirectory()) {
    return;
  }

  File f = dir.openNextFile();
  while (f && bookCount < MAX_BOOKS) {
    if (f.isDirectory()) {
      books[bookCount++] = String(f.name());
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
}

void drawBookList() {
  mode = MODE_LIST;
  tft.fillScreen(TFT_BLACK);
  drawTopBar("Books", false);

  if (bookCount == 0) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(MARGIN_X, TOPBAR_H + 8);
    tft.println("No folders in /books");
    return;
  }

  tft.setTextSize(2);
  int rowsPerPage = (SCREEN_H - TOPBAR_H) / ROW_H;
  int start = listPage * rowsPerPage;
  int end = min(bookCount, start + rowsPerPage);

  for (int i = start, r = 0; i < end; ++i, ++r) {
    int y = TOPBAR_H + r * ROW_H;
    uint16_t bg = (r % 2 == 0) ? TFT_BLACK : (uint16_t)0x0841;
    tft.fillRect(0, y, SCREEN_W, ROW_H, bg);
    tft.setTextColor(TFT_WHITE, bg);
    tft.setCursor(MARGIN_X, y + 4);
    tft.print(books[i]);
  }
}

// ---------- Page view ----------
int detectMaxPage(const String& bookPath) {
  int foundMax = 0;
  File dir = SD.open(bookPath);
  if (!dir || !dir.isDirectory()) {
    return 0;
  }

  File f = dir.openNextFile();
  while (f) {
    String name = String(f.name());
    if (!f.isDirectory() && name.startsWith("page_") && name.endsWith(".bmp")) {
      String num = name.substring(5, 9);
      int n = num.toInt();
      if (n > foundMax) foundMax = n;
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
  return foundMax;
}

String pagePath(const String& bookPath, int pageNum) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s/page_%04d.bmp", bookPath.c_str(), pageNum);
  return String(buf);
}

void drawPageHeader() {
  String title = currentBookName + "  " + String(currentPage) + "/" + String(maxPage);
  drawTopBar(title, true);
}

void drawCurrentPage() {
  String path = pagePath(currentBookPath, currentPage);
  bool ok = drawBMP(path.c_str(), 0, 0);
  drawPageHeader();
  if (!ok) {
    showError("Missing page image");
  }
}

void openBook(int idx) {
  currentBookName = books[idx];
  currentBookPath = "/books/" + currentBookName;
  maxPage = detectMaxPage(currentBookPath);
  if (maxPage <= 0) {
    tft.fillScreen(TFT_BLACK);
    drawTopBar(currentBookName, true);
    showError("No pages found");
    mode = MODE_PAGE;
    return;
  }
  currentPage = 1;
  mode = MODE_PAGE;
  drawCurrentPage();
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(3);
  loadTouchCalibration();

  tft.fillScreen(TFT_BLACK);
  drawTopBar("Mounting SD...", false);

  if (!SD.begin(SD_CS)) {
    tft.fillScreen(TFT_BLACK);
    drawTopBar("SD init failed", false);
    showError("Card Mount Failed");
    while (true) delay(100);
  }

  listBooks();
  drawBookList();
}

void loop() {
  uint16_t x, y;
  uint16_t rx, ry;
  uint16_t rz = tft.getTouchRawZ();

  if (rz > 0) {
    tft.getTouchRaw(&rx, &ry);
    drawTouchRawDebug(rx, ry, rz);
  }

  if (!tft.getTouch(&x, &y, 400)) {
    return;
  }

  drawTouchDebug(x, y);

  // Back to list
  if (x >= BACK_X1 && x < BACK_X2 && y >= BACK_Y1 && y < BACK_Y2) {
    if (mode == MODE_PAGE) {
      mode = MODE_LIST;
      drawBookList();
    }
    waitRelease();
    return;
  }

  if (mode == MODE_LIST && y >= TOPBAR_H && y < SCREEN_H) {
    int rowsPerPage = (SCREEN_H - TOPBAR_H) / ROW_H;
    int row = (y - TOPBAR_H) / ROW_H;
    int idx = listPage * rowsPerPage + row;
    if (idx >= 0 && idx < bookCount) {
      openBook(idx);
      waitRelease();
      return;
    }
  }

  if (mode == MODE_PAGE && y >= TOPBAR_H) {
    if (x < (SCREEN_W / 2)) {
      if (currentPage > 1) {
        currentPage--;
        drawCurrentPage();
      }
    } else {
      if (currentPage < maxPage) {
        currentPage++;
        drawCurrentPage();
      } else {
        showError("End of book");
      }
    }
    waitRelease();
    return;
  }

  waitRelease();
}
