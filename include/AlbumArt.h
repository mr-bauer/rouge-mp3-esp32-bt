#ifndef ALBUM_ART_H
#define ALBUM_ART_H

#include <SdFat.h>

// Returns true if JPEG art was found and buffered.
// For M4A files, pass covrOffset/covrSize from the Song DB record to seek directly
// to the cover art bytes instead of scanning ID3 tags.
bool loadAlbumArt(SdFat32& sd, const char* path,
                  uint64_t covrOffset = 0, uint32_t covrSize = 0);
void drawAlbumArt(int x, int y, int maxSize);  // Decode + render to display
void clearAlbumArt();  // Free PSRAM buffer

#endif
