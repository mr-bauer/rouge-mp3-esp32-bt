# Music Indexer — Desktop Tool

`music_indexer.py` is a Python script that scans your music library on an SD card and builds the SQLite database (`music.db`) that the Rouge Audio Player loads at startup. You run it once on your computer whenever you add or reorganize music on the SD card.

---

## Prerequisites

Python 3.8 or later, and the `mutagen` library for reading audio tags:

```bash
pip install mutagen
```

---

## SD Card Layout

The indexer expects MP3 and M4A files organized in a three-level folder structure:

```
SD_CARD/
└── Music/
    └── <Artist Name>/
        └── <Album Name>/
            ├── 01 - Track One.mp3
            ├── 02 - Track Two.m4a
            └── ...
```

The `<Artist Name>` and `<Album Name>` folders are used as fallbacks if tags are missing. If tags are present, the indexer uses the tag values and ignores the folder names.

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
| `--source id3` | (default) Read title, artist, album, track, and year from embedded audio tags (ID3 for MP3, iTunes atoms for M4A) |
| `--source folder` | Derive artist from the parent folder name, album from the subfolder name, and title from the filename; leading track-number prefixes are stripped automatically |
| `-v`, `--verbose` | Print each file as it is processed, including M4A box-parsing details |
| `--verify` | After indexing, print a sample of artists, albums, and songs to confirm the database looks correct |

```bash
# Use audio tags (default)
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db

# Use folder/filename structure instead of tags
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db --source folder

# Verbose output — shows every file, including M4A parse results
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db -v

# Verify after indexing — recommended for first run
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db --verify

# Both
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db -v --verify
```

In `--source folder` mode the indexer assumes:
```
Music/
└── Artist Name/
    └── Album Name/
        ├── 01 - Track Title.mp3
        └── 02 - Another Track.m4a
```
Leading track-number prefixes (`"01 - "`, `"02. "`, `"3 "`, etc.) are stripped from filenames to produce clean titles.

---

## What the Indexer Does

1. Recursively walks `music_folder` for `.mp3` and `.m4a` files; all other file types are skipped
2. Skips macOS system files automatically (`.DS_Store`, `._*`, `.Spotlight-V100`, `.Trashes`, and any hidden file)
3. **MP3 files:** reads ID3 tags using `mutagen` — extracts title, artist, album, track number, year, and duration
4. **M4A files:** reads iTunes metadata atoms using `mutagen`, then additionally parses the raw MP4 box structure to extract the AAC audio layout needed for fast on-device playback (see [M4A Metadata Extraction](#m4a-metadata-extraction) below)
5. Falls back to `"Unknown Artist"` / `"Unknown Album"` / the filename if tags are missing
6. Normalizes text: decomposes Unicode (removes accents), replaces smart quotes and dashes, strips non-printable characters — all metadata is stored as plain ASCII for reliable rendering on the ESP32 display
7. Stores file paths as `Music/<relative_path>` — matching how the ESP32 opens files from the SD card root
8. Commits to SQLite in batches of 100 songs to handle large libraries without memory pressure
9. Prints a summary (artists, albums, songs indexed; M4A metadata success/failure counts; errors; duplicates skipped)

---

## M4A Metadata Extraction

For M4A files the indexer performs an additional binary parse of the MP4 box (atom) structure to extract seven fields that the ESP32 uses to begin playback instantly — without scanning the file at runtime:

| Field | Description |
|-------|-------------|
| `mdat_start` | Byte offset where AAC audio data begins (after the `mdat` box header) |
| `stsz_offset` | Byte offset of the `stsz` (sample size) box |
| `sample_count` | Total number of audio samples in the file |
| `fixed_size` | Fixed sample size in bytes (0 = variable-size samples) |
| `aac_profile` | AAC audio object type (2 = AAC-LC) |
| `aac_sr_idx` | Sample rate index (4 = 44 100 Hz, per ISO 14496-3 table) |
| `aac_ch_cfg` | Channel configuration (2 = stereo) |

Without these fields the player would need to scan each M4A file from the beginning before starting playback (noticeable delay). With them, playback begins immediately.

**What the parse covers:**

- Recursively walks the MP4 box tree (`moov → trak → mdia → minf → stbl`)
- Locates the `stsz` box to read sample count and fixed sample size
- Locates the `mdat` box to record the audio data start offset
- Locates the `esds` descriptor inside the `mp4a` audio sample entry and reads the two-byte `AudioSpecificConfig` word to decode profile, sample rate index, and channel config

If any box is missing or the parse fails, the fields default to 0 and a warning is printed. The player detects this and falls back to a full runtime file scan — playback still works, just with a brief delay at start.

The `-v` / `--verbose` flag prints per-file parse results:

```
  ✅ mdat@120355 stsz@557 8753 samples profile=2 sr=44100Hz ch=2
```

A summary line is always printed at the end:

```
M4A meta:  42/42 files parsed ✅ (fast startup)
```

or, if some files failed:

```
M4A meta:  39/42 files parsed ✅ (3 missing — those songs will scan at runtime)
```

---

## Troubleshooting

### Artist names appear as duplicates (e.g. "Beatles" and "The Beatles")

The indexer uses the exact value from each file's tag. If your library has inconsistent tagging, you will see separate entries for each spelling.

**Fix:** Use a tag editor to normalize your library before indexing:
- [MusicBrainz Picard](https://picard.musicbrainz.org/) — free, automatic tag matching against the MusicBrainz database
- [beets](https://beets.io/) — command-line tool with powerful auto-tagging and normalization
- [Mp3tag](https://www.mp3tag.de/) (Windows/macOS) — manual bulk editing

After fixing tags, re-run the indexer — it always creates the database from scratch, replacing the previous one.

### Special characters appear wrong on the display

The indexer normalizes all text to printable ASCII (character codes 32–126). Accented characters (é, ü, ñ) have their accents stripped (→ e, u, n). Smart quotes and em dashes are replaced with their ASCII equivalents. If something still looks wrong on the display, run with `--verbose` to see exactly what text is being stored.

### The indexer is slow on a large library

This is normal — reading tags and parsing MP4 boxes from thousands of files over USB can take a few minutes. The indexer commits every 100 songs, so progress is not lost if interrupted. Re-running starts fresh (the old database is deleted first).

### M4A files show "will scan at runtime" in the summary

The indexer could not locate one or more required MP4 boxes (`mdat`, `stsz`, or `esds`). This can happen with:
- Non-standard M4A encoders that use unusual box layouts
- Files encoded with ALAC (lossless) rather than AAC — ALAC is not supported by the player
- Corrupted or truncated M4A files

Run with `--verbose` to see which files failed and why. The player will still play these songs; it just performs a short runtime scan of the file first.

### "Error reading" messages for certain files

Files with corrupt tags or non-standard formatting may fail metadata extraction. The indexer skips these files and counts them as errors in the summary. Use `--verbose` to see exactly which files failed, then inspect them with a tag editor.

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
    file_size    INTEGER,   -- bytes

    -- M4A / AAC layout metadata (populated for .m4a files; 0 for MP3)
    -- Used by the ESP32 to begin playback instantly without scanning the file.
    mdat_start   INTEGER DEFAULT 0,  -- byte offset of audio data start (after mdat header)
    stsz_offset  INTEGER DEFAULT 0,  -- byte offset of the stsz (sample sizes) box
    sample_count INTEGER DEFAULT 0,  -- total number of audio samples
    fixed_size   INTEGER DEFAULT 0,  -- fixed sample size in bytes (0 = variable)
    aac_profile  INTEGER DEFAULT 2,  -- AAC audio object type (2 = AAC-LC)
    aac_sr_idx   INTEGER DEFAULT 4,  -- sample rate index per ISO 14496-3 (4 = 44100 Hz)
    aac_ch_cfg   INTEGER DEFAULT 2   -- channel configuration (2 = stereo)
);

-- Indexes for fast lookup
CREATE INDEX idx_artists_name ON artists(name);
CREATE INDEX idx_albums_artist ON albums(artist_id);
CREATE INDEX idx_songs_album ON songs(album_id);
CREATE INDEX idx_songs_title ON songs(title);
```

The ESP32 loads this database entirely into PSRAM at boot using `sqlite3_deserialize()`, then runs all queries in memory for instant response times. The seven M4A layout columns are loaded alongside title/path/duration when a song list is opened — no extra queries at playback time.

---

## Re-indexing

The indexer **always replaces** the existing `music.db` — it does not merge or update incrementally. Whenever you add, remove, or rename music on the SD card, run the indexer again from scratch.

```bash
# Quick re-index after adding new albums
python3 music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db
```
