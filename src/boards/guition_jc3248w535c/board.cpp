#include "boards/board.h"

#include <Arduino.h>
#include <Wire.h>
#include <stdarg.h>
#include <stdio.h>

#include "hardware/sd_logger.h"

// The IP5306 leaves its 5 V boost off when a battery is connected and USB is
// not. A short press of the battery button turns the boost on, and after that
// the ESP32's own current keeps it on. Register 0x00 bit 1 is the other way:
// standby, in which the boost stays on without the button.
//
// The register is RAM inside the chip. It is lost when the battery is
// unplugged, because that removes the chip's only supply. A battery connected
// to a chip that is fully off still needs the button, or USB, once, so this
// can run. From then until the battery is unplugged, the boost stays up,
// including across USB being removed and across a reset.
//
// Only the I2C variant answers at 0x75. The plain eSOP8 part uses those pins
// for the charge LEDs and will not ack. The module does not document a private
// pair for the power chip, so both buses this board already has are asked.
// The touch controller is 0x3B on the first; a probe of 0x75 does not address
// it. The sensor bus is the I2C connector.

extern TwoWire I2C_EXT;

static constexpr uint8_t IP5306_ADDR = 0x75;
static constexpr uint8_t IP5306_REG_SYS = 0x00;
static constexpr uint8_t IP5306_STANDBY = 0x02;
static constexpr uint32_t IP5306_HZ = 100000;

enum Hold { HOLD_ABSENT, HOLD_FAILED, HOLD_ON };

static void boardLog(const char *msg) {
  Serial.println(msg);
  logSD(msg);
}

static void boardLogf(const char *fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  boardLog(buf);
}

static bool readSys(TwoWire &wire, uint8_t *val) {
  wire.beginTransmission(IP5306_ADDR);
  wire.write(IP5306_REG_SYS);
  // Repeated start. A stop between the register and the read is a new
  // transaction, and this chip often NACKs that.
  if (wire.endTransmission(false) != 0) return false;
  if (wire.requestFrom((int)IP5306_ADDR, 1) != 1) return false;
  *val = (uint8_t)wire.read();
  return true;
}

static bool writeSys(TwoWire &wire, uint8_t val) {
  wire.beginTransmission(IP5306_ADDR);
  wire.write(IP5306_REG_SYS);
  wire.write(val);
  return wire.endTransmission() == 0;
}

// Bit 1 only. Anything else in the register (charger, load detect, the button)
// stays as the chip had it. A read of 0x00 is not written back: that byte is
// what a dead read looks like, and storing it would clear the boost bit.
static Hold holdOn(TwoWire &wire, const char *bus) {
  wire.beginTransmission(IP5306_ADDR);
  if (wire.endTransmission() != 0) return HOLD_ABSENT;

  uint8_t val = 0;
  if (!readSys(wire, &val)) {
    boardLogf("Board: IP5306 on the %s acked at 0x75, register 0x00 did not read",
              bus);
    return HOLD_FAILED;
  }
  if (val == 0x00) {
    boardLogf("Board: IP5306 on the %s, register 0x00 read 0x00, left unchanged",
              bus);
    return HOLD_FAILED;
  }
  if ((val & IP5306_STANDBY) != 0) {
    boardLogf("Board: IP5306 on the %s, boost already held on (reg 0x00 0x%02X)",
              bus, val);
    return HOLD_ON;
  }

  const uint8_t next = val | IP5306_STANDBY;
  uint8_t back = 0;
  bool kept = false;
  for (int attempt = 0; attempt < 2 && !kept; attempt++) {
    kept = writeSys(wire, next) && readSys(wire, &back)
           && (back & IP5306_STANDBY) != 0;
  }
  if (!kept) {
    boardLogf("Board: IP5306 on the %s, register 0x00 did not keep bit 1 (0x%02X)",
              bus, back);
    return HOLD_FAILED;
  }
  boardLogf("Board: IP5306 on the %s, boost held on (reg 0x00 0x%02X -> 0x%02X)",
            bus, val, back);
  return HOLD_ON;
}

// begin() a second time does not change the pins or the clock, so the bus is
// closed again. The touch driver and the sensor bus start their own way later.
static Hold holdBoost(TwoWire &wire, int sda, int scl, const char *bus) {
  if (!wire.begin(sda, scl, IP5306_HZ)) {
    boardLogf("Board: IP5306 %s did not start", bus);
    return HOLD_FAILED;
  }
  const Hold rc = holdOn(wire, bus);
  wire.end();
  return rc;
}

void boardEarlyInit() {
  const Hold touch = holdBoost(Wire, hw_pins::TOUCH_SDA, hw_pins::TOUCH_SCL,
                               "touch bus");
  if (touch == HOLD_ON) return;
  const Hold sensor = holdBoost(I2C_EXT, hw_pins::I2C_EXT_SDA,
                                hw_pins::I2C_EXT_SCL, "sensor bus");
  if (sensor == HOLD_ON) return;
  if (touch == HOLD_FAILED || sensor == HOLD_FAILED) return;
  boardLog("Board: IP5306 did not answer at 0x75. "
           "The battery button is still required.");
}
