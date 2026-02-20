#ifndef ALBUM_ART_H
#define ALBUM_ART_H

#include <SdFat.h>

bool loadAlbumArt(SdFat32& sd, const char* mp3Path);  // Returns true if JPEG art was found and buffered
void drawAlbumArt(int x, int y, int maxSize);  // Decode + render to display
void clearAlbumArt();  // Free PSRAM buffer

#endif
