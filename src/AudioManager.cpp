#include "AudioManager.h"
#include "State.h"
#include "Indexer.h"
#include "Navigation.h"
#include "Display.h"

#include "AudioTools.h"
#include "AudioTools/Communication/A2DPStream.h"
#include "AudioTools/Disk/AudioSourceSDFAT.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecAACFDK.h"  // NEW - AAC decoder

#include <SdFat.h>
#include "esp_a2dp_api.h"

const char *startFilePath = "/";
const char *ext = "mp3";  // Will be changed dynamically
const int buffer_size = 128 * 1024;

const char *headphoneName = "JBL TUNE235NC TWS";

BufferRTOS<uint8_t> buffer(0);
QueueStream<uint8_t> out(buffer);
MP3DecoderHelix mp3Decoder;
AACDecoderFDK aacDecoder;  // NEW - AAC decoder instance

AudioSourceSDFAT<SdFat32, File32> source(startFilePath, ext, 32);
AudioPlayer player(source, out, mp3Decoder);  // Will use mp3Decoder by default
BluetoothA2DPSource a2dp;

// State tracking
String last_device_name = headphoneName;
unsigned long last_watchdog_check = 0;

const unsigned long WATCHDOG_INTERVAL = 500;

// Current file type tracking - NEW
enum FileType {
    FILE_MP3,
    FILE_AAC
};
FileType currentFileType = FILE_MP3;

// ============================================================================
// AUDIO DATA CALLBACK
// ============================================================================

int32_t get_sound_data(uint8_t* data, int32_t bytes) {
    if (!bluetoothConnected || player_state != STATE_PLAYING || data == NULL || bytes <= 0) {
        return 0;
    }
    
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
            
            if (player_state != STATE_STOPPED) {
                Serial.println("[PLAYER] Stopping due to disconnect");
                player_state = STATE_STOPPED;
                buffer.reset();
                delay(10);
                a2dp.disconnect();
                delay(500);
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
// FILE TYPE DETECTION - NEW
// ============================================================================

FileType detectFileType(const char* path) {
    String pathStr = String(path);
    pathStr.toLowerCase();
    
    if (pathStr.endsWith(".m4a") || pathStr.endsWith(".mp4") || pathStr.endsWith(".aac")) {
        return FILE_AAC;
    }
    
    return FILE_MP3;  // Default to MP3
}

// ============================================================================
// CONNECTION WATCHDOG
// ============================================================================

void checkConnectionWatchdog() {
    if (millis() - last_watchdog_check < WATCHDOG_INTERVAL) {
        return;
    }
    last_watchdog_check = millis();
    
    bool actually_connected = a2dp.is_connected();
    
    if (actually_connected != bluetoothConnected) {
        Serial.println("[WATCHDOG] Connection state mismatch detected!");
        Serial.printf("[WATCHDOG] Tracked: %s, Actual: %s\n", 
                     bluetoothConnected ? "CONNECTED" : "DISCONNECTED",
                     actually_connected ? "CONNECTED" : "DISCONNECTED");
        
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
    
    if (player_state != STATE_STOPPED) {
        stopPlayback();
    }
    
    a2dp.disconnect();
}

void changeBluetoothDevice(const String& new_device_name) {
    Serial.printf("[BT] Changing device to: %s\n", new_device_name.c_str());
    
    if (bluetoothConnected) {
        disconnectBluetooth();
        delay(1000);
    }
    
    last_device_name = new_device_name;
    a2dp.start(last_device_name.c_str());
    Serial.println("[BT] Connecting to new device...");
}

// ============================================================================
// AUDIO INITIALIZATION
// ============================================================================

void initAudio()
{
    buffer.resize(buffer_size);
    Serial.printf("Audio buffer allocated: %d KB\n", buffer_size / 1024);
    logRamSpace("audio buffer allocation");

    AudioLogger::instance().begin(Serial, AudioLogger::Warning);

    source.begin();
    out.begin(60);
    player.setDelayIfOutputFull(0);
    player.setVolume(0.4);
    player.setAutoNext(false);
    player.setAutoFade(true);

    Serial.println("\n[BT] Configuring Bluetooth A2DP Source...");
    a2dp.set_data_callback(get_sound_data);
    a2dp.set_on_connection_state_changed(connection_state_changed);
    a2dp.set_on_audio_state_changed(audio_state_changed);    

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
    if (player_state == STATE_PLAYING && bluetoothConnected) {
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

// ============================================================================
// PLAY CURRENT SONG - WITH AAC SUPPORT
// ============================================================================

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

    // Detect file type - NEW
    FileType newFileType = detectFileType(song.path.c_str());
    
    if (newFileType != currentFileType) {
        Serial.println("[DECODER] Switching decoder...");
        
        // Stop current playback
        if (player.isActive()) {
            player.stop();
        }
        
        // Switch decoder
        if (newFileType == FILE_AAC) {
            Serial.println("[DECODER] Using AAC decoder");
            player.setDecoder(aacDecoder);
        } else {
            Serial.println("[DECODER] Using MP3 decoder");
            player.setDecoder(mp3Decoder);
        }
        
        currentFileType = newFileType;
    }

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
    Serial.printf("   Format: %s\n", currentFileType == FILE_AAC ? "AAC" : "MP3");
    
    if (currentMenu == MENU_MAIN) {
        buildMainMenu();
    }
    
    if (updateDisplay) {
        displayNeedsUpdate = true;
    }
}