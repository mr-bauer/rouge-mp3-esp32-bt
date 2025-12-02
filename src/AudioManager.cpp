#include "AudioManager.h"
#include "State.h"
#include "Indexer.h"
#include "Navigation.h"
#include "Display.h"

#include "AudioTools.h"
#include "AudioTools/Communication/A2DPStream.h"
#include "AudioTools/Disk/AudioSourceSDFAT.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

#include <SdFat.h>
#include "esp_a2dp_api.h"
// #include <esp_task_wdt.h>

const char *startFilePath = "/";
const char *ext = "mp3";
const int buffer_size = 128 * 1024;

const char *headphoneName = "JBL TUNE235NC TWS";

BufferRTOS<uint8_t> buffer(0);
QueueStream<uint8_t> out(buffer);
MP3DecoderHelix decoder;

AudioSourceSDFAT<SdFat32, File32> source(startFilePath, ext, 32);
AudioPlayer player(source, out, decoder);
BluetoothA2DPSource a2dp;

// State tracking
String last_device_name = headphoneName;
unsigned long last_watchdog_check = 0;

const unsigned long WATCHDOG_INTERVAL = 500;  // ms

// ============================================================================
// AUDIO DATA CALLBACK
// ============================================================================

int32_t get_sound_data(uint8_t* data, int32_t bytes) {
    // CRITICAL: Always return data to keep Bluetooth stack responsive
    // Return silence when not playing or not connected
    if (!bluetoothConnected || player_state != STATE_PLAYING || data == NULL || bytes <= 0) {
        return 0;
    }
    
    // Just read directly - no mutex needed for buffer read
    return buffer.readArray(data, bytes);
}

// ============================================================================
// BLUETOOTH CALLBACKS
// ============================================================================

void connection_state_changed(esp_a2d_connection_state_t state, void* ptr) {
    Serial.printf("[BT] Connection state changed: ");
    
    switch (state) {
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            Serial.println("DISCONNECTED");
            bluetoothConnected = false;
            
            // Stop playback and clear buffer on disconnect
            if (player_state != STATE_STOPPED) {
                Serial.println("[PLAYER] Stopping due to disconnect");
                player_state = STATE_STOPPED;
                buffer.reset();
                delay(10);
                a2dp.disconnect();
                delay(500);
                // Send back to Main Menu
                currentMenu = MENU_MAIN;
                buildMainMenu();
                displayNeedsUpdate = true;
                delay(10);
            }
            break;
            
        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            Serial.println("CONNECTING...");
            break;
            
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            Serial.println("CONNECTED");
            bluetoothConnected = true;
            
            // Save device name for reconnection
            // Note: get_peer_name() may not exist in all versions
            // We'll use the name we connected to instead
            last_device_name = headphoneName;
            Serial.printf("[BT] Connected to: %s\n", last_device_name.c_str());
            break;
            
        case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
            Serial.println("DISCONNECTING...");
            break;
    }
}

void audio_state_changed(esp_a2d_audio_state_t state, void* ptr) {
    Serial.printf("[BT] Audio state changed: ");
    
    switch (state) {
        case ESP_A2D_AUDIO_STATE_STARTED:
            Serial.println("STARTED");
            break;
            
        case ESP_A2D_AUDIO_STATE_STOPPED:
            Serial.println("STOPPED");
            break;
            
        default:
            Serial.println("REMOTE_SUSPEND");
            break;
    }
}

// ============================================================================
// CONNECTION WATCHDOG
// ============================================================================

void checkConnectionWatchdog() {
    if (millis() - last_watchdog_check < WATCHDOG_INTERVAL) {
        return;
    }
    last_watchdog_check = millis();
    
    // Check if actual connection state matches our tracked state
    bool actually_connected = a2dp.is_connected();
    
    if (actually_connected != bluetoothConnected) {
        Serial.println("[WATCHDOG] Connection state mismatch detected!");
        Serial.printf("[WATCHDOG] Tracked: %s, Actual: %s\n", 
                     bluetoothConnected ? "CONNECTED" : "DISCONNECTED",
                     actually_connected ? "CONNECTED" : "DISCONNECTED");
        
        // Force disconnect handling
        if (!actually_connected && bluetoothConnected) {
            connection_state_changed(ESP_A2D_CONNECTION_STATE_DISCONNECTED, nullptr);
        }
    }
}
// ============================================================================
// PLAYBACK CONTROL FUNCTIONS
// ============================================================================

void startPlayback() {
    if (!bluetoothConnected) {
        Serial.println("[ERROR] Not connected to Bluetooth speaker");
        return;
    }
    
    if (player_state == STATE_PLAYING) {
        Serial.println("[PLAYER] Already playing");
        return;
    }

    playCurrentSong(true);
    player_state = STATE_PLAYING;
}

void pausePlayback() {
    if (player_state != STATE_PLAYING) {
        Serial.println("[PLAYER] Not currently playing");
        return;
    }
    
    player_state = STATE_PAUSED;
    Serial.println("[PLAYER] Paused");
    // Note: Audio callback continues returning silence
}

void resumePlayback() {
    if (player_state != STATE_PAUSED) {
        Serial.println("[PLAYER] Not paused");
        return;
    }
    
    if (!bluetoothConnected) {
        Serial.println("[ERROR] Not connected to Bluetooth speaker");
        player_state = STATE_STOPPED;
        return;
    }
    
    player_state = STATE_PLAYING;
    Serial.println("[PLAYER] Resumed");
}

void stopPlayback() {
    if (player_state == STATE_STOPPED) {
        Serial.println("[PLAYER] Already stopped");
        return;
    }
    
    player_state = STATE_STOPPED;
    player.stop();
    buffer.reset();
    Serial.println("[PLAYER] Stopped");
}

// ============================================================================
// BLUETOOTH CONNECTION FUNCTIONS
// ============================================================================

void reconnectBluetooth() {
    if (bluetoothConnected) {
        Serial.println("[BT] Already connected");
        return;
    }
    
    Serial.printf("[BT] Attempting to reconnect to: %s\n", last_device_name.c_str());
    
    // Method 1: Use reconnect() if available
    if (a2dp.reconnect()) {
        Serial.println("[BT] Reconnect initiated");
    } else {
        Serial.println("[BT] Reconnect failed - try changing device name");
    }
}

void disconnectBluetooth() {
    if (!bluetoothConnected) {
        Serial.println("[BT] Not connected");
        return;
    }
    
    Serial.println("[BT] Disconnecting...");
    
    // Stop playback first
    if (player_state != STATE_STOPPED) {
        stopPlayback();
    }
    
    a2dp.disconnect();
}

void changeBluetoothDevice(const String& new_device_name) {
    Serial.printf("[BT] Changing device to: %s\n", new_device_name.c_str());
    
    // Disconnect if connected
    if (bluetoothConnected) {
        disconnectBluetooth();
        delay(1000);  // Wait for clean disconnect
    }
    
    // Update device name
    last_device_name = new_device_name;
    
    // Start connection to new device
    a2dp.start(last_device_name.c_str());
    Serial.println("[BT] Connecting to new device...");
}


void initAudio()
{
    buffer.resize(buffer_size);
    Serial.printf("Audio buffer allocated: %d KB\n", buffer_size / 1024);
    logRamSpace("audio buffer allocation");

    // Configure audio logging (reduce verbosity)
    AudioLogger::instance().begin(Serial, AudioLogger::Warning);

    source.begin();
    out.begin(60);
    player.setDelayIfOutputFull(0);
    player.setVolume(0.4);
    player.setAutoNext(false);
    player.setAutoFade(true);

    // Register callbacks BEFORE starting A2DP
    Serial.println("\n[BT] Configuring Bluetooth A2DP Source...");
    a2dp.set_data_callback(get_sound_data);
    a2dp.set_on_connection_state_changed(connection_state_changed);
    a2dp.set_on_audio_state_changed(audio_state_changed);    

    // Disable auto-reconnect
    a2dp.set_auto_reconnect(false);
    Serial.println("[BT] Auto-reconnect: DISABLED");
    
    a2dp.start(headphoneName);
    Serial.println("✅ A2DP Started!");
    bluetoothConnected = false;
    btStatus = "BT Disconnected";
    
    logRamSpace("A2DP start");
}

void audioLoop()
{
    // Feed buffer when playing
    if (player_state == STATE_PLAYING && bluetoothConnected) {
        // Read from file and decode
        // Process audio
        size_t copied = 0;
        
        try {
            copied = player.copy();
        } catch (...) {
            Serial.println("❌ Audio copy exception!");
            return;
        }
        
        if (copied == 0)
        {
            Serial.println("📀 End of file reached (song finished)");    
            autoNext();
        }
    }
}

void playCurrentSong(bool updateDisplay)
{
    Serial.println("🔍 Starting playback...");
    
    if (!bluetoothConnected) {
        Serial.println("❌ Cannot play - Bluetooth disconnected");
        currentTitle = "BT Disconnected";
        return;
    }
    
    if (songIndex < 0 || songIndex >= (int)songs.size()) {
        Serial.printf("❌ Invalid song index: %d (size: %d)\n", songIndex, songs.size());
        return;
    }
    
    const Song& song = songs[songIndex];
    
    if (song.path.empty()) {
        Serial.println("❌ Empty song path!");
        currentTitle = "Error: No path";
        displayNeedsUpdate = true;
        autoNext();
        return;
    }

    currentTitle = song.title;
    
    Serial.printf("▶️ Playing: %s\n", song.title.c_str());
    Serial.printf("   Path: %s\n", song.path.c_str());

    if (player.isActive()) {
        player.stop();
    }
    
    if (!player.setPath(song.path.c_str())) {
        Serial.printf("❌ Could not open file: %s\n", song.path.c_str());
        currentTitle = "Error: Cannot open";
        displayNeedsUpdate = true;
        autoNext();
        return;
    }
    
    player.play();
    player_state = STATE_PLAYING;
    
    Serial.println("✅ Playback started");
    
    if (currentMenu == MENU_MAIN) {
        buildMainMenu();
    }
    
    if (updateDisplay) {
        displayNeedsUpdate = true;
    }
}
