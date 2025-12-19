#include "Display.h"
#include "Preferences.h"
#include <cstring>

// Create TFT instance using HSPI
SPIClass hspi(HSPI);
Adafruit_ST7789 display = Adafruit_ST7789(&hspi, TFT_CS, TFT_DC, TFT_RST);

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

// ============================================================================
// DISPLAY TASK
// ============================================================================

void displayTask(void *param) {
  while(1) {
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
  Serial.println("🖥️  Initializing ST7789 display (HSPI)...");
  
  // Initialize HSPI with custom pins
  hspi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  hspi.setFrequency(60000000);
  Serial.println("⚡ HSPI frequency set to 60 MHz");

  // Initialize backlight PWM
  ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ, BL_PWM_RESOLUTION);
  ledcAttachPin(TFT_BL, BL_PWM_CHANNEL);
  ledcWrite(BL_PWM_CHANNEL, 0);
  Serial.println("🔆 Backlight PWM initialized on GPIO7 (TX)");

  // Initialize display
  display.init(SCREEN_WIDTH, SCREEN_HEIGHT);
  display.setRotation(3);
  display.fillScreen(COLOR_BG);
  display.setTextColor(COLOR_TEXT);
  display.setTextSize(2);
  display.setTextWrap(false);
  
  // Show splash screen
  display.setCursor(30, 100);
  display.setTextSize(3);
  display.println("ROUGE");
  display.setCursor(50, 130);
  display.setTextSize(2);
  display.println("MP3 Player");
  display.setCursor(60, 160);
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
    ledcWrite(BL_PWM_CHANNEL, brightness);
    
    int percent = (brightness * 100) / 255;
    Serial.printf("🔆 Brightness set to: %d/255 (%d%%)\n", brightness, percent);
}

// ============================================================================
// HELPER DRAWING FUNCTIONS
// ============================================================================

void drawCenteredText(const char* text, int y, uint8_t textSize)
{
  if (!text) return;
  
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(textSize);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

void drawPlaybackIcon(int x, int y, PlayerState state) {
  const int iconSize = 16;
  
  if (state == STATE_PLAYING) {
    display.fillTriangle(
      x, y, x, y + iconSize, x + iconSize, y + iconSize/2, COLOR_SELECTED
    );
  } else if (state == STATE_PAUSED) {
    int barWidth = 5;
    int gap = 4;
    display.fillRect(x, y, barWidth, iconSize, COLOR_DISABLED);
    display.fillRect(x + barWidth + gap, y, barWidth, iconSize, COLOR_DISABLED);
  }
}

void drawLightningIcon(int x, int y, uint16_t color) {
  display.drawLine(x + 3, y, x + 1, y + 4, color);
  display.drawLine(x + 1, y + 4, x + 3, y + 4, color);
  display.drawLine(x + 3, y + 4, x + 1, y + 8, color);
  display.drawPixel(x + 2, y + 2, color);
  display.drawPixel(x + 2, y + 6, color);
}

void drawMenuItem(const char* text, int y, bool selected, bool disabled)
{
  if (!text) return;
  
  display.setTextWrap(false);
  display.setTextSize(2);
  
  if (selected) {
    display.fillRoundRect(4, y - 4, display.width() - 8, UI_ITEM_HEIGHT, 4, COLOR_SELECTED);
    display.setTextColor(COLOR_BG);
  } else if (disabled) {
    display.setTextColor(COLOR_DISABLED);
  } else {
    display.setTextColor(COLOR_TEXT);
  }
  
  display.setCursor(UI_PADDING, y + 4);
  display.print(text);
  
  if (!disabled && !selected) {
    display.setCursor(display.width() - 20, y + 4);
    display.print(">");
  }
  
  display.setTextColor(COLOR_TEXT);
}

void drawMenuItemWithPlayback(const char* text, int y, bool selected, bool disabled, 
                               bool isPlaying, PlayerState playState) {
  if (!text) return;
  
  display.setTextWrap(false);
  display.setTextSize(2);
  
  if (selected) {
    display.fillRoundRect(4, y - 4, display.width() - 8, UI_ITEM_HEIGHT, 4, COLOR_SELECTED);
    display.setTextColor(COLOR_BG);
  } else if (disabled) {
    display.setTextColor(COLOR_DISABLED);
  } else {
    display.setTextColor(COLOR_TEXT);
  }
  
  display.setCursor(UI_PADDING, y + 4);
  display.print(text);
  
  if (isPlaying) {
    int iconX = display.width() - 20;
    int iconY = y + 8;
    
    if (playState == STATE_PLAYING) {
      int iconSize = 12;
      display.fillTriangle(
        iconX, iconY, iconX, iconY + iconSize, iconX + iconSize, iconY + iconSize/2,
        selected ? COLOR_BG : COLOR_SELECTED
      );
    } else if (playState == STATE_PAUSED) {
      int barWidth = 4;
      int barHeight = 12;
      int gap = 3;
      uint16_t color = selected ? COLOR_BG : COLOR_DISABLED;
      
      display.fillRect(iconX, iconY, barWidth, barHeight, color);
      display.fillRect(iconX + barWidth + gap, iconY, barWidth, barHeight, color);
    }
  } else if (!disabled && !selected) {
    display.setCursor(display.width() - 20, y + 4);
    display.print(">");
  }
  
  display.setTextColor(COLOR_TEXT);
}

void drawScrollIndicator(int currentIndex, int listSize) {
  if (listSize <= UI_MAX_VISIBLE_ITEMS) return;
  
  display.fillRect(SCREEN_WIDTH - UI_SCROLL_INDICATOR_WIDTH, 
                   SCREEN_HEIGHT - 30, UI_SCROLL_INDICATOR_WIDTH, 20, COLOR_BG);
  
  display.setTextSize(1);
  display.setTextColor(COLOR_TEXT);
  display.setCursor(SCREEN_WIDTH - 40, SCREEN_HEIGHT - 20);
  display.printf("%d/%d", currentIndex + 1, listSize);
}

void drawControlBar(int centerY, const char* label, int value, int maxValue, 
                   const char* unit) {
  // Label
  display.setTextSize(2);
  display.setTextColor(COLOR_TEXT);
  drawCenteredText(label, centerY);
  centerY += 30;
  
  // Value with unit
  char valueText[16];
  snprintf(valueText, sizeof(valueText), "%d%s", value, unit);
  display.setTextSize(3);
  drawCenteredText(valueText, centerY);
  centerY += 40;
  
  // Bar
  int barWidth = 200;
  int barHeight = 20;
  int barX = (SCREEN_WIDTH - barWidth) / 2;
  int barY = centerY;
  
  display.drawRect(barX, barY, barWidth, barHeight, COLOR_TEXT);
  
  int fillWidth = (barWidth - 4) * value / maxValue;
  if (fillWidth > 0) {
    display.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, COLOR_ACCENT);
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

void updateHeader(bool fullRedraw, bool playbackStateChanged, bool periodicUpdate) {
  if (!fullRedraw && !playbackStateChanged && !periodicUpdate) return;
  
  if (periodicUpdate) {
    #ifdef DEBUG
    Serial.println("🔄 Periodic header update (battery status)");
    #endif
  }
  
  const char* headerText = "ROUGE MP3";
  switch(currentMenu) {
    case MENU_MAIN: headerText = "Main Menu"; break;
    case MENU_MUSIC: headerText = "Music"; break;
    case MENU_SETTINGS: headerText = "Settings"; break;
    case MENU_BLUETOOTH: headerText = "Bluetooth"; break;
    case MENU_ARTIST_LIST: headerText = "Artists"; break;
    case MENU_ALBUM_LIST: headerText = "Albums"; break;
    case MENU_SONG_LIST: headerText = "Songs"; break;
    case MENU_NOW_PLAYING: headerText = "Now Playing"; break;
  }
  
  // Header bar
  display.fillRect(0, 0, SCREEN_WIDTH, UI_HEADER_HEIGHT, COLOR_ACCENT);
  display.setTextColor(COLOR_HEADER);
  drawCenteredText(headerText, 12, 2);
  
  // Playback indicator
  if (player_state == STATE_PLAYING || player_state == STATE_PAUSED) {
    drawPlaybackIcon(8, 12, player_state);
  }
  
  // Battery indicator
  display.setTextSize(1);
  display.setTextColor(COLOR_HEADER);
  
  char batteryText[16];
  snprintf(batteryText, sizeof(batteryText), "%d%%", batteryPercent);
  
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(batteryText, 0, 0, &x1, &y1, &w, &h);
  
  int iconWidth = batteryCharging ? 10 : 0;
  display.setCursor(SCREEN_WIDTH - w - iconWidth - 8, 12);
  
  if (batteryPercent <= 10) {
    display.setTextColor(0xF800);  // Red
  } else if (batteryPercent <= 20) {
    display.setTextColor(0xFD20);  // Orange
  } else {
    display.setTextColor(COLOR_HEADER);
  }
  
  display.print(batteryText);
  
  if (batteryCharging) {
    drawLightningIcon(SCREEN_WIDTH - iconWidth - 4, 12, COLOR_SELECTED);
  }
  
  display.setTextColor(COLOR_TEXT);
}

void updateMenuList(MenuType menu, int idx, bool fullRedraw) {
  int listSize = currentMenuItems.size();
  int windowStart = calculateWindowStart(idx, lastIndex[0], lastWindowStart[0], 
                                         listSize, UI_MAX_VISIBLE_ITEMS);
  
  bool windowChanged = (windowStart != lastWindowStart[0]) || fullRedraw;
  lastWindowStart[0] = windowStart;
  
  if (windowChanged) {
    display.fillRect(0, UI_START_Y - 5, SCREEN_WIDTH, 
                    UI_MAX_VISIBLE_ITEMS * UI_ITEM_HEIGHT + 10, COLOR_BG);
    
    for (int i = 0; i < UI_MAX_VISIBLE_ITEMS && (windowStart + i) < listSize; i++) {
      int y = UI_START_Y + i * UI_ITEM_HEIGHT;
      bool selected = (windowStart + i) == idx;
      
      const MenuItem& item = currentMenuItems[windowStart + i];
      drawMenuItem(item.label.c_str(), y, selected, !item.enabled);
    }
  } else {
    // Redraw old selected item
    if (lastDisplayedIndex >= windowStart && lastDisplayedIndex < windowStart + UI_MAX_VISIBLE_ITEMS) {
      int oldPos = lastDisplayedIndex - windowStart;
      int y = UI_START_Y + oldPos * UI_ITEM_HEIGHT;
      
      display.fillRect(0, y - 5, SCREEN_WIDTH, UI_ITEM_HEIGHT + 5, COLOR_BG);
      
      const MenuItem& item = currentMenuItems[lastDisplayedIndex];
      drawMenuItem(item.label.c_str(), y, false, !item.enabled);
    }
    
    // Draw new selected item
    if (idx >= windowStart && idx < windowStart + UI_MAX_VISIBLE_ITEMS) {
      int newPos = idx - windowStart;
      int y = UI_START_Y + newPos * UI_ITEM_HEIGHT;
      
      display.fillRect(0, y - 5, SCREEN_WIDTH, UI_ITEM_HEIGHT + 5, COLOR_BG);
      
      const MenuItem& item = currentMenuItems[idx];
      drawMenuItem(item.label.c_str(), y, true, !item.enabled);
    }
  }
  
  lastIndex[0] = idx;
  lastDisplayedIndex = idx;
  
  drawScrollIndicator(idx, listSize);
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
                                         listSize, UI_MAX_VISIBLE_ITEMS);
  
  bool windowChanged = (windowStart != lastWindowStart[arrayIndex]) || fullRedraw;
  lastWindowStart[arrayIndex] = windowStart;
  
  // Draw subheader if needed
  if (fullRedraw && subheader) {
    display.setTextSize(1);
    display.setTextColor(COLOR_DISABLED);
    display.setCursor(8, 45);
    display.print(subheader);
  }
  
  if (windowChanged) {
    display.fillRect(0, UI_START_Y + yOffset - 5, SCREEN_WIDTH, 
                    UI_MAX_VISIBLE_ITEMS * UI_ITEM_HEIGHT + 10, COLOR_BG);
    
    for (int i = 0; i < UI_MAX_VISIBLE_ITEMS && (windowStart + i) < listSize; i++) {
      int y = UI_START_Y + yOffset + i * UI_ITEM_HEIGHT;
      bool selected = (windowStart + i) == idx;
      
      bool isPlaying = false;
      if (menu == MENU_ARTIST_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentArtist.empty() && 
                    (*items)[windowStart + i] == currentArtist);
        drawMenuItemWithPlayback((*items)[windowStart + i].c_str(), y, selected, 
                                false, isPlaying, player_state);
      } else if (menu == MENU_ALBUM_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentAlbum.empty() && 
                    (*items)[windowStart + i] == currentAlbum);
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
    if (lastDisplayedIndex >= windowStart && lastDisplayedIndex < windowStart + UI_MAX_VISIBLE_ITEMS) {
      int oldPos = lastDisplayedIndex - windowStart;
      int y = UI_START_Y + yOffset + oldPos * UI_ITEM_HEIGHT;
      display.fillRect(0, y - 5, SCREEN_WIDTH, UI_ITEM_HEIGHT + 5, COLOR_BG);
      
      bool isPlaying = false;
      if (menu == MENU_ARTIST_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentArtist.empty() && 
                    (*items)[lastDisplayedIndex] == currentArtist);
        drawMenuItemWithPlayback((*items)[lastDisplayedIndex].c_str(), y, false, 
                                false, isPlaying, player_state);
      } else if (menu == MENU_ALBUM_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentAlbum.empty() && 
                    (*items)[lastDisplayedIndex] == currentAlbum);
        drawMenuItemWithPlayback((*items)[lastDisplayedIndex].c_str(), y, false, 
                                false, isPlaying, player_state);
      } else if (menu == MENU_SONG_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentTitle.empty() && 
                    songs[lastDisplayedIndex].title == currentTitle);
        drawMenuItemWithPlayback(songs[lastDisplayedIndex].displayTitle.c_str(), y, 
                                false, false, isPlaying, player_state);
      }
    }
    
    if (idx >= windowStart && idx < windowStart + UI_MAX_VISIBLE_ITEMS) {
      int newPos = idx - windowStart;
      int y = UI_START_Y + yOffset + newPos * UI_ITEM_HEIGHT;
      display.fillRect(0, y - 5, SCREEN_WIDTH, UI_ITEM_HEIGHT + 5, COLOR_BG);
      
      bool isPlaying = false;
      if (menu == MENU_ARTIST_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentArtist.empty() && 
                    (*items)[idx] == currentArtist);
        drawMenuItemWithPlayback((*items)[idx].c_str(), y, true, false, 
                                isPlaying, player_state);
      } else if (menu == MENU_ALBUM_LIST) {
        isPlaying = (player_state != STATE_STOPPED && !currentAlbum.empty() && 
                    (*items)[idx] == currentAlbum);
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
}

void updateBrightnessScreen() {
  display.fillRect(0, UI_HEADER_HEIGHT, SCREEN_WIDTH, 
                   SCREEN_HEIGHT - UI_HEADER_HEIGHT, COLOR_BG);
  
  int brightPercent = (screenBrightness * 100) / 255;
  drawControlBar(90, "Brightness", brightPercent, 100, "%");
  
  // Instructions
  display.setTextSize(1);
  display.setTextColor(COLOR_TEXT);
  display.setCursor(10, SCREEN_HEIGHT - 30);
  display.print("Turn: Adjust");
  display.setCursor(10, SCREEN_HEIGHT - 15);
  display.print("Wait/Back: Save");
}

void updateVolumeScreen() {
  display.fillRect(0, 50, SCREEN_WIDTH, SCREEN_HEIGHT - 80, COLOR_BG);
  drawControlBar(90, "Volume", currentVolume, 100, "%");
}

void updateNowPlayingScreen() {
  char title[128] = {0};
  char artist[128] = {0};
  char album[128] = {0};
  
  strncpy(title, currentTitle.c_str(), 127);
  strncpy(artist, currentArtist.c_str(), 127);
  strncpy(album, currentAlbum.c_str(), 127);
  
  display.fillRect(0, 50, SCREEN_WIDTH, SCREEN_HEIGHT - 80, COLOR_BG);
  
  int centerY = 80;
  
  if (strlen(title) > 0) {
    display.setTextSize(2);
    display.setTextColor(COLOR_TEXT);
    
    String titleStr = String(title);
    int charsPerLine = 16;
    
    for (int line = 0; line < 3 && !titleStr.isEmpty(); line++) {
      String chunk = titleStr.substring(0, min((int)titleStr.length(), charsPerLine));
      drawCenteredText(chunk.c_str(), centerY + line * 20, 2);
      titleStr = titleStr.substring(chunk.length());
    }
    
    centerY += 70;
  }
  
  if (strlen(artist) > 0) {
    display.setTextSize(1);
    display.setTextColor(COLOR_DISABLED);
    drawCenteredText(artist, centerY);
    centerY += 16;
  }
  
  if (strlen(album) > 0) {
    display.setTextSize(1);
    display.setTextColor(COLOR_DISABLED);
    drawCenteredText(album, centerY);
  }
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
  bool fullRedraw = (menu != lastMenu) || forceDisplayRedraw;
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
    display.fillScreen(COLOR_BG);
  }
  
  display.setTextSize(2);
  display.setTextColor(COLOR_TEXT);
  display.setTextWrap(false);
  
  // Update header
  updateHeader(fullRedraw, playbackStateChanged, periodicHeaderUpdate);
  
  if (periodicHeaderUpdate) {
    lastHeaderUpdate = now;
    // If only header update, return early
    if (!fullRedraw && !playbackStateChanged) return;
  }
  
  // Update content based on menu
  if (menu == MENU_NOW_PLAYING) {
    if (volumeControlActive) {
      updateVolumeScreen();
    } else {
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
  } else {
    // MENU_MAIN, MENU_MUSIC, MENU_BLUETOOTH
    updateMenuList(menu, idx, fullRedraw);
  }
}

void drawUI() {
  display.fillScreen(COLOR_BG);
  display.fillRect(0, 0, SCREEN_WIDTH, UI_HEADER_HEIGHT, COLOR_ACCENT);
  display.setTextColor(COLOR_HEADER);
  drawCenteredText("ROUGE MP3 PLAYER", 12, 2);
  display.setTextColor(COLOR_TEXT);
}