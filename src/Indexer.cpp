#include "Indexer.h"
#include <ArduinoJson.h>

SdFat sd;

bool loadIndex() {
  if (!indexDoc) {
    Serial.println("❌ indexDoc is null!");
    return false;
  }
  
  File file = sd.open("/index.json");
  if (!file) {
    Serial.println("❌ Failed to open index.json");
    return false;
  }
  
  DeserializationError error = deserializeJson(*indexDoc, file);
  file.close();
  
  if (error) {
    Serial.print("❌ JSON parse error: ");
    Serial.println(error.c_str());
    return false;
  }
  
  Serial.println("✅ Index loaded successfully");
  return true;
}

bool buildArtistList() {
  artists.clear();
  
  if (!indexDoc) {
    Serial.println("❌ indexDoc is null!");
    return false;
  }
  
  JsonObject library = (*indexDoc)["library"];
  if (library.isNull()) {
    Serial.println("❌ Library object is null!");
    return false;
  }
  
  // Reserve space to avoid reallocation
  artists.reserve(100);
  
  for (JsonPair kv : library) {
    artists.push_back(kv.key().c_str());
  }
  
  if (artists.empty()) {
    Serial.println("⚠️ No artists found in library");
    return false;
  }
  
  Serial.printf("✅ Loaded %d artists\n", artists.size());
  return true;
}

bool buildAlbumList(const std::string &artist) {
  albums.clear();
  
  if (!indexDoc) {
    Serial.println("❌ indexDoc is null!");
    return false;
  }
  
  if (artist.empty()) {
    Serial.println("❌ Artist name is empty!");
    return false;
  }
  
  JsonObject library = (*indexDoc)["library"];
  if (library.isNull()) {
    Serial.println("❌ Library is null!");
    return false;
  }
  
  JsonObject artistObj = library[artist.c_str()];
  if (artistObj.isNull()) {
    Serial.printf("❌ Artist '%s' not found!\n", artist.c_str());
    return false;
  }
  
  albums.reserve(20);
  
  for (JsonPair kv : artistObj) {
    albums.push_back(kv.key().c_str());
  }
  
  if (albums.empty()) {
    Serial.printf("⚠️ Artist '%s' has no albums\n", artist.c_str());
    return false;
  }
  
  Serial.printf("✅ Loaded %d albums for %s\n", albums.size(), artist.c_str());
  return true;
}

bool buildSongList(const std::string &artist, const std::string &album) {
  songs.clear();
  
  if (!indexDoc) {
    Serial.println("❌ indexDoc is null!");
    return false;
  }
  
  if (artist.empty() || album.empty()) {
    Serial.println("❌ Artist or album name is empty!");
    return false;
  }
  
  JsonObject library = (*indexDoc)["library"];
  if (library.isNull()) {
    Serial.println("❌ Library is null!");
    return false;
  }
  
  JsonObject artistObj = library[artist.c_str()];
  if (artistObj.isNull()) {
    Serial.printf("❌ Artist '%s' not found!\n", artist.c_str());
    return false;
  }
  
  JsonArray songArr = artistObj[album.c_str()];
  if (songArr.isNull()) {
    Serial.printf("⚠️ Album '%s' has no songs\n", album.c_str());
    return false;
  }

  songs.reserve(songArr.size());

  for (JsonObject s : songArr) {
    Song song;
    // Use defaults if fields are missing
    song.title = s["title"] | "Unknown Title";
    song.path = s["path"] | "";
    song.track = s["track"] | 0;
    song.duration = s["duration"] | 0;
    
    // Validate path
    if (song.path.empty()) {
      Serial.printf("⚠️ Song '%s' has empty path, skipping\n", song.title.c_str());
      continue;
    }
    
    songs.push_back(song);
  }
  
  if (songs.empty()) {
    Serial.printf("⚠️ No valid songs in album '%s'\n", album.c_str());
    return false;
  }
  
  Serial.printf("✅ Loaded %d songs from %s - %s\n", songs.size(), artist.c_str(), album.c_str());
  return true;
}