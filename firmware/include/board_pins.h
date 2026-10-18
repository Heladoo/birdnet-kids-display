// Pins of the Waveshare ESP32-S3-ePaper-1.54. NOT CONFIRMED: the plan was written
// without the board. Fill the -1 values below from Waveshare's demo code (phase 0,
// PLAN.md section 2); the build stops with a static_assert until the required pins
// (EPD SPI, CS, DC, RST, BUSY, I2C) are set. Pins marked optional may stay -1.
#pragma once

#ifdef PINS_PLACEHOLDER
// Dummy numbers so the code can be compile-checked. Wrong for the real board.
#define PIN_EPD_SCK 12
#define PIN_EPD_MOSI 11
#define PIN_EPD_CS 10
#define PIN_EPD_DC 9
#define PIN_EPD_RST 46
#define PIN_EPD_BUSY 8
#define PIN_I2C_SDA 47
#define PIN_I2C_SCL 48
#else
#define PIN_EPD_SCK -1
#define PIN_EPD_MOSI -1
#define PIN_EPD_CS -1
#define PIN_EPD_DC -1
#define PIN_EPD_RST -1
#define PIN_EPD_BUSY -1
#define PIN_I2C_SDA -1   // SHTC3 (0x70) and PCF85063 (0x51)
#define PIN_I2C_SCL -1
#endif

// Buttons. BOOT is GPIO0 on ESP32-S3 boards. Both are read as active low. Only RTC
// pins (GPIO 0..21) can wake the chip from deep sleep.
#define PIN_BTN_BOOT 0
#define PIN_BTN_PWR -1   // optional: if PWR is not a readable GPIO, hold BOOT for 1.5 s to refresh

// Optional. -1 = this board has no such pin.
#define PIN_PWR_LATCH -1     // power-latch: drive to PWR_LATCH_ON and hold, or the board switches off on battery
#define PWR_LATCH_ON HIGH
#define PIN_EPD_PWR -1       // panel supply switch
#define EPD_PWR_ON HIGH
#define PIN_AMP_EN -1        // speaker amplifier enable: kept OFF (speakers are not used for now)
#define AMP_OFF LOW
