# Music Indexer — Desktop Tool

`music_indexer.py` is a Python script that scans your music library on an SD card and builds the SQLite database (`music.db`) that the Rouge MP3 Player loads at startup. You run it once on your computer whenever you add or reorganize music on the SD card.

---

## Prerequisites

Python 3.8 or later, and the `mutagen` library for reading ID3 tags:

```bash
pip install mutagen
```

---

## SD Card Layout

The indexer expects MP3 files organized in a three-level folder structure:

```
SD_CARD/
└── Music/
    └── <Artist Name>/
        └── <Album Name>/
            ├── 01 - Track One.mp3
            ├── 02 - Track Two.mp3
            └── ...
```

The `<Artist Name>` and `<Album Name>` folders are used as fallbacks if ID3 tags are missing. If ID3 tags are present, the indexer uses the tag values and ignores the folder names.

After running the indexer, `music.db` goes in the SD card root:

```
SD_CARD/
├── Music/
│   └── ...
└── music.db    ← generated here
```

---

## Usage

### Basic

```bash
python3 music_indexer.py <music_folder> <output_db>
```

| Argument | Description |
|----------|-------------|
| `music_folder` | Path to the `Music/` folder on the SD card |
| `output_db` | Output path for `music.db` — should be the SD card root |

### Examples

```bash
# macOS
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db

# Windows
python music_indexer.py D:\Music D:\music.db

# Linux
python3 music_indexer.py /media/user/SD_CARD/Music /media/user/SD_CARD/music.db
```

### Options

| Flag | Description |
|------|-------------|
| `-v`, `--verbose` | Print each MP3 file as it is processed |
| `--verify` | After indexing, print a sample of artists, albums, and songs to confirm the database looks correct |

```bash
# Verbose output — shows every file
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db -v

# Verify after indexing — recommended for first run
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db --verify

# Both
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db -v --verify
```

---

## What the Indexer Does

1. Recursively walks `music_folder` for `.mp3` files only; all other file types are skipped
2. Skips macOS system files automatically (`.DS_Store`, `._*`, `.Spotlight-V100`, `.Trashes`, and any hidden file)
3. Reads ID3 tags from each MP3 using `mutagen` — extracts title, artist, album, track number, year, and duration
4. Falls back to `"Unknown Artist"` / `"Unknown Album"` / the filename if tags are missing
5. Normalizes text: decomposes Unicode (removes accents), replaces smart quotes and dashes, strips non-printable characters — all metadata is stored as plain ASCII for reliable rendering on the ESP32 display
6. Stores file paths as `Music/<relative_path>` — matching how the ESP32 opens files from the SD card root
7. Commits to SQLite in batches of 100 songs to handle large libraries without memory pressure
8. Prints a summary (artists, albums, songs indexed; errors; duplicates skipped)

---

## Troubleshooting

### Artist names appear as duplicates (e.g. "Beatles" and "The Beatles")

The indexer uses the exact value from each file's ID3 `artist` tag. If your library has inconsistent tagging, you will see separate entries for each spelling.

**Fix:** Use a tag editor to normalize your library before indexing:
- [MusicBrainz Picard](https://picard.musicbrainz.org/) — free, automatic tag matching against the MusicBrainz database
- [beets](https://beets.io/) — command-line tool with powerful auto-tagging and normalization
- [Mp3tag](https://www.mp3tag.de/) (Windows/macOS) — manual bulk editing

After fixing tags, re-run the indexer — it always creates the database from scratch, replacing the previous one.

### Special characters appear wrong on the display

The indexer normalizes all text to printable ASCII (character codes 32–126). Accented characters (é, ü, ñ) have their accents stripped (→ e, u, n). Smart quotes and em dashes are replaced with their ASCII equivalents. If something still looks wrong on the display, run with `--verbose` to see exactly what text is being stored.

### The indexer is slow on a large library

This is normal — reading ID3 tags from thousands of files over USB can take a few minutes. The indexer commits every 100 songs, so progress is not lost if interrupted. Re-running starts fresh (the old database is deleted first).

### "Error reading" messages for certain files

Files with corrupt ID3 tags or non-standard MP3 formatting may fail metadata extraction. The indexer skips these files and counts them as errors in the summary. Use `--verbose` to see exactly which files failed, then inspect them with a tag editor.

### The ESP32 shows "Database Error — Run indexer tool"

This means `music.db` was not found on the SD card root, or the file is corrupt. Make sure:
1. The output path ends with `music.db` at the SD card root, not inside the `Music/` folder
2. The SD card was safely ejected before inserting it into the ESP32
3. The SD card is FAT32 formatted (exFAT is not supported)

---

## Database Schema

For reference, or if you want to query `music.db` directly (e.g. to write a cleanup script):

```sql
CREATE TABLE artists (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT    NOT NULL UNIQUE
);

CREATE TABLE albums (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    artist_id INTEGER NOT NULL REFERENCES artists(id),
    name      TEXT    NOT NULL,
    year      INTEGER,
    UNIQUE(artist_id, name)
);

CREATE TABLE songs (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    album_id     INTEGER NOT NULL REFERENCES albums(id),
    title        TEXT    NOT NULL,
    path         TEXT    NOT NULL UNIQUE,
    track_number INTEGER,
    duration     INTEGER,   -- seconds
    file_size    INTEGER    -- bytes
);

-- Indexes for fast lookup
CREATE INDEX idx_artists_name ON artists(name);
CREATE INDEX idx_albums_artist ON albums(artist_id);
CREATE INDEX idx_songs_album ON songs(album_id);
CREATE INDEX idx_songs_title ON songs(title);
```

The ESP32 loads this database entirely into PSRAM at boot using `sqlite3_deserialize()`, then runs all queries in memory for instant response times.

---

## Re-indexing

The indexer **always replaces** the existing `music.db` — it does not merge or update incrementally. Whenever you add, remove, or rename music on the SD card, run the indexer again from scratch.

```bash
# Quick re-index after adding new albums
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db
```
