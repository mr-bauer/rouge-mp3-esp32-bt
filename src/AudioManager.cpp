#include "AudioManager.h"
#include "State.h"
#include "Indexer.h"
#include "Navigation.h"
#include "Display.h"
#include "Preferences.h"
#include "AlbumArt.h"

#include "AudioTools.h"
#include "AudioTools/Communication/A2DPStream.h"
#include "AudioTools/Disk/AudioSourceSDFAT.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecAACHelix.h"
#include "AudioTools/AudioCodecs/M4ACommonDemuxer.h"

#include <SdFat.h>
#include "esp_a2dp_api.h"

// ============================================================================
// M4A FILE DEMUXER — SdFat File32 based, handles both faststart and
// non-faststart M4A files via random file access (avoids the library's
// assert(written == box.available) that fires when mdat precedes stsz).
// ============================================================================
using namespace audio_tools;

class M4AFile32Demuxer : public M4ACommonDemuxer {
public:
    M4AFile32Demuxer() { setupParser(); }

    // Open using pre-stored layout metadata from the DB (no file scan).
    // Falls back to full open() scan if metadata is missing/invalid.
    bool openWithMeta(File32& file, AACDecoderHelix& dec, const Song& song) {
        if (song.mdatStart == 0 || song.stszOffset == 0 || song.sampleCount == 0) {
            Serial.println("   ⚠️ No DB metadata — falling back to full M4A scan");
            return open(file, dec);
        }

        p_file         = &file;
        p_dec          = &dec;
        mdat_start     = song.mdatStart;
        mdat_cur       = song.mdatStart;
        samp_idx       = 0;
        fixed_size     = song.fixedSize;
        sample_count   = song.sampleCount;
        stsz_offset    = (uint32_t)song.stszOffset;
        stsz_data_off  = (uint64_t)song.stszOffset + 20;  // data starts after 20-byte header
        stsz_buf_valid = 0;
        stsz_buf_pos   = 0;

        // Apply stored AAC config (skips esds parsing entirely)
        audio_config.aacProfile    = song.aacProfile;
        audio_config.sampleRateIdx = song.aacSrIdx;
        audio_config.channelCfg    = song.aacChCfg;

        Serial.printf("✅ M4A (DB meta): %u samples, profile=%d sr_idx=%d ch=%d\n",
                      sample_count, audio_config.aacProfile,
                      audio_config.sampleRateIdx, audio_config.channelCfg);
        return true;
    }

    // Open and pre-scan the M4A file. Works for faststart (moov first) and
    // non-faststart (mdat first) files. Returns true on success.
    bool open(File32& file, AACDecoderHelix& dec) {
        p_file         = &file;
        p_dec          = &dec;
        mdat_start     = 0;
        mdat_cur       = 0;
        samp_idx       = 0;
        fixed_size     = 0;
        stsz_data_off  = 0;
        stsz_buf_valid = 0;
        stsz_buf_pos   = 0;

        M4ACommonDemuxer::begin();  // resets stsd_processed, audio_config, sample_count
        setupParser();              // re-register callbacks after parser.begin()

        if (!preParseFile()) {
            Serial.println("❌ M4A: failed to parse file structure");
            return false;
        }
        if (!readStszHeader()) {
            Serial.println("❌ M4A: failed to read stsz header");
            return false;
        }
        mdat_cur = mdat_start;
        Serial.printf("✅ M4A: %u samples, profile=%d sr_idx=%d ch=%d\n",
                      sample_count, audio_config.aacProfile,
                      audio_config.sampleRateIdx, audio_config.channelCfg);
        return sample_count > 0 && mdat_start > 0;
    }

    // Call every audioLoop() iteration. Returns true while frames remain.
    bool copy() {
        if (!p_file || samp_idx >= sample_count) return false;

        uint32_t fsize = nextFrameSize();
        if (fsize == 0 || fsize > 65536) return false;   // sanity

        // Ensure frame buffer is large enough for ADTS header (7) + raw frame
        if (frame_buf.size() < fsize + 7) frame_buf.resize(fsize + 7);

        // Write 7-byte ADTS header
        writeAdts(frame_buf.data(), audio_config.aacProfile,
                  audio_config.sampleRateIdx, audio_config.channelCfg, fsize);

        // Seek to current mdat position and read the raw AAC frame
        if (!p_file->seek(mdat_cur)) return false;
        if ((size_t)p_file->read(frame_buf.data() + 7, fsize) != fsize) return false;

        // Feed ADTS-wrapped frame to decoder; PCM output goes to `out` buffer
        p_dec->write(frame_buf.data(), fsize + 7);

        mdat_cur += fsize;
        samp_idx++;
        return true;
    }

    void close() {
        p_file   = nullptr;
        samp_idx = 0;
    }

    bool isActive() const { return p_file && samp_idx < sample_count; }

protected:
    void setupParser() override {
        parser.setReference(this);
        parser.setCallback("mp4a", [](MP4Parser::Box& box, void* ref) {
            static_cast<M4AFile32Demuxer*>(ref)->onMp4a(box);
        }, false);
        parser.setCallback("esds", [](MP4Parser::Box& box, void* ref) {
            static_cast<M4AFile32Demuxer*>(ref)->onEsds(box);
        }, false);
        parser.setCallback("alac", [](MP4Parser::Box& box, void* ref) {
            static_cast<M4AFile32Demuxer*>(ref)->onAlac(box);
        }, false);
        parser.setCallback("stsd", [](MP4Parser::Box& box, void* ref) {
            auto* self = static_cast<M4AFile32Demuxer*>(ref);
            self->onStsd(box);
            self->stsd_processed = true;
        }, false);
        parser.setCallback("stsz", [](MP4Parser::Box& box, void* ref) {
            // Record box offset; do NOT buffer all sizes into RAM
            auto* self = static_cast<M4AFile32Demuxer*>(ref);
            if (box.seq == 0) self->stsz_offset = (uint32_t)box.file_offset;
        }, false);
        parser.setCallback("mdat", [](MP4Parser::Box& box, void* ref) {
            // Record data start ONLY — do not call sampleExtractor.write()
            // (that's the library code path that has the crash-inducing assert)
            auto* self = static_cast<M4AFile32Demuxer*>(ref);
            if (box.seq == 0) self->mdat_start = (uint64_t)box.file_offset + 8;
        }, false);
    }

    // Feed the file into the MP4 parser until all three positions are found.
    // yield() prevents watchdog expiry while streaming through large mdat
    // boxes in non-faststart files.
    bool preParseFile() {
        uint8_t buf[512];
        p_file->seek(0);
        while (p_file->available()) {
            yield();
            int space = parser.availableForWrite();
            if (space <= 0) { vTaskDelay(1); continue; }
            int to_read = min((int)sizeof(buf), space);
            size_t len  = p_file->read(buf, to_read);
            if (len == 0) break;
            parser.write(buf, len);
            if (stsd_processed && stsz_offset && mdat_start) return true;
        }
        return stsd_processed && stsz_offset && mdat_start;
    }

    // Read fixed_size and sample_count from the stsz box header.
    // stsz layout: [4 box_size][4 "stsz"][4 ver+flags][4 sample_size][4 sample_count]
    bool readStszHeader() {
        if (stsz_offset == 0) return false;
        uint8_t hdr[20];
        if (!p_file->seek(stsz_offset)) return false;
        if ((size_t)p_file->read(hdr, 20) != 20) return false;
        if (!checkType(hdr, "stsz", 4)) return false;
        fixed_size    = readU32(hdr + 12);
        sample_count  = readU32(hdr + 16);
        stsz_data_off = (uint64_t)stsz_offset + 20;   // first entry byte offset
        return true;
    }

    // Return the next frame size, reading from the stsz table in batches
    // to minimise SD card seeks.
    uint32_t nextFrameSize() {
        if (fixed_size) return fixed_size;
        if (stsz_buf_pos >= stsz_buf_valid) {
            uint64_t entry_pos = stsz_data_off + (uint64_t)samp_idx * 4;
            if (!p_file->seek(entry_pos)) return 0;
            int to_fetch   = min((int)STSZ_BATCH, (int)(sample_count - samp_idx));
            stsz_buf_valid = p_file->read(stsz_buf, to_fetch * 4) / 4;
            stsz_buf_pos   = 0;
            if (stsz_buf_valid == 0) return 0;
            // Convert big-endian to host byte order
            for (int i = 0; i < stsz_buf_valid; i++) {
                auto* b = reinterpret_cast<uint8_t*>(&stsz_buf[i]);
                stsz_buf[i] = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
                            | ((uint32_t)b[2] <<  8) |  b[3];
            }
        }
        return stsz_buf[stsz_buf_pos++];
    }

    static void writeAdts(uint8_t* adts, int profile, int srIdx, int chCfg,
                          uint32_t frameLen) {
        uint32_t total = frameLen + 7;
        adts[0] = 0xFF;
        adts[1] = 0xF1;   // MPEG-4 AAC, no CRC
        adts[2] = (uint8_t)(((profile - 1) << 6) | (srIdx << 2) | ((chCfg >> 2) & 1));
        adts[3] = (uint8_t)(((chCfg & 3) << 6) | ((total >> 11) & 3));
        adts[4] = (uint8_t)((total >> 3) & 0xFF);
        adts[5] = (uint8_t)(((total & 7) << 5) | 0x1F);
        adts[6] = 0xFC;
    }

private:
    File32*          p_file        = nullptr;
    AACDecoderHelix* p_dec         = nullptr;
    uint64_t         mdat_start    = 0;    // byte offset where mdat data begins
    uint64_t         mdat_cur      = 0;    // current read head in mdat
    uint32_t         samp_idx      = 0;    // current sample index
    uint32_t         fixed_size    = 0;    // 0 = variable, >0 = fixed per frame
    uint64_t         stsz_data_off = 0;    // file offset to first stsz entry

    static const int STSZ_BATCH = 64;
    uint32_t stsz_buf[STSZ_BATCH];
    int stsz_buf_valid = 0;
    int stsz_buf_pos   = 0;

    SingleBuffer<uint8_t> frame_buf{0};    // ADTS header + raw AAC frame
};

const char *startFilePath = "/";
const char *ext = "mp3";
const int buffer_size = 128 * 1024;

const char *headphoneName = "JBL TUNE235NC TWS";

BufferRTOS<uint8_t> buffer(0);
QueueStream<uint8_t> out(buffer);
MP3DecoderHelix decoder;
MetaDataFilterDecoder filtered_mp3(decoder);
AudioSourceSDFAT<SdFat32, File32> source(startFilePath, ext, 32);
AudioPlayer player(source, out, filtered_mp3);  // filtered_mp3 strips metadata frames before Helix sees them

// M4A / AAC decoder chain (file-based, bypasses the streaming-demuxer assert)
AACDecoderHelix  aac_decoder;
VolumeStream     m4a_vol;          // volume control for M4A output
M4AFile32Demuxer m4a_demuxer;
File32           m4a_file;
bool             m4aActive = false;

BluetoothA2DPSource a2dp;

// State tracking
String last_device_name = headphoneName;
unsigned long last_watchdog_check = 0;

// Volume saving - NEW
unsigned long lastVolumeSaveTime = 0;
int lastSavedVolume = -1;

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
// BLUETOOTH DEVICE SCAN CALLBACKS
// ============================================================================

// Called for each device found during BT inquiry; return false = don't connect
static bool btScanCallback(const char* name, esp_bd_addr_t addr, int rssi) {
    if (name && strlen(name) > 0) {
        for (const auto& n : btFoundDevices) {
            if (n == name) return false;
        }
        btFoundDevices.push_back(std::string(name));
        Serial.printf("[BT Scan] Found: %s (RSSI: %d)\n", name, rssi);
        if (currentMenu == MENU_BT_SCAN) {
            buildBTScanMenu();
            displayNeedsUpdate = true;
        }
    }
    return false;  // never auto-connect during scan
}

// Called when inquiry starts or stops
static void btDiscoveryModeCallback(esp_bt_gap_discovery_state_t discoveryMode) {
    if (discoveryMode == ESP_BT_GAP_DISCOVERY_STOPPED) {
        Serial.println("[BT Scan] Discovery complete");
        btScanning = false;
        a2dp.set_ssid_callback(nullptr);
        a2dp.set_discovery_mode_callback(nullptr);
        if (currentMenu == MENU_BT_SCAN) {
            buildBTScanMenu();
            displayNeedsUpdate = true;
        }
    }
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
                if (m4aActive) {
                    m4a_demuxer.close();
                    aac_decoder.end();
                    m4a_file.close();
                    m4aActive = false;
                } else if (player.isActive()) {
                    player.stop();
                }
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

            // Save connected device name to NVS for persistence across reboots
            rougePrefs.saveBTDevice(last_device_name.c_str());
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
    if (millis() - last_watchdog_check < BT_WATCHDOG_INTERVAL) {
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
    pauseStartMillis = millis();
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
    if (pauseStartMillis > 0) {
        totalPausedMs   += millis() - pauseStartMillis;
        pauseStartMillis = 0;
    }
    Serial.println("[PLAYER] Resumed");
}

void stopPlayback() {
    if (player_state == STATE_STOPPED) {
        Serial.println("[PLAYER] Already stopped");
        return;
    }
    
    Serial.println("[PLAYER] Stopping...");
    
    // Stop the active decoder
    if (m4aActive) {
        m4a_demuxer.close();
        aac_decoder.end();
        m4a_file.close();
        m4aActive = false;
    } else if (player.isActive()) {
        player.stop();
    }

    // Clear the buffer
    buffer.reset();
    
    // Reset state
    player_state = STATE_STOPPED;
    
    Serial.println("[PLAYER] Stopped");
}

void setM4AVolume(float vol) {
    m4a_vol.setVolume(vol);
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

    // Update device name and connect
    last_device_name = new_device_name;
    a2dp.start(last_device_name.c_str());
    Serial.println("[BT] Connecting to new device...");
}

void startBTScan() {
    if (btScanning) return;

    // Stop playback if active
    if (player_state != STATE_STOPPED) stopPlayback();

    // Disconnect if connected
    if (bluetoothConnected) {
        disconnectBluetooth();
        delay(500);
    }

    btFoundDevices.clear();
    btScanning = true;

    // Set callbacks: collect all found devices, never auto-connect
    a2dp.set_ssid_callback(btScanCallback);
    a2dp.set_discovery_mode_callback(btDiscoveryModeCallback);

    // start() with empty name list = scan all BT devices in range
    a2dp.start();
    Serial.println("[BT Scan] Scanning for nearby devices...");
}

void initAudio()
{
    buffer.resize(buffer_size);
    Serial.printf("Audio buffer allocated: %d KB\n", buffer_size / 1024);
    logRamSpace("audio buffer allocation");

    AudioLogger::instance().begin(Serial, AudioLogger::Warning);

    source.begin();
    out.begin(60);

    // Load saved volume
    currentVolume = rougePrefs.loadVolume();
    Serial.printf("🔊 Volume set to %d%%\n", currentVolume);

    // Configure MP3 player
    player.setDelayIfOutputFull(0);
    player.setVolume(currentVolume / 100.0f);
    player.setAutoNext(false);
    player.setAutoFade(false);

    // Pre-wire the AAC decoder output through a VolumeStream for M4A playback
    {
        AudioInfo m4a_fmt;
        m4a_fmt.sample_rate    = 44100;
        m4a_fmt.channels       = 2;
        m4a_fmt.bits_per_sample = 16;
        m4a_vol.setOutput(out);
        m4a_vol.begin(m4a_fmt);
        m4a_vol.setVolume(currentVolume / 100.0f);
        aac_decoder.setOutput(m4a_vol);
    }

    Serial.println("\n[BT] Configuring Bluetooth A2DP Source...");
    a2dp.set_data_callback(get_sound_data);
    a2dp.set_on_connection_state_changed(connection_state_changed);
    a2dp.set_on_audio_state_changed(audio_state_changed);    

    a2dp.set_auto_reconnect(false);
    Serial.println("[BT] Auto-reconnect: DISABLED");

    // Load saved device name from NVS; fall back to hardcoded default
    String savedDevice = rougePrefs.loadBTDevice();
    last_device_name = savedDevice.isEmpty() ? String(headphoneName) : savedDevice;
    Serial.printf("[BT] Connecting to: %s\n", last_device_name.c_str());
    a2dp.start(last_device_name.c_str());
    Serial.println("✅ A2DP Started!");
    bluetoothConnected = false;
    btStatus = "BT Disconnected";
    
    logRamSpace("A2DP start");
}

void audioLoop()
{
    // Feed buffer when playing
    if (player_state == STATE_PLAYING && bluetoothConnected) {
        if (m4aActive) {
            // M4A: file-based frame decode — only run when buffer has space
            if (buffer.availableForWrite() > 8192) {
                if (!m4a_demuxer.copy()) {
                    Serial.println("📀 M4A: End of file (song finished)");
                    autoNext();
                }
            }
        } else {
            // MP3: AudioPlayer-based decode
            size_t copied = 0;
            try {
                copied = player.copy();
            } catch (...) {
                Serial.println("❌ Audio copy exception! Stopping and skipping...");
                player_state = STATE_STOPPED;
                player.stop();
                buffer.reset();
                delay(50);
                autoNext();
                return;
            }
            if (copied == 0) {
                Serial.println("📀 End of file reached (song finished)");
                autoNext();
            }
        }
    }
    
    // Handle delayed volume save (debouncing) - NEW
    if (lastVolumeSaveTime > 0 && 
        currentVolume != lastSavedVolume &&
        millis() - lastVolumeSaveTime > VOLUME_SAVE_DELAY)
    {
        rougePrefs.saveVolume(currentVolume);
        lastSavedVolume = currentVolume;
        lastVolumeSaveTime = 0;  // Reset
    }
}

void playCurrentSong(bool updateDisplay)
{
    Serial.println("🔍 Starting playback...");
    clearAlbumArt();  // Always clear stale art before any early returns

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

    // Detect format from file extension (.m4a → M4A/AAC chain; anything else → MP3)
    bool isM4A = false;
    {
        const std::string& p = song.path;
        if (p.size() >= 4) {
            const char* tail = p.c_str() + p.size() - 4;
            isM4A = (tail[0] == '.' &&
                     tolower((unsigned char)tail[1]) == 'm' &&
                     tail[2] == '4' &&
                     tolower((unsigned char)tail[3]) == 'a');
        }
    }

    Serial.printf("▶️ Playing: %s\n", song.title.c_str());
    Serial.printf("   Path: %s  Format: %s\n", song.path.c_str(), isM4A ? "M4A" : "MP3");

    // Clean up any previous M4A state
    if (m4aActive) {
        m4a_demuxer.close();
        aac_decoder.end();
        m4a_file.close();
        m4aActive = false;
    }

    // Stop MP3 player if it was running
    if (player.isActive()) {
        Serial.println("   ⚠️ Player still active, stopping first");
        player.stop();
        delay(100);
    }

    // Reset buffer to ensure clean start
    buffer.reset();

    // Reset progress tracking
    playbackStartMillis = millis();
    totalPausedMs       = 0;
    pauseStartMillis    = 0;

    // Load album art from ID3 tags using audio source's SdFat32 instance
    loadAlbumArt(source.getAudioFs(), song.path.c_str());

    Serial.println("   Opening file...");

    if (isM4A) {
        // --- M4A / AAC path: file-based demuxer, no AudioPlayer ---
        m4a_file = source.getAudioFs().open(song.path.c_str());
        if (!m4a_file) {
            Serial.printf("❌ Cannot open M4A: %s\n", song.path.c_str());
            currentTitle = "Error: Cannot open";
            displayNeedsUpdate = true;
            autoNext();
            return;
        }
        aac_decoder.begin();
        if (!m4a_demuxer.openWithMeta(m4a_file, aac_decoder, song)) {
            Serial.println("❌ Failed to parse M4A structure");
            m4a_file.close();
            aac_decoder.end();
            currentTitle = "Error: Bad M4A";
            displayNeedsUpdate = true;
            autoNext();
            return;
        }
        m4aActive    = true;
        player_state = STATE_PLAYING;
    } else {
        // --- MP3 path: AudioPlayer as before ---
        try {
            if (!player.setPath(song.path.c_str())) {
                Serial.printf("❌ Could not open file: %s\n", song.path.c_str());
                currentTitle = "Error: Cannot open";
                displayNeedsUpdate = true;
                autoNext();
                return;
            }
            AudioInfo btFormat;
            btFormat.sample_rate    = 44100;
            btFormat.channels       = 2;
            btFormat.bits_per_sample = 16;
            player.setAudioInfo(btFormat);
            Serial.println("   Starting playback...");
            player.play();
        } catch (...) {
            Serial.printf("❌ Exception opening/starting: %s\n", song.path.c_str());
            currentTitle = "Error: Bad file";
            displayNeedsUpdate = true;
            autoNext();
            return;
        }
        player_state = STATE_PLAYING;
    }
    
    Serial.println("✅ Playback started");
    
    if (currentMenu == MENU_MAIN) {
        buildMainMenu();
    }
    
    if (updateDisplay) {
        displayNeedsUpdate = true;
    }
}
