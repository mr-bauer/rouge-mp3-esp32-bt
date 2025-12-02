#ifndef INDEXER_H
#define INDEXER_H

#include <SdFat.h>
#include "State.h"

extern SdFat sd;

bool loadIndex();
bool buildArtistList();
bool buildAlbumList(const std::string &artist);
bool buildSongList(const std::string &artist, const std::string &album);

#endif