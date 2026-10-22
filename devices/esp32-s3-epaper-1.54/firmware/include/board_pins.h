// Pins of the Waveshare ESP32-S3-ePaper-1.54. Taken from Waveshare's demo code
// (github.com/waveshareteam/ESP32-S3-ePaper-1.54, 02_Example/Arduino/*/user_config.h) and
// cross-checked against the factory firmware's boot log on the real board (I2C 47/48,
// SHTC3 answered with ID 0887). The panel uses SSD1681 commands -> GxEPD2_154_D67.
// Not yet checked on the panel itself: power polarity under GxEPD2, and the PWR button.
#pragma once

#define PIN_EPD_SCK 12
#define PIN_EPD_MOSI 13
#define PIN_EPD_CS 11
#define PIN_EPD_DC 10
#define PIN_EPD_RST 9
#define PIN_EPD_BUSY 8
#define PIN_I2C_SDA 47   // SHTC3 (0x70), PCF85063 (0x51), ES8311 (0x18)
#define PIN_I2C_SCL 48

#define PIN_BAT_ADC 4    // battery voltage through a 1:2 divider (Waveshare ADC example: mV x 2)

// Buttons. BOOT is GPIO0 on ESP32-S3 boards. Both are read as active low. Only RTC
// pins (GPIO 0..21) can wake the chip from deep sleep.
#define PIN_BTN_BOOT 0
#define PIN_BTN_PWR 18   // BAT_KEY: the PWR button, active low (Waveshare wakes from sleep on it)

// Optional. -1 = this board has no such pin.
#define PIN_PWR_LATCH 17     // power-latch: drive to PWR_LATCH_ON and hold, or the board switches off on battery
#define PWR_LATCH_ON HIGH
#define PIN_EPD_PWR 6        // panel supply switch (EPD3V3_EN), ACTIVE LOW in Waveshare's demo
#define EPD_PWR_ON LOW
#define PIN_AMP_EN 42        // audio supply (codec + amplifier), ACTIVE LOW in Waveshare's demo: HIGH = off.
                             // PA_CTRL (GPIO46, a strapping pin) is left alone.
#define AMP_OFF HIGH
#define PIN_PA_EN 46         // speaker amplifier enable, active high; low (off) except while a clip plays
