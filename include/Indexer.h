#ifndef INDEXER_H
#define INDEXER_H

#include <SdFat.h>
#include "State.h"

extern SdFat32 sd;

// New database-based functions
bool loadDatabase();
bool buildArtistList();
bool buildAlbumList(const std::string &artist);
bool buildSongList(const std::string &artist, const std::string &album);

#endif