#ifndef ROUGE_PREFERENCES_H
#define ROUGE_PREFERENCES_H

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string>

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

    // BT device name
    void saveBTDevice(const char* name);
    String loadBTDevice();  // returns "" if not saved

    // Last-played position
    void saveLastPlayed(int artistIdx, int albumIdx, int songIdx, const std::string& artist, const std::string& album);
    void loadLastPlayed(int& artistIdx, int& albumIdx, int& songIdx, std::string& artist, std::string& album);

    // Resume on boot toggle
    void saveResumeOnBoot(bool enabled);
    bool loadResumeOnBoot();

private:
    nvs_handle_t nvsHandle;
    bool isOpen;
};

extern RougePreferences rougePrefs;

#endif