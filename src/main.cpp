#include <Arduino.h>
#include <SdFat.h>
#include <esp_task_wdt.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"

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
#include "Battery.h"

const int cs = 32;

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\n🎧 Rouge MP3 Player starting...");

    // Release GPIO holds and RTC config that persist from deep sleep entry
    gpio_hold_dis(GPIO_NUM_7);   // backlight hold — non-RTC GPIO hold survives deep sleep wake, must release before LEDC init
    rtc_gpio_deinit(GPIO_NUM_4); // CENTER button — return from RTC GPIO mode to normal digital input
    
    Serial.println("✅ Watchdog enabled");

    logRamSpace("initial load");

    // Initialize preferences FIRST
    if (!rougePrefs.begin()) {
        Serial.println("⚠️  Preferences init failed, using defaults");
    }

    // Load saved preferences
    screenBrightness = rougePrefs.loadBrightness();
    Serial.printf("💾 Loaded brightness: %d\n", screenBrightness);
    textSizePreference = rougePrefs.loadTextSize();
    Serial.printf("💾 Loaded text size: %d\n", textSizePreference);
    themeIndex = rougePrefs.loadTheme();
    Serial.printf("💾 Loaded theme: %d\n", themeIndex);
    shuffleMode = rougePrefs.loadShuffle();
    hapticsEnabled = rougePrefs.loadHaptics();
    Serial.printf("💾 Loaded shuffle: %d\n", shuffleMode);
    btSavedDevices = rougePrefs.loadBTDeviceList();
    Serial.printf("💾 Loaded %d saved BT device(s)\n", (int)btSavedDevices.size());

    // Initialize hardware modules
    initDisplay();
    initHaptics();
    initButtons();
    initEncoder();
    initBattery();

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
        drawCenteredText(display, "SD Card Error", SCREEN_HEIGHT / 2);
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
        drawCenteredText(display, "Database Error", SCREEN_HEIGHT / 2);
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
        drawCenteredText(display, "No Artists", SCREEN_HEIGHT / 2);
        return;
    }

    // Restore last-played position
    resumeOnBoot = rougePrefs.loadResumeOnBoot();
    if (resumeOnBoot) {
        rougePrefs.loadLastPlayed(playingArtistIndex, playingAlbumIndex, playingSongIndex,
                                   playingArtist, playingAlbum);
        if (!playingArtist.empty()) {
            Serial.printf("💾 Restored last played: %s / %s / song %d\n",
                          playingArtist.c_str(), playingAlbum.c_str(), playingSongIndex);
            // Restore browse state so Play button works immediately on boot
            currentArtist = playingArtist;
            currentAlbum  = playingAlbum;
            artistIndex   = playingArtistIndex;
            if (buildAlbumList(playingArtist)) {
                albumIndex = playingAlbumIndex;
                if (buildSongList(playingArtist, playingAlbum)) {
                    songIndex = playingSongIndex;
                }
            }
        }
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

    // Initialize inactivity timer so device doesn't dim immediately on boot
    lastActivityTime = millis();
}

static unsigned long sleepStartMs = 0;  // millis() when current sleep session began

void loop()
{
    esp_task_wdt_reset();

    // ── SCREEN-OFF MODE: skip non-essential work ───────────────────────────
    if (screenIsFullyOff) {
        if (sleepStartMs == 0) {
            sleepStartMs = millis();
            Serial.println("[SLEEP] Screen off — entering sleep mode");
        }

        // Tiered BT disconnect: after 15 min of screen-off and not playing, drop A2DP connection
        if (!btDisconnectedBySleep && bluetoothConnected &&
            player_state != STATE_PLAYING &&
            millis() - sleepStartMs >= BT_SLEEP_TIMEOUT_MS) {
            disconnectBluetooth();
            btDisconnectedBySleep = true;
            Serial.println("[SLEEP] BT disconnected by 15-min sleep timer");
            return; // don't evaluate deep sleep on the same iteration as BT disconnect
        }

        // Deep sleep: 15 min screen-off + not playing → full power-off until CENTER press
        if (millis() - sleepStartMs >= BT_SLEEP_TIMEOUT_MS && player_state != STATE_PLAYING) {
            Serial.println("[SLEEP] Entering deep sleep — press CENTER to wake");
            Serial.flush();
            // Block the display task (Core 0) so it can't render during sleep entry
            xSemaphoreTake(displayMutex, portMAX_DELAY);
            // Clear display to black to prevent burn-in if backlight stays on during sleep
            sprite.fillScreen(0x0000);
            sprite.pushSprite(0, 0);
            // Force backlight LOW and hold — detach LEDC first so GPIO matrix fully owns the pin
            ledcDetachPin(TFT_BL);
            pinMode(TFT_BL, OUTPUT);
            digitalWrite(TFT_BL, LOW);
            gpio_hold_en(GPIO_NUM_7);
            gpio_deep_sleep_hold_en();
            // Wake on CENTER button (GPIO4, active-low)
            rtc_gpio_pullup_en(GPIO_NUM_4);
            rtc_gpio_pulldown_dis(GPIO_NUM_4);
            esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);
            esp_deep_sleep_start();
        }

        // audioLoop needed for playStopRequested handling and buffer feed when playing
        audioLoop();
        pollButtons();

        static unsigned long lastSleepBattMs = 0;
        if (millis() - lastSleepBattMs >= 30000) {
            updateBattery();
            lastSleepBattMs = millis();
        }

        return;
    }

    // ── NORMAL MODE ─────────────────────────────────────────────────────────

    // Waking from sleep: reset session timer; auto-reconnect BT if needed
    if (sleepStartMs != 0) {
        sleepStartMs = 0;
        if (btDisconnectedBySleep) {
            btDisconnectedBySleep = false;
            if (!bluetoothConnected) {
                Serial.println("[SLEEP] Waking — auto-reconnecting BT");
                reconnectBluetooth();
            }
        }
    }

    audioLoop();
    updateBattery();
    updateEncoder();
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