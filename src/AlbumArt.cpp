#include "AlbumArt.h"
#include "State.h"
#include "Display.h"
#include <TJpg_Decoder.h>

extern lgfx::LGFX_Sprite sprite;

static uint8_t* artBuffer = nullptr;
static size_t   artSize   = 0;

static const size_t ART_BUFFER_MAX = 300 * 1024;  // 300 KB cap (allows high-res embedded art)

// TJpg_Decoder callback: receives decoded pixel blocks and writes to sprite
static bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= SCREEN_HEIGHT) return false;
    sprite.pushImage(x, y, w, h, bitmap);
    return true;
}

// Read a 4-byte syncsafe integer (used for ID3v2 tag total size)
static uint32_t readSyncsafe(const uint8_t* b) {
    return ((uint32_t)(b[0] & 0x7F) << 21) |
           ((uint32_t)(b[1] & 0x7F) << 14) |
           ((uint32_t)(b[2] & 0x7F) <<  7) |
           ((uint32_t)(b[3] & 0x7F));
}

// Read a plain 4-byte big-endian uint32 (used for ID3v2 frame data size)
static uint32_t readUint32BE(const uint8_t* b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
}

bool loadAlbumArt(SdFat32& sd, const char* mp3Path) {
    clearAlbumArt();

    Serial.println("   📂 Opening MP3 for album art...");
    Serial.printf("   Path: %s\n", mp3Path);
    Serial.println("   Calling sd.open()...");

    File32 f = sd.open(mp3Path, O_RDONLY);

    Serial.println("   sd.open() returned");
    if (!f) {
        Serial.println("   ❌ Could not open file for art");
        return false;
    }
    Serial.println("   File opened successfully");

    // Verify ID3v2 header magic bytes
    uint8_t hdr[10];
    if (f.read(hdr, 10) != 10 || hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') {
        Serial.println("   ℹ️  No ID3v2 tag found");
        f.close();
        return false;
    }

    // Support ID3v2.3 and ID3v2.4 only
    uint8_t ver = hdr[3];
    if (ver < 3 || ver > 4) {
        Serial.printf("   ℹ️  Unsupported ID3v2.%d\n", ver);
        f.close();
        return false;
    }

    uint32_t tagSize = readSyncsafe(hdr + 6);
    Serial.printf("   🔍 Scanning ID3v2.%d tag (%u bytes)\n", ver, tagSize);

    uint32_t pos = 0;
    int frameCount = 0;
    const int MAX_FRAMES = 100;  // Safety limit to prevent infinite loops

    while (pos + 10 <= tagSize && frameCount < MAX_FRAMES) {
        uint8_t fhdr[10];
        if (f.read(fhdr, 10) != 10) break;
        pos += 10;
        frameCount++;

        // ID3v2.4 uses syncsafe integers for frame sizes; ID3v2.3 uses plain big-endian
        uint32_t frameSize = (ver == 4) ? readSyncsafe(fhdr + 4) : readUint32BE(fhdr + 4);
        if (frameSize == 0) break;

        bool isAPIC = (fhdr[0] == 'A' && fhdr[1] == 'P' && fhdr[2] == 'I' && fhdr[3] == 'C');

        if (!isAPIC) {
            if (!f.seekCur(frameSize)) break;
            pos += frameSize;
            continue;
        }

        // APIC frame found
        Serial.printf("   🖼️  Found APIC frame (%u bytes)\n", frameSize);

        if (frameSize > ART_BUFFER_MAX) {
            Serial.printf("   ⚠️  Album art too large (%u bytes, max %u), skipping\n", frameSize, ART_BUFFER_MAX);
            f.close();
            return false;
        }

        // Check available PSRAM before allocating
        size_t freePSRAM = ESP.getFreePsram();
        if (frameSize > freePSRAM - 100000) {  // Keep 100KB safety margin
            Serial.printf("   ⚠️  Not enough PSRAM (%u bytes needed, %u available)\n", frameSize, freePSRAM);
            f.close();
            return false;
        }

        // Read entire APIC frame into a temporary PSRAM buffer
        uint8_t* tmp = (uint8_t*)ps_malloc(frameSize);
        if (!tmp) {
            Serial.println("   ❌ PSRAM allocation failed");
            f.close();
            return false;
        }
        if ((uint32_t)f.read(tmp, frameSize) != frameSize) {
            Serial.println("   ❌ Read failed");
            free(tmp);
            f.close();
            return false;
        }
        f.close();

        // Parse APIC frame: [encoding 1B][mime \0][pictype 1B][description \0][jpeg...]
        size_t off = 1;  // skip text encoding byte

        // Skip MIME type (null-terminated)
        while (off < frameSize && tmp[off] != 0) off++;
        off++;  // skip null terminator

        off++;  // skip picture type byte

        // Skip description (null-terminated)
        while (off < frameSize && tmp[off] != 0) off++;
        off++;  // skip null terminator

        if (off >= frameSize) {
            free(tmp);
            return false;
        }

        size_t jpegLen = frameSize - off;

        // Verify JPEG magic bytes (FF D8)
        if (jpegLen < 2 || tmp[off] != 0xFF || tmp[off + 1] != 0xD8) {
            Serial.println("⚠️  Album art is not JPEG, skipping");
            free(tmp);
            return false;
        }

        // Copy JPEG data into its own PSRAM buffer
        artBuffer = (uint8_t*)ps_malloc(jpegLen);
        if (!artBuffer) {
            free(tmp);
            return false;
        }
        memcpy(artBuffer, tmp + off, jpegLen);
        artSize = jpegLen;
        free(tmp);

        albumArtAvailable = true;
        Serial.printf("🖼️  Album art loaded: %u bytes JPEG\n", artSize);
        return true;
    }

    f.close();
    return false;
}

void drawAlbumArt(int targetX, int targetY, int maxSize) {
    if (!albumArtAvailable || !artBuffer || artSize == 0) return;

    Serial.printf("🎨 Drawing album art: maxSize=%d, bufferSize=%u\n", maxSize, artSize);

    TJpgDec.setSwapBytes(true);  // LovyanGFX sprite buffer is big-endian; TJpg default is little-endian
    TJpgDec.setCallback(jpegOutput);

    uint16_t w = 0, h = 0;
    TJpgDec.getJpgSize(&w, &h, artBuffer, artSize);
    if (w == 0 || h == 0) {
        Serial.println("⚠️ Invalid JPEG dimensions");
        return;
    }

    Serial.printf("   JPEG size: %dx%d\n", w, h);

    // Select largest scale divisor (1, 2, 4, 8) that keeps both dimensions within maxSize
    uint8_t scale = 1;
    if (w > (uint16_t)maxSize || h > (uint16_t)maxSize) {
        if (w > (uint16_t)(maxSize * 4) || h > (uint16_t)(maxSize * 4))
            scale = 8;
        else if (w > (uint16_t)(maxSize * 2) || h > (uint16_t)(maxSize * 2))
            scale = 4;
        else
            scale = 2;
    }
    TJpgDec.setJpgScale(scale);

    uint16_t scaledW = w / scale;
    uint16_t scaledH = h / scale;

    // Center the scaled image within the target area
    int drawX = targetX + (maxSize - (int)scaledW) / 2;
    int drawY = targetY + (maxSize - (int)scaledH) / 2;

    Serial.printf("   Scaled: %dx%d (1/%d), drawing at (%d,%d)\n", scaledW, scaledH, scale, drawX, drawY);

    TJpgDec.drawJpg(drawX, drawY, artBuffer, artSize);

    Serial.println("✅ Album art drawn");
}

void clearAlbumArt() {
    // Set flag to false FIRST to prevent display task from accessing buffer during cleanup
    albumArtAvailable = false;

    if (artBuffer) {
        free(artBuffer);
        artBuffer = nullptr;
        artSize = 0;
    }
}
