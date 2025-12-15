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