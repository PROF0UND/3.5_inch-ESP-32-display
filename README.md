# 3.5-esp-32-display

Documentation for the 3.5 inch ESP 32 display with the SD card.

## Board:
- [Aliexpress link](https://www.aliexpress.us/item/3256808473949829.html?spm=a2g0o.order_list.order_list_main.17.622d1802tacMVU&gatewayAdapt=glo2usa)

 
  <img width="354" height="387" alt="image" src="https://github.com/user-attachments/assets/b9523601-6f25-4fe3-811b-ed98b42f3350" />


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
## Example Codes:
- The [example_codes](./example_codes) folder, you can find a list of example programs that can get you started with development on this board.
- [Bouncing ball](./example_codes/Bouncing_ball): Creates a red bouncing ball that goes around the screen.
- [SD_card_file_explorer](./example_codes/SD_card_file_explorer): Explore the files how have on the connected SD card!
