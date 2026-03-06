#include "State.h"
#include "Haptics.h"
#include <Arduino.h>

// Bluetooth status
std::string btStatus = "BT Ready";
volatile bool btScanning = false;
std::vector<std::string> btFoundDevices;

// Menu state
MenuType currentMenu = MENU_MAIN;
std::vector<MenuItem> currentMenuItems;
int menuIndex = 0;
std::vector<MenuStackEntry> menuStack;

// Currently playing info
std::string currentArtist = "";
std::string currentAlbum = "";
std::string currentTitle = "";

// Library data
std::vector<std::string> artists;
std::vector<std::string> albums;
std::vector<Song> songs;

// Navigation indices (browse position)
volatile int artistIndex = 0;
volatile int albumIndex = 0;
volatile int songIndex = 0;

// Playback-position context (where we're actually playing from)
int playingSongIndex = 0;
int playingAlbumIndex = 0;
int playingArtistIndex = 0;
std::string playingArtist;
std::string playingAlbum;

// Playback state
volatile PlayerState player_state = STATE_STOPPED;
bool bluetoothConnected = false;

// Volume control
int currentVolume = 50;  // Start at 50%
bool volumeControlActive = false;
unsigned long lastVolumeChange = 0;

// Battery monitoring
float batteryVoltage = 0.0f;
int batteryPercent = 0;
bool batteryCharging = false;
unsigned long lastBatteryCheck = 0;

// Brightness control
int screenBrightness = 255;  // Default full brightness
bool brightnessControlActive = false;
unsigned long lastBrightnessChange = 0;

// Sleep / dim state
volatile unsigned long lastActivityTime = 0;
volatile bool          isScreenDimmed   = false;

// Progress tracking
volatile unsigned long playbackStartMillis = 0;
volatile unsigned long totalPausedMs       = 0;
volatile unsigned long pauseStartMillis    = 0;

// Display control - NEW
bool forceDisplayRedraw = false;
int textSizePreference = 2;  // 1=small (6x8px), 2=medium (DejaVu12), 3=large (12x16px)
int themeIndex = 0;           // 0 = dark, 1 = light
bool resumeOnBoot = true;
bool albumArtAvailable = false;

// Alpha fast-scroll
std::vector<AlphaEntry> alphaIndex;
bool          fastScrollActive   = false;
char          fastScrollLetter   = 'A';
int           fastScrollAlphaIdx = 0;
unsigned long fastScrollLastTick = 0;
unsigned long fastScrollLastStep = 0;
bool          alphaOverlayOnly   = false;

void buildAlphaIndex(MenuType menu) {
  alphaIndex.clear();
  fastScrollActive   = false;
  fastScrollAlphaIdx = 0;
  fastScrollLastStep = 0;

  auto addEntry = [&](char raw, int idx) {
    char c = toupper((unsigned char)raw);
    if (c < 'A' || c > 'Z') c = '#';
    if (alphaIndex.empty() || alphaIndex.back().letter != c)
      alphaIndex.push_back({c, idx});
  };

  if (menu == MENU_ARTIST_LIST) {
    for (int i = 0; i < (int)artists.size(); i++)
      if (!artists[i].empty()) addEntry(artists[i][0], i);
  } else if (menu == MENU_ALBUM_LIST) {
    for (int i = 0; i < (int)albums.size(); i++)
      if (!albums[i].empty()) addEntry(albums[i][0], i);
  } else if (menu == MENU_SONG_LIST) {
    for (int i = 0; i < (int)songs.size(); i++)
      if (!songs[i].title.empty()) addEntry(songs[i].title[0], i);
  }
}

void initFastScrollPosition(int currentIdx) {
  fastScrollAlphaIdx = 0;
  for (int i = 0; i < (int)alphaIndex.size(); i++) {
    if (alphaIndex[i].firstIndex <= currentIdx) fastScrollAlphaIdx = i;
    else break;
  }
  fastScrollLetter = alphaIndex.empty() ? '?' : alphaIndex[fastScrollAlphaIdx].letter;
}

// Menu builders
void buildMainMenu() {
  currentMenuItems.clear();
  currentMenuItems.push_back(MenuItem("Music", MENU_MUSIC));
  currentMenuItems.push_back(MenuItem("Now Playing", MENU_NOW_PLAYING, (player_state == STATE_PLAYING || player_state == STATE_PAUSED)));  // Disabled if not playing
  currentMenuItems.push_back(MenuItem("Settings", MENU_SETTINGS));
  currentMenuItems.push_back(MenuItem("Bluetooth", MENU_BLUETOOTH));
  menuIndex = 0;
}

void buildMusicMenu() {
  currentMenuItems.clear();
  currentMenuItems.push_back(MenuItem("Artists", MENU_ARTIST_LIST));
  currentMenuItems.push_back(MenuItem("Albums", MENU_ALBUM_LIST));  // Future: album browser
  currentMenuItems.push_back(MenuItem("All Songs", MENU_SONG_LIST)); // Future: all songs
  currentMenuItems.push_back(MenuItem("Playlists", MENU_MUSIC));     // Future: playlists
  menuIndex = 0;
}

void buildSettingsMenu() {
  currentMenuItems.clear();
  currentMenuItems.push_back(MenuItem("Brightness", MENU_SETTINGS));
  currentMenuItems.push_back(MenuItem(
    textSizePreference == 1 ? "Text Size: Small" : (textSizePreference == 2 ? "Text Size: Medium" : "Text Size: Large"),
    MENU_SETTINGS));
  currentMenuItems.push_back(MenuItem(
    themeIndex == 0 ? "Theme: Dark" : "Theme: Light",
    MENU_SETTINGS));
  currentMenuItems.push_back(MenuItem(
    resumeOnBoot ? "Resume on Boot: On" : "Resume on Boot: Off",
    MENU_SETTINGS));
  currentMenuItems.push_back(MenuItem("Shuffle: Off", MENU_SETTINGS));
  currentMenuItems.push_back(MenuItem("Repeat: Off", MENU_SETTINGS));
  currentMenuItems.push_back(MenuItem("About", MENU_SETTINGS));
  menuIndex = 0;
}

void buildBluetoothMenu() {
  currentMenuItems.clear();

  if (bluetoothConnected) {
    currentMenuItems.push_back(MenuItem("Status: Connected", MENU_BLUETOOTH));
    currentMenuItems.push_back(MenuItem("Disconnect", MENU_BLUETOOTH));
  } else {
    currentMenuItems.push_back(MenuItem("Status: Disconnected", MENU_BLUETOOTH));
    currentMenuItems.push_back(MenuItem("Reconnect", MENU_BLUETOOTH));
  }
  currentMenuItems.push_back(MenuItem("Scan Devices", MENU_BT_SCAN));

  menuIndex = 0;
}

void buildBTScanMenu() {
  currentMenuItems.clear();
  for (const auto& name : btFoundDevices) {
    currentMenuItems.push_back(MenuItem(name.c_str(), MENU_BT_SCAN));
  }
  if (btFoundDevices.empty()) {
    currentMenuItems.push_back(MenuItem(
      btScanning ? "Scanning..." : "No devices found", MENU_BT_SCAN, false));
  }
  menuIndex = 0;
}

void navigateToMenu(MenuType menu) {
  // Save current position to stack (for back button)
  if (currentMenu != menu) {
    menuStack.push_back({currentMenu, menuIndex});
    hapticMenuTransition();  // NEW: Haptic on menu change
  }
  
  currentMenu = menu;
  
  switch(menu) {
    case MENU_MAIN:
      buildMainMenu();
      break;
    case MENU_MUSIC:
      buildMusicMenu();
      break;
    case MENU_SETTINGS:
      buildSettingsMenu();
      break;
    case MENU_BLUETOOTH:
      buildBluetoothMenu();
      break;
    case MENU_BT_SCAN:
      buildBTScanMenu();
      break;
    case MENU_ARTIST_LIST:
      // Keep existing artist list
      menuIndex = artistIndex;
      buildAlphaIndex(MENU_ARTIST_LIST);
      break;
    case MENU_ALBUM_LIST:
      menuIndex = albumIndex;
      break;
    case MENU_SONG_LIST:
      menuIndex = songIndex;
      break;
    case MENU_NOW_PLAYING:
      // Just switch to now playing screen
      break;
  }
  
  Serial.printf("Navigated to menu: %d\n", menu);
}

void navigateBack() {
  if (menuStack.empty()) {
    // Already at top, go to main menu
    currentMenu = MENU_MAIN;
    buildMainMenu();
    Serial.println("Back to main menu (stack empty)");
    return;
  }
  
  // Pop last menu from stack
  MenuStackEntry last = menuStack.back();
  menuStack.pop_back();
  
  currentMenu = last.menu;
  menuIndex = last.index;
  
  Serial.printf("Back to menu: %d, index: %d\n", currentMenu, menuIndex);
  
  // Rebuild menu if needed
  switch(currentMenu) {
    case MENU_MAIN:
      buildMainMenu();
      break;
    case MENU_MUSIC:
      buildMusicMenu();
      break;
    case MENU_SETTINGS:
      buildSettingsMenu();
      break;
    case MENU_BLUETOOTH:
      buildBluetoothMenu();
      break;
    case MENU_ARTIST_LIST: buildAlphaIndex(MENU_ARTIST_LIST); break;
    case MENU_ALBUM_LIST:  buildAlphaIndex(MENU_ALBUM_LIST);  break;
    case MENU_SONG_LIST:   buildAlphaIndex(MENU_SONG_LIST);   break;
    default:
      // Other screens don't need rebuilding
      break;
  }
}

void logRamSpace(const char* op) {
  Serial.print("Free heap after ");
  Serial.print(op);
  Serial.print(": ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Free PSRAM after ");
  Serial.print(op);
  Serial.print(": ");
  Serial.println(ESP.getFreePsram());
}