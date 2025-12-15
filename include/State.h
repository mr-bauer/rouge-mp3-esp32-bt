#ifndef STATE_H
#define STATE_H

#include <vector>
#include <string>

// Bluetooth status
extern std::string btStatus;

// Menu system
enum MenuType {
  MENU_MAIN,
  MENU_MUSIC,
  MENU_SETTINGS,
  MENU_BLUETOOTH,
  MENU_ARTIST_LIST,
  MENU_ALBUM_LIST,
  MENU_SONG_LIST,
  MENU_NOW_PLAYING
};

struct MenuItem {
  std::string label;
  MenuType action;
  bool enabled;
  
  MenuItem(const char* lbl, MenuType act, bool en = true) 
    : label(lbl), action(act), enabled(en) {}
};

// Current menu state
extern MenuType currentMenu;
extern std::vector<MenuItem> currentMenuItems;
extern int menuIndex;

// Navigation history
struct MenuStackEntry {
  MenuType menu;
  int index;
};
extern std::vector<MenuStackEntry> menuStack;

// Currently playing info
extern std::string currentArtist;
extern std::string currentAlbum;
extern std::string currentTitle;

// Song structure
struct Song
{
  std::string title;         // Full title (for Now Playing)
  std::string displayTitle;  // Truncated title (for list display) - NEW
  std::string path;
  int track;
  int duration;
  
  Song() : track(0), duration(0) {}
};

// Library data
extern std::vector<std::string> artists;
extern std::vector<std::string> albums;
extern std::vector<Song> songs;

// Navigation indices
extern volatile int artistIndex;
extern volatile int albumIndex;
extern volatile int songIndex;

// Playback state
enum PlayerState { STATE_STOPPED, STATE_PLAYING, STATE_PAUSED };
extern volatile PlayerState player_state;
extern bool bluetoothConnected;

// Volume control
extern int currentVolume;  // 0-100
extern bool volumeControlActive;
extern unsigned long lastVolumeChange;
#define VOLUME_TIMEOUT 2000  // 2 seconds to return to song display
#define VOLUME_ACTIVATION_TICKS 3  // Ticks needed to enter volume mode
#define VOLUME_SAVE_DELAY 3000  // Save 3 seconds after last change

// Battery monitoring
extern float batteryVoltage;
extern int batteryPercent;
extern bool batteryCharging;
extern unsigned long lastBatteryCheck;
#define BATTERY_CHECK_INTERVAL 5000  // Check every 5 seconds

extern int screenBrightness;
extern bool brightnessControlActive;
extern unsigned long lastBrightnessChange;
#define BRIGHTNESS_TIMEOUT 3000
#define BRIGHTNESS_ACTIVATION_TICKS 2

// Display control - NEW
extern bool forceDisplayRedraw;

// Menu functions
void buildMainMenu();
void buildMusicMenu();
void buildSettingsMenu();
void buildBluetoothMenu();
void navigateToMenu(MenuType menu);
void navigateBack();

// Utility
void logRamSpace(const char* op);

#endif