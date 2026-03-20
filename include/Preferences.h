#ifndef ROUGE_PREFERENCES_H
#define ROUGE_PREFERENCES_H

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string>
#include <vector>

// Preference keys
#define PREF_NAMESPACE "rouge"
#define PREF_VOLUME "volume"

class RougePreferences {
public:
    RougePreferences();
    ~RougePreferences();
    
    bool begin();
    void end();
    
    // Volume
    void saveVolume(int volume);
    int loadVolume();
    
    // Brightness
    void saveBrightness(int brightness);
    int loadBrightness();

    // Text size
    void saveTextSize(int size);
    int loadTextSize();

    // Theme
    void saveTheme(int theme);
    int loadTheme();

    // BT device name (last connected)
    void saveBTDevice(const char* name);
    String loadBTDevice();  // returns "" if not saved

    // BT saved device list (up to 5, most recent first)
    // mac may be nullptr if unknown; existing saved MAC for that device is preserved
    void addBTDevice(const std::string& name, const uint8_t* mac = nullptr);
    std::vector<std::string> loadBTDeviceList();
    // Parallel MAC list (12-char hex each, "" if unknown), same order as loadBTDeviceList()
    std::vector<std::string> loadBTMACList();

    // Last-played position
    void saveLastPlayed(int artistIdx, int albumIdx, int songIdx, const std::string& artist, const std::string& album);
    void loadLastPlayed(int& artistIdx, int& albumIdx, int& songIdx, std::string& artist, std::string& album);

    // Resume on boot toggle
    void saveResumeOnBoot(bool enabled);
    bool loadResumeOnBoot();

    // Shuffle mode (0=off, 1=song, 2=library)
    void saveShuffle(int mode);
    int  loadShuffle();

    // Haptics enabled
    void saveHaptics(bool enabled);
    bool loadHaptics();

private:
    nvs_handle_t nvsHandle;
    bool isOpen;
};

extern RougePreferences rougePrefs;

#endif