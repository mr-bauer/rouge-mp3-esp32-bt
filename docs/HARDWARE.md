# Hardware Reference

Full GPIO map, component list, and wiring guide for the Rouge MP3 Player.

---

## Component List

| Component | Part | Notes |
|-----------|------|-------|
| Microcontroller | [Adafruit Feather ESP32 V2](https://www.adafruit.com/product/5400) | ESP32-PICO-MINI-02, 2MB PSRAM, USB-C, built-in LiPo charger |
| Display | ST7789 240×240 TFT | 1.3" (e.g. Adafruit #4313) or 1.54" SPI module; must be ST7789 driver |
| Storage | microSD card | FAT32 formatted, ≤32GB recommended for compatibility |
| Battery | 3.7V LiPo | Any capacity; JST-PH 2-pin connector |
| Haptic driver | [Adafruit DRV2605L breakout](https://www.adafruit.com/product/2305) | I2C; includes ERM motor or use your own |
| Haptic motor | ERM vibration motor | Included with DRV2605L breakout, or substitute LRA |
| Rotary encoder | PEC11R or similar | Quadrature (two-channel); push-button not required |
| Push buttons (×4) | Momentary tactile switches | SPST, NO; any form factor |
| Pull-up resistors (×4) | 10kΩ ¼W | One per ADC button pin (GPIOs 34, 36, 37, 39) |

---

## GPIO Map

| GPIO | Pin Label | Direction | Function | Notes |
|------|-----------|-----------|----------|-------|
| 2 | NEOPIXEL_I2C_POWER | OUT | I2C power enable | Drive HIGH before using I2C; also enables NeoPixel power rail |
| 4 | BTN_CENTER | IN | Center button | Uses internal pull-up — no external resistor needed |
| 7 | TFT_BL | OUT | Backlight PWM | 8-bit, 5 kHz; managed automatically by LovyanGFX |
| 13 | TFT_MOSI | SPI (HSPI) | Display data out | 60 MHz |
| 14 | TFT_SCLK | SPI (HSPI) | Display clock | |
| 15 | TFT_CS | SPI (HSPI) | Display chip select | |
| 20 | I2C_SCL | I2C | DRV2605L clock | |
| 22 | I2C_SDA | I2C | DRV2605L data | |
| 25 | ENCODER_PIN_B | IN | Encoder channel B | Internal pull-up; interrupt on CHANGE |
| 26 | ENCODER_PIN_A | IN | Encoder channel A | Internal pull-up; interrupt on CHANGE |
| 27 | TFT_DC | SPI (HSPI) | Display data/command | |
| 32 | SD_CS | SPI (VSPI) | SD card chip select | Connected via Feather V2 built-in SD header |
| 33 | TFT_RST | OUT | Display reset | Active LOW |
| 34 | BTN_RIGHT | IN | Next track button | ADC-only pin — **needs 10kΩ external pull-up to 3.3V** |
| 35 | BATTERY_PIN | ADC | Battery voltage sense | A13; read-only ADC; 200kΩ+200kΩ divider built into Feather V2 |
| 36 | BTN_TOP | IN | Back/Menu button | ADC-only pin — **needs 10kΩ external pull-up to 3.3V** |
| 37 | BTN_BOTTOM | IN | Play/Pause button | ADC-only pin — **needs 10kΩ external pull-up to 3.3V** |
| 39 | BTN_LEFT | IN | Previous track button | ADC-only pin — **needs 10kΩ external pull-up to 3.3V** |

---

## Wiring Notes

### External Pull-ups — ADC Button Pins (REQUIRED)

GPIOs 34, 36, 37, and 39 are input-only ADC pins on the ESP32. They have **no internal pull-up resistors**. Each button on these pins needs an external 10kΩ resistor from the pin to 3.3V.

```
3.3V ──── 10kΩ ──── GPIO (34/36/37/39)
                         │
                       Button
                         │
                        GND
```

The Center button on GPIO 4 is a standard GPIO with an internal pull-up enabled in firmware — no external resistor is needed.

### SPI Bus — Display (HSPI)

The display uses the ESP32's secondary SPI peripheral (HSPI), **not VSPI**. VSPI is reserved for the SD card via the Feather V2's built-in header.

| HSPI Signal | GPIO | Display Pin |
|-------------|------|-------------|
| MOSI | 13 | SDA / DIN |
| SCLK | 14 | SCL / CLK |
| CS | 15 | CS |
| DC | 27 | DC / RS |
| RST | 33 | RST |
| BL | 7 | BL |

The display is configured write-only (no MISO). SPI write speed is 60 MHz; LovyanGFX handles the SPI transaction and DMA automatically.

### I2C Bus — DRV2605L Haptic Driver

| I2C Signal | GPIO |
|------------|------|
| SDA | 22 |
| SCL | 20 |

GPIO 2 must be driven HIGH before the I2C bus is used — it controls the power rail shared by the DRV2605L and the onboard NeoPixel. This is done automatically in `initHaptics()`.

The DRV2605L uses its default I2C address (0x5A). No address configuration needed.

### SD Card

The Feather V2 includes a built-in microSD slot connected to the VSPI bus. Chip select is GPIO 32. The firmware accesses it at up to 25 MHz via the SdFat library. No external wiring is required — just plug a microSD card into the slot on the board.

### Battery Monitoring

The Feather V2 includes a voltage divider (200kΩ + 200kΩ) on the VBAT rail, connected to GPIO 35 (A13). No external components are needed for battery monitoring. The firmware reads this pin and interpolates percentage using a LiPo discharge curve.

| Voltage | Approximate charge |
|---------|--------------------|
| 4.2V | 100% |
| 3.9V | ~70% |
| 3.7V | ~40% |
| 3.5V | ~20% |
| 3.2V | 0% (minimum safe) |

Charging is detected by monitoring the voltage rise rate and absolute voltage level. The Feather V2 charges via USB-C using the onboard MCP73831 charger — no external charging circuit needed.

### Backlight PWM

GPIO 7 controls the display backlight brightness. LovyanGFX manages this automatically via its `Light_PWM` driver at 5 kHz, 8-bit resolution (0–255). The firmware exposes this as `screenBrightness` and adjusts it through the Settings → Brightness menu. Brightness is persisted to NVS.

---

## Power Notes

- **USB-C:** Powers the board and charges the LiPo battery simultaneously via the onboard MCP73831 charger.
- **LiPo battery:** Connect to the JST-PH 2-pin connector. Provides ~3.2–4.2V to the regulator; the onboard 3.3V LDO powers the ESP32 and peripherals.
- **Current draw:** With display at full brightness and Bluetooth active, expect 150–250 mA from the battery. A 500 mAh cell gives roughly 2–3 hours of playback.
- **Deep sleep:** After 15 min of screen-off with no active playback, the firmware enters ESP32 deep sleep (<1 mA). The CENTER button (GPIO 4, EXT0 wakeup) wakes the device. GPIO 7 (backlight) is held LOW during sleep; a hardware pull-down on GPIO 7 is recommended on custom PCBs to ensure the backlight stays off if the GPIO floats.
