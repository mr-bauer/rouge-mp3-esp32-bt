#ifndef DISPLAY_H
#define DISPLAY_H

#include <LovyanGFX.hpp>
#include "State.h"

// Display dimensions (visual, after rotation 3 — 240x320 panel in landscape)
#define SCREEN_WIDTH  320
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

// LovyanGFX display driver configuration for ST7789 240x320 on HSPI (rotation 3 → 320x240 landscape)
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI      _bus_instance;
    lgfx::Light_PWM    _light_instance;
public:
    LGFX(void) {
        { auto cfg = _bus_instance.config();
          cfg.spi_host    = HSPI_HOST;
          cfg.spi_mode    = 0;
          cfg.freq_write  = 60000000;
          cfg.freq_read   = 16000000;
          cfg.spi_3wire   = false;
          cfg.use_lock    = true;
          cfg.dma_channel = 0;  // DMA disabled: its IRAM ISR would overflow iram0_0_seg
          cfg.pin_sclk    = TFT_SCLK;
          cfg.pin_mosi    = TFT_MOSI;
          cfg.pin_miso    = -1;
          cfg.pin_dc      = TFT_DC;
          _bus_instance.config(cfg);
          _panel_instance.setBus(&_bus_instance); }
        { auto cfg = _panel_instance.config();
          cfg.pin_cs    = TFT_CS;
          cfg.pin_rst   = TFT_RST;
          cfg.pin_busy  = -1;
          cfg.memory_width  = 240;   // ST7789 physical memory width (always 240)
          cfg.memory_height = 320;   // ST7789 physical memory height (always 320)
          cfg.panel_width   = 240;   // Physical panel width
          cfg.panel_height  = 320;   // Physical panel height (240x320 panel)
          cfg.offset_x = 0; cfg.offset_y = 0; cfg.offset_rotation = 0;
          cfg.dummy_read_pixel = 8; cfg.dummy_read_bits = 1;
          cfg.readable   = false;
          cfg.invert     = true;   // ST7789 requires color inversion
          cfg.rgb_order  = false;
          cfg.dlen_16bit = false;
          cfg.bus_shared = false;
          _panel_instance.config(cfg); }
        { auto cfg = _light_instance.config();
          cfg.pin_bl      = TFT_BL;
          cfg.invert      = false;
          cfg.freq        = BL_PWM_FREQ;
          cfg.pwm_channel = BL_PWM_CHANNEL;
          _light_instance.config(cfg);
          _panel_instance.setLight(&_light_instance); }
        setPanel(&_panel_instance);
    }
};

// UI Layout Constants
// These two scale with textSizePreference (1=small, 2=large)
inline int uiItemHeight()      { return textSizePreference == 1 ? 14 : 36; }
inline int uiMaxVisibleItems() { return textSizePreference == 1 ? 13 : 5; }
// Fixed layout constants (independent of font size)
#define UI_START_Y 50
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

extern LGFX display;
extern lgfx::LGFX_Sprite sprite;  // full-screen off-screen buffer (320x240, PSRAM ~150KB)
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
void drawCenteredText(lgfx::LGFXBase& gfx, const char* text, int y, uint8_t textSize = 1);
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
