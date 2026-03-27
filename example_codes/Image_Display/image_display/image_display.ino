#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>

// ====== USER SETTINGS ======
#define SD_CS 5
// Set to 1 if red/blue are swapped on your display
#define BMP_SWAP_RB 0
// Set to 1 if colors look "garbled" due to byte order
#define BMP_SWAP_BYTES 1
// ===========================

TFT_eSPI tft = TFT_eSPI(); // Create display object

// Screen size
const int SCREEN_W = 480;
const int SCREEN_H = 320;

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
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
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

  uint32_t fileSize = read32(bmpFile);
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

  uint32_t imageSize = read32(bmpFile);
  (void)read32(bmpFile); // xPixelsPerMeter
  (void)read32(bmpFile); // yPixelsPerMeter
  (void)read32(bmpFile); // colorsUsed
  (void)read32(bmpFile); // importantColors
  if (compression == 3) {
    // For BITFIELDS, masks are located at fixed offsets inside the DIB header
    // (right after the first 40 bytes of BITMAPINFOHEADER).
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

  Serial.println("BMP header:");
  Serial.print("  fileSize="); Serial.println(fileSize);
  Serial.print("  imageOffset="); Serial.println(imageOffset);
  Serial.print("  headerSize="); Serial.println(headerSize);
  Serial.print("  width="); Serial.println(bmpWidth);
  Serial.print("  height="); Serial.println(bmpHeight);
  Serial.print("  planes="); Serial.println(planes);
  Serial.print("  depth="); Serial.println(depth);
  Serial.print("  compression="); Serial.println(compression);
  Serial.print("  imageSize="); Serial.println(imageSize);
  if (compression == 3) {
    Serial.print("  rMask=0x"); Serial.println(rMask, HEX);
    Serial.print("  gMask=0x"); Serial.println(gMask, HEX);
    Serial.print("  bMask=0x"); Serial.println(bMask, HEX);
    Serial.print("  aMask=0x"); Serial.println(aMask, HEX);
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
    // Default 16-bit BMP is 5-6-5
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
        if (BMP_SWAP_RB) {
          uint8_t tmp = r;
          r = b;
          b = tmp;
        }
        lcdbuffer[i] = tft.color565(r, g, b);
        if (BMP_SWAP_BYTES) {
          lcdbuffer[i] = (uint16_t)((lcdbuffer[i] << 8) | (lcdbuffer[i] >> 8));
        }
      }

      tft.pushImage(x + col, y + row, chunk, 1, lcdbuffer);
      col += chunk;
    }
  }

  bmpFile.close();
  return true;
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);  // Landscape
  tft.fillScreen(TFT_BLACK);

  if (!SD.begin(SD_CS)) {
    showError("SD init failed");
    return;
  }

  if (drawBMP("/image.bmp", 0, 0)) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, SCREEN_H - 24);
    tft.println("BMP loaded");
  }
}

void loop() {
  // Nothing to do; image is drawn in setup()
}
