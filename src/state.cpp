#include "State.h"
#include "Haptics.h"
#include <Arduino.h>

// Bluetooth status
std::string btStatus = "BT Ready";

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

// Navigation indices
volatile int artistIndex = 0;
volatile int albumIndex = 0;
volatile int songIndex = 0;

// Playback state
volatile PlayerState player_state = STATE_STOPPED;
bool bluetoothConnected = false;

// Volume control - NEW
int currentVolume = 50;  // Start at 50%
bool volumeControlActive = false;
unsigned long lastVolumeChange = 0;

// Battery monitoring - NEW
float batteryVoltage = 0.0f;
int batteryPercent = 0;
bool batteryCharging = false;
unsigned long lastBatteryCheck = 0;

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
    case MENU_ARTIST_LIST:
      // Keep existing artist list
      menuIndex = artistIndex;
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
    default:
      // Music browser screens don't need rebuilding
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