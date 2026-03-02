#ifndef STATE_H
#define STATE_H

#include <vector>
#include <string>

// ============================================================================
// TIMING CONSTANTS
// ============================================================================

// Volume control
#define VOLUME_TIMEOUT 2000              // 2 seconds to return to song display
#define VOLUME_ACTIVATION_TICKS 3        // Ticks needed to enter volume mode
#define VOLUME_SAVE_DELAY 3000           // Save 3 seconds after last change

// Brightness control
#define BRIGHTNESS_TIMEOUT 3000          // 3 seconds to save and exit
#define BRIGHTNESS_ACTIVATION_TICKS 2    // Ticks to enter brightness mode

// Battery monitoring
#define BATTERY_CHECK_INTERVAL 5000      // Check every 5 seconds

// Display updates
#define DISPLAY_HEADER_UPDATE_INTERVAL 5000  // Update header every 5 seconds

// Encoder
#define ENCODER_UPDATE_INTERVAL 90       // Throttle encoder updates (ms)
#define ENCODER_JUMP_THRESHOLD 3         // Anti-jump protection
#define ENCODER_DIRECTION_HISTORY_SIZE 5 // Direction filtering samples
#define ENCODER_DIRECTION_LOCK_THRESHOLD 3  // Steps before locking direction

// Button timing
#define BUTTON_DEBOUNCE_MS 300           // General button debounce
#define BUTTON_MIN_DURATION_ADC 10       // Min press duration for ADC pins
#define BUTTON_SUPPRESS_TIME 300         // Suppress buttons during scroll

// Bluetooth
#define BT_WATCHDOG_INTERVAL 500         // Connection watchdog check (ms)

// Watchdog
#define WDT_TIMEOUT 30                   // Watchdog timeout (seconds)

// Sleep / dim
#define DIM_TIMEOUT_MS      30000UL      // 30 s no input → dim display
#define SLEEP_TIMEOUT_MS   300000UL      // 5 min no input + stopped/paused → deep sleep
#define DIM_BRIGHTNESS         15        // dimmed brightness level (0-255, ~6%)
#define DIM_STEP_DOWN           4        // brightness units per 50 ms tick while dimming (~3 s fade)
#define DIM_STEP_UP            20        // brightness units per 50 ms tick while restoring (~0.6 s)

// ============================================================================
// BLUETOOTH STATUS
// ============================================================================
extern std::string btStatus;
extern volatile bool btScanning;                  // true while BT inquiry is running
extern std::vector<std::string> btFoundDevices;   // device names collected during scan

// Menu system
enum MenuType {
  MENU_MAIN,
  MENU_MUSIC,
  MENU_SETTINGS,
  MENU_BLUETOOTH,
  MENU_BT_SCAN,       // BT device scan results screen
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
  std::string displayTitle;  // Truncated title (for list display)
  std::string path;
  int track;
  int duration;

  // M4A playback metadata (from DB — avoids file pre-scan at playback time)
  uint64_t mdatStart   = 0;  // byte offset where audio data begins (after mdat header)
  uint64_t stszOffset  = 0;  // byte offset of stsz box
  uint32_t sampleCount = 0;  // total number of audio samples
  uint32_t fixedSize   = 0;  // fixed sample size (0 = variable)
  int      aacProfile  = 2;  // AAC audio object type (2 = LC)
  int      aacSrIdx    = 4;  // sample rate index (4 = 44100 Hz)
  int      aacChCfg    = 2;  // channel config (2 = stereo)

  // M4A cover art location (populated by indexer for files with JPEG embedded art)
  uint64_t covrOffset  = 0;  // byte offset of JPEG data in .m4a file (0 = no art)
  uint32_t covrSize    = 0;  // byte count of JPEG data

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

// Sleep / dim state
extern volatile unsigned long lastActivityTime;  // millis() of last button/encoder input
extern volatile bool          isScreenDimmed;    // true while display is dimmed

// Progress tracking (set/reset by AudioManager)
extern volatile unsigned long playbackStartMillis;  // millis() when current song started
extern volatile unsigned long totalPausedMs;         // accumulated pause duration for this song
extern volatile unsigned long pauseStartMillis;      // millis() when current pause began (0 if playing)

// Display control - NEW
extern bool forceDisplayRedraw;
extern int textSizePreference;  // 1 (small/6x8px) or 2 (large/12x16px)
extern int themeIndex;          // 0 = dark, 1 = light
extern bool albumArtAvailable;

// Menu functions
void buildMainMenu();
void buildMusicMenu();
void buildSettingsMenu();
void buildBluetoothMenu();
void buildBTScanMenu();
void navigateToMenu(MenuType menu);
void navigateBack();

// Utility
void logRamSpace(const char* op);

#endif