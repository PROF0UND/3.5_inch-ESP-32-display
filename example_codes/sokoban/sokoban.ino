#include <Arduino.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include <string.h>

#define CALIBRATION_FILE "/sokobanTouchCal"

// ── Screen ────────────────────────────────────────────────────────────────────
static const int SCREEN_W   = 480;
static const int SCREEN_H   = 320;

// ── Game grid ─────────────────────────────────────────────────────────────────
static const int TILE        = 24;   // px per cell
static const int COLS        = 16;   // max level width  (16 * 24 = 384)
static const int ROWS        = 10;   // max level height (10 * 24 = 240)

// Grid is drawn starting here so it's centred in the top 240 px
static const int GRID_X      = (SCREEN_W - COLS * TILE) / 2;  // 48
static const int GRID_Y      = 0;

// ── Controls strip ────────────────────────────────────────────────────────────
static const int CTRL_Y      = 256;
static const int CTRL_H      = 64;

// ── Colours (RGB565) ──────────────────────────────────────────────────────────
// These are pre-swapped for displays that need byte-swap (like ILI9341 via SPI)
// If your colors look wrong, toggle the swap in the helper below.
static TFT_eSPI tft = TFT_eSPI();

static uint16_t swap16(uint16_t c) {
  // Remove this swap if your display does NOT need it
  return (c << 8) | (c >> 8);
}

static uint16_t C(uint8_t r, uint8_t g, uint8_t b) {
  return swap16(tft.color565(r, g, b));
}

// Colours filled after tft.init()
static uint16_t COL_WALL, COL_FLOOR, COL_GOAL, COL_BOX,
                COL_BOX_OK, COL_PLAYER, COL_BG, COL_TEXT;

static void initColors() {
  COL_WALL   = C(0x5f, 0x57, 0x4f);
  COL_FLOOR  = C(0xc2, 0xc3, 0xc7);
  COL_GOAL   = C(0xff, 0xec, 0x27);
  COL_BOX    = C(0xab, 0x52, 0x36);
  COL_BOX_OK = C(0x00, 0xe4, 0x36);
  COL_PLAYER = C(0x29, 0xad, 0xff);
  COL_BG     = C(0x1d, 0x2b, 0x53);
  COL_TEXT   = C(0xff, 0xf1, 0xe8);
}

// ── Level data ────────────────────────────────────────────────────────────────
// Cell encoding:
//   ' ' floor   '#' wall   '.' goal   '@' player   '+' player-on-goal
//   '$' box     '*' box-on-goal
// Levels are stored in PROGMEM.  Each level is a 10-row, 16-col block of chars
// padded with spaces.  A NUL row signals end-of-level-set.

static const char levels[][ROWS][COLS + 1] PROGMEM = {
  // Level 1 – very easy
  {
    "################",
    "#     #        #",
    "#  $ .#   #    #",
    "#     #   #    #",
    "# @ . # . #    #",
    "#     #   #    #",
    "# $ . #   #    #",
    "#     #        #",
    "################",
    "                ",
  },
  // Level 2
  {
    "  ######        ",
    "  #    #        ",
    "  # $  #        ",
    "### $ ##        ",
    "#  .. #         ",
    "# #.. #         ",
    "# @ $ #         ",
    "#     #         ",
    "#######         ",
    "                ",
  },
  // Level 3
  {
    "########        ",
    "#  @   #        ",
    "#  $ $ #        ",
    "## # # ##       ",
    " # . . #        ",
    " #######        ",
    "                ",
    "                ",
    "                ",
    "                ",
  },
  // Level 4
  {
    "   #####        ",
    "   #   #        ",
    "   # $ #        ",
    " ### $ ##       ",
    " # . . ##       ",
    " # #.  #        ",
    "## @   #        ",
    "#  #####        ",
    "#               ",
    "####            ",
  },
  // Level 5
  {
    " ######         ",
    " #    ##        ",
    "## $   #        ",
    "#  $ $ #        ",
    "# . .  #        ",
    "#  . ###        ",
    "## @ #          ",
    " ####           ",
    "                ",
    "                ",
  },
  // Level 6
  {
    "#####           ",
    "#   ##          ",
    "# $  #          ",
    "#  $ ##         ",
    "## .  #         ",
    " # .. #         ",
    " # @  #         ",
    " ######         ",
    "                ",
    "                ",
  },
  // Level 7
  {
    "  ######        ",
    "  #    #        ",
    "### $$ #        ",
    "#  . . ##       ",
    "# @..   #       ",
    "#   $   #       ",
    "#########       ",
    "                ",
    "                ",
    "                ",
  },
  // Level 8
  {
    " #######        ",
    " #     #        ",
    " # .$. #        ",
    "## $@$ ##       ",
    "#  .$.  #       ",
    "#       #       ",
    "#########       ",
    "                ",
    "                ",
    "                ",
  },
};

static const int NUM_LEVELS = sizeof(levels) / sizeof(levels[0]);

// ── Game state ────────────────────────────────────────────────────────────────
static char  grid[ROWS][COLS + 1];   // live mutable grid
static int   playerRow, playerCol;
static int   currentLevel = 0;
static int   moves = 0;
static bool  won = false;

// Undo stack – store (playerR, playerC, grid snapshot) up to 64 moves
static const int UNDO_DEPTH = 64;
struct UndoFrame {
  int8_t pr, pc;
  char   g[ROWS][COLS + 1];
};
static UndoFrame undoStack[UNDO_DEPTH];
static int undoTop = 0;  // next free slot

// ── Touch state ───────────────────────────────────────────────────────────────
static bool  touchWasDown = false;
static uint32_t touchDownAt = 0;
static uint16_t touchDownX = 0, touchDownY = 0;
static const int SWIPE_MIN = 20;   // px to count as a swipe

// ── Touch calibration ─────────────────────────────────────────────────────────
static void loadTouchCalibration() {
  uint16_t cal[5];
  bool ok = false;
  if (!SPIFFS.begin()) { SPIFFS.format(); SPIFFS.begin(); }
  if (SPIFFS.exists(CALIBRATION_FILE)) {
    File f = SPIFFS.open(CALIBRATION_FILE, "r");
    if (f) {
      ok = f.readBytes((char *)cal, sizeof(cal)) == sizeof(cal);
      f.close();
    }
  }
  if (!ok) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 20);
    tft.println("Touch calibration");
    tft.calibrateTouch(cal, TFT_WHITE, TFT_RED, 15);
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) { f.write((const uint8_t *)cal, sizeof(cal)); f.close(); }
  }
  tft.setTouch(cal);
}

// ── Tile drawing ──────────────────────────────────────────────────────────────
static void drawTile(int row, int col, char cell) {
  int x = GRID_X + col * TILE;
  int y = GRID_Y + row * TILE;

  uint16_t bg = COL_FLOOR;
  if (cell == '#') bg = COL_WALL;
  else if (cell == ' ') bg = COL_BG;

  tft.fillRect(x, y, TILE, TILE, bg);

  if (cell == '#' || cell == ' ') return;

  // Inner decoration
  if (cell == '.' || cell == '+' || cell == '*') {
    // Goal marker – small yellow diamond
    int cx = x + TILE / 2, cy = y + TILE / 2, r = TILE / 5;
    tft.fillTriangle(cx, cy - r, cx + r, cy, cx, cy + r, COL_GOAL);
    tft.fillTriangle(cx, cy - r, cx - r, cy, cx, cy + r, COL_GOAL);
  }
  if (cell == '$' || cell == '*') {
    // Box – brown square with highlight
    int pad = 3;
    tft.fillRect(x + pad, y + pad, TILE - pad * 2, TILE - pad * 2,
                 (cell == '*') ? COL_BOX_OK : COL_BOX);
    tft.drawRect(x + pad, y + pad, TILE - pad * 2, TILE - pad * 2, COL_TEXT);
  }
  if (cell == '@' || cell == '+') {
    // Player – blue circle
    int cx = x + TILE / 2, cy = y + TILE / 2;
    tft.fillCircle(cx, cy, TILE / 2 - 3, COL_PLAYER);
    tft.drawCircle(cx, cy, TILE / 2 - 3, COL_TEXT);
  }
}

static void redrawGrid() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      drawTile(r, c, grid[r][c]);
}

// ── HUD ───────────────────────────────────────────────────────────────────────
static void drawHUD() {
  tft.fillRect(0, CTRL_Y, SCREEN_W, CTRL_H, COL_BG);

  // Level / moves info
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(4, CTRL_Y + 4);
  tft.print("Lv ");
  tft.print(currentLevel + 1);
  tft.print("  Mv ");
  tft.print(moves);

  // Buttons: UNDO  RESTART  NEXT
  auto btn = [&](int x, int w, const char *label, uint16_t col) {
    tft.fillRoundRect(x + 2, CTRL_Y + 30, w - 4, 30, 4, col);
    tft.drawRoundRect(x + 2, CTRL_Y + 30, w - 4, 30, 4, COL_TEXT);
    tft.setTextColor(COL_BG, col);
    tft.setTextSize(2);
    int tx = x + (w / 2) - (int)strlen(label) * 6;
    tft.setCursor(tx, CTRL_Y + 37);
    tft.print(label);
  };

  btn(160, 100, "UNDO",    C(0x83, 0x76, 0x9c));
  btn(265, 110, "RESTART", C(0xff, 0xa3, 0x00));
  btn(380, 100, "NEXT",    C(0x00, 0x87, 0x51));
}

static void drawWin() {
  tft.fillRect(GRID_X + 40, GRID_Y + 88, COLS * TILE - 80, 64, COL_BG);
  tft.drawRect(GRID_X + 40, GRID_Y + 88, COLS * TILE - 80, 64, COL_GOAL);
  tft.setTextColor(COL_GOAL, COL_BG);
  tft.setTextSize(3);
  tft.setCursor(GRID_X + 68, GRID_Y + 96);
  tft.print("YOU WIN!");
  tft.setTextSize(2);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(GRID_X + 52, GRID_Y + 128);
  tft.print("Moves: ");
  tft.print(moves);
}

// ── Level loading ─────────────────────────────────────────────────────────────
static void loadLevel(int idx) {
  if (idx < 0) idx = 0;
  if (idx >= NUM_LEVELS) idx = NUM_LEVELS - 1;
  currentLevel = idx;
  moves = 0;
  won = false;
  undoTop = 0;
  playerRow = playerCol = 0;

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      char ch = (char)pgm_read_byte(&levels[idx][r][c]);
      grid[r][c] = ch;
      if (ch == '@' || ch == '+') {
        playerRow = r;
        playerCol = c;
      }
    }
    grid[r][COLS] = '\0';
  }

  tft.fillRect(0, GRID_Y, SCREEN_W, ROWS * TILE, COL_BG);
  redrawGrid();
  drawHUD();
}

// ── Win check ─────────────────────────────────────────────────────────────────
static bool checkWin() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (grid[r][c] == '$') return false;  // unsettled box
  return true;
}

// ── Move logic ────────────────────────────────────────────────────────────────
static void pushUndo() {
  if (undoTop >= UNDO_DEPTH) {
    // Shift stack down
    memmove(undoStack, undoStack + 1, sizeof(UndoFrame) * (UNDO_DEPTH - 1));
    undoTop = UNDO_DEPTH - 1;
  }
  undoStack[undoTop].pr = (int8_t)playerRow;
  undoStack[undoTop].pc = (int8_t)playerCol;
  memcpy(undoStack[undoTop].g, grid, sizeof(grid));
  undoTop++;
}

static void popUndo() {
  if (undoTop <= 0) return;
  undoTop--;
  playerRow = undoStack[undoTop].pr;
  playerCol = undoStack[undoTop].pc;
  memcpy(grid, undoStack[undoTop].g, sizeof(grid));
  won = false;
  moves = (moves > 0) ? moves - 1 : 0;
  redrawGrid();
  drawHUD();
}

// Returns true if cell is walkable floor/goal (not wall/box/out-of-bounds)
static bool isFloor(int r, int c) {
  if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return false;
  char ch = grid[r][c];
  return ch == ' ' || ch == '.' || ch == '@' || ch == '+';
}

static bool isBox(int r, int c) {
  if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return false;
  char ch = grid[r][c];
  return ch == '$' || ch == '*';
}

static bool isGoal(int r, int c) {
  if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return false;
  char ch = grid[r][c];
  return ch == '.' || ch == '+' || ch == '*';
}

static void tryMove(int dr, int dc) {
  if (won) return;
  int nr = playerRow + dr;
  int nc = playerCol + dc;

  if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) return;
  if (grid[nr][nc] == '#') return;

  bool movingBox = isBox(nr, nc);
  if (movingBox) {
    int br = nr + dr, bc = nc + dc;
    if (!isFloor(br, bc)) return;  // can't push box
    pushUndo();
    // Move box
    bool boxOnGoal = isGoal(nr, nc);
    bool destGoal  = isGoal(br, bc);
    grid[nr][nc]   = boxOnGoal ? '.' : ' ';
    grid[br][bc]   = destGoal  ? '*' : '$';
    drawTile(nr, nc, grid[nr][nc]);
    drawTile(br, bc, grid[br][bc]);
  } else {
    pushUndo();
  }

  // Move player
  bool srcGoal  = isGoal(playerRow, playerCol);
  bool destGoal = isGoal(nr, nc) && !movingBox ? true
                : (grid[nr][nc] == '.' || grid[nr][nc] == '+');
  // Re-check dest after box was possibly moved
  destGoal = (grid[nr][nc] == '.' || grid[nr][nc] == '+');

  grid[playerRow][playerCol] = srcGoal ? '.' : ' ';
  drawTile(playerRow, playerCol, grid[playerRow][playerCol]);

  playerRow = nr;
  playerCol = nc;
  grid[playerRow][playerCol] = destGoal ? '+' : '@';
  drawTile(playerRow, playerCol, grid[playerRow][playerCol]);

  moves++;
  drawHUD();

  if (checkWin()) {
    won = true;
    drawWin();
  }
}

// ── Touch input ───────────────────────────────────────────────────────────────
static void handleTouch() {
  uint16_t tx, ty;
  uint32_t now = millis();
  bool touched = tft.getTouch(&tx, &ty, 400);

  if (touched) {
    if (!touchWasDown) {
      touchWasDown = true;
      touchDownAt  = now;
      touchDownX   = tx;
      touchDownY   = ty;
    }
    // Held in button area – handled on release
    return;
  }

  if (!touchWasDown) return;
  touchWasDown = false;

  int dx = (int)tx - (int)touchDownX;
  int dy = (int)ty - (int)touchDownY;
  int adx = abs(dx), ady = abs(dy);

  // ── Button strip ──────────────────────────────────────────────────────────
  if (touchDownY >= CTRL_Y) {
    if (touchDownX >= 160 && touchDownX < 260) { popUndo(); return; }
    if (touchDownX >= 265 && touchDownX < 375) { loadLevel(currentLevel); return; }
    if (touchDownX >= 380) {
      if (currentLevel + 1 < NUM_LEVELS) loadLevel(currentLevel + 1);
      return;
    }
  }

  // ── Swipe on game area ────────────────────────────────────────────────────
  if (adx < SWIPE_MIN && ady < SWIPE_MIN) return;  // tap, ignore
  if (adx > ady) {
    tryMove(0, dx > 0 ? 1 : -1);
  } else {
    tryMove(dy > 0 ? 1 : -1, 0);
  }
}

// ── Arduino entry points ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Onboard LED off (GPIO 2 on most WROOM devkits)
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  initColors();
  loadTouchCalibration();
  loadLevel(0);
}

void loop() {
  handleTouch();
  delay(16);  // ~60 fps poll rate; no heavy rendering in loop
}
