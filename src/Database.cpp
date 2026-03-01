#include "Database.h"
#include "State.h"
#include <SdFat.h>

extern SdFat32 sd;

MusicDatabase musicDB;

MusicDatabase::MusicDatabase() : db(nullptr), isOpen(false) {}

MusicDatabase::~MusicDatabase() {
    close();
}

// Helper function to truncate long strings - UPDATED
std::string truncateString(const std::string& str, size_t maxLen) {
    if (str.length() <= maxLen) {
        return str;
    }
    
    // Leave room for "..."
    if (maxLen <= 3) {
        return "...";
    }
    
    return str.substr(0, maxLen - 3) + "...";
}

bool MusicDatabase::openFromMemory(const char* sdPath) {
    Serial.printf("📂 Loading database from SD to PSRAM: %s\n", sdPath);
    
    if (!sd.exists(sdPath)) {
        Serial.println("❌ Database file not found");
        return false;
    }
    
    // Open source file
    File32 srcFile = sd.open(sdPath, O_RDONLY);
    if (!srcFile) {
        Serial.println("❌ Cannot open database file");
        return false;
    }
    
    size_t fileSize = srcFile.size();
    Serial.printf("   File size: %lu bytes (%.1f KB)\n", fileSize, fileSize / 1024.0);
    
    // Check PSRAM availability
    size_t freePSRAM = ESP.getFreePsram();
    Serial.printf("   Free PSRAM: %lu bytes (%.1f KB)\n", freePSRAM, freePSRAM / 1024.0);
    
    if (fileSize > freePSRAM - 100000) {  // Keep 100KB buffer
        Serial.println("❌ Not enough PSRAM");
        srcFile.close();
        return false;
    }
    
    // Allocate buffer in PSRAM
    uint8_t* dbBuffer = (uint8_t*)ps_malloc(fileSize);
    if (!dbBuffer) {
        Serial.println("❌ Failed to allocate PSRAM");
        srcFile.close();
        return false;
    }
    
    Serial.println("   📥 Reading database into PSRAM...");
    
    // Read in chunks to show progress
    size_t bytesRead = 0;
    size_t chunkSize = 8192;
    
    while (bytesRead < fileSize) {
        size_t toRead = min(chunkSize, fileSize - bytesRead);
        size_t read = srcFile.read(dbBuffer + bytesRead, toRead);
        
        if (read == 0) {
            Serial.println("❌ Read error");
            free(dbBuffer);
            srcFile.close();
            return false;
        }
        
        bytesRead += read;
        
        if (bytesRead % 102400 == 0) {  // Progress every ~100KB
            Serial.printf("      %lu / %lu bytes (%.0f%%)\n", 
                         bytesRead, fileSize, (bytesRead * 100.0) / fileSize);
        }
    }
    
    srcFile.close();
    Serial.printf("   ✅ Database loaded (%lu bytes)\n", bytesRead);
    
    // Open in-memory database
    int rc = sqlite3_open(":memory:", &db);
    
    if (rc != SQLITE_OK) {
        Serial.printf("❌ Cannot create memory database: %s\n", sqlite3_errmsg(db));
        free(dbBuffer);
        return false;
    }
    
    // Deserialize buffer into database
    Serial.println("   💾 Deserializing database...");
    rc = sqlite3_deserialize(
        db,
        "main",
        dbBuffer,
        fileSize,
        fileSize,
        SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE
    );
    
    if (rc != SQLITE_OK) {
        Serial.printf("❌ Cannot deserialize: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        db = nullptr;
        free(dbBuffer);
        return false;
    }
    
    isOpen = true;
    Serial.println("✅ Database opened from PSRAM");
    
    // Test query
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM artists", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            Serial.printf("   🎵 %d artists found\n", count);
        }
        sqlite3_finalize(stmt);
    } else {
        Serial.printf("⚠️  Test query failed: %s\n", sqlite3_errmsg(db));
    }
    
    return true;
}

bool MusicDatabase::open(const char* path) {
    return openFromMemory(path);
}

void MusicDatabase::close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
        isOpen = false;
        Serial.println("📂 Database closed");
    }
}

std::vector<std::string> MusicDatabase::getArtistNames() {
    std::vector<std::string> result;
    
    if (!isOpen) return result;
    
    const char* sql = "SELECT name FROM artists ORDER BY name COLLATE NOCASE";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = (const char*)sqlite3_column_text(stmt, 0);
            if (name) {
                result.push_back(name);
            }
        }
    } else {
        Serial.printf("❌ SQL error: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);

    Serial.printf("📊 Loaded %d artists\n", result.size());
    return result;
}

std::vector<std::string> MusicDatabase::getAlbumNamesByArtist(const std::string& artistName) {
    std::vector<std::string> result;
    
    if (!isOpen || artistName.empty()) return result;
    
    const char* sql = 
        "SELECT albums.name FROM albums "
        "JOIN artists ON albums.artist_id = artists.id "
        "WHERE artists.name LIKE ? "
        "ORDER BY albums.year, albums.name COLLATE NOCASE";
    
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, artistName.c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = (const char*)sqlite3_column_text(stmt, 0);
            if (name) {
                result.push_back(name);
            }
        }
    } else {
        Serial.printf("❌ SQL error: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    
    Serial.printf("📊 Loaded %d albums for %s\n", result.size(), artistName.c_str());
    return result;
}

std::vector<Song> MusicDatabase::getSongsByAlbum(const std::string& artistName, const std::string& albumName) {
    std::vector<Song> result;

    if (!isOpen || artistName.empty() || albumName.empty()) return result;

    // Try extended query first (DB has M4A layout metadata columns)
    const char* sql_ext =
        "SELECT songs.title, songs.path, songs.track_number, songs.duration,"
        " songs.mdat_start, songs.stsz_offset, songs.sample_count, songs.fixed_size,"
        " songs.aac_profile, songs.aac_sr_idx, songs.aac_ch_cfg"
        " FROM songs"
        " JOIN albums  ON songs.album_id    = albums.id"
        " JOIN artists ON albums.artist_id  = artists.id"
        " WHERE artists.name LIKE ? AND albums.name LIKE ?"
        " ORDER BY songs.track_number";

    // Fallback for older databases without M4A columns
    const char* sql_basic =
        "SELECT songs.title, songs.path, songs.track_number, songs.duration"
        " FROM songs"
        " JOIN albums  ON songs.album_id    = albums.id"
        " JOIN artists ON albums.artist_id  = artists.id"
        " WHERE artists.name LIKE ? AND albums.name LIKE ?"
        " ORDER BY songs.track_number";

    sqlite3_stmt* stmt = nullptr;
    bool extended = false;

    if (sqlite3_prepare_v2(db, sql_ext, -1, &stmt, nullptr) == SQLITE_OK) {
        extended = true;
    } else if (sqlite3_prepare_v2(db, sql_basic, -1, &stmt, nullptr) == SQLITE_OK) {
        extended = false;
    } else {
        Serial.printf("❌ SQL error: %s\n", sqlite3_errmsg(db));
        return result;
    }

    sqlite3_bind_text(stmt, 1, artistName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, albumName.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Song song;
        const char* title = (const char*)sqlite3_column_text(stmt, 0);
        const char* path  = (const char*)sqlite3_column_text(stmt, 1);

        if (title) {
            song.title        = title;
            song.displayTitle = title;  // Full title; display layer truncates as needed
        }
        if (path) {
            song.path = path;
        }

        song.track    = sqlite3_column_int(stmt, 2);
        song.duration = sqlite3_column_int(stmt, 3);

        if (extended) {
            song.mdatStart   = (uint64_t)sqlite3_column_int64(stmt, 4);
            song.stszOffset  = (uint64_t)sqlite3_column_int64(stmt, 5);
            song.sampleCount = (uint32_t)sqlite3_column_int(stmt,  6);
            song.fixedSize   = (uint32_t)sqlite3_column_int(stmt,  7);
            song.aacProfile  = sqlite3_column_int(stmt, 8);
            song.aacSrIdx    = sqlite3_column_int(stmt, 9);
            song.aacChCfg    = sqlite3_column_int(stmt, 10);
        }

        result.push_back(song);
    }

    sqlite3_finalize(stmt);

    Serial.printf("📊 Loaded %d songs from %s - %s%s\n",
                  result.size(), artistName.c_str(), albumName.c_str(),
                  extended ? " (with M4A meta)" : "");
    return result;
}

int MusicDatabase::getSongCount() {
    if (!isOpen) return 0;
    
    const char* sql = "SELECT COUNT(*) FROM songs";
    sqlite3_stmt* stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
    }
    
    sqlite3_finalize(stmt);
    return count;
}

int MusicDatabase::getArtistCount() {
    if (!isOpen) return 0;
    
    const char* sql = "SELECT COUNT(*) FROM artists";
    sqlite3_stmt* stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
    }
    
    sqlite3_finalize(stmt);
    return count;
}

int MusicDatabase::getAlbumCount() {
    if (!isOpen) return 0;
    
    const char* sql = "SELECT COUNT(*) FROM albums";
    sqlite3_stmt* stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
    }
    
    sqlite3_finalize(stmt);
    return count;
}