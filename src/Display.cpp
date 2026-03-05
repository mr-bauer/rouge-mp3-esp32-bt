#include "Display.h"
#include "Preferences.h"
#include "AlbumArt.h"
#include "icons.h"
#include <cstring>

// LovyanGFX display instance and full-screen sprite (off-screen buffer in PSRAM)
LGFX display;
lgfx::LGFX_Sprite sprite(&display);

volatile bool displayNeedsUpdate = false;
SemaphoreHandle_t displayMutex = NULL;

// Track window position for smooth scrolling
static int lastWindowStart[4] = {0, 0, 0, 0};
static int lastIndex[4] = {0, 0, 0, 0};

// Track display state
static MenuType lastMenu = (MenuType)-1;
static int lastDisplayedIndex = -1;
static PlayerState lastPlayerState = STATE_STOPPED;
static unsigned long lastHeaderUpdate = 0;

// Import scroll direction from EncoderModule
extern int lastScrollDirection;

// Current hardware brightness and desired target (separate from user pref so
// dim/sleep never corrupts the NVS-saved screenBrightness value)
static int activeBrightness = -1;  // -1 = uninitialized
static int targetBrightness = -1;  // -1 = uninitialized

// Set true by the 1s progress tick — updateNowPlayingScreen() skips JPEG
// decode and only repaints the bottom progress-bar strip
static bool nowPlayingProgressOnly = false;

// ============================================================================
// COLOR THEME GLOBALS
// ============================================================================

// Initialized to dark theme; updated by applyTheme()
uint16_t COLOR_BG        = 0x0000;
uint16_t COLOR_TEXT      = 0xFFFF;
uint16_t COLOR_SELECTED  = 0x07E0;
uint16_t COLOR_DISABLED  = 0x7BEF;
uint16_t COLOR_ACCENT    = 0x051F;
uint16_t COLOR_HEADER    = 0xFFFF;
uint16_t COLOR_SEPARATOR = 0x4208;

// Apply the right font + scale for the current textSizePreference.
// 1=small (DejaVu9 @1x), 2=medium (DejaVu12 @1x), 3=large (DejaVu18 @1x).
// Call this before any content-area text draw; header code resets font to nullptr explicitly.
static void applyContentFont(lgfx::LGFXBase& gfx) {
    if (textSizePreference == 1) {
        gfx.setFont(&lgfx::fonts::DejaVu9);
        gfx.setTextSize(1);
    } else if (textSizePreference == 2) {
        gfx.setFont(&lgfx::fonts::DejaVu12);
        gfx.setTextSize(1);
    } else {
        gfx.setFont(&lgfx::fonts::DejaVu18);
        gfx.setTextSize(1);
    }
}

void applyTheme(int themeIdx) {
  if (themeIdx == 1) {  // Light
    COLOR_BG        = 0xFFFF;
    COLOR_TEXT      = 0x0000;
    COLOR_SELECTED  = 0x03DA;  // Material blue ~RGB(0,121,214) — white text readable
    COLOR_DISABLED  = 0x4208;
    COLOR_ACCENT    = 0x001F;
    COLOR_HEADER    = 0x0000;
    COLOR_SEPARATOR = 0xC618;
  } else {              // Dark (default)
    COLOR_BG        = 0x0000;
    COLOR_TEXT      = 0xFFFF;
    COLOR_SELECTED  = 0x07E0;
    COLOR_DISABLED  = 0x7BEF;
    COLOR_ACCENT    = 0x051F;
    COLOR_HEADER    = 0xFFFF;
    COLOR_SEPARATOR = 0x4208;
  }
}

// ============================================================================
// SLEEP / DIM MANAGEMENT
// ============================================================================

static void manageSleep() {
  unsigned long idle = millis() - lastActivityTime;

  // Compute brightness target based on inactivity level:
  //   active       → user's full brightness
  //   dim timeout  → DIM_BRIGHTNESS (faint glow)
  //   sleep timeout (not playing) → 0 (screen off, wake on button press)
  int newTarget;
  if (idle < DIM_TIMEOUT_MS) {
    isScreenDimmed = false;
    newTarget = screenBrightness;
  } else if (idle < SLEEP_TIMEOUT_MS ||
             (player_state == STATE_PLAYING)) {
    isScreenDimmed = true;
    newTarget = DIM_BRIGHTNESS;
  } else {
    // Stopped or paused + sleep timeout reached → screen off
    isScreenDimmed = true;
    newTarget = 0;
  }
  targetBrightness = newTarget;
}

static void stepBrightness() {
  if (activeBrightness < 0 || targetBrightness < 0) {
    // First run: sync to current user brightness without fading
    activeBrightness = targetBrightness = screenBrightness;
    display.setBrightness(activeBrightness);
    return;
  }
  if (activeBrightness == targetBrightness) return;

  // Slow dim, fast restore
  if (activeBrightness > targetBrightness)
    activeBrightness = max(activeBrightness - DIM_STEP_DOWN, targetBrightness);
  else
    activeBrightness = min(activeBrightness + DIM_STEP_UP,   targetBrightness);

  display.setBrightness(activeBrightness);
}

// ============================================================================
// DISPLAY TASK
// ============================================================================

void displayTask(void *param) {
  while(1) {
    manageSleep();      // check inactivity → dim flag or deep sleep
    stepBrightness();   // smooth brightness transition toward target

    unsigned long now = millis();

    // Tick the Now Playing progress bar once per second while playing.
    // Set nowPlayingProgressOnly so only the bottom strip is repainted (no JPEG decode).
    static unsigned long lastProgressTick = 0;
    if (currentMenu == MENU_NOW_PLAYING && player_state == STATE_PLAYING) {
      if (now - lastProgressTick >= 1000) {
        lastProgressTick = now;
        nowPlayingProgressOnly = true;
        displayNeedsUpdate = true;
      }
    }

    // Periodic header refresh (battery / BT status / play-pause icon).
    // Fires only when periodicHeaderUpdate would be true in updateDisplay(),
    // so the early-return path is guaranteed to execute — no JPEG decode.
    if (now - lastHeaderUpdate > DISPLAY_HEADER_UPDATE_INTERVAL) {
      displayNeedsUpdate = true;
    }

    // Tick BT scan header animation every 500 ms while scanning
    static unsigned long lastScanTick = 0;
    if (btScanning && currentMenu == MENU_BT_SCAN) {
      if (now - lastScanTick >= 500) {
        lastScanTick = now;
        displayNeedsUpdate = true;
      }
    }

    if (displayNeedsUpdate) {
      if (xSemaphoreTake(displayMutex, portMAX_DELAY)) {
        updateDisplay();
        displayNeedsUpdate = false;
        xSemaphoreGive(displayMutex);
      }
    }

    #ifdef DEBUG
    UBaseType_t highWater = uxTaskGetStackHighWaterMark(NULL);
    if (highWater < 512) {
      Serial.printf("⚠️ Display task stack low: %u bytes\n", highWater);
    }
    #endif

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initDisplay()
{
  Serial.println("🖥️  Initializing ST7789 display (LovyanGFX + HSPI)...");

  // Apply saved theme before first render
  applyTheme(themeIndex);

  // LovyanGFX handles SPI init, reset sequence, and backlight PWM internally
  display.init();
  display.setRotation(3);
  display.fillScreen(COLOR_BG);
  display.setTextColor(COLOR_TEXT);
  display.setTextWrap(false);

  Serial.println("✅ Display initialized");

  // Allocate full-screen sprite in PSRAM (~150KB at 16bpp for 320x240)
  // Must call setPsram(true) — LGFX_Sprite defaults to DMA (internal) allocation
  // which cannot fit 115KB. setPsram(true) uses MALLOC_CAP_SPIRAM with DMA fallback.
  sprite.setColorDepth(16);
  sprite.setPsram(true);
  if (!sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT)) {
    Serial.println("❌ Sprite allocation failed! Rendering will proceed without sprite.");
  } else {
    Serial.printf("✅ Sprite allocated (%dx%d, %u bytes)\n",
                  SCREEN_WIDTH, SCREEN_HEIGHT, (uint32_t)(SCREEN_WIDTH * SCREEN_HEIGHT * 2));
  }

  // Show splash screen (direct to display — before display task starts)
  // Text widths at 6px/char base: size3="ROUGE"=90px, size2="MP3 Player"=120px, size1="Loading..."=60px
  display.setCursor((SCREEN_WIDTH - 90) / 2, 90);   // "ROUGE" centered
  display.setTextSize(3);
  display.println("ROUGE");
  display.setCursor((SCREEN_WIDTH - 120) / 2, 125);  // "MP3 Player" centered
  display.setTextSize(2);
  display.println("MP3 Player");
  display.setCursor((SCREEN_WIDTH - 60) / 2, 160);   // "Loading..." centered
  display.setTextSize(1);
  display.println("Loading...");

  setScreenBrightness(screenBrightness);
  delay(1000);

  Serial.println("✅ ST7789 Display initialized");

  displayMutex = xSemaphoreCreateMutex();
  if (displayMutex == NULL) {
    Serial.println("❌ Failed to create display mutex!");
    return;
  }

  BaseType_t result = xTaskCreatePinnedToCore(
    displayTask, "Display", 4096, NULL, 1, NULL, 0
  );

  if (result != pdPASS) {
    Serial.println("❌ Failed to create display task!");
  }
}

void setScreenBrightness(int brightness) {
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;

    screenBrightness = brightness;
    display.setBrightness(brightness);

    int percent = (brightness * 100) / 255;
    Serial.printf("🔆 Brightness set to: %d/255 (%d%%)\n", brightness, percent);
}

// ============================================================================
// HELPER DRAWING FUNCTIONS
// ============================================================================

void drawCenteredText(lgfx::LGFXBase& gfx, const char* text, int y, uint8_t textSize)
{
  if (!text) return;
  gfx.setTextSize(textSize);
  int16_t w = gfx.textWidth(text);
  int16_t x = (gfx.width() - w) / 2;
  gfx.setCursor(x, y);
  gfx.print(text);
}

void drawPlaybackIcon(int x, int y, PlayerState state) {
  const int iconSize = 16;

  if (state == STATE_PLAYING) {
    sprite.fillTriangle(
      x, y, x, y + iconSize, x + iconSize, y + iconSize/2, COLOR_SELECTED
    );
  } else if (state == STATE_PAUSED) {
    int barWidth = 5;
    int gap = 4;
    sprite.fillRect(x, y, barWidth, iconSize, COLOR_DISABLED);
    sprite.fillRect(x + barWidth + gap, y, barWidth, iconSize, COLOR_DISABLED);
  }
}

void drawLightningIcon(int x, int y, uint16_t color) {
  sprite.drawLine(x + 3, y, x + 1, y + 4, color);
  sprite.drawLine(x + 1, y + 4, x + 3, y + 4, color);
  sprite.drawLine(x + 3, y + 4, x + 1, y + 8, color);
  sprite.drawPixel(x + 2, y + 2, color);
  sprite.drawPixel(x + 2, y + 6, color);
}

// Truncate text to fit a menu item row at the current font size.
// Measures pixel width using the active content font so it works for all
// three size preferences including proportional fonts (e.g. DejaVu12).
static std::string truncateForDisplay(const char* text) {
  if (!text) return "";
  std::string s(text);
  // Set the content font on sprite so textWidth() is accurate
  applyContentFont(sprite);
  const int maxW = SCREEN_WIDTH - UI_PADDING - 20;  // padding + arrow
  if (sprite.textWidth(s.c_str()) <= maxW) return s;
  while (s.size() > 3 && sprite.textWidth((s + "...").c_str()) > maxW)
    s.pop_back();
  return s + "...";
}

void drawMenuItem(const char* text, int y, bool selected, bool disabled)
{
  if (!text) return;

  sprite.setTextWrap(false);
  applyContentFont(sprite);

  int textOffsetY = (textSizePreference == 1) ? y + 1 : (textSizePreference == 2) ? y + 1 : y + 6;
  std::string displayText = truncateForDisplay(text);

  if (selected) {
    sprite.fillRoundRect(4, y - 4, SCREEN_WIDTH - 8, uiItemHeight(), 4, COLOR_SELECTED);
    sprite.setTextColor(COLOR_BG);
  } else if (disabled) {
    sprite.setTextColor(COLOR_DISABLED);
  } else {
    sprite.setTextColor(COLOR_TEXT);
  }

  sprite.setCursor(UI_PADDING, textOffsetY);
  sprite.print(displayText.c_str());

  if (!disabled && !selected) {
    sprite.setCursor(SCREEN_WIDTH - 20, textOffsetY);
    sprite.print(">");
  }

  sprite.setTextColor(COLOR_TEXT);
}

void drawMenuItemWithPlayback(const char* text, int y, bool selected, bool disabled,
                               bool isPlaying, PlayerState playState) {
  if (!text) return;

  sprite.setTextWrap(false);
  applyContentFont(sprite);

  int textOffsetY = (textSizePreference == 1) ? y + 1 : (textSizePreference == 2) ? y + 1 : y + 6;
  std::string displayText = truncateForDisplay(text);

  if (selected) {
    sprite.fillRoundRect(4, y - 4, SCREEN_WIDTH - 8, uiItemHeight(), 4, COLOR_SELECTED);
    sprite.setTextColor(COLOR_BG);
  } else if (disabled) {
    sprite.setTextColor(COLOR_DISABLED);
  } else {
    sprite.setTextColor(COLOR_TEXT);
  }

  sprite.setCursor(UI_PADDING, textOffsetY);
  sprite.print(displayText.c_str());

  if (isPlaying) {
    int iconX = SCREEN_WIDTH - 20;
    int iconSize = (textSizePreference == 1) ? 8 : (textSizePreference == 2) ? 10 : 12;
    int iconY = (y - 4) + (uiItemHeight() - iconSize) / 2;  // row box starts at y-4

    if (playState == STATE_PLAYING) {
      sprite.fillTriangle(
        iconX, iconY, iconX, iconY + iconSize, iconX + iconSize, iconY + iconSize/2,
        selected ? COLOR_BG : COLOR_SELECTED
      );
    } else if (playState == STATE_PAUSED) {
      int barWidth = (textSizePreference == 1) ? 3 : 4;
      int gap = (textSizePreference == 1) ? 2 : 3;
      uint16_t color = selected ? COLOR_BG : COLOR_DISABLED;

      sprite.fillRect(iconX, iconY, barWidth, iconSize, color);
      sprite.fillRect(iconX + barWidth + gap, iconY, barWidth, iconSize, color);
    }
  } else if (!disabled && !selected) {
    sprite.setCursor(SCREEN_WIDTH - 20, textOffsetY);
    sprite.print(">");
  }

  sprite.setTextColor(COLOR_TEXT);
}

void drawScrollIndicator(int currentIndex, int listSize) {
  if (listSize <= uiMaxVisibleItems()) return;

  sprite.fillRect(SCREEN_WIDTH - UI_SCROLL_INDICATOR_WIDTH,
                  SCREEN_HEIGHT - 30, UI_SCROLL_INDICATOR_WIDTH, 20, COLOR_BG);

  sprite.setFont(nullptr);
  sprite.setTextSize(1);
  sprite.setTextColor(COLOR_TEXT);
  sprite.setCursor(SCREEN_WIDTH - 40, SCREEN_HEIGHT - 20);
  sprite.printf("%d/%d", currentIndex + 1, listSize);
}

void drawControlBar(int centerY, const char* label, int value, int maxValue,
                   const char* unit) {
  // Label
  applyContentFont(sprite);
  sprite.setTextColor(COLOR_TEXT);
  drawCenteredText(sprite, label, centerY);
  centerY += 30;

  // Value with unit
  char valueText[16];
  snprintf(valueText, sizeof(valueText), "%d%s", value, unit);
  sprite.setFont(nullptr);
  sprite.setTextSize(3);
  drawCenteredText(sprite, valueText, centerY);
  centerY += 40;

  // Bar
  int barWidth = 200;
  int barHeight = 20;
  int barX = (SCREEN_WIDTH - barWidth) / 2;
  int barY = centerY;

  sprite.drawRect(barX, barY, barWidth, barHeight, COLOR_TEXT);

  int fillWidth = (barWidth - 4) * value / maxValue;
  if (fillWidth > 0) {
    sprite.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, COLOR_ACCENT);
  }
}

int calculateWindowStart(int currentIndex, int lastIdx, int lastWinStart,
                        int listSize, const int maxDisplay)
{
  if (listSize <= maxDisplay) return 0;

  int cursorPos = lastIdx - lastWinStart;
  if (cursorPos < 0) cursorPos = 0;
  if (cursorPos >= maxDisplay) cursorPos = maxDisplay - 1;

  int delta = currentIndex - lastIdx;
  int newWindowStart = lastWinStart;

  if (delta > 0) {
    if (cursorPos < maxDisplay - 1) {
      newWindowStart = lastWinStart;
    } else {
      newWindowStart = lastWinStart + delta;
    }
  } else if (delta < 0) {
    if (cursorPos > 0) {
      newWindowStart = lastWinStart;
    } else {
      newWindowStart = lastWinStart + delta;
    }
  }

  if (newWindowStart < 0) newWindowStart = 0;
  if (newWindowStart > listSize - maxDisplay) {
    newWindowStart = listSize - maxDisplay;
  }

  return newWindowStart;
}

// ============================================================================
// COMPONENT UPDATE FUNCTIONS
// ============================================================================

static void drawBatteryIcon(int x, int y, int percent, bool charging) {
  const int W       = 32;   // body width
  const int H       = 16;   // body height
  const int NUB_W   = 4;    // nub width
  const int NUB_H   = 8;    // nub height
  const int INSET   = 2;    // border thickness
  const int INNER_W = W - 2 * INSET;  // 28
  const int INNER_H = H - 2 * INSET;  // 12
  const uint16_t EMPTY_COLOR = 0xC618; // light gray for uncharged portion

  // 1. Charging indicator — green lightning bolt to the left of the battery
  if (charging)
    drawLightningIcon(x - 10, y + (H - 8) / 2, COLOR_SELECTED);

  // 2. Empty portion — fill entire inner area light gray
  sprite.fillRect(x + INSET, y + INSET, INNER_W, INNER_H, EMPTY_COLOR);

  // 3. Charged portion — white fill from left edge, proportional to percent
  int fillW = (percent * INNER_W) / 100;
  if (fillW > 0)
    sprite.fillRect(x + INSET, y + INSET, fillW, INNER_H, COLOR_TEXT);

  // 4. Body border
  sprite.drawRect(x, y, W, H, COLOR_TEXT);

  // 5. Nub — solid white, vertically centered on body
  sprite.fillRect(x + W, y + (H - NUB_H) / 2, NUB_W, NUB_H, COLOR_TEXT);

  // 6. Number only (no %) centered on body in black — always use default bitmap font
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", percent);
  sprite.setFont(nullptr);  // reset from any inherited DejaVu font
  sprite.setTextSize(1);
  sprite.setTextColor(COLOR_BG);
  int16_t tw = sprite.textWidth(buf);
  sprite.setCursor(x + (W - tw) / 2, y + (H - 8) / 2);
  sprite.print(buf);

  sprite.setTextColor(COLOR_TEXT);
}

void updateHeader(bool fullRedraw, bool playbackStateChanged, bool periodicUpdate) {
  if (!fullRedraw && !playbackStateChanged && !periodicUpdate) return;

  if (periodicUpdate) {
    #ifdef DEBUG
    Serial.println("🔄 Periodic header update (battery status)");
    #endif
  }

  // On the home screen, show the currently highlighted item name as the title
  std::string mainMenuTitle;
  const char* headerText = "ROUGE MP3";
  if (currentMenu == MENU_MAIN && !currentMenuItems.empty()) {
    mainMenuTitle = currentMenuItems[menuIndex].label;
    headerText = mainMenuTitle.c_str();
  } else {
    switch(currentMenu) {
      case MENU_MAIN: headerText = "Main Menu"; break;
      case MENU_MUSIC: headerText = "Music"; break;
      case MENU_SETTINGS: headerText = "Settings"; break;
      case MENU_BLUETOOTH: headerText = "Bluetooth"; break;
      case MENU_BT_SCAN: headerText = btScanning ? "BT Scan..." : "BT Scan"; break;
      case MENU_ARTIST_LIST: headerText = "Artists"; break;
      case MENU_ALBUM_LIST: headerText = "Albums"; break;
      case MENU_SONG_LIST: headerText = "Songs"; break;
      case MENU_NOW_PLAYING: headerText = "Now Playing"; break;
    }
  }

  // Header bar — DejaVu18 @1x, vertically centered in 40px header
  sprite.setFont(&lgfx::fonts::DejaVu18);
  sprite.setTextSize(1);
  sprite.fillRect(0, 0, SCREEN_WIDTH, UI_HEADER_HEIGHT, COLOR_BG);
  sprite.setTextColor(COLOR_HEADER);
  drawCenteredText(sprite, headerText, 8, 1);

  // Playback indicator
  if (player_state == STATE_PLAYING || player_state == STATE_PAUSED) {
    drawPlaybackIcon(8, 12, player_state);
  }

  // Battery icon
  const int BATT_W  = 32;
  const int BATT_NW = 4;
  int battX = SCREEN_WIDTH - BATT_W - BATT_NW - 6;
  int battY = (UI_HEADER_HEIGHT - 16) / 2;
  drawBatteryIcon(battX, battY, batteryPercent, batteryCharging);

  sprite.setTextColor(COLOR_TEXT);
}

// ============================================================================
// HOME SCREEN — HORIZONTAL ICON MENU
// ============================================================================


void drawHomeScreen(int selectedIdx) {
  sprite.fillRect(0, UI_HEADER_HEIGHT, SCREEN_WIDTH,
                  SCREEN_HEIGHT - UI_HEADER_HEIGHT, COLOR_BG);

  const int ZONE_W     = SCREEN_WIDTH / 4;  // 80px per zone
  const int ICON_CY    = 140;  // center of content area: UI_HEADER_HEIGHT + (SCREEN_HEIGHT - UI_HEADER_HEIGHT) / 2
  const int BOX_TOP    = 105;  // centered: UI_HEADER_HEIGHT + (200 - 70) / 2
  const int BOX_BOTTOM = 175;  // BOX_TOP + 70

  for (int i = 0; i < (int)currentMenuItems.size() && i < 4; i++) {
    const MenuItem& item = currentMenuItems[i];
    bool selected = (i == selectedIdx);
    bool enabled  = item.enabled;
    int  zoneCX   = ZONE_W * i + ZONE_W / 2;

    uint16_t iconColor, labelColor;
    if (selected && enabled) {
      iconColor  = COLOR_TEXT;
      labelColor = COLOR_SELECTED;
    } else if (selected && !enabled) {
      iconColor  = COLOR_DISABLED;
      labelColor = COLOR_DISABLED;
    } else if (enabled) {
      iconColor  = COLOR_DISABLED;
      labelColor = COLOR_DISABLED;
    } else {
      iconColor  = 0x39E7;  // dark gray (dimmer than disabled)
      labelColor = 0x39E7;
    }

    // Selection border — 3px thick rounded rect around the zone
    if (selected) {
      uint16_t borderColor = enabled ? COLOR_SELECTED : 0x02E0;  // green or dark green
      int bx = ZONE_W * i + 4;
      int by = BOX_TOP;
      int bw = ZONE_W - 8;
      int bh = BOX_BOTTOM - BOX_TOP;
      sprite.drawRoundRect(bx,     by,     bw,     bh,     8, borderColor);
      sprite.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 7, borderColor);
      sprite.drawRoundRect(bx + 2, by + 2, bw - 4, bh - 4, 6, borderColor);
    }

    // Icon — XBM bitmap (bit1=bg, bit0=icon, so fg/bg are swapped)
    int ix = zoneCX - ICON_W / 2;
    int iy = ICON_CY - ICON_H / 2;
    sprite.drawXBitmap(ix, iy, HOME_ICONS[i], ICON_W, ICON_H, (uint16_t)COLOR_BG, iconColor);

  }

  sprite.setTextColor(COLOR_TEXT);
}

void updateMenuList(MenuType menu, int idx, bool fullRedraw) {
  int listSize = currentMenuItems.size();
  int windowStart = calculateWindowStart(idx, lastIndex[0], lastWindowStart[0],
                                         listSize, uiMaxVisibleItems());

  bool windowChanged = (windowStart != lastWindowStart[0]) || fullRedraw;
  lastWindowStart[0] = windowStart;

  if (windowChanged) {
    sprite.fillRect(0, UI_START_Y - 5, SCREEN_WIDTH,
                    uiMaxVisibleItems() * uiItemHeight() + 10, COLOR_BG);

    for (int i = 0; i < uiMaxVisibleItems() && (windowStart + i) < listSize; i++) {
      int y = UI_START_Y + i * uiItemHeight();
      bool selected = (windowStart + i) == idx;

      const MenuItem& item = currentMenuItems[windowStart + i];
      drawMenuItem(item.label.c_str(), y, selected, !item.enabled);
    }
  } else {
    // Redraw old selected item
    if (lastDisplayedIndex >= windowStart && lastDisplayedIndex < windowStart + uiMaxVisibleItems()) {
      int oldPos = lastDisplayedIndex - windowStart;
      int y = UI_START_Y + oldPos * uiItemHeight();

      sprite.fillRect(0, y - 5, SCREEN_WIDTH, uiItemHeight() + 5, COLOR_BG);

      const MenuItem& item = currentMenuItems[lastDisplayedIndex];
      drawMenuItem(item.label.c_str(), y, false, !item.enabled);
    }

    // Draw new selected item
    if (idx >= windowStart && idx < windowStart + uiMaxVisibleItems()) {
      int newPos = idx - windowStart;
      int y = UI_START_Y + newPos * uiItemHeight();

      sprite.fillRect(0, y - 5, SCREEN_WIDTH, uiItemHeight() + 5, COLOR_BG);

      const MenuItem& item = currentMenuItems[idx];
      drawMenuItem(item.label.c_str(), y, true, !item.enabled);
    }
  }

  lastIndex[0] = idx;
  lastDisplayedIndex = idx;

  drawScrollIndicator(idx, listSize);
}

static void drawAlphaScrollOverlay() {
  const int OW = 90, OH = 70;
  const int OX = (SCREEN_WIDTH - OW) / 2;
  const int OY = UI_HEADER_HEIGHT + (SCREEN_HEIGHT - UI_HEADER_HEIGHT - OH) / 2;

  sprite.fillRoundRect(OX, OY, OW, OH, 8, COLOR_SELECTED);
  sprite.drawRoundRect(OX, OY, OW, OH, 8, COLOR_TEXT);

  sprite.setFont(nullptr);
  sprite.setTextSize(1);
  sprite.setTextColor(COLOR_BG);
  drawCenteredText(sprite, "Jump to", OY + 8, 1);

  char ls[2] = { fastScrollLetter, '\0' };
  sprite.setTextSize(4);
  int tw = sprite.textWidth(ls);
  sprite.setCursor(OX + (OW - tw) / 2, OY + 22);
  sprite.print(ls);

  sprite.setTextColor(COLOR_TEXT);
}

void updateMusicBrowserList(MenuType menu, int idx, bool fullRedraw) {
  int listSize = 0;
  int arrayIndex = 0;
  const char* subheader = nullptr;
  std::vector<std::string>* items = nullptr;

  // Determine which list we're rendering
  if (menu == MENU_ARTIST_LIST) {
    listSize = artists.size();
    arrayIndex = 1;
    items = &artists;
  } else if (menu == MENU_ALBUM_LIST) {
    listSize = albums.size();
    arrayIndex = 2;
    items = &albums;
    subheader = currentArtist.c_str();
  } else if (menu == MENU_SONG_LIST) {
    listSize = songs.size();
    arrayIndex = 3;
    subheader = currentAlbum.c_str();
  }

  if (!items && menu != MENU_SONG_LIST) return;
  if (listSize == 0) return;

  int yOffset = subheader ? UI_SUBHEADER_OFFSET : 0;
  int windowStart = calculateWindowStart(idx, lastIndex[arrayIndex],
                                         lastWindowStart[arrayIndex],
                                         listSize, uiMaxVisibleItems());

  // Guard: ensure selected item is actually visible (handles large index jumps from alpha scroll)
  if (idx < windowStart || idx >= windowStart + uiMaxVisibleItems()) {
    windowStart = idx - uiMaxVisibleItems() / 2;
    if (windowStart < 0) windowStart = 0;
    if (windowStart > listSize - uiMaxVisibleItems()) windowStart = listSize - uiMaxVisibleItems();
  }

  bool windowChanged = (windowStart != lastWindowStart[arrayIndex]) || fullRedraw;
  lastWindowStart[arrayIndex] = windowStart;

  // Draw subheader if needed (always small/default font regardless of textSizePreference)
  if (fullRedraw && subheader) {
    sprite.setFont(nullptr);
    sprite.setTextSize(1);
    sprite.setTextColor(COLOR_DISABLED);
    sprite.setCursor(8, 45);
    sprite.print(subheader);
  }

  if (windowChanged) {
    sprite.fillRect(0, UI_START_Y + yOffset - 5, SCREEN_WIDTH,
                    uiMaxVisibleItems() * uiItemHeight() + 10, COLOR_BG);

    for (int i = 0; i < uiMaxVisibleItems() && (windowStart + i) < listSize; i++) {
      int y = UI_START_Y + yOffset + i * uiItemHeight();
      bool selected = (windowStart + i) == idx;

      bool isPlaying = false;
      if (menu == MENU_ARTIST_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !playingArtist.empty() &&
                    (*items)[windowStart + i] == playingArtist);
        drawMenuItemWithPlayback((*items)[windowStart + i].c_str(), y, selected,
                                false, isPlaying, player_state);
      } else if (menu == MENU_ALBUM_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !playingAlbum.empty() &&
                    (*items)[windowStart + i] == playingAlbum);
        drawMenuItemWithPlayback((*items)[windowStart + i].c_str(), y, selected,
                                false, isPlaying, player_state);
      } else if (menu == MENU_SONG_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentTitle.empty() &&
                    songs[windowStart + i].title == currentTitle);
        drawMenuItemWithPlayback(songs[windowStart + i].displayTitle.c_str(), y,
                                selected, false, isPlaying, player_state);
      }
    }
  } else {
    // Partial redraw logic (similar to above but for changed selection only)
    if (lastDisplayedIndex >= windowStart && lastDisplayedIndex < windowStart + uiMaxVisibleItems()) {
      int oldPos = lastDisplayedIndex - windowStart;
      int y = UI_START_Y + yOffset + oldPos * uiItemHeight();
      sprite.fillRect(0, y - 5, SCREEN_WIDTH, uiItemHeight() + 5, COLOR_BG);

      bool isPlaying = false;
      if (menu == MENU_ARTIST_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !playingArtist.empty() &&
                    (*items)[lastDisplayedIndex] == playingArtist);
        drawMenuItemWithPlayback((*items)[lastDisplayedIndex].c_str(), y, false,
                                false, isPlaying, player_state);
      } else if (menu == MENU_ALBUM_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !playingAlbum.empty() &&
                    (*items)[lastDisplayedIndex] == playingAlbum);
        drawMenuItemWithPlayback((*items)[lastDisplayedIndex].c_str(), y, false,
                                false, isPlaying, player_state);
      } else if (menu == MENU_SONG_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentTitle.empty() &&
                    songs[lastDisplayedIndex].title == currentTitle);
        drawMenuItemWithPlayback(songs[lastDisplayedIndex].displayTitle.c_str(), y,
                                false, false, isPlaying, player_state);
      }
    }

    if (idx >= windowStart && idx < windowStart + uiMaxVisibleItems()) {
      int newPos = idx - windowStart;
      int y = UI_START_Y + yOffset + newPos * uiItemHeight();
      sprite.fillRect(0, y - 5, SCREEN_WIDTH, uiItemHeight() + 5, COLOR_BG);

      bool isPlaying = false;
      if (menu == MENU_ARTIST_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !playingArtist.empty() &&
                    (*items)[idx] == playingArtist);
        drawMenuItemWithPlayback((*items)[idx].c_str(), y, true, false,
                                isPlaying, player_state);
      } else if (menu == MENU_ALBUM_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !playingAlbum.empty() &&
                    (*items)[idx] == playingAlbum);
        drawMenuItemWithPlayback((*items)[idx].c_str(), y, true, false,
                                isPlaying, player_state);
      } else if (menu == MENU_SONG_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentTitle.empty() &&
                    songs[idx].title == currentTitle);
        drawMenuItemWithPlayback(songs[idx].displayTitle.c_str(), y, true, false,
                                isPlaying, player_state);
      }
    }
  }

  lastIndex[arrayIndex] = idx;
  lastDisplayedIndex = idx;

  drawScrollIndicator(idx, listSize);

  if (fastScrollActive) {
    drawAlphaScrollOverlay();
  }
}

void updateBrightnessScreen() {
  sprite.fillRect(0, UI_HEADER_HEIGHT, SCREEN_WIDTH,
                  SCREEN_HEIGHT - UI_HEADER_HEIGHT, COLOR_BG);

  int brightPercent = (screenBrightness * 100) / 255;
  drawControlBar(90, "Brightness", brightPercent, 100, "%");

  // Instructions
  sprite.setTextSize(1);
  sprite.setTextColor(COLOR_TEXT);
  sprite.setCursor(10, SCREEN_HEIGHT - 30);
  sprite.print("Turn: Adjust");
  sprite.setCursor(10, SCREEN_HEIGHT - 15);
  sprite.print("Wait/Back: Save");
}

void updateVolumeScreen() {
  sprite.fillRect(0, UI_HEADER_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - UI_HEADER_HEIGHT, COLOR_BG);
  drawControlBar(90, "Volume", currentVolume, 100, "%");
}

void updateNowPlayingScreen() {
  // ── Fast path: 1s progress tick — only repaint the bottom strip ──────────
  // Avoids JPEG decode and full text redraw on every tick.
  if (nowPlayingProgressOnly) {
    nowPlayingProgressOnly = false;

    // Compute elapsed (same as full path below)
    unsigned long elapsed = 0;
    if (playbackStartMillis > 0) {
      unsigned long paused = totalPausedMs;
      if (player_state == STATE_PAUSED && pauseStartMillis > 0)
        paused += millis() - pauseStartMillis;
      unsigned long rawMs = millis() - playbackStartMillis;
      elapsed = (rawMs > paused) ? (rawMs - paused) / 1000 : 0;
    }
    int duration = (songIndex >= 0 && songIndex < (int)songs.size())
                   ? songs[songIndex].duration : 0;
    if (duration > 0 && (int)elapsed > duration) elapsed = duration;

    const int BAR_X = 8;
    const int BAR_Y = 196;
    const int BAR_W = SCREEN_WIDTH - 16;
    const int BAR_H = 8;
    const int STRIP_Y = BAR_Y - 4;  // small margin above bar

    // Clear only the bottom strip
    sprite.fillRect(0, STRIP_Y, SCREEN_WIDTH, SCREEN_HEIGHT - STRIP_Y, COLOR_BG);

    // Bar outline + fill
    sprite.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, COLOR_DISABLED);
    if (duration > 0) {
      int fillW = (int)((long)elapsed * (BAR_W - 4) / duration);
      if (fillW > BAR_W - 4) fillW = BAR_W - 4;
      if (fillW > 0)
        sprite.fillRect(BAR_X + 2, BAR_Y + 2, fillW, BAR_H - 4, COLOR_SELECTED);
    }

    // Time labels
    char elapsedStr[8], totalStr[8];
    snprintf(elapsedStr, sizeof(elapsedStr), "%d:%02d",
             (int)elapsed / 60, (int)elapsed % 60);
    if (duration > 0)
      snprintf(totalStr, sizeof(totalStr), "%d:%02d", duration / 60, duration % 60);
    else
      snprintf(totalStr, sizeof(totalStr), "--:--");

    sprite.setTextSize(1);
    sprite.setTextColor(COLOR_DISABLED);
    sprite.setCursor(BAR_X, BAR_Y + BAR_H + 4);
    sprite.print(elapsedStr);
    sprite.setCursor(BAR_X + BAR_W - sprite.textWidth(totalStr), BAR_Y + BAR_H + 4);
    sprite.print(totalStr);
    sprite.setTextColor(COLOR_TEXT);
    return;
  }
  // ── Full redraw ───────────────────────────────────────────────────────────

  sprite.fillRect(0, UI_HEADER_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - UI_HEADER_HEIGHT, COLOR_BG);

  char title[128]  = {0};
  char artist[128] = {0};
  char album[128]  = {0};
  strncpy(title,  currentTitle.c_str(),  127);
  strncpy(artist, currentArtist.c_str(), 127);
  strncpy(album,  currentAlbum.c_str(),  127);

  if (albumArtAvailable) {
    // Side-by-side layout: art on left, text on right
    const int ART_SIZE = 130;
    const int ART_X    = 10;
    const int ART_Y    = 55;
    const int TEXT_X   = ART_X + ART_SIZE + 12;  // 152
    const int TEXT_W   = SCREEN_WIDTH - TEXT_X - 8;  // 160px

    sprite.fillRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, COLOR_BG);
    drawAlbumArt(ART_X, ART_Y, ART_SIZE);

    int charsPerLine = TEXT_W / (textSizePreference == 1 ? 6 : (textSizePreference == 2 ? 8 : 10));
    int lineSpacing  = textSizePreference == 1 ? 10 : (textSizePreference == 2 ? 15 : 26);
    int y = 90;

    if (strlen(title) > 0) {
      applyContentFont(sprite);
      sprite.setTextColor(COLOR_TEXT);
      String titleStr = String(title);
      for (int line = 0; line < 2 && !titleStr.isEmpty(); line++) {
        String chunk = titleStr.substring(0, min((int)titleStr.length(), charsPerLine));
        sprite.setCursor(TEXT_X, y + line * lineSpacing);
        sprite.print(chunk.c_str());
        titleStr = titleStr.substring(chunk.length());
      }
      y += 2 * lineSpacing + 8;
    }

    sprite.setFont(nullptr);
    sprite.setTextSize(1);
    sprite.setTextColor(COLOR_DISABLED);
    if (strlen(artist) > 0) {
      String s = String(artist);
      if ((int)s.length() > TEXT_W / 6) s = s.substring(0, TEXT_W / 6);
      sprite.setCursor(TEXT_X, y);
      sprite.print(s);
      y += 16;
    }
    if (strlen(album) > 0 && y < SCREEN_HEIGHT - 10) {
      String s = String(album);
      if ((int)s.length() > TEXT_W / 6) s = s.substring(0, TEXT_W / 6);
      sprite.setCursor(TEXT_X, y);
      sprite.print(s);
    }

  } else {
    // No art: centered layout using full width
    int charsPerLine = textSizePreference == 1 ? 46 : (textSizePreference == 2 ? 36 : 30);
    int lineSpacing  = textSizePreference == 1 ? 10 : (textSizePreference == 2 ? 15 : 26);
    int y = 80;

    if (strlen(title) > 0) {
      applyContentFont(sprite);
      sprite.setTextColor(COLOR_TEXT);
      String titleStr = String(title);
      for (int line = 0; line < 3 && !titleStr.isEmpty(); line++) {
        String chunk = titleStr.substring(0, min((int)titleStr.length(), charsPerLine));
        drawCenteredText(sprite, chunk.c_str(), y + line * lineSpacing, 1);
        titleStr = titleStr.substring(chunk.length());
      }
      y += textSizePreference == 1 ? 50 : (textSizePreference == 2 ? 65 : 80);
    }

    sprite.setFont(nullptr);
    sprite.setTextSize(1);
    sprite.setTextColor(COLOR_DISABLED);
    if (strlen(artist) > 0) {
      drawCenteredText(sprite, artist, y);
      y += 16;
    }
    if (strlen(album) > 0 && y <= SCREEN_HEIGHT - 20) {
      drawCenteredText(sprite, album, y);
    }
  }

  // ── Progress bar + time labels (both layouts) ─────────────────────────────
  // Compute elapsed seconds, accounting for accumulated pause time
  unsigned long elapsed = 0;
  if (playbackStartMillis > 0) {
    unsigned long paused = totalPausedMs;
    if (player_state == STATE_PAUSED && pauseStartMillis > 0)
      paused += millis() - pauseStartMillis;
    unsigned long rawMs = millis() - playbackStartMillis;
    elapsed = (rawMs > paused) ? (rawMs - paused) / 1000 : 0;
  }

  int duration = (songIndex >= 0 && songIndex < (int)songs.size())
                 ? songs[songIndex].duration : 0;
  if (duration > 0 && (int)elapsed > duration) elapsed = duration;

  const int BAR_X = 8;
  const int BAR_Y = 196;
  const int BAR_W = SCREEN_WIDTH - 16;  // 304px
  const int BAR_H = 8;

  sprite.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, COLOR_DISABLED);

  if (duration > 0) {
    int fillW = (int)((long)elapsed * (BAR_W - 4) / duration);
    if (fillW > BAR_W - 4) fillW = BAR_W - 4;
    if (fillW > 0)
      sprite.fillRect(BAR_X + 2, BAR_Y + 2, fillW, BAR_H - 4, COLOR_SELECTED);
  }

  char elapsedStr[8], totalStr[8];
  snprintf(elapsedStr, sizeof(elapsedStr), "%d:%02d",
           (int)elapsed / 60, (int)elapsed % 60);
  if (duration > 0)
    snprintf(totalStr, sizeof(totalStr), "%d:%02d", duration / 60, duration % 60);
  else
    snprintf(totalStr, sizeof(totalStr), "--:--");

  sprite.setFont(nullptr);
  sprite.setTextSize(1);
  sprite.setTextColor(COLOR_DISABLED);
  sprite.setCursor(BAR_X, BAR_Y + BAR_H + 4);
  sprite.print(elapsedStr);
  sprite.setCursor(BAR_X + BAR_W - sprite.textWidth(totalStr), BAR_Y + BAR_H + 4);
  sprite.print(totalStr);
  sprite.setTextColor(COLOR_TEXT);
}

// ============================================================================
// MAIN UPDATE FUNCTION
// ============================================================================

void updateDisplay()
{
  MenuType menu = currentMenu;
  int idx = menuIndex;
  int artIdx = artistIndex;
  int albIdx = albumIndex;
  int sngIdx = songIndex;

  // Check for state changes
  // MENU_MAIN always forces a full redraw so the dynamic header title stays current
  bool fullRedraw = (menu != lastMenu) || forceDisplayRedraw || (menu == MENU_MAIN);
  lastMenu = menu;

  if (forceDisplayRedraw) {
    Serial.println("🔄 Force redraw requested");
    forceDisplayRedraw = false;
  }

  bool playbackStateChanged = (player_state != lastPlayerState);
  lastPlayerState = player_state;

  unsigned long now = millis();
  bool periodicHeaderUpdate = (now - lastHeaderUpdate > DISPLAY_HEADER_UPDATE_INTERVAL);

  if (fullRedraw) {
    sprite.fillSprite(COLOR_BG);
  }

  applyContentFont(sprite);
  sprite.setTextColor(COLOR_TEXT);
  sprite.setTextWrap(false);

  // Update header
  updateHeader(fullRedraw, playbackStateChanged, periodicHeaderUpdate);

  if (periodicHeaderUpdate) {
    lastHeaderUpdate = now;
    // If only header update, still push sprite to show updated battery/status
    if (!fullRedraw && !playbackStateChanged) {
      sprite.pushSprite(0, 0);
      return;
    }
  }

  // Update content based on menu
  if (menu == MENU_NOW_PLAYING) {
    if (volumeControlActive) {
      updateVolumeScreen();
    } else {
      // On state changes (new song or menu arrival) force the full render path
      // even if the 1s ticker happened to set nowPlayingProgressOnly in the same tick
      if (fullRedraw || playbackStateChanged) nowPlayingProgressOnly = false;
      updateNowPlayingScreen();
    }
  } else if (menu == MENU_SETTINGS) {
    if (brightnessControlActive) {
      updateBrightnessScreen();
    } else {
      updateMenuList(menu, idx, fullRedraw);
    }
  } else if (menu == MENU_ARTIST_LIST) {
    updateMusicBrowserList(menu, artIdx, fullRedraw);
  } else if (menu == MENU_ALBUM_LIST) {
    updateMusicBrowserList(menu, albIdx, fullRedraw);
  } else if (menu == MENU_SONG_LIST) {
    updateMusicBrowserList(menu, sngIdx, fullRedraw);
  } else if (menu == MENU_MAIN) {
    drawHomeScreen(idx);
  } else {
    // MENU_MUSIC, MENU_BLUETOOTH, MENU_BT_SCAN — all plain list menus
    updateMenuList(menu, idx, fullRedraw);
  }

  // Flush sprite to display via DMA in one operation — eliminates flicker
  sprite.pushSprite(0, 0);
}

void drawUI() {
  sprite.fillSprite(COLOR_BG);
  sprite.fillRect(0, 0, SCREEN_WIDTH, UI_HEADER_HEIGHT, COLOR_ACCENT);
  sprite.setTextColor(COLOR_HEADER);
  drawCenteredText(sprite, "ROUGE MP3 PLAYER", 12, 2);
  sprite.setTextColor(COLOR_TEXT);
  sprite.pushSprite(0, 0);
}
