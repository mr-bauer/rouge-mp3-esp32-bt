#ifndef DATABASE_H
#define DATABASE_H

#include <Arduino.h>
#include <sqlite3.h>
#include <vector>
#include <string>
#include "State.h"

struct Artist {
    int id;
    std::string name;
};

struct Album {
    int id;
    int artistId;
    std::string name;
    int year;
};

struct SongDB {
    int id;
    int albumId;
    std::string title;
    std::string path;
    int trackNumber;
    int duration;
};

class MusicDatabase {
public:
    MusicDatabase();
    ~MusicDatabase();
    
    bool open(const char* path);
    bool openFromMemory(const char* sdPath);  // NEW
    void close();
    
    // Query methods
    std::vector<std::string> getArtistNames();
    std::vector<std::string> getAlbumNamesByArtist(const std::string& artistName);
    std::vector<Song> getSongsByAlbum(const std::string& artistName, const std::string& albumName);
    
    // Stats
    int getSongCount();
    int getArtistCount();
    int getAlbumCount();
    
private:
    sqlite3* db;
    bool isOpen;
};

extern MusicDatabase musicDB;

#endif