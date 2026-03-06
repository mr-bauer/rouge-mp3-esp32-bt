#include "Preferences.h"

RougePreferences rougePrefs;

RougePreferences::RougePreferences() : nvsHandle(0), isOpen(false) {}

RougePreferences::~RougePreferences() {
    end();
}

bool RougePreferences::begin() {
    if (isOpen) return true;
    
    Serial.println("💾 Opening preferences...");
    
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated, erase and reinitialize
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    if (err != ESP_OK) {
        Serial.printf("❌ NVS init failed: %d\n", err);
        return false;
    }
    
    // Open NVS handle
    err = nvs_open(PREF_NAMESPACE, NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("❌ Failed to open NVS handle: %d\n", err);
        return false;
    }
    
    isOpen = true;
    Serial.println("✅ Preferences opened");
    return true;
}

void RougePreferences::end() {
    if (isOpen) {
        nvs_close(nvsHandle);
        isOpen = false;
        nvsHandle = 0;
        Serial.println("💾 Preferences closed");
    }
}

void RougePreferences::saveVolume(int volume) {
    if (!isOpen) {
        Serial.println("⚠️  Preferences not open, can't save volume");
        return;
    }
    
    // Clamp to valid range
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    
    esp_err_t err = nvs_set_i32(nvsHandle, PREF_VOLUME, volume);
    if (err != ESP_OK) {
        Serial.printf("❌ Failed to save volume: %d\n", err);
        return;
    }
    
    // Commit changes
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("❌ Failed to commit volume: %d\n", err);
        return;
    }
    
    Serial.printf("💾 Volume saved: %d%%\n", volume);
}

int RougePreferences::loadVolume() {
    if (!isOpen) {
        Serial.println("⚠️  Preferences not open, using default volume");
        return 50;  // Default
    }
    
    int32_t volume = 50;  // Default value
    esp_err_t err = nvs_get_i32(nvsHandle, PREF_VOLUME, &volume);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        Serial.println("💾 No saved volume found, using default: 50%");
        return 50;
    } else if (err != ESP_OK) {
        Serial.printf("❌ Failed to load volume: %d, using default\n", err);
        return 50;
    }
    
    // Validate range
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    
    Serial.printf("💾 Volume loaded: %d%%\n", (int)volume);
    return (int)volume;
}

// Brightness functions - NEW
void RougePreferences::saveBrightness(int brightness) {
    if (!isOpen) return;
    
    esp_err_t err = nvs_set_i32(nvsHandle, "brightness", brightness);
    if (err != ESP_OK) {
        Serial.printf("⚠️  Failed to save brightness: %d\n", err);
        return;
    }
    
    nvs_commit(nvsHandle);
}

int RougePreferences::loadBrightness() {
    if (!isOpen) return 255;  // Default full brightness
    
    int32_t brightness = 255;
    esp_err_t err = nvs_get_i32(nvsHandle, "brightness", &brightness);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        saveBrightness(255);
        return 255;
    }
    
    if (err != ESP_OK) {
        Serial.printf("⚠️  Failed to load brightness: %d\n", err);
        return 255;
    }
    
    return (int)brightness;
}

// Text size functions
void RougePreferences::saveTextSize(int size) {
    if (!isOpen) return;

    esp_err_t err = nvs_set_i32(nvsHandle, "textSize", size);
    if (err != ESP_OK) {
        Serial.printf("⚠️  Failed to save text size: %d\n", err);
        return;
    }

    nvs_commit(nvsHandle);
}

int RougePreferences::loadTextSize() {
    if (!isOpen) return 2;

    int32_t size = 2;
    esp_err_t err = nvs_get_i32(nvsHandle, "textSize", &size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        saveTextSize(2);
        return 2;
    }

    if (err != ESP_OK) {
        Serial.printf("⚠️  Failed to load text size: %d\n", err);
        return 2;
    }

    return (size >= 1 && size <= 3) ? (int)size : 2;
}

// Theme functions
void RougePreferences::saveTheme(int theme) {
    if (!isOpen) return;
    nvs_set_i32(nvsHandle, "theme", theme);
    nvs_commit(nvsHandle);
}

int RougePreferences::loadTheme() {
    if (!isOpen) return 0;

    int32_t theme = 0;
    esp_err_t err = nvs_get_i32(nvsHandle, "theme", &theme);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        saveTheme(0);
        return 0;
    }

    if (err != ESP_OK) {
        Serial.printf("⚠️  Failed to load theme: %d\n", err);
        return 0;
    }

    return (theme == 0 || theme == 1) ? (int)theme : 0;
}

// Last-played position
void RougePreferences::saveLastPlayed(int artistIdx, int albumIdx, int songIdx,
                                       const std::string& artist, const std::string& album) {
    if (!isOpen) return;
    nvs_set_i32(nvsHandle, "lastArtIdx", artistIdx);
    nvs_set_i32(nvsHandle, "lastAlbIdx", albumIdx);
    nvs_set_i32(nvsHandle, "lastSngIdx", songIdx);
    nvs_set_str(nvsHandle, "lastArtist", artist.c_str());
    nvs_set_str(nvsHandle, "lastAlbum",  album.c_str());
    nvs_commit(nvsHandle);
}

void RougePreferences::loadLastPlayed(int& artistIdx, int& albumIdx, int& songIdx,
                                       std::string& artist, std::string& album) {
    if (!isOpen) return;
    int32_t ai = 0, ali = 0, si = 0;
    nvs_get_i32(nvsHandle, "lastArtIdx", &ai);
    nvs_get_i32(nvsHandle, "lastAlbIdx", &ali);
    nvs_get_i32(nvsHandle, "lastSngIdx", &si);
    artistIdx = (int)ai;
    albumIdx  = (int)ali;
    songIdx   = (int)si;
    char buf[128] = {};
    size_t len = sizeof(buf);
    if (nvs_get_str(nvsHandle, "lastArtist", buf, &len) == ESP_OK) artist = buf;
    len = sizeof(buf);
    if (nvs_get_str(nvsHandle, "lastAlbum",  buf, &len) == ESP_OK) album  = buf;
}

// Resume on boot toggle
void RougePreferences::saveResumeOnBoot(bool enabled) {
    if (!isOpen) return;
    nvs_set_i32(nvsHandle, "resumeOnBoot", enabled ? 1 : 0);
    nvs_commit(nvsHandle);
}

bool RougePreferences::loadResumeOnBoot() {
    if (!isOpen) return true;
    int32_t val = 1;
    nvs_get_i32(nvsHandle, "resumeOnBoot", &val);
    return val != 0;
}

// BT device name functions
void RougePreferences::saveBTDevice(const char* name) {
    if (!isOpen || !name) return;
    nvs_set_str(nvsHandle, "btDevice", name);
    nvs_commit(nvsHandle);
}

String RougePreferences::loadBTDevice() {
    if (!isOpen) return "";
    char buf[64] = {};
    size_t len = sizeof(buf);
    esp_err_t err = nvs_get_str(nvsHandle, "btDevice", buf, &len);
    if (err != ESP_OK) return "";
    return String(buf);
}