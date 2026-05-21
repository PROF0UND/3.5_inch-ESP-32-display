#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include <stdarg.h>
#include <string.h>

extern "C" {
#include "celeste.h"
}
#include "tilemap.h"

#define SD_CS 5
#define CALIBRATION_FILE "/celesteTouchCal"

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int P8_W = 128;
static const int P8_H = 128;
static const int GAME_SCALE = 2;
static const int GAME_W = P8_W * GAME_SCALE;
static const int GAME_H = P8_H * GAME_SCALE;
static const int GAME_X = (SCREEN_W - GAME_W) / 2;
static const int GAME_Y = 0;
static const int CONTROLS_Y = 256;
static const int CONTROLS_H = 64;
static const int TARGET_FRAME_MS = 33;

enum {
  BTN_LEFT = 0,
  BTN_RIGHT = 1,
  BTN_UP = 2,
  BTN_DOWN = 3,
  BTN_JUMP = 4,
  BTN_DASH = 5
};

TFT_eSPI tft = TFT_eSPI();

static uint8_t frameBuffer[P8_W * P8_H];
static uint8_t gfxPixels[128 * 64];
static uint8_t fontPixels[128 * 85];
static uint16_t drawLineA[GAME_W];
static uint16_t drawLineB[GAME_W];

static const uint8_t basePaletteRgb[16][3] = {
  {0x00, 0x00, 0x00}, {0x1d, 0x2b, 0x53}, {0x7e, 0x25, 0x53}, {0x00, 0x87, 0x51},
  {0xab, 0x52, 0x36}, {0x5f, 0x57, 0x4f}, {0xc2, 0xc3, 0xc7}, {0xff, 0xf1, 0xe8},
  {0xff, 0x00, 0x4d}, {0xff, 0xa3, 0x00}, {0xff, 0xec, 0x27}, {0x00, 0xe4, 0x36},
  {0x29, 0xad, 0xff}, {0x83, 0x76, 0x9c}, {0xff, 0x77, 0xa8}, {0xff, 0xcc, 0xaa}
};
static uint16_t basePalette565[16];
static uint8_t paletteMap[16];

static int cameraX = 0;
static int cameraY = 0;
static uint8_t buttonsState = 0;
static uint8_t lastDirMask = 0;
static uint32_t dirLatchUntil = 0;
static uint32_t jumpLatchUntil = 0;
static uint32_t dashLatchUntil = 0;
static bool paused = false;
static bool centerWasDown = false;
static uint32_t centerDownAt = 0;
static uint32_t lastFrameAt = 0;
static bool gameReady = false;

static uint16_t read16(File &f) {
  uint16_t v = f.read();
  v |= (uint16_t)f.read() << 8;
  return v;
}

static uint32_t read32(File &f) {
  uint32_t v = f.read();
  v |= (uint32_t)f.read() << 8;
  v |= (uint32_t)f.read() << 16;
  v |= (uint32_t)f.read() << 24;
  return v;
}

static void resetPalette() {
  for (int i = 0; i < 16; i++) {
    paletteMap[i] = i;
  }
}

static void showFatal(const char *line1, const char *line2 = nullptr) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(12, 18);
  tft.println(line1);
  if (line2) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(12, 48);
    tft.println(line2);
  }
  Serial.println(line1);
  if (line2) Serial.println(line2);
}

static uint8_t readIndexedPixel(const uint8_t *row, int depth, int x) {
  if (depth == 8) return row[x];
  if (depth == 4) {
    uint8_t b = row[x / 2];
    return (x & 1) ? (b & 0x0f) : (b >> 4);
  }
  if (depth == 1) {
    uint8_t b = row[x / 8];
    return (b & (0x80 >> (x & 7))) ? 1 : 0;
  }
  return 0;
}

static bool loadIndexedBmp(const char *path, uint8_t *dst, int expectedW, int expectedH) {
  File bmp = SD.open(path, FILE_READ);
  if (!bmp) return false;

  if (read16(bmp) != 0x4d42) {
    bmp.close();
    return false;
  }

  (void)read32(bmp);
  (void)read32(bmp);
  uint32_t pixelOffset = read32(bmp);
  uint32_t headerSize = read32(bmp);
  int32_t width = (int32_t)read32(bmp);
  int32_t height = (int32_t)read32(bmp);
  uint16_t planes = read16(bmp);
  uint16_t depth = read16(bmp);
  uint32_t compression = read32(bmp);

  if (headerSize < 40 || planes != 1 || compression != 0 ||
      width != expectedW || abs(height) != expectedH ||
      (depth != 1 && depth != 4 && depth != 8)) {
    bmp.close();
    return false;
  }

  bool bottomUp = height > 0;
  int rowSize = ((expectedW * depth + 31) / 32) * 4;
  uint8_t row[64];
  if (rowSize > (int)sizeof(row)) {
    bmp.close();
    return false;
  }

  for (int y = 0; y < expectedH; y++) {
    int srcY = bottomUp ? (expectedH - 1 - y) : y;
    bmp.seek(pixelOffset + (uint32_t)srcY * rowSize);
    if (bmp.read(row, rowSize) != rowSize) {
      bmp.close();
      return false;
    }
    for (int x = 0; x < expectedW; x++) {
      dst[x + y * expectedW] = readIndexedPixel(row, depth, x);
    }
  }

  bmp.close();
  return true;
}

static bool loadAssets() {
  return loadIndexedBmp("/celeste/gfx.bmp", gfxPixels, 128, 64) &&
         loadIndexedBmp("/celeste/font.bmp", fontPixels, 128, 85);
}

static void putPixel(int x, int y, uint8_t color) {
  if ((unsigned)x < P8_W && (unsigned)y < P8_H) {
    frameBuffer[x + y * P8_W] = paletteMap[color & 0x0f];
  }
}

static void rectFill(int x0, int y0, int x1, int y1, uint8_t color) {
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
  if (x1 < 0 || x0 >= P8_W || y1 < 0 || y0 >= P8_H) return;
  x0 = constrain(x0, 0, P8_W - 1);
  x1 = constrain(x1, 0, P8_W - 1);
  y0 = constrain(y0, 0, P8_H - 1);
  y1 = constrain(y1, 0, P8_H - 1);
  uint8_t mappedColor = paletteMap[color & 0x0f];
  for (int y = y0; y <= y1; y++) {
    memset(frameBuffer + y * P8_W + x0, mappedColor, x1 - x0 + 1);
  }
}

static void lineDraw(int x0, int y0, int x1, int y1, uint8_t color) {
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    putPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void circFill(int cx, int cy, int r, uint8_t color) {
  if (r < 0) return;
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) putPixel(cx + x, cy + y, color);
    }
  }
}

static uint8_t getGfxPixel(int x, int y) {
  if ((unsigned)x >= 128 || (unsigned)y >= 64) return 0;
  return gfxPixels[x + y * 128] & 0x0f;
}

static void blitTile(int tile, int x, int y, bool flipX, bool flipY, int colorOverride) {
  if (tile < 0) return;
  int sx = (tile % 16) * 8;
  int sy = (tile / 16) * 8;
  x -= cameraX;
  y -= cameraY;

  for (int yy = 0; yy < 8; yy++) {
    for (int xx = 0; xx < 8; xx++) {
      int srcX = sx + (flipX ? 7 - xx : xx);
      int srcY = sy + (flipY ? 7 - yy : yy);
      uint8_t pix = getGfxPixel(srcX, srcY);
      if (pix) putPixel(x + xx, y + yy, colorOverride >= 0 ? colorOverride : pix);
    }
  }
}

static void printP8(const char *str, int x, int y, uint8_t color) {
  x -= cameraX;
  y -= cameraY;
  while (*str) {
    uint8_t c = ((uint8_t)*str++) & 0x7f;
    int sx = (c % 16) * 8;
    int sy = (c / 16) * 8;
    for (int yy = 0; yy < 8; yy++) {
      for (int xx = 0; xx < 8; xx++) {
        int fy = sy + yy;
        if (fy < 85 && fontPixels[sx + xx + fy * 128]) {
          putPixel(x + xx, y + yy, color);
        }
      }
    }
    x += 4;
  }
}

static bool getTileFlag(int tile, int flag) {
  return tile >= 0 &&
         tile < (int)(sizeof(tile_flags) / sizeof(tile_flags[0])) &&
         (tile_flags[tile] & (1 << flag));
}

static int celesteCallback(CELESTE_P8_CALLBACK_TYPE call, ...) {
  va_list args;
  va_start(args, call);
  int ret = 0;

  switch (call) {
    case CELESTE_P8_MUSIC:
      (void)va_arg(args, int);
      (void)va_arg(args, int);
      (void)va_arg(args, int);
      break;
    case CELESTE_P8_SPR: {
      int sprite = va_arg(args, int);
      int x = va_arg(args, int);
      int y = va_arg(args, int);
      int cols = va_arg(args, int);
      int rows = va_arg(args, int);
      bool flipX = va_arg(args, int);
      bool flipY = va_arg(args, int);
      for (int ry = 0; ry < rows; ry++) {
        for (int cx = 0; cx < cols; cx++) {
          blitTile(sprite + cx + ry * 16, x + cx * 8, y + ry * 8, flipX, flipY, -1);
        }
      }
      break;
    }
    case CELESTE_P8_BTN: {
      int b = va_arg(args, int);
      ret = (b >= 0 && b <= 5) ? !!(buttonsState & (1 << b)) : 0;
      break;
    }
    case CELESTE_P8_SFX:
      (void)va_arg(args, int);
      break;
    case CELESTE_P8_PAL: {
      int a = va_arg(args, int);
      int b = va_arg(args, int);
      if (a >= 0 && a < 16 && b >= 0 && b < 16) {
        paletteMap[a] = b;
      }
      break;
    }
    case CELESTE_P8_PAL_RESET:
      resetPalette();
      break;
    case CELESTE_P8_CIRCFILL: {
      int x = va_arg(args, int) - cameraX;
      int y = va_arg(args, int) - cameraY;
      int r = va_arg(args, int);
      int c = va_arg(args, int);
      circFill(x, y, r, c);
      break;
    }
    case CELESTE_P8_PRINT: {
      const char *str = va_arg(args, const char *);
      int x = va_arg(args, int);
      int y = va_arg(args, int);
      int c = va_arg(args, int);
      printP8(str, x, y, c);
      break;
    }
    case CELESTE_P8_RECTFILL: {
      int x0 = va_arg(args, int) - cameraX;
      int y0 = va_arg(args, int) - cameraY;
      int x1 = va_arg(args, int) - cameraX;
      int y1 = va_arg(args, int) - cameraY;
      int c = va_arg(args, int);
      rectFill(x0, y0, x1, y1, c);
      break;
    }
    case CELESTE_P8_LINE: {
      int x0 = va_arg(args, int) - cameraX;
      int y0 = va_arg(args, int) - cameraY;
      int x1 = va_arg(args, int) - cameraX;
      int y1 = va_arg(args, int) - cameraY;
      int c = va_arg(args, int);
      lineDraw(x0, y0, x1, y1, c);
      break;
    }
    case CELESTE_P8_MGET: {
      int tx = va_arg(args, int);
      int ty = va_arg(args, int);
      ret = ((unsigned)tx < 128 && (unsigned)ty < 64) ? tilemap_data[tx + ty * 128] : 0;
      break;
    }
    case CELESTE_P8_CAMERA:
      cameraX = va_arg(args, int);
      cameraY = va_arg(args, int);
      break;
    case CELESTE_P8_FGET: {
      int tile = va_arg(args, int);
      int flag = va_arg(args, int);
      ret = getTileFlag(tile, flag);
      break;
    }
    case CELESTE_P8_MAP: {
      int mx = va_arg(args, int);
      int my = va_arg(args, int);
      int tx = va_arg(args, int);
      int ty = va_arg(args, int);
      int mw = va_arg(args, int);
      int mh = va_arg(args, int);
      int mask = va_arg(args, int);
      for (int x = 0; x < mw; x++) {
        for (int y = 0; y < mh; y++) {
          int mapX = mx + x;
          int mapY = my + y;
          if ((unsigned)mapX >= 128 || (unsigned)mapY >= 64) continue;
          int tile = tilemap_data[mapX + mapY * 128];
          bool exactFlag4 = tile >= 0 &&
                            tile < (int)(sizeof(tile_flags) / sizeof(tile_flags[0])) &&
                            tile_flags[tile] == 4;
          if (mask == 0 || (mask == 4 && exactFlag4) ||
              getTileFlag(tile, mask != 4 ? mask - 1 : mask)) {
            blitTile(tile, tx + x * 8, ty + y * 8, false, false, -1);
          }
        }
      }
      break;
    }
  }

  va_end(args);
  return ret;
}

static void flushGameFrame() {
  for (int y = 0; y < P8_H; y++) {
    for (int x = 0; x < P8_W; x++) {
      uint16_t c = basePalette565[frameBuffer[x + y * P8_W] & 0x0f];
      drawLineA[x * 2] = c;
      drawLineA[x * 2 + 1] = c;
    }
    memcpy(drawLineB, drawLineA, sizeof(drawLineA));
    tft.pushImage(GAME_X, GAME_Y + y * 2, GAME_W, 1, drawLineA);
    tft.pushImage(GAME_X, GAME_Y + y * 2 + 1, GAME_W, 1, drawLineB);
  }
}

static void drawControlButton(int x, int y, int w, int h, const char *label, uint16_t border, bool active) {
  uint16_t fill = active ? border : TFT_BLACK;
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 4, fill);
  tft.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 4, border);
  tft.setTextColor(active ? TFT_BLACK : border, fill);
  tft.setTextSize(2);
  int tx = x + (w / 2) - (int)strlen(label) * 6;
  int ty = y + (h / 2) - 8;
  tft.setCursor(tx, ty);
  tft.print(label);
}

static void drawControls() {
  tft.fillRect(0, CONTROLS_Y, SCREEN_W, CONTROLS_H, TFT_BLACK);
  drawControlButton(0, CONTROLS_Y, 70, CONTROLS_H, "<", TFT_CYAN, buttonsState & (1 << BTN_LEFT));
  drawControlButton(70, CONTROLS_Y, 70, CONTROLS_H / 2, "^", TFT_CYAN, buttonsState & (1 << BTN_UP));
  drawControlButton(70, CONTROLS_Y + CONTROLS_H / 2, 70, CONTROLS_H / 2, "v", TFT_CYAN, buttonsState & (1 << BTN_DOWN));
  drawControlButton(140, CONTROLS_Y, 70, CONTROLS_H, ">", TFT_CYAN, buttonsState & (1 << BTN_RIGHT));
  drawControlButton(210, CONTROLS_Y, 90, CONTROLS_H, paused ? "PLAY" : "PAUSE", TFT_YELLOW, paused);
  drawControlButton(300, CONTROLS_Y, 90, CONTROLS_H, "JUMP", TFT_GREEN, buttonsState & (1 << BTN_JUMP));
  drawControlButton(390, CONTROLS_Y, 90, CONTROLS_H, "DASH", TFT_RED, buttonsState & (1 << BTN_DASH));
}

static void loadTouchCalibration() {
  uint16_t calibrationData[5];
  bool ok = false;

  if (!SPIFFS.begin()) {
    SPIFFS.format();
    SPIFFS.begin();
  }

  if (SPIFFS.exists(CALIBRATION_FILE)) {
    File f = SPIFFS.open(CALIBRATION_FILE, "r");
    if (f) {
      ok = f.readBytes((char *)calibrationData, sizeof(calibrationData)) == sizeof(calibrationData);
      f.close();
    }
  }

  if (!ok) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 20);
    tft.println("Touch calibration");
    tft.calibrateTouch(calibrationData, TFT_WHITE, TFT_RED, 15);
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const uint8_t *)calibrationData, sizeof(calibrationData));
      f.close();
    }
  }

  tft.setTouch(calibrationData);
}

static void resetGame() {
  paused = false;
  buttonsState = 0;
  lastDirMask = 0;
  dirLatchUntil = jumpLatchUntil = dashLatchUntil = 0;
  cameraX = cameraY = 0;
  Celeste_P8_set_rndseed((unsigned)(micros() ^ millis()));
  Celeste_P8_init();
  drawControls();
}

static void updateTouchInput() {
  uint16_t x, y;
  uint32_t now = millis();
  bool touched = tft.getTouch(&x, &y, 400);

  if (touched && y >= CONTROLS_Y) {
    if (x < 70) {
      lastDirMask = 1 << BTN_LEFT;
      dirLatchUntil = now + 180;
    } else if (x < 140) {
      lastDirMask = (y < CONTROLS_Y + CONTROLS_H / 2) ? (1 << BTN_UP) : (1 << BTN_DOWN);
      dirLatchUntil = now + 180;
    } else if (x < 210) {
      lastDirMask = 1 << BTN_RIGHT;
      dirLatchUntil = now + 180;
    } else if (x < 300) {
      if (!centerWasDown) centerDownAt = now;
      centerWasDown = true;
    } else if (x < 390) {
      jumpLatchUntil = now + 120;
    } else {
      dashLatchUntil = now + 140;
    }
  } else if (centerWasDown) {
    if (now - centerDownAt > 900) {
      resetGame();
    } else {
      paused = !paused;
      drawControls();
    }
    centerWasDown = false;
  }

  uint8_t next = 0;
  if (now < dirLatchUntil) next |= lastDirMask;
  if (now < jumpLatchUntil) next |= 1 << BTN_JUMP;
  if (now < dashLatchUntil) next |= 1 << BTN_DASH;
  if ((next & (1 << BTN_DASH)) && !(next & 0x0f)) next |= lastDirMask;

  if (next != buttonsState) {
    buttonsState = next;
    drawControls();
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  for (int i = 0; i < 16; i++) {
    basePalette565[i] = tft.color565(basePaletteRgb[i][0], basePaletteRgb[i][1], basePaletteRgb[i][2]);
  }
  resetPalette();
  loadTouchCalibration();

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(14, 14);
  tft.println("Mounting SD...");
  if (!SD.begin(SD_CS)) {
    showFatal("SD init failed", "Copy assets to /celeste on SD");
    return;
  }
  if (!loadAssets()) {
    showFatal("Celeste assets missing", "Need /celeste/gfx.bmp and font.bmp");
    return;
  }

  tft.fillScreen(TFT_BLACK);
  drawControls();
  Celeste_P8_set_call_func(celesteCallback);
  resetGame();
  gameReady = true;
  lastFrameAt = millis();
}

void loop() {
  if (!gameReady) {
    delay(100);
    return;
  }

  updateTouchInput();

  uint32_t now = millis();
  if (now - lastFrameAt < TARGET_FRAME_MS) {
    delay(1);
    return;
  }
  lastFrameAt = now;

  if (!paused) {
    Celeste_P8_update();
  }
  Celeste_P8_draw();
  flushGameFrame();

  if (paused) {
    tft.fillRect(GAME_X + 76, GAME_Y + 112, 104, 32, TFT_BLACK);
    tft.drawRect(GAME_X + 76, GAME_Y + 112, 104, 32, TFT_YELLOW);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(GAME_X + 92, GAME_Y + 120);
    tft.print("PAUSED");
  }
}
