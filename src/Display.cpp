#include "Display.h"
#include <cstring>

// Create TFT instance using HSPI
SPIClass hspi(HSPI);
Adafruit_ST7789 display = Adafruit_ST7789(&hspi, TFT_CS, TFT_DC, TFT_RST);

volatile bool displayNeedsUpdate = false;
SemaphoreHandle_t displayMutex = NULL;

// Track window position for smooth scrolling
static int lastWindowStart[4] = {0, 0, 0, 0};  // For menus and lists
static int lastIndex[4] = {0, 0, 0, 0};

// Import scroll direction from EncoderModule
extern int lastScrollDirection;

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

void initDisplay()
{
  Serial.println("🖥️  Initializing ST7789 display (HSPI)...");
  
  // Initialize HSPI with custom pins
  hspi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);  // SCK, MISO (not used), MOSI, CS
  
  // Set SPI speed to 60 MHz
  hspi.setFrequency(60000000);
  Serial.println("⚡ HSPI frequency set to 60 MHz");

  // Initialize display
  display.init(SCREEN_WIDTH, SCREEN_HEIGHT);
  
  // Set rotation (0 degrees as requested)
  display.setRotation(3);
  
  // Clear screen
  display.fillScreen(COLOR_BG);
  
  // Set text defaults
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
  
  delay(1000);
  
  Serial.println("✅ ST7789 Display initialized");
  
  displayMutex = xSemaphoreCreateMutex();
  
  if (displayMutex == NULL) {
    Serial.println("❌ Failed to create display mutex!");
    return;
  }
  
  BaseType_t result = xTaskCreatePinnedToCore(
    displayTask,
    "Display",
    4096,
    NULL,
    1,
    NULL,
    0
  );
  
  if (result != pdPASS) {
    Serial.println("❌ Failed to create display task!");
  }
}

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

// Draw playback status icon
void drawPlaybackIcon(int x, int y, PlayerState state) {
  const int iconSize = 16;
  
  if (state == STATE_PLAYING) {
    // Draw play triangle (pointing right)
    display.fillTriangle(
      x, y,                    // Top left
      x, y + iconSize,         // Bottom left
      x + iconSize, y + iconSize/2,  // Right point
      COLOR_SELECTED
    );
  } else if (state == STATE_PAUSED) {
    // Draw pause bars
    int barWidth = 5;
    int gap = 4;
    display.fillRect(x, y, barWidth, iconSize, COLOR_DISABLED);
    display.fillRect(x + barWidth + gap, y, barWidth, iconSize, COLOR_DISABLED);
  }
  // STATE_STOPPED - draw nothing
}

void drawMenuItem(const char* text, int y, bool selected, bool disabled)
{
  if (!text) return;
  
  display.setTextWrap(false);
  display.setTextSize(2);
  
  const int padding = 8;
  const int itemHeight = 36;
  
  // Text is already truncated from database, just display it
  
  // Draw selection background
  if (selected) {
    display.fillRoundRect(4, y - 4, display.width() - 8, itemHeight, 4, COLOR_SELECTED);
    display.setTextColor(COLOR_BG);
  } else if (disabled) {
    display.setTextColor(COLOR_DISABLED);
  } else {
    display.setTextColor(COLOR_TEXT);
  }
  
  display.setCursor(padding, y + 4);
  display.print(text);
  
  // Draw arrow for enabled items
  if (!disabled && !selected) {
    display.setCursor(display.width() - 20, y + 4);
    display.print(">");
  }
  
  display.setTextColor(COLOR_TEXT);
}

// Draw menu item with playback indicator
void drawMenuItemWithPlayback(const char* text, int y, bool selected, bool disabled, bool isPlaying, PlayerState playState) {
  if (!text) return;
  
  display.setTextWrap(false);
  display.setTextSize(2);
  
  const int padding = 8;
  const int itemHeight = 36;
  
  // Draw selection background
  if (selected) {
    display.fillRoundRect(4, y - 4, display.width() - 8, itemHeight, 4, COLOR_SELECTED);
    display.setTextColor(COLOR_BG);
  } else if (disabled) {
    display.setTextColor(COLOR_DISABLED);
  } else {
    display.setTextColor(COLOR_TEXT);
  }
  
  display.setCursor(padding, y + 4);
  display.print(text);
  
  // Draw playback indicator OR arrow
  if (isPlaying) {
    int iconX = display.width() - 20;
    int iconY = y + 8;
    
    if (playState == STATE_PLAYING) {
      // Draw small play triangle
      int iconSize = 12;
      display.fillTriangle(
        iconX, iconY,
        iconX, iconY + iconSize,
        iconX + iconSize, iconY + iconSize/2,
        selected ? COLOR_BG : COLOR_SELECTED
      );
    } else if (playState == STATE_PAUSED) {
      // Draw small pause bars
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

void drawUI()
{
  display.fillScreen(COLOR_BG);
  
  // Header
  display.fillRect(0, 0, SCREEN_WIDTH, 40, COLOR_ACCENT);
  display.setTextColor(COLOR_HEADER);
  drawCenteredText("ROUGE MP3 PLAYER", 12, 2);
  
  display.setTextColor(COLOR_TEXT);
}

int calculateWindowStart(int currentIndex, int lastIdx, int lastWinStart, int listSize, const int maxDisplay)
{
  if (listSize <= maxDisplay) {
    return 0;  // List fits on screen
  }
  
  // Calculate current cursor position in window
  int cursorPos = lastIdx - lastWinStart;
  
  // Clamp cursor position to valid range
  if (cursorPos < 0) cursorPos = 0;
  if (cursorPos >= maxDisplay) cursorPos = maxDisplay - 1;
  
  // Determine scroll direction
  int delta = currentIndex - lastIdx;
  
  int newWindowStart = lastWinStart;
  
  if (delta > 0) {
    // Scrolling DOWN
    if (cursorPos < maxDisplay - 1) {
      // Cursor can move down within window
      newWindowStart = lastWinStart;
    } else {
      // Cursor at bottom, scroll the list
      newWindowStart = lastWinStart + delta;
    }
  } else if (delta < 0) {
    // Scrolling UP
    if (cursorPos > 0) {
      // Cursor can move up within window
      newWindowStart = lastWinStart;
    } else {
      // Cursor at top, scroll the list
      newWindowStart = lastWinStart + delta;
    }
  }
  
  // Clamp window to valid range
  if (newWindowStart < 0) {
    newWindowStart = 0;
  }
  if (newWindowStart > listSize - maxDisplay) {
    newWindowStart = listSize - maxDisplay;
  }
  
  return newWindowStart;
}

void updateDisplay()
{
  MenuType menu = currentMenu;
  int idx = menuIndex;
  int artIdx = artistIndex;
  int albIdx = albumIndex;
  int sngIdx = songIndex;
  
  char title[128] = {0};
  char artist[128] = {0};
  char album[128] = {0};
  
  strncpy(title, currentTitle.c_str(), 127);
  strncpy(artist, currentArtist.c_str(), 127);
  strncpy(album, currentAlbum.c_str(), 127);
  
  // Track what was previously displayed
  static MenuType lastMenu = (MenuType)-1;  // Invalid initial state
  static int lastDisplayedIndex = -1;
  static PlayerState lastPlayerState = STATE_STOPPED;  // NEW - track playback state
  
  // Full redraw if menu changed (will be true on first call)
  bool fullRedraw = (menu != lastMenu);
  lastMenu = menu;
  
  // Also redraw header if playback state changed - NEW
  bool playbackStateChanged = (player_state != lastPlayerState);
  lastPlayerState = player_state;
  
  if (fullRedraw) {
    display.fillScreen(COLOR_BG);
  }
  
  display.setTextSize(2);
  display.setTextColor(COLOR_TEXT);
  display.setTextWrap(false);

  if (fullRedraw || playbackStateChanged) {
    const char* headerText = "ROUGE MP3";
    switch(menu) {
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
    display.fillRect(0, 0, SCREEN_WIDTH, 40, COLOR_ACCENT);
    display.setTextColor(COLOR_HEADER);
    drawCenteredText(headerText, 12, 2);
    
    // Playback indicator in top-left of header
    if (player_state == STATE_PLAYING || player_state == STATE_PAUSED) {
      drawPlaybackIcon(8, 12, player_state);
    }
    
    // Battery indicator in top-right of header - NEW
    display.setTextSize(1);
    display.setTextColor(COLOR_HEADER);
    
    // Show percentage
    char batteryText[16];
    if (batteryCharging) {
      snprintf(batteryText, sizeof(batteryText), "%d%%⚡", batteryPercent);
    } else {
      snprintf(batteryText, sizeof(batteryText), "%d%%", batteryPercent);
    }
    
    // Position at top-right
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(batteryText, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(SCREEN_WIDTH - w - 8, 12);
    
    // Color based on battery level
    if (batteryPercent <= 10) {
      display.setTextColor(0xF800);  // Red
    } else if (batteryPercent <= 20) {
      display.setTextColor(0xFD20);  // Orange
    } else {
      display.setTextColor(COLOR_HEADER);  // White
    }
    
    display.print(batteryText);
    
    display.setTextColor(COLOR_TEXT);
}

  const int maxDisplay = 5;
  const int startY = 50;
  const int itemHeight = 36;

  // Render based on menu type
  if (menu == MENU_MAIN || menu == MENU_MUSIC || 
      menu == MENU_SETTINGS || menu == MENU_BLUETOOTH)
  {
    int listSize = currentMenuItems.size();
    int windowStart = calculateWindowStart(idx, lastIndex[0], lastWindowStart[0], listSize, maxDisplay);
    
    // Detect if we need full list redraw (window scrolled)
    bool windowChanged = (windowStart != lastWindowStart[0]) || fullRedraw;
    
    lastWindowStart[0] = windowStart;
    
    if (windowChanged) {
      // Clear menu area only
      display.fillRect(0, startY - 5, SCREEN_WIDTH, maxDisplay * itemHeight + 10, COLOR_BG);
      
      // Redraw all visible items
      for (int i = 0; i < maxDisplay && (windowStart + i) < listSize; i++)
      {
        int y = startY + i * itemHeight;
        bool selected = (windowStart + i) == idx;
        
        const MenuItem& item = currentMenuItems[windowStart + i];
        drawMenuItem(item.label.c_str(), y, selected, !item.enabled);
      }
    } else {
      // Only selection changed within same window
      // Redraw old selected item (unselected)
      if (lastDisplayedIndex >= windowStart && lastDisplayedIndex < windowStart + maxDisplay) {
        int oldPos = lastDisplayedIndex - windowStart;
        int y = startY + oldPos * itemHeight;
        
        // Clear old selection
        display.fillRect(0, y - 5, SCREEN_WIDTH, itemHeight + 5, COLOR_BG);
        
        const MenuItem& item = currentMenuItems[lastDisplayedIndex];
        drawMenuItem(item.label.c_str(), y, false, !item.enabled);
      }
      
      // Draw new selected item
      if (idx >= windowStart && idx < windowStart + maxDisplay) {
        int newPos = idx - windowStart;
        int y = startY + newPos * itemHeight;
        
        // Clear new selection area
        display.fillRect(0, y - 5, SCREEN_WIDTH, itemHeight + 5, COLOR_BG);
        
        const MenuItem& item = currentMenuItems[idx];
        drawMenuItem(item.label.c_str(), y, true, !item.enabled);
      }
    }
    
    lastIndex[0] = idx;
    lastDisplayedIndex = idx;
    
    // Scroll indicator (always redraw on change)
    if (listSize > maxDisplay) {
      // Clear indicator area
      display.fillRect(SCREEN_WIDTH - 50, SCREEN_HEIGHT - 30, 50, 20, COLOR_BG);
      
      display.setTextSize(1);
      display.setTextColor(COLOR_TEXT);
      display.setCursor(SCREEN_WIDTH - 40, SCREEN_HEIGHT - 20);
      display.printf("%d/%d", idx + 1, listSize);
    }
  }
  else if (menu == MENU_ARTIST_LIST && !artists.empty())
  {
    int listSize = artists.size();
    int windowStart = calculateWindowStart(artIdx, lastIndex[1], lastWindowStart[1], listSize, maxDisplay);
    
    bool windowChanged = (windowStart != lastWindowStart[1]) || fullRedraw;
    lastWindowStart[1] = windowStart;
    
    if (windowChanged) {
      display.fillRect(0, startY - 5, SCREEN_WIDTH, maxDisplay * itemHeight + 10, COLOR_BG);
      
      for (int i = 0; i < maxDisplay && (windowStart + i) < listSize; i++)
      {
        int y = startY + i * itemHeight;
        bool selected = (windowStart + i) == artIdx;
        
        // Check if this is the currently playing artist
        bool isPlayingArtist = (player_state != STATE_STOPPED && 
                                !currentArtist.empty() && 
                                artists[windowStart + i] == currentArtist);
        
        drawMenuItemWithPlayback(artists[windowStart + i].c_str(), y, selected, false, isPlayingArtist, player_state);
      }
    } else {
      // Only selection changed
      if (lastDisplayedIndex >= windowStart && lastDisplayedIndex < windowStart + maxDisplay) {
        int oldPos = lastDisplayedIndex - windowStart;
        int y = startY + oldPos * itemHeight;
        display.fillRect(0, y - 5, SCREEN_WIDTH, itemHeight + 5, COLOR_BG);
        
        bool isPlayingArtist = (player_state != STATE_STOPPED && 
                                !currentArtist.empty() && 
                                artists[lastDisplayedIndex] == currentArtist);
        
        drawMenuItemWithPlayback(artists[lastDisplayedIndex].c_str(), y, false, false, isPlayingArtist, player_state);
      }
      
      if (artIdx >= windowStart && artIdx < windowStart + maxDisplay) {
        int newPos = artIdx - windowStart;
        int y = startY + newPos * itemHeight;
        display.fillRect(0, y - 5, SCREEN_WIDTH, itemHeight + 5, COLOR_BG);
        
        bool isPlayingArtist = (player_state != STATE_STOPPED && 
                                !currentArtist.empty() && 
                                artists[artIdx] == currentArtist);
        
        drawMenuItemWithPlayback(artists[artIdx].c_str(), y, true, false, isPlayingArtist, player_state);
      }
    }
    
    lastIndex[1] = artIdx;
    lastDisplayedIndex = artIdx;
    
    if (listSize > maxDisplay) {
      display.fillRect(SCREEN_WIDTH - 50, SCREEN_HEIGHT - 30, 50, 20, COLOR_BG);
      display.setTextSize(1);
      display.setTextColor(COLOR_TEXT);
      display.setCursor(SCREEN_WIDTH - 40, SCREEN_HEIGHT - 20);
      display.printf("%d/%d", artIdx + 1, listSize);
    }
  }
  else if (menu == MENU_ALBUM_LIST && !albums.empty())
  {
    int listSize = albums.size();
    int windowStart = calculateWindowStart(albIdx, lastIndex[2], lastWindowStart[2], listSize, maxDisplay);
    
    bool windowChanged = (windowStart != lastWindowStart[2]) || fullRedraw;
    lastWindowStart[2] = windowStart;
    
    if (fullRedraw) {
      // Show current artist in subheader
      display.setTextSize(1);
      display.setTextColor(COLOR_DISABLED);
      display.setCursor(8, 45);
      display.print(artist);
    }
    
    int offsetY = 15;
    
    if (windowChanged) {
      display.fillRect(0, startY + offsetY - 5, SCREEN_WIDTH, maxDisplay * itemHeight + 10, COLOR_BG);
      
      for (int i = 0; i < maxDisplay && (windowStart + i) < listSize; i++)
      {
        int y = startY + offsetY + i * itemHeight;
        bool selected = (windowStart + i) == albIdx;
        
        // Check if this is the currently playing album
        bool isPlayingAlbum = (player_state != STATE_STOPPED && 
                               !currentAlbum.empty() && 
                               albums[windowStart + i] == currentAlbum);
        
        drawMenuItemWithPlayback(albums[windowStart + i].c_str(), y, selected, false, isPlayingAlbum, player_state);
      }
    } else {
      if (lastDisplayedIndex >= windowStart && lastDisplayedIndex < windowStart + maxDisplay) {
        int oldPos = lastDisplayedIndex - windowStart;
        int y = startY + offsetY + oldPos * itemHeight;
        display.fillRect(0, y - 5, SCREEN_WIDTH, itemHeight + 5, COLOR_BG);
        
        bool isPlayingAlbum = (player_state != STATE_STOPPED && 
                               !currentAlbum.empty() && 
                               albums[lastDisplayedIndex] == currentAlbum);
        
        drawMenuItemWithPlayback(albums[lastDisplayedIndex].c_str(), y, false, false, isPlayingAlbum, player_state);
      }
      
      if (albIdx >= windowStart && albIdx < windowStart + maxDisplay) {
        int newPos = albIdx - windowStart;
        int y = startY + offsetY + newPos * itemHeight;
        display.fillRect(0, y - 5, SCREEN_WIDTH, itemHeight + 5, COLOR_BG);
        
        bool isPlayingAlbum = (player_state != STATE_STOPPED && 
                               !currentAlbum.empty() && 
                               albums[albIdx] == currentAlbum);
        
        drawMenuItemWithPlayback(albums[albIdx].c_str(), y, true, false, isPlayingAlbum, player_state);
      }
    }
    
    lastIndex[2] = albIdx;
    lastDisplayedIndex = albIdx;
    
    if (listSize > maxDisplay) {
      display.fillRect(SCREEN_WIDTH - 50, SCREEN_HEIGHT - 30, 50, 20, COLOR_BG);
      display.setTextSize(1);
      display.setTextColor(COLOR_TEXT);
      display.setCursor(SCREEN_WIDTH - 40, SCREEN_HEIGHT - 20);
      display.printf("%d/%d", albIdx + 1, listSize);
    }
  }
  else if (menu == MENU_SONG_LIST && !songs.empty())
  {
    int listSize = songs.size();
    int windowStart = calculateWindowStart(sngIdx, lastIndex[3], lastWindowStart[3], listSize, maxDisplay);
    
    bool windowChanged = (windowStart != lastWindowStart[3]) || fullRedraw;
    lastWindowStart[3] = windowStart;
    
    if (fullRedraw) {
      display.setTextSize(1);
      display.setTextColor(COLOR_DISABLED);
      display.setCursor(8, 45);
      display.print(album);
    }
    
    int offsetY = 15;
    
    if (windowChanged) {
      display.fillRect(0, startY + offsetY - 5, SCREEN_WIDTH, maxDisplay * itemHeight + 10, COLOR_BG);
      
      for (int i = 0; i < maxDisplay && (windowStart + i) < listSize; i++)
      {
        int y = startY + offsetY + i * itemHeight;
        bool selected = (windowStart + i) == sngIdx;
        
        // Check if this is the currently playing song
        bool isPlayingSong = (player_state != STATE_STOPPED && 
                              !currentTitle.empty() && 
                              songs[windowStart + i].title == currentTitle);
        
        drawMenuItemWithPlayback(songs[windowStart + i].displayTitle.c_str(), y, selected, false, isPlayingSong, player_state);
      }
    } else {
      if (lastDisplayedIndex >= windowStart && lastDisplayedIndex < windowStart + maxDisplay) {
        int oldPos = lastDisplayedIndex - windowStart;
        int y = startY + offsetY + oldPos * itemHeight;
        display.fillRect(0, y - 5, SCREEN_WIDTH, itemHeight + 5, COLOR_BG);
        
        bool isPlayingSong = (player_state != STATE_STOPPED && 
                              !currentTitle.empty() && 
                              songs[lastDisplayedIndex].title == currentTitle);
        
        drawMenuItemWithPlayback(songs[lastDisplayedIndex].displayTitle.c_str(), y, false, false, isPlayingSong, player_state);
      }
      
      if (sngIdx >= windowStart && sngIdx < windowStart + maxDisplay) {
        int newPos = sngIdx - windowStart;
        int y = startY + offsetY + newPos * itemHeight;
        display.fillRect(0, y - 5, SCREEN_WIDTH, itemHeight + 5, COLOR_BG);
        
        bool isPlayingSong = (player_state != STATE_STOPPED && 
                              !currentTitle.empty() && 
                              songs[sngIdx].title == currentTitle);
        
        drawMenuItemWithPlayback(songs[sngIdx].displayTitle.c_str(), y, true, false, isPlayingSong, player_state);
      }
    }
    
    lastIndex[3] = sngIdx;
    lastDisplayedIndex = sngIdx;
    
    if (listSize > maxDisplay) {
      display.fillRect(SCREEN_WIDTH - 50, SCREEN_HEIGHT - 30, 50, 20, COLOR_BG);
      display.setTextSize(1);
      display.setTextColor(COLOR_TEXT);
      display.setCursor(SCREEN_WIDTH - 40, SCREEN_HEIGHT - 20);
      display.printf("%d/%d", sngIdx + 1, listSize);
    }
  }
  else if (menu == MENU_NOW_PLAYING)
  {
    // Check if showing volume control
    if (volumeControlActive) {
      // VOLUME CONTROL MODE
      display.fillRect(0, 50, SCREEN_WIDTH, SCREEN_HEIGHT - 80, COLOR_BG);
      
      int centerY = 90;
      
      // "Volume" label
      display.setTextSize(2);
      display.setTextColor(COLOR_TEXT);
      drawCenteredText("Volume", centerY);
      centerY += 30;
      
      // Volume percentage
      char volText[16];
      snprintf(volText, sizeof(volText), "%d%%", currentVolume);
      display.setTextSize(3);
      drawCenteredText(volText, centerY);
      centerY += 40;
      
      // Volume bar
      int barWidth = 200;
      int barHeight = 20;
      int barX = (SCREEN_WIDTH - barWidth) / 2;
      int barY = centerY;
      
      // Background (empty bar)
      display.drawRect(barX, barY, barWidth, barHeight, COLOR_TEXT);
      
      // Fill based on volume
      int fillWidth = (barWidth - 4) * currentVolume / 100;
      if (fillWidth > 0) {
        display.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, COLOR_ACCENT);
      }
      
    } else {
      // NORMAL NOW PLAYING DISPLAY
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
    
    // Only redraw controls on fullRedraw
    // if (fullRedraw) {
    //   display.setTextSize(1);
    //   display.setTextColor(COLOR_TEXT);
    //   display.setCursor(10, SCREEN_HEIGHT - 30);
    //   display.print("Press: Play/Pause");
    //   display.setCursor(10, SCREEN_HEIGHT - 15);
    //   display.print("Turn: Volume");
    // }
  }
}