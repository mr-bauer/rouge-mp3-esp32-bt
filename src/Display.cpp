#include "Display.h"
#include <cstring>

Adafruit_SSD1327 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

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
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  delay(10);
  
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  
  if (!display.begin(0x3D)) {
    Serial.println("❌ OLED not found!");
  } else {
    Serial.println("✅ Display initialized");
  }

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

void drawMenuItem(const char* text, int y, bool selected)
{
  if (!text) return;
  
  display.setTextWrap(false);
  display.setTextSize(1);
  
  const int padding = 4;
  const int arrowWidth = 6;
  const int maxWidth = display.width() - padding * 2 - arrowWidth - 2;
  
  char shown[64];
  strncpy(shown, text, 60);
  shown[60] = '\0';
  
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(shown, 0, y, &x1, &y1, &w, &h);
  
  if (w > maxWidth) {
    int maxChars = (maxWidth / 6) - 3;
    if (maxChars > 0 && maxChars < 60) {
      shown[maxChars] = '.';
      shown[maxChars + 1] = '.';
      shown[maxChars + 2] = '.';
      shown[maxChars + 3] = '\0';
    }
  }
  
  if (selected) {
    display.fillRect(0, y - 3, display.width(), 14, SSD1327_WHITE);
    display.setTextColor(SSD1327_BLACK);
  } else {
    display.setTextColor(SSD1327_WHITE);
  }
  
  display.setCursor(padding, y);
  display.print(shown);
  display.setCursor(display.width() - arrowWidth, y);
  display.print(">");
  
  display.setTextColor(SSD1327_WHITE);
}

void drawUI()
{
  display.clearDisplay();
  drawCenteredText("ROUGE MP3 PLAYER", 4);
  display.drawLine(0, 14, 127, 14, SSD1327_WHITE);
  display.drawLine(0, 112, 127, 112, SSD1327_WHITE);
  drawCenteredText(btStatus.c_str(), 116);
  display.display();
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
      // Cursor at bottom (position 4), scroll the list
      newWindowStart = lastWinStart + delta;
    }
  } else if (delta < 0) {
    // Scrolling UP
    if (cursorPos > 0) {
      // Cursor can move up within window
      newWindowStart = lastWinStart;
    } else {
      // Cursor at top (position 0), scroll the list
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
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1327_WHITE);
  display.setTextWrap(false);

  // Draw header based on menu type
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
  
  drawCenteredText(headerText, 8);
  display.drawLine(0, 16, SCREEN_WIDTH, 16, SSD1327_WHITE);

  const int maxDisplay = 5;

  // Render based on menu type
  if (menu == MENU_MAIN || menu == MENU_MUSIC || 
      menu == MENU_SETTINGS || menu == MENU_BLUETOOTH)
  {
    // Generic menu rendering
    int listSize = currentMenuItems.size();
    int windowStart = calculateWindowStart(idx, lastIndex[0], lastWindowStart[0], listSize, maxDisplay);
    
    lastWindowStart[0] = windowStart;
    lastIndex[0] = idx;
    
    for (int i = 0; i < maxDisplay && (windowStart + i) < listSize; i++)
    {
      int y = 32 + i * 18;
      bool selected = (windowStart + i) == idx;
      
      const MenuItem& item = currentMenuItems[windowStart + i];
      
      // Gray out disabled items
      if (!item.enabled) {
        display.setTextColor(100);  // Dimmed
      }
      
      drawMenuItem(item.label.c_str(), y, selected);
      
      display.setTextColor(SSD1327_WHITE);
    }
  }
  else if (menu == MENU_ARTIST_LIST && !artists.empty())
  {
    int listSize = artists.size();
    int windowStart = calculateWindowStart(artIdx, lastIndex[1], lastWindowStart[1], listSize, maxDisplay);
    
    lastWindowStart[1] = windowStart;
    lastIndex[1] = artIdx;
    
    for (int i = 0; i < maxDisplay && (windowStart + i) < listSize; i++)
    {
      int y = 32 + i * 18;
      bool selected = (windowStart + i) == artIdx;
      drawMenuItem(artists[windowStart + i].c_str(), y, selected);
    }
  }
  else if (menu == MENU_ALBUM_LIST && !albums.empty())
  {
    int listSize = albums.size();
    int windowStart = calculateWindowStart(albIdx, lastIndex[2], lastWindowStart[2], listSize, maxDisplay);
    
    lastWindowStart[2] = windowStart;
    lastIndex[2] = albIdx;
    
    for (int i = 0; i < maxDisplay && (windowStart + i) < listSize; i++)
    {
      int y = 32 + i * 18;
      bool selected = (windowStart + i) == albIdx;
      drawMenuItem(albums[windowStart + i].c_str(), y, selected);
    }
  }
  else if (menu == MENU_SONG_LIST && !songs.empty())
  {
    int listSize = songs.size();
    int windowStart = calculateWindowStart(sngIdx, lastIndex[3], lastWindowStart[3], listSize, maxDisplay);
    
    lastWindowStart[3] = windowStart;
    lastIndex[3] = sngIdx;
    
    for (int i = 0; i < maxDisplay && (windowStart + i) < listSize; i++)
    {
      int y = 32 + i * 18;
      bool selected = (windowStart + i) == sngIdx;
      drawMenuItem(songs[windowStart + i].title.c_str(), y, selected);
    }
  }
  else if (menu == MENU_NOW_PLAYING)
  {
    int centerY = 40;
    
    if (strlen(title) > 0) {
      drawCenteredText(title, centerY);
      centerY += 14;
    }
    if (strlen(artist) > 0) {
      drawCenteredText(artist, centerY);
      centerY += 14;
    }
    if (strlen(album) > 0) {
      drawCenteredText(album, centerY);
    }
  }

  display.display();
}