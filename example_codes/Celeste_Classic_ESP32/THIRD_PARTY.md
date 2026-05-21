# Third-party Attribution

This example includes files from `ccleste`, an archived C source port of the original PICO-8 Celeste Classic.

- Upstream repository: https://github.com/lemon32767/ccleste
- Vendored files: `celeste.c`, `celeste.h`, `tilemap.h`, `data/gfx.bmp`, `data/font.bmp`
- Original Celeste Classic credits, per upstream README: Maddy Thorson and Noel Berry

The ESP32/TFT_eSPI frontend in `Celeste_Classic_ESP32.ino` is local integration code for this workspace. Audio files from upstream are intentionally not vendored because this first port is silent.
