#include <Arduino.h>
#include <SdFat.h>
#include <esp_task_wdt.h>

// Include all project modules
#include "Display.h"
#include "Buttons.h"
#include "EncoderModule.h"
#include "Indexer.h"
#include "Navigation.h"
#include "AudioManager.h"
#include "Spinner.h"
#include "State.h"
#include "Haptics.h"
#include "Database.h"
#include "Preferences.h"

#define WDT_TIMEOUT 30
const int cs = 32;

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\n🎧 Rouge MP3 Player starting...");
    
    Serial.println("✅ Watchdog enabled");

    logRamSpace("initial load");

    // Initialize preferences FIRST
    if (!rougePrefs.begin()) {
        Serial.println("⚠️  Preferences init failed, using defaults");
    }

    // Initialize hardware modules
    initDisplay();
    initHaptics();
    initButtons();
    initEncoder();

    // Show loading animation
    startLoadingAnimation();
    delay(500);

    // Initialize SD card
    if (!sd.begin(cs, SD_SCK_MHZ(25)))
    {
        Serial.println("❌ SD initialization failed!");
        stopLoadingAnimation();
        delay(100);
        display.fillScreen(COLOR_BG);
        display.setTextColor(COLOR_TEXT);
        drawCenteredText("SD Card Error", SCREEN_HEIGHT / 2);
        return;
    }
    Serial.println("✅ SD initialized");
    logRamSpace("SD Init");

    // Load database (replaces loadIndex())
    if (!loadDatabase())
    {
        Serial.println("❌ Database initialization failed!");
        stopLoadingAnimation();
        delay(100);
        display.fillScreen(COLOR_BG);
        display.setTextColor(COLOR_TEXT);
        drawCenteredText("Database Error", SCREEN_HEIGHT / 2);
        display.setTextSize(1);
        display.setCursor(10, SCREEN_HEIGHT / 2 + 30);
        display.println("Run indexer tool");
        return;
    }
    Serial.println("✅ Database initialized");
    logRamSpace("Database load");

    // Build artist list
    if (!buildArtistList()) {
        Serial.println("❌ Failed to build artist list!");
        stopLoadingAnimation();
        delay(100);
        display.fillScreen(COLOR_BG);
        display.setTextColor(COLOR_TEXT);
        drawCenteredText("No Artists", SCREEN_HEIGHT / 2);
        return;
    }

    // Stop loading animation
    stopLoadingAnimation();
    delay(200);
    
    if (xSemaphoreTake(displayMutex, 100 / portTICK_PERIOD_MS)) {
        display.fillScreen(COLOR_BG);
        xSemaphoreGive(displayMutex);
    }
    
    // Initialize audio system
    initAudio();
    
    // Build and show main menu
    buildMainMenu();
    navigateToMenu(MENU_MAIN);
    displayNeedsUpdate = true;
    delay(200);

    Serial.println("✅ Setup complete!");
    Serial.println("==========================================");
    logRamSpace("setup complete");

    // Enable watchdog timer
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);
}

void loop()
{
    // Feed the watchdog
    esp_task_wdt_reset();

    // CORE 1: Audio processing - highest priority
    audioLoop();

    // Encoder updates (lightweight)
    updateEncoder();

    // Button processing
    pollButtons();

    #ifdef DEBUG
    // Monitor heap periodically (debug builds only)
    static unsigned long lastHeapCheck = 0;
    static size_t minHeap = 999999;
    static size_t minPSRAM = 999999;

    if (millis() - lastHeapCheck > 10000)  // Every 10 seconds
    {
        size_t freeHeap = ESP.getFreeHeap();
        size_t freePSRAM = ESP.getFreePsram();

        if (freeHeap < minHeap) {
            minHeap = freeHeap;
            Serial.printf("⚠️  New heap low: %u bytes\n", minHeap);
        }
        
        if (freePSRAM < minPSRAM) {
            minPSRAM = freePSRAM;
            Serial.printf("⚠️  New PSRAM low: %u bytes\n", minPSRAM);
        }

        Serial.printf("Heap: %u (min: %u), PSRAM: %u (min: %u)\n", 
                      freeHeap, minHeap, freePSRAM, minPSRAM);
        lastHeapCheck = millis();
    }
    #endif
}