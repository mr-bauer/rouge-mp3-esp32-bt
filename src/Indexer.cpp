#include "Indexer.h"
#include "Database.h"

SdFat32 sd;

bool loadDatabase() {
    // Check if database file exists
    if (!sd.exists("music.db")) {
        Serial.println("❌ music.db not found on SD card!");
        Serial.println("   Please run the desktop indexer tool to create it.");
        return false;
    }
    
    // Get file info
    File32 file = sd.open("music.db");  // Changed to File32
    if (file) {
        Serial.printf("📁 Found music.db (%lu bytes)\n", file.size());
        file.close();
    }
    
    // Open the database
    if (!musicDB.open("music.db")) {
        Serial.println("❌ Failed to open music.db");
        return false;
    }
    
    // Show stats
    int songCount = musicDB.getSongCount();
    int artistCount = musicDB.getArtistCount();
    int albumCount = musicDB.getAlbumCount();
    
    Serial.println("✅ Database loaded successfully");
    Serial.printf("   Artists: %d, Albums: %d, Songs: %d\n", 
                  artistCount, albumCount, songCount);
    
    return true;
}

bool buildArtistList() {
    artists.clear();
    
    // Query database for artists
    artists = musicDB.getArtistNames();
    
    if (artists.empty()) {
        Serial.println("⚠️ No artists found in database");
        return false;
    }
    
    Serial.printf("✅ Loaded %d artists\n", artists.size());
    return true;
}

bool buildAlbumList(const std::string &artist) {
    albums.clear();
    
    if (artist.empty()) {
        Serial.println("❌ Artist name is empty!");
        return false;
    }
    
    // Query database for albums by artist
    albums = musicDB.getAlbumNamesByArtist(artist);
    
    if (albums.empty()) {
        Serial.printf("⚠️ Artist '%s' has no albums\n", artist.c_str());
        return false;
    }
    
    Serial.printf("✅ Loaded %d albums for %s\n", albums.size(), artist.c_str());
    return true;
}

bool buildSongList(const std::string &artist, const std::string &album) {
    songs.clear();
    
    if (artist.empty() || album.empty()) {
        Serial.println("❌ Artist or album name is empty!");
        return false;
    }
    
    // Query database for songs
    songs = musicDB.getSongsByAlbum(artist, album);
    
    if (songs.empty()) {
        Serial.printf("⚠️ No songs found for %s - %s\n", artist.c_str(), album.c_str());
        return false;
    }
    
    Serial.printf("✅ Loaded %d songs from %s - %s\n", 
                  songs.size(), artist.c_str(), album.c_str());
    return true;
}