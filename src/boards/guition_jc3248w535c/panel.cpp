#include "hardware/panel.h"

#include "app_config.h"
#include "boards/board.h"
#include "hardware/display.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_io_spi.h>
#include <esp_heap_caps.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <freertos/semphr.h>
#include <string.h>

// ============================================================
//  A 320x480 GLASS FOR A 480x320 PICTURE
//
//  The AXS15231B is portrait. Its memory-access register can mirror either
//  axis, and exchanging them makes the window commands address the wrong
//  edge, so the turn into landscape is done here, on the way to the glass.
//  The touch readings are turned back by the same rule.
//
//  The first boot of a real unit decides three knobs. They have not been
//  checked against this board: the values are the ones the published drivers
//  for it use.
//
//  LANDSCAPE_TURN  0 counter-clockwise (ESPHome's rotation 90), 1 clockwise.
//                  Upside down, picture and touch together: switch it.
//  WIRE_SWAPS_BYTES  LVGL stores a pixel low byte first. The panel reads the
//                  high byte first. Colours scrambled into noise: flip it.
//  MADCTL          0x36. 0x08 exchanges red and blue. 0x80 and 0x40 mirror
//                  the glass. Leave 0x20 alone: that is the axis swap.
//  INVERT          the panel's own colour inversion, command 0x21.
// ============================================================

static constexpr int LANDSCAPE_TURN = 0;
static constexpr bool WIRE_SWAPS_BYTES = true;
static constexpr uint8_t MADCTL = 0x00;
static constexpr bool INVERT = false;

static constexpr int PANEL_W = 320;
static constexpr int PANEL_H = 480;
static_assert(LANDSCAPE_TURN == 0 || LANDSCAPE_TURN == 1, "turn is 0 or 1");
static_assert(DISPLAY_W_PX == PANEL_H && DISPLAY_H_PX == PANEL_W,
              "the turned glass is not the UI's size");
static_assert(BOARD_PANEL_ALIGN_X == 8 && BOARD_PANEL_ALIGN_Y == 8,
              "panelWrite() relies on the 8 pixel blocks");

// Quad SPI write clock. 40 MHz is what the board's own examples run. A field
// of static, or a line that starts late, means going down.
static constexpr uint32_t LCD_PCLK_HZ = 40000000;
static constexpr int LCD_HOST = SPI2_HOST;

// One LVGL flush is at most the draw buffer, the full width by 20 lines
// (display.cpp, DRAW_BUF_LINES). The DMA buffer is that many pixels. A strip
// that ever grows past it is sent in pieces.
static constexpr int DMA_LINES = 20;
static constexpr int DMA_PX = DISPLAY_W_PX * DMA_LINES;

// Backlight PWM. 8 bit, so the saved level lands on the pin unchanged.
static constexpr uint32_t BL_HZ = 5000;
static constexpr uint8_t BL_BITS = 8;

static esp_lcd_panel_io_handle_t s_io = nullptr;
static uint16_t* s_dma = nullptr;
static SemaphoreHandle_t s_done = nullptr;
static bool s_ready = false;
static bool s_touch = false;
static bool s_pwm = false;

static bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t,
                                  esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t woke = pdFALSE;
  xSemaphoreGiveFromISR(s_done, &woke);
  return woke == pdTRUE;
}

// Command phase is one line, opcode 0x02 and the command in the next byte.
// Pixel phase is four lines, opcode 0x32. The panel IO sends the 32 bit word
// high byte first.
static bool sendCmd(uint8_t cmd, const void* data, size_t len) {
  const int word = (0x02 << 24) | ((int)cmd << 8);
  const esp_err_t err = esp_lcd_panel_io_tx_param(s_io, word, data, len);
  if (err == ESP_OK) return true;
  Serial.printf("Panel: command 0x%02x refused, error 0x%x\n", cmd, (unsigned)err);
  return false;
}

// The colour transfer is queued. The buffer has to stay put until the last
// chunk has gone out, which is what the callback reports.
static bool sendPixels(const void* px, size_t bytes) {
  const int word = (0x32 << 24) | (0x2C << 8);
  xSemaphoreTake(s_done, 0);
  const esp_err_t err = esp_lcd_panel_io_tx_color(s_io, word, px, bytes);
  if (err != ESP_OK) {
    Serial.printf("Panel: pixel write refused, error 0x%x\n", (unsigned)err);
    return false;
  }
  if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) == pdTRUE) return true;
  Serial.println("Panel: pixel write did not finish");
  return false;
}

// x, y, w, h are the glass's own, portrait, origin top left.
static bool writeNative(int x, int y, int w, int h, const uint16_t* px) {
  const uint16_t x1 = (uint16_t)x;
  const uint16_t x2 = (uint16_t)(x + w - 1);
  const uint16_t y1 = (uint16_t)y;
  const uint16_t y2 = (uint16_t)(y + h - 1);
  const uint8_t rows[4] = {
    (uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2
  };
  const uint8_t cols[4] = {
    (uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2
  };
  if (!sendCmd(0x2B, rows, 4)) return false;
  if (!sendCmd(0x2A, cols, 4)) return false;
  return sendPixels(px, (size_t)w * h * sizeof(uint16_t));
}

static uint16_t wirePixel(uint16_t c) {
  if (!WIRE_SWAPS_BYTES) return c;
  return (uint16_t)((c << 8) | (c >> 8));
}

// One UI strip, already inside the DMA buffer's budget, turned onto the glass.
static bool writeTurned(int x, int y, int w, int h, const uint16_t* src) {
  int x0, y0;
  if (LANDSCAPE_TURN == 0) {
    x0 = PANEL_W - y - h;
    y0 = x;
  } else {
    x0 = y;
    y0 = PANEL_H - x - w;
  }
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      const uint16_t c = wirePixel(src[j * w + i]);
      int dx, dy;
      if (LANDSCAPE_TURN == 0) {
        dx = (h - 1) - j;
        dy = i;
      } else {
        dx = j;
        dy = (w - 1) - i;
      }
      s_dma[dy * h + dx] = c;
    }
  }
  return writeNative(x0, y0, h, w, s_dma);
}

static bool fillBlack() {
  memset(s_dma, 0, (size_t)DMA_PX * sizeof(uint16_t));
  // Full width of the glass, and a height that divides it and the 8 pixel
  // block, and that fits in the DMA buffer.
  static constexpr int BAND = 24;
  static_assert(PANEL_W * BAND <= DMA_PX, "a black band does not fit");
  static_assert(PANEL_H % BAND == 0, "the bands do not cover the glass");
  for (int y = 0; y < PANEL_H; y += BAND) {
    if (!writeNative(0, y, PANEL_W, BAND, s_dma)) return false;
  }
  return true;
}

// The short sequence the maintained driver for this module sends: unlock the
// vendor page, the interface byte, lock it again. No software reset. The
// chip has no reset pin, and a reset command leaves it blank. Sleep-out,
// 16 bit pixels, the access register and display-on are the ordinary set
// around it.
static bool panelInit() {
  delay(120);
  if (!sendCmd(0x11, nullptr, 0)) return false;
  delay(120);

  const uint8_t unlock[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5};
  const uint8_t iface[1] = {0x33};
  const uint8_t lock[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  const uint8_t pix[1] = {0x55};
  const uint8_t mad[1] = {MADCTL};
  if (!sendCmd(0xBB, unlock, sizeof unlock)) return false;
  if (!sendCmd(0xC1, iface, sizeof iface)) return false;
  if (!sendCmd(0xBB, lock, sizeof lock)) return false;
  if (!sendCmd(0x3A, pix, sizeof pix)) return false;
  if (!sendCmd(0x36, mad, sizeof mad)) return false;
  if (!sendCmd(INVERT ? 0x21 : 0x20, nullptr, 0)) return false;
  if (!sendCmd(0x13, nullptr, 0)) return false;
  if (!sendCmd(0x29, nullptr, 0)) return false;
  delay(20);
  return true;
}

static void backlightOffPin() {
  if (s_pwm) {
    ledcDetach(hw_pins::LCD_BACKLIGHT);
    s_pwm = false;
  }
  pinMode(hw_pins::LCD_BACKLIGHT, OUTPUT);
  digitalWrite(hw_pins::LCD_BACKLIGHT, LOW);
}

static void backlightOnPin(uint8_t brightness) {
  ledcAttach(hw_pins::LCD_BACKLIGHT, BL_HZ, BL_BITS);
  s_pwm = true;
  ledcWrite(hw_pins::LCD_BACKLIGHT, brightness);
}

bool panelBegin() {
  s_ready = false;
  s_touch = false;
  // Undo what panelPrepareDeepSleep() set up. Both are no-ops on a cold boot.
  gpio_hold_dis((gpio_num_t)hw_pins::LCD_BACKLIGHT);
  gpio_deep_sleep_hold_dis();
  rtc_gpio_deinit((gpio_num_t)hw_pins::TOUCH_INT);
  backlightOffPin();

  if (!s_done) s_done = xSemaphoreCreateBinary();
  if (!s_dma) {
    s_dma = (uint16_t*)heap_caps_malloc((size_t)DMA_PX * sizeof(uint16_t),
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  }
  if (!s_done || !s_dma) {
    Serial.println("Panel: no DMA buffer");
    return false;
  }

  spi_bus_config_t bus = {};
  bus.data0_io_num = hw_pins::LCD_D0;
  bus.data1_io_num = hw_pins::LCD_D1;
  bus.sclk_io_num = hw_pins::LCD_SCK;
  bus.data2_io_num = hw_pins::LCD_D2;
  bus.data3_io_num = hw_pins::LCD_D3;
  bus.data4_io_num = -1;
  bus.data5_io_num = -1;
  bus.data6_io_num = -1;
  bus.data7_io_num = -1;
  bus.max_transfer_sz = DMA_PX * (int)sizeof(uint16_t);

  esp_err_t err = spi_bus_initialize((spi_host_device_t)LCD_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    Serial.printf("Panel: the QSPI bus refused, error 0x%x\n", (unsigned)err);
    return false;
  }

  esp_lcd_panel_io_spi_config_t io = {};
  io.cs_gpio_num = hw_pins::LCD_CS;
  io.dc_gpio_num = -1;
  io.spi_mode = 0;
  io.pclk_hz = LCD_PCLK_HZ;
  io.trans_queue_depth = 1;
  io.on_color_trans_done = onColorDone;
  io.lcd_cmd_bits = 32;
  io.lcd_param_bits = 8;
  io.flags.quad_mode = 1;
  err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &s_io);
  if (err != ESP_OK || !s_io) {
    Serial.printf("Panel: the panel IO refused, error 0x%x\n", (unsigned)err);
    s_io = nullptr;
    return false;
  }
  if (!panelInit() || !fillBlack()) {
    Serial.println("Panel: init did not finish");
    return false;
  }

  pinMode(hw_pins::TOUCH_INT, INPUT_PULLUP);
  s_touch = Wire.begin(hw_pins::TOUCH_SDA, hw_pins::TOUCH_SCL, 400000);
  Wire.setTimeOut(10);

  backlightOnPin(BRIGHT_NORMAL_DEFAULT);
  s_ready = true;
  Serial.printf("Panel: Guition JC3248W535C, turn %d\n", LANDSCAPE_TURN);
  return true;
}

void panelWrite(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) {
  if (!s_ready || w <= 0 || h <= 0) return;
  int done = 0;
  while (done < h) {
    int rows = h - done;
    while ((int64_t)w * rows > DMA_PX) rows--;
    rows = rows / BOARD_PANEL_ALIGN_Y * BOARD_PANEL_ALIGN_Y;
    if (rows < BOARD_PANEL_ALIGN_Y) {
      Serial.println("Panel: strip does not fit the DMA buffer");
      return;
    }
    if (!writeTurned(x, y + done, w, rows, px + (size_t)done * w)) return;
    done += rows;
  }
}

// One finger. The chip can be asked for more, and the maintained driver for
// this module reads one. The gesture hook still gets a count of 0 or 1.
//
// This controller never sends a clean "no finger". After a lift it sends a
// report the reference drivers throw away: eight 0xFF bytes, or a frame whose
// event field (bits 7..6 of the X byte) is the lift. The next tap is that
// same slot, moved. Kept as one finger, the move is a slide across whatever
// sits between the two keys. A lift drops the finger at once. When a lift
// was missed, the new finger still comes with a touch-down event (0) while
// one is held: that is a release on this read and a new press on the next,
// which is the soonest LVGL can see them as two touches. Distance can't
// tell them apart: a quick flick through a list moves 60+ px between reads,
// and splitting it ended the scroll and could tap the row under the finger.
//
// A read sooner than 20 ms after the last comes back invalid, so the bus is
// read at 20 ms and a call in between repeats the answer in hand. LVGL polls
// this board every 25 ms (BOARD_TOUCH_READ_MS) so that never happens: a
// repeated point is a zero move to LVGL, which cuts a flick's coast short.
static constexpr uint32_t TOUCH_GAP_MS = 20;
static constexpr uint8_t TOUCH_EVENT_DOWN = 0;
static constexpr uint8_t TOUCH_EVENT_LIFT = 1;

static uint32_t s_touch_ms = 0;
static bool s_touch_down = false;
static bool s_touch_arm = false;
static PanelTouchPoint s_touch_pt = {};
static PanelTouchPoint s_touch_next = {};

static bool touchBytes(const uint8_t* d, uint8_t v) {
  for (int i = 0; i < 8; i++) if (d[i] != v) return false;
  return true;
}

// What the reference drivers refuse, plus the two shapes a lift takes here.
static bool touchLifted(const uint8_t* d) {
  if (touchBytes(d, 0xFF)) return true;
  if (d[0] != 0 || d[1] == 0) return true;
  if (touchBytes(d, d[0])) return true;
  if ((d[2] >> 6) == TOUCH_EVENT_LIFT) return true;
  return false;
}

static uint8_t touchRelease() {
  s_touch_down = false;
  s_touch_arm = false;
  return 0;
}

uint8_t panelTouchRead(PanelTouchPoint* points, uint8_t max_points) {
  if (!s_touch || max_points == 0) return 0;

  // The release for a jump was handed up on the previous call.
  if (s_touch_arm) {
    s_touch_arm = false;
    s_touch_down = true;
    s_touch_pt = s_touch_next;
    points[0] = s_touch_pt;
    return 1;
  }

  const uint32_t now = millis();
  if ((uint32_t)(now - s_touch_ms) < TOUCH_GAP_MS) {
    if (s_touch_down) points[0] = s_touch_pt;
    return s_touch_down ? 1 : 0;
  }
  s_touch_ms = now;

  // 11 bytes on the wire: the 8 byte read command and three trailing zeros.
  static constexpr uint8_t READ_CMD[11] = {
    0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00
  };
  Wire.beginTransmission(hw_pins::TOUCH_ADDR);
  Wire.write(READ_CMD, sizeof READ_CMD);
  uint8_t data[8];
  const bool got = Wire.endTransmission() == 0
                   && Wire.requestFrom((int)hw_pins::TOUCH_ADDR, 8) == 8;
  if (!got) {
    if (s_touch_down) points[0] = s_touch_pt;
    return s_touch_down ? 1 : 0;
  }
  for (int i = 0; i < 8; i++) data[i] = (uint8_t)Wire.read();
  if (touchLifted(data)) return touchRelease();

  const int raw_x = ((data[2] & 0x0F) << 8) | data[3];
  const int raw_y = ((data[4] & 0x0F) << 8) | data[5];
  if (raw_x >= PANEL_W || raw_y >= PANEL_H) return touchRelease();

  int ux, uy;
  if (LANDSCAPE_TURN == 0) {
    ux = raw_y;
    uy = (PANEL_W - 1) - raw_x;
  } else {
    ux = (PANEL_H - 1) - raw_y;
    uy = raw_x;
  }
  if (ux < 0 || uy < 0 || ux >= DISPLAY_W_PX || uy >= DISPLAY_H_PX) return touchRelease();

  if (s_touch_down) {
    if ((data[2] >> 6) == TOUCH_EVENT_DOWN) {
      s_touch_arm = true;
      s_touch_next.x = (int16_t)ux;
      s_touch_next.y = (int16_t)uy;
      s_touch_down = false;
      return 0;
    }
  }

  s_touch_down = true;
  s_touch_pt.x = (int16_t)ux;
  s_touch_pt.y = (int16_t)uy;
  points[0] = s_touch_pt;
  return 1;
}

void panelSetBrightness(uint8_t brightness) {
  if (s_pwm) ledcWrite(hw_pins::LCD_BACKLIGHT, brightness);
}

void panelBacklightOff() {
  backlightOffPin();
}

void panelBacklightOn(uint8_t brightness) {
  backlightOnPin(brightness);
}

// The digital pull-up on the interrupt dies in deep sleep. The RTC pull-up
// holds the open-drain line, and the wake is armed only once the line is
// actually high: a line that is already low would wake the chip at once.
// GPIO3 is where the schematic puts the interrupt. A unit whose line is not
// fitted stays asleep, and the reset button brings it back.
void panelPrepareDeepSleep() {
  backlightOffPin();
  const gpio_num_t bl = (gpio_num_t)hw_pins::LCD_BACKLIGHT;
  gpio_set_level(bl, 0);
  gpio_hold_en(bl);
  gpio_deep_sleep_hold_en();

  const gpio_num_t pin = (gpio_num_t)hw_pins::TOUCH_INT;
  rtc_gpio_init(pin);
  rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis(pin);
  rtc_gpio_pullup_en(pin);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  int level = 0;
  for (int i = 0; i < 20; i++) {
    delay(10);
    level = gpio_get_level(pin);
    if (level != 0) break;
  }
  Serial.printf("Deep sleep: touch INT %d\n", level);
  if (level == 0) {
    Serial.println("Deep sleep: touch wake not armed");
    return;
  }
  esp_sleep_enable_ext0_wakeup(pin, 0);
}
