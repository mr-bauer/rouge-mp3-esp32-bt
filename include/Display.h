#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "State.h"

// Display dimensions
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

// HSPI pins for display
#define TFT_CS    15
#define TFT_RST   33
#define TFT_DC    27
#define TFT_MOSI  13
#define TFT_SCLK  14

// Backlight control
#define TFT_BL    7  // PWM backlight control
#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ 5000
#define BL_PWM_RESOLUTION 8  // 8-bit (0-255)

// UI Layout Constants - NEW
#define UI_MAX_VISIBLE_ITEMS 5
#define UI_START_Y 50
#define UI_ITEM_HEIGHT 36
#define UI_PADDING 8
#define UI_HEADER_HEIGHT 40
#define UI_SCROLL_INDICATOR_WIDTH 50
#define UI_SUBHEADER_OFFSET 15

// Colors (16-bit RGB565)
#define COLOR_BG       0x0000  // Black
#define COLOR_TEXT     0xFFFF  // White
#define COLOR_SELECTED 0x07E0  // Green
#define COLOR_DISABLED 0x7BEF  // Gray
#define COLOR_ACCENT   0x051F  // Dark Blue
#define COLOR_HEADER   0xFFFF  // White

extern Adafruit_ST7789 display;
extern volatile bool displayNeedsUpdate;
extern SemaphoreHandle_t displayMutex;

// Initialization
void initDisplay();
void setScreenBrightness(int brightness);

// Main update function
void updateDisplay();

// Component drawing functions - NEW
void updateHeader(bool fullRedraw, bool playbackStateChanged, bool periodicUpdate);
void updateMenuList(MenuType menu, int idx, bool fullRedraw);
void updateMusicBrowserList(MenuType menu, int idx, bool fullRedraw);
void updateNowPlayingScreen();
void updateBrightnessScreen();
void updateVolumeScreen();

// Helper drawing functions
void drawCenteredText(const char* text, int y, uint8_t textSize = 1);
void drawMenuItem(const char* text, int y, bool selected = false, bool disabled = false);
void drawMenuItemWithPlayback(const char* text, int y, bool selected, bool disabled, 
                               bool isPlaying, PlayerState playState);
void drawPlaybackIcon(int x, int y, PlayerState state);
void drawLightningIcon(int x, int y, uint16_t color);
void drawScrollIndicator(int currentIndex, int listSize);
void drawControlBar(int centerY, const char* label, int value, int maxValue, 
                   const char* unit);

// Utility functions
void drawUI();
int calculateWindowStart(int currentIndex, int lastIdx, int lastWinStart, 
                        int listSize, const int maxDisplay);

#endif