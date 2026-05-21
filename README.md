# 3.5-esp-32-display

Documentation for the 3.5 inch ESP 32 display with the SD card.

## Board:
- [Aliexpress link](https://www.aliexpress.us/item/3256808473949829.html?spm=a2g0o.order_list.order_list_main.17.622d1802tacMVU&gatewayAdapt=glo2usa)

 
  <img width="354" height="387" alt="image" src="https://github.com/user-attachments/assets/b9523601-6f25-4fe3-811b-ed98b42f3350" />

## Example Codes:
The [example_codes](./example_codes) folder, you can find a list of example programs that can get you started with development on this board.
1. [Bouncing ball](./example_codes/Bouncing_ball): Creates a red bouncing ball that goes around the screen.
2. [SD_card_file_explorer](./example_codes/SD_card_file_explorer): Explore the files how have on the connected SD card!
3. [WiFi_Time_Display](./example_codes/WiFi_Time_Display): Connects to Wi-Fi, syncs time with NTP, and displays a live clock on the screen.
4. [WiFi_Time_Weather_Display](./example_codes/WiFi_Time_Weather_Display): Connects to Wi-Fi, syncs time with NTP, and displays a live clock plus current weather from Open-Meteo.
5. [SD_Web_File_Server](./example_codes/SD_Web_File_Server): Hosts a small browser file manager for the SD card over Wi-Fi.
6. [Image_Display](./example_codes/Image_Display/image_display): Displays a BMP image on the screen.
   1. Use [This converter](https://image.online-convert.com/convert-to-bmp) to turn your image into a 480x320 BMP image.
   2. Save it as "image.bmp" on your SD card. 
7. [Spotify_Now_Playing_Display](./example_codes/Spotify_Now_Playing_Display): Connects to Wi-Fi, reads your Spotify currently playing song, and displays album art plus track details.

### WiFi_Time_Display config
- Copy `.env.example` to `.env` for local Wi-Fi values. `.env` is ignored by Git.
- Arduino IDE does not automatically load `.env` files. To use local values at compile time, copy `example_codes/WiFi_Time_Display/wifi_config.h.example` to `example_codes/WiFi_Time_Display/wifi_config.h` and edit that file.
- `wifi_config.h` is ignored by Git, so private hotspot passwords stay out of GitHub.

### WiFi_Time_Weather_Display config
- Flash `example_codes/WiFi_Time_Weather_Display/WiFi_Time_Weather_Display.ino`.
- The sketch uses Open-Meteo for current weather, so no API key is needed.
- To set your weather location, copy `example_codes/WiFi_Time_Weather_Display/wifi_config.h.example` to `example_codes/WiFi_Time_Weather_Display/wifi_config.h` and edit `WEATHER_LATITUDE`, `WEATHER_LONGITUDE`, and `WEATHER_LOCATION_NAME`. The example uses placeholder coordinates so your location is not committed.
- It refreshes the clock every second and weather every 10 minutes.
- If the weather stays unavailable on `UMBC Visitor`, the ESP32 may be behind a captive portal or need MAC approval for internet access.

### SD_Web_File_Server
- Flash `example_codes/SD_Web_File_Server/SD_Web_File_Server.ino`.
- The sketch uses the same SD card setup as the other examples: `SD.begin(5)`.
- By default it connects to `UMBC Visitor` with no password. To use a different network, copy `example_codes/SD_Web_File_Server/wifi_config.h.example` to `example_codes/SD_Web_File_Server/wifi_config.h` and edit it.
- The browser login defaults to username `admin` and password `esp32`. Change `FILE_SERVER_USERNAME` and `FILE_SERVER_PASSWORD` in `wifi_config.h` before using it around other people.
- After Wi-Fi connects, the display shows a URL like `http://10.x.x.x`. Open that URL from a device on the same Wi-Fi network to browse, download, upload, create folders, and delete SD-card files.
- If `UMBC Visitor` blocks device-to-device access or requires a captive portal/MAC approval, the ESP32 may connect but the URL may not be reachable from your laptop or phone. In that case, use a phone hotspot or ask UMBC DoIT whether the ESP32 MAC address can be approved.

### Spotify_Now_Playing_Display config
- Install Arduino libraries: `TFT_eSPI`, `ArduinoJson`, and `TJpg_Decoder`.
- In the Spotify Developer Dashboard, create an app and add this redirect URI exactly: `http://127.0.0.1:8080/callback`.
- Run `python example_codes/Spotify_Now_Playing_Display/tools/spotify_pkce_helper.py YOUR_SPOTIFY_CLIENT_ID` and log in with Spotify.
- Copy `example_codes/Spotify_Now_Playing_Display/spotify_config.h.example` to `example_codes/Spotify_Now_Playing_Display/spotify_config.h`.
- Paste the generated `SPOTIFY_CLIENT_ID` and `SPOTIFY_REFRESH_TOKEN` into `spotify_config.h`, then set your Wi-Fi values.
- Flash `example_codes/Spotify_Now_Playing_Display/Spotify_Now_Playing_Display.ino`.
- The sketch uses `SD.begin(5)` to cache the current album art at `/spotify_art.jpg`; if SD init fails, the song text still works but album art shows a placeholder.
- Spotify may rotate refresh tokens. The sketch saves any rotated refresh token in ESP32 flash so it survives restarts. If you need to force a new token from `spotify_config.h`, set `SPOTIFY_RESET_SAVED_REFRESH_TOKEN` to `true`, upload once, then set it back to `false`.
- This example is for a personal display project. It reads currently playing Spotify metadata over Wi-Fi and does not use Bluetooth audio or playback controls.

## Setup:
### Library installation:
- On Arduino IDE, install the `TFT_eSPI" library.
### Driver Used:
- The Board uses the "ILI9488 Driver".
- You need to edit the `User_Setup.h` to configure the library to this board.
- Do this by either uncommenting the following lines or just copy pasting this text on the file.
### User_setup.h:
- Use these settings in the `User_Setup.h` file of the `TFT_eSPI` library:
```
#define ILI9488_DRIVER

#define TFT_WIDTH  480
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1

#define TFT_BL    27
#define TFT_BACKLIGHT_ON HIGH

#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_OFF

#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY 2500000

// Optional: Touch support
#define TOUCH_CS   33
#define TOUCH_CLK  14
#define TOUCH_MOSI 13
#define TOUCH_MISO 12
#define TOUCH_IRQ  36

// Load fonts
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
```

