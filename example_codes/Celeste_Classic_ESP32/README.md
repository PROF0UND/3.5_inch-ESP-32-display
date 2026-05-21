# Celeste Classic ESP32

Touch-controlled, silent Celeste Classic port for this 3.5 inch ESP32 ILI9488 display.

## Setup

1. Install the Arduino libraries used by the other examples: `TFT_eSPI`.
2. Copy this folder's `data` files to the SD card:
   - `data/gfx.bmp` -> `/celeste/gfx.bmp`
   - `data/font.bmp` -> `/celeste/font.bmp`
3. Flash `Celeste_Classic_ESP32.ino`.
4. Complete touch calibration on first boot. Calibration is saved in SPIFFS as `/celesteTouchCal`.

## Controls

The bottom 64 pixels are reserved for touch controls.

- Left side: left, up, down, right
- Right side: jump and dash
- Center: tap to pause/resume, hold for about one second to reset

The XPT2046 touch panel is single-touch, so the sketch latches the last direction briefly. To dash right, for example, tap/hold right and then tap dash.

## Notes

- Audio is intentionally disabled for v1.
- The game view renders at the original 128x128 resolution and is scaled to 256x256.
- External buttons would make the game much easier, but this example keeps v1 touch-only as requested.
- If the screen reports missing assets, confirm the SD card contains `/celeste/gfx.bmp` and `/celeste/font.bmp`.

## Third-party core

This example vendors `celeste.c`, `celeste.h`, `tilemap.h`, `data/gfx.bmp`, and `data/font.bmp` from `ccleste`, a C source port of Celeste Classic:

https://github.com/lemon32767/ccleste

See `THIRD_PARTY.md` for attribution notes.
