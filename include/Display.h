#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1327.h>
#include <Wire.h>
#include "State.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128
#define OLED_RESET -1
#define OLED_SDA 22
#define OLED_SCL 20
#define NEOPIXEL_I2C_POWER 2

extern Adafruit_SSD1327 display;
extern volatile bool displayNeedsUpdate;
extern SemaphoreHandle_t displayMutex;

void initDisplay();
void drawCenteredText(const char* text, int y, uint8_t textSize = 1);
void drawMenuItem(const char* text, int y, bool selected = false);
void drawUI();
void updateDisplay();

#endif