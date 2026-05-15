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
4. [SD_Web_File_Server](./example_codes/SD_Web_File_Server): Hosts a small browser file manager for the SD card over Wi-Fi.
5. [Image_Display](./example_codes/Image_Display/image_display): Displays a BMP image on the screen.
   1. Use [This converter](https://image.online-convert.com/convert-to-bmp) to turn your image into a 480x320 BMP image.
   2. Save it as "image.bmp" on your SD card. 

### WiFi_Time_Display config
- Copy `.env.example` to `.env` for local Wi-Fi values. `.env` is ignored by Git.
- Arduino IDE does not automatically load `.env` files. To use local values at compile time, copy `example_codes/WiFi_Time_Display/wifi_config.h.example` to `example_codes/WiFi_Time_Display/wifi_config.h` and edit that file.
- `wifi_config.h` is ignored by Git, so private hotspot passwords stay out of GitHub.

### SD_Web_File_Server
- Flash `example_codes/SD_Web_File_Server/SD_Web_File_Server.ino`.
- The sketch uses the same SD card setup as the other examples: `SD.begin(5)`.
- By default it connects to `UMBC Visitor` with no password. To use a different network, copy `example_codes/SD_Web_File_Server/wifi_config.h.example` to `example_codes/SD_Web_File_Server/wifi_config.h` and edit it.
- The browser login defaults to username `admin` and password `esp32`. Change `FILE_SERVER_USERNAME` and `FILE_SERVER_PASSWORD` in `wifi_config.h` before using it around other people.
- After Wi-Fi connects, the display shows a URL like `http://10.x.x.x`. Open that URL from a device on the same Wi-Fi network to browse, download, upload, create folders, and delete SD-card files.
- If `UMBC Visitor` blocks device-to-device access or requires a captive portal/MAC approval, the ESP32 may connect but the URL may not be reachable from your laptop or phone. In that case, use a phone hotspot or ask UMBC DoIT whether the ESP32 MAC address can be approved.

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

