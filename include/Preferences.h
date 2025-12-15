#ifndef ROUGE_PREFERENCES_H
#define ROUGE_PREFERENCES_H

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>

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
    
    // Brightness - NEW
    void saveBrightness(int brightness);
    int loadBrightness();
    
private:
    nvs_handle_t nvsHandle;
    bool isOpen;
};

extern RougePreferences rougePrefs;

#endif