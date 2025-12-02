#ifndef STATE_H
#define STATE_H

#include <vector>
#include <string>
#include <ArduinoJson.h>

// Bluetooth status
extern std::string btStatus;

// Custom allocator for PSRAM
struct SpiRamAllocator
{
  void *allocate(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
  void deallocate(void *pointer) { heap_caps_free(pointer); }
  void *reallocate(void *ptr, size_t new_size) { return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM); }
};
using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;
extern SpiRamJsonDocument *indexDoc;

// Menu system
enum MenuType {
  MENU_MAIN,           // Top-level menu
  MENU_MUSIC,          // Music submenu
  MENU_SETTINGS,       // Settings submenu
  MENU_BLUETOOTH,      // Bluetooth submenu
  MENU_ARTIST_LIST,    // Artist browser
  MENU_ALBUM_LIST,     // Album browser
  MENU_SONG_LIST,      // Song browser
  MENU_NOW_PLAYING     // Playback screen
};

struct MenuItem {
  std::string label;
  MenuType action;     // Where this item leads
  bool enabled;        // Can be disabled (grayed out)
  
  MenuItem(const char* lbl, MenuType act, bool en = true) 
    : label(lbl), action(act), enabled(en) {}
};

// Current menu state
extern MenuType currentMenu;
extern std::vector<MenuItem> currentMenuItems;
extern int menuIndex;  // Cursor position in current menu

// Navigation history (for back button)
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
  std::string title;
  std::string path;
  int track;
  int duration;
  
  Song() : track(0), duration(0) {}
};

// Library data (used in MENU_ARTIST_LIST, etc.)
extern std::vector<std::string> artists;
extern std::vector<std::string> albums;
extern std::vector<Song> songs;

// Navigation indices for music browser
extern volatile int artistIndex;
extern volatile int albumIndex;
extern volatile int songIndex;

// Playback state
enum PlayerState { STATE_STOPPED, STATE_PLAYING, STATE_PAUSED };
extern volatile PlayerState player_state;
extern bool bluetoothConnected;

// Menu functions
void buildMainMenu();
void buildMusicMenu();
void buildSettingsMenu();
void buildBluetoothMenu();
void navigateToMenu(MenuType menu);
void navigateBack();

// Utility function
void logRamSpace(const char* op);

#endif