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

// Returns true only if the JPEG uses Baseline DCT (SOF0 marker, 0xFF 0xC0).
// TJpgDec does not support Progressive JPEG (SOF2) or other non-baseline types.
// Feeding a progressive JPEG to drawJpg() causes a heap-corrupting buffer overflow
// that manifests as a NULL FreeRTOS queue assertion crash.
static bool isBaselineJpeg(const uint8_t* data, size_t len) {
    if (len < 4) return false;
    size_t pos = 2;  // skip SOI marker (FF D8)
    while (pos + 3 < len) {
        if (data[pos] != 0xFF) return false;  // lost marker sync
        uint8_t marker = data[pos + 1];
        if (marker == 0xC0) return true;   // SOF0: Baseline DCT — supported
        if (marker == 0xC2) return false;  // SOF2: Progressive DCT — NOT supported
        // Other non-baseline SOF types (excluding DHT=C4, JPG ext=C8, DAC=CC)
        if (marker >= 0xC1 && marker <= 0xCF &&
            marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            return false;
        }
        uint16_t segLen = ((uint16_t)data[pos + 2] << 8) | data[pos + 3];
        if (segLen < 2) break;
        pos += 2 + segLen;
    }
    return true;  // couldn't find SOF marker — assume baseline to be permissive
}

bool loadAlbumArt(SdFat32& sd, const char* path,
                  uint64_t covrOffset, uint32_t covrSize) {
    clearAlbumArt();

    // M4A fast path: seek directly to pre-indexed JPEG bytes in the file
    if (covrOffset > 0 && covrSize > 0) {
        Serial.printf("   🖼️  Loading M4A cover art: offset=%llu size=%u\n", covrOffset, covrSize);

        if (covrSize > ART_BUFFER_MAX) {
            Serial.printf("   ⚠️  Cover art too large (%u bytes, max %u)\n", covrSize, ART_BUFFER_MAX);
            return false;
        }
        size_t freePSRAM = ESP.getFreePsram();
        if (covrSize > freePSRAM - 100000) {
            Serial.printf("   ⚠️  Not enough PSRAM for cover art (%u needed, %u free)\n", covrSize, freePSRAM);
            return false;
        }

        File32 f = sd.open(path, O_RDONLY);
        if (!f) {
            Serial.println("   ❌ Could not open M4A file for cover art");
            return false;
        }
        if (!f.seek(covrOffset)) {
            Serial.println("   ❌ Seek to cover art offset failed");
            f.close();
            return false;
        }

        artBuffer = (uint8_t*)ps_malloc(covrSize);
        if (!artBuffer) {
            Serial.println("   ❌ PSRAM allocation failed for cover art");
            f.close();
            return false;
        }
        if ((uint32_t)f.read(artBuffer, covrSize) != covrSize) {
            Serial.println("   ❌ Cover art read failed");
            free(artBuffer); artBuffer = nullptr;
            f.close();
            return false;
        }
        f.close();
        artSize = covrSize;

        // Validate JPEG magic bytes (FF D8)
        if (artSize < 2 || artBuffer[0] != 0xFF || artBuffer[1] != 0xD8) {
            Serial.println("   ⚠️  M4A cover art is not JPEG, skipping");
            free(artBuffer); artBuffer = nullptr; artSize = 0;
            return false;
        }
        // Reject progressive JPEGs — TJpgDec only handles Baseline DCT (SOF0)
        if (!isBaselineJpeg(artBuffer, artSize)) {
            Serial.println("   ⚠️  M4A cover art is progressive JPEG — skipping");
            free(artBuffer); artBuffer = nullptr; artSize = 0;
            return false;
        }

        albumArtAvailable = true;
        Serial.printf("🖼️  M4A cover art loaded: %u bytes JPEG\n", artSize);
        return true;
    }

    // MP3 / fallback path: scan ID3v2 APIC frame
    Serial.println("   📂 Opening MP3 for album art...");
    Serial.printf("   Path: %s\n", path);
    Serial.println("   Calling sd.open()...");

    File32 f = sd.open(path, O_RDONLY);

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

        // Reject non-baseline JPEGs — TJpgDec only handles Baseline DCT (SOF0).
        // Progressive JPEGs (SOF2) pass header parsing but corrupt heap during decode.
        if (!isBaselineJpeg(tmp + off, jpegLen)) {
            Serial.println("⚠️  Album art is progressive JPEG — skipping (TJpgDec requires baseline)");
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
