#pragma once

#include <stdint.h>

// Guition JC3248W535C (SKU JC3248W535C_I_Y, and the L_I_Y marking of the
// same module): ESP32-S3-WROOM-1 with 16 MB flash and 8 MB octal PSRAM, a
// 3.5 inch 320x480 AXS15231B on a quad SPI bus, the same chip's touch
// controller on its own I2C bus, and a PWM backlight. Turned on its side the
// glass is 480x320, the UI's own size, so nothing is enlarged. See panel.cpp
// for the turn.

#define BOARD_ID     "guition-jc3248w535c"
#define BOARD_MODEL  "Guition-JC3248W535C"

// Its own image and its own entry in version.json. An image for another
// board would start on this one and find nothing where it expects it.
#define BOARD_OTA_ASSET     "SpoolmanScale-" BOARD_ID ".bin"
#define BOARD_VERSION_NODE  BOARD_ID

// GPIO1 is a real PWM backlight. The AXS15231B draws a window whose edges
// fall on an 8 pixel block; a strip that starts between two of them lands
// on the wrong rows. 480 and 320 both divide by 8.
#define BOARD_BACKLIGHT_PWM     1
#define BOARD_PANEL_ALIGN_X     8
#define BOARD_PANEL_ALIGN_Y     8

// The touch controller answers a read sooner than 20 ms after the last with
// junk, so LVGL polls it every 25 ms instead of LV_INDEV_DEF_READ_PERIOD.
#define BOARD_TOUCH_READ_MS     25

// 8 MB of octal PSRAM, the same kind the list rows were measured on.
#define BOARD_LIST_ROWS_IN_PSRAM  1

// Holds the battery boost on when an I2C IP5306 answers. The display and the
// card do not depend on it: the touch bus is not the sensor bus, and the
// card's chip select is a GPIO of its own. See board.cpp.
void boardEarlyInit();

namespace hw_pins {

// The sensor bus, PN532 and NAU7802, on the I2C plug. It is brought out
// twice, a 1.25 mm JST and a 1.0 mm Qwiic, wired the same way:
// pin 1 GND, pin 2 3.3 V, pin 3 GPIO17, pin 4 GPIO18. Qwiic puts SDA on
// pin 3 and SCL on pin 4.
constexpr int8_t I2C_EXT_SDA = 17;
constexpr int8_t I2C_EXT_SCL = 18;

// Optional, on the 8 pin GPIO header, the pin labelled IO6. No unit of this
// board was ever wired the old way round, so the safe pin and the wire are
// the same pin: see boards/wt32_sc01_plus/board_config.h for why there are
// two. Left unconnected, driving it does nothing.
// GPIO5 on that header is the battery divider. Driving it fights the
// resistors. GPIO46 is a strapping pin.
constexpr int8_t PN532_RESET_WIRE = 6;
constexpr int8_t PN532_RESET_SAFE = 6;

// The library calls pinMode(INPUT) on this pin and never reads it. GPIO7 is
// the next pin on the same header, IO7, and an input costs nothing there.
constexpr int8_t PN532_IRQ_UNUSED = 7;

// The panel's own bus. Quad SPI, chip select on GPIO45, which is a strapping
// pin and is only read at reset: the board pulls it down, and the firmware
// takes it over after that.
constexpr int8_t LCD_CS  = 45;
constexpr int8_t LCD_SCK = 47;
constexpr int8_t LCD_D0  = 21;
constexpr int8_t LCD_D1  = 48;
constexpr int8_t LCD_D2  = 40;
constexpr int8_t LCD_D3  = 39;

// Active high. GPIO1 turns on the transistor that sinks the backlight LEDs.
constexpr int8_t LCD_BACKLIGHT = 1;

// AXS15231B touch, I2C address 0x3B. The schematic wires its interrupt to
// GPIO3. The controller has no reset of its own.
constexpr int8_t TOUCH_SDA  = 4;
constexpr int8_t TOUCH_SCL  = 8;
constexpr int8_t TOUCH_INT  = 3;
constexpr uint8_t TOUCH_ADDR = 0x3B;

// The microSD socket, on its own SPI bus. The panel takes the other one.
constexpr int8_t SD_CS   = 10;
constexpr int8_t SD_MOSI = 11;
constexpr int8_t SD_SCK  = 12;
constexpr int8_t SD_MISO = 13;

}
