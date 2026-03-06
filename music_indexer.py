#!/usr/bin/env python3
"""
Rouge Audio Player - Music Library Indexer
Scans a music folder and creates a SQLite database for the ESP32.

Supports: .mp3, .m4a

Usage:
    python music_indexer.py <music_folder> <output_db> [options]

Metadata sources (--source):
    id3     (default) Read title/artist/album/track/year from audio tags
            (ID3 for MP3, iTunes atoms for M4A)
    folder  Derive artist from parent folder, album from subfolder,
            title from filename; tags are still used for duration,
            track number, and year where available

Examples:
    # Use audio tags (default)
    python music_indexer.py /Volumes/SD/Music /Volumes/SD/music.db

    # Use folder/filename structure
    python music_indexer.py /Volumes/SD/Music /Volumes/SD/music.db --source folder

    # Verbose output with verification
    python music_indexer.py /Volumes/SD/Music /Volumes/SD/music.db -v --verify
"""

import os
import struct
import sqlite3
from pathlib import Path
from mutagen.mp3 import MP3
from mutagen.easyid3 import EasyID3
from mutagen.mp4 import MP4
from mutagen.easymp4 import EasyMP4
import sys
import argparse
import unicodedata
import re

SUPPORTED_EXTENSIONS = ('.mp3', '.m4a')


# ============================================================================
# TEXT SANITIZATION
# ============================================================================

def sanitize_text(text):
    """
    Normalize text to printable ASCII for the ESP32 display.
    Strips accents, smart quotes, dashes, etc.
    """
    if not text:
        return text

    # Decompose accented characters → base letter + combining accent
    text = unicodedata.normalize('NFD', text)
    # Drop combining accent marks
    text = ''.join(c for c in text if unicodedata.category(c) != 'Mn')

    replacements = {
        '\u2013': '-',   # en dash
        '\u2014': '-',   # em dash
        '\u2018': "'",   # left single quote
        '\u2019': "'",   # right single quote
        '\u201C': '"',   # left double quote
        '\u201D': '"',   # right double quote
        '\u2026': '...',  # ellipsis
        '\u00A0': ' ',   # non-breaking space
        '\t': ' ', '\n': ' ', '\r': ' ',
    }
    for old, new in replacements.items():
        text = text.replace(old, new)

    # Keep only printable ASCII (32–126)
    text = ''.join(c if 32 <= ord(c) <= 126 else ' ' for c in text)
    text = re.sub(r'\s+', ' ', text).strip()
    return text


# ============================================================================
# M4A BOX PARSER — extracts layout metadata for fast ESP32 playback
# ============================================================================

_M4A_CONTAINER_BOXES = {b'moov', b'trak', b'mdia', b'minf', b'stbl', b'udta', b'ilst', b'covr'}
_M4A_AUDIO_ENTRY_BOXES = {b'mp4a', b'alac'}


def _read_box_header(f):
    """Read MP4 box header. Returns (box_start, size, box_type, data_offset) or None."""
    box_start = f.tell()
    raw = f.read(8)
    if len(raw) < 8:
        return None
    size = struct.unpack('>I', raw[:4])[0]
    box_type = raw[4:8]
    data_offset = 8
    if size == 1:
        ext = f.read(8)
        if len(ext) < 8:
            return None
        size = struct.unpack('>Q', ext)[0]
        data_offset = 16
    elif size == 0:
        cur = f.tell()
        f.seek(0, 2)
        size = f.tell() - box_start
        f.seek(cur)
    return box_start, size, box_type, data_offset


def _parse_m4a_boxes_recursive(f, end_pos, result):
    """Recursively parse MP4 boxes within [current pos, end_pos]."""
    while f.tell() < end_pos - 8:
        hdr = _read_box_header(f)
        if hdr is None:
            break
        box_start, size, box_type, data_offset = hdr
        if size < data_offset:
            break
        box_end = box_start + size
        data_start = box_start + data_offset

        if box_type == b'mdat':
            if result['mdat_start'] == 0:
                result['mdat_start'] = data_start

        elif box_type == b'stsz':
            if result['stsz_offset'] == 0:
                result['stsz_offset'] = box_start
                f.seek(data_start)
                stsz_hdr = f.read(12)
                if len(stsz_hdr) == 12:
                    _, fixed_size, sample_count = struct.unpack('>III', stsz_hdr)
                    result['fixed_size'] = fixed_size
                    result['sample_count'] = sample_count

        elif box_type == b'esds':
            f.seek(data_start)
            _parse_esds(f, result)

        elif box_type == b'meta':
            # meta is a "full box": 4 extra bytes (version + flags) before child boxes
            f.seek(data_start + 4)
            _parse_m4a_boxes_recursive(f, box_end, result)

        elif box_type == b'data':
            # Cover art data box inside moov/udta/meta/ilst/covr
            # Format: [4B type indicator][4B locale][raw image bytes]
            if size >= 16:
                f.seek(data_start)
                type_indicator = struct.unpack('>I', f.read(4))[0]
                f.read(4)  # skip locale
                if type_indicator == 0x0000000D and result['covr_offset'] == 0:  # JPEG only
                    result['covr_offset'] = f.tell()   # first byte of JPEG data
                    result['covr_size']   = size - 16  # box size minus 8B header + 4B type + 4B locale

        elif box_type in _M4A_CONTAINER_BOXES:
            f.seek(data_start)
            _parse_m4a_boxes_recursive(f, box_end, result)

        elif box_type in _M4A_AUDIO_ENTRY_BOXES:
            # Audio sample entry: 28-byte fixed header before child boxes
            f.seek(data_start + 28)
            _parse_m4a_boxes_recursive(f, box_end, result)

        f.seek(box_end)


def _read_descriptor(f):
    """Read MPEG-4 descriptor tag and variable-length size. Returns (tag, size)."""
    tag_byte = f.read(1)
    if not tag_byte:
        return None, 0
    tag = tag_byte[0]
    size = 0
    for _ in range(4):
        b = f.read(1)
        if not b:
            break
        b = b[0]
        size = (size << 7) | (b & 0x7F)
        if not (b & 0x80):
            break
    return tag, size


def _parse_esds(f, result):
    """Parse esds box to extract AAC AudioSpecificConfig (profile/sr/channels)."""
    f.read(4)  # version + flags
    tag, _ = _read_descriptor(f)
    if tag != 0x03:
        return
    es_hdr = f.read(3)
    if len(es_hdr) < 3:
        return
    es_flags = es_hdr[2]
    if es_flags & 0x80:   # streamDependenceFlag
        f.read(2)
    if es_flags & 0x40:   # URL_Flag
        url_len = f.read(1)
        if url_len:
            f.read(url_len[0])
    if es_flags & 0x20:   # OCRstreamFlag
        f.read(2)
    tag, _ = _read_descriptor(f)
    if tag != 0x04:
        return
    f.read(13)  # objectTypeIndication + streamType + bufferSize + maxBitrate + avgBitrate
    tag, size = _read_descriptor(f)
    if tag != 0x05 or size < 2:
        return
    asc = f.read(min(size, 4))
    if len(asc) < 2:
        return
    val = (asc[0] << 8) | asc[1]
    # AudioSpecificConfig: [5 bits audioObjectType][4 bits samplingFreqIdx][4 bits channelCfg]
    result['aac_profile'] = (val >> 11) & 0x1F
    result['aac_sr_idx']  = (val >> 7)  & 0x0F
    result['aac_ch_cfg']  = (val >> 3)  & 0x0F


def parse_m4a_boxes(file_path):
    """
    Parse M4A/MP4 box structure to extract layout metadata for fast ESP32 playback
    and cover art location for Now Playing display.

    Returns a dict with: mdat_start, stsz_offset, sample_count, fixed_size,
    aac_profile, aac_sr_idx, aac_ch_cfg, covr_offset, covr_size.
    Returns None if required playback data (mdat/stsz/sample_count) is missing.
    covr_offset/covr_size are 0 if no JPEG cover art was found (non-fatal).
    """
    result = {
        'mdat_start':   0,
        'stsz_offset':  0,
        'sample_count': 0,
        'fixed_size':   0,
        'aac_profile':  2,   # AAC-LC default
        'aac_sr_idx':   4,   # 44100 Hz default
        'aac_ch_cfg':   2,   # stereo default
        'covr_offset':  0,   # byte offset of JPEG cover art data in file (0 = none)
        'covr_size':    0,   # byte count of JPEG cover art data
    }
    try:
        file_size = os.path.getsize(file_path)
        with open(file_path, 'rb') as f:
            _parse_m4a_boxes_recursive(f, file_size, result)
    except Exception as e:
        print(f"⚠️  Error parsing M4A layout from {file_path}: {e}")
        return None

    if result['mdat_start'] == 0 or result['stsz_offset'] == 0 or result['sample_count'] == 0:
        return None
    return result


# ============================================================================
# DATABASE HELPERS
# ============================================================================

def create_database(db_path):
    """Create (or recreate) the database schema."""
    print(f"📂 Creating database: {db_path}")
    if os.path.exists(db_path):
        os.remove(db_path)
        print("   Removed existing database")

    conn = sqlite3.connect(db_path)
    c = conn.cursor()

    c.execute('''
        CREATE TABLE artists (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        )
    ''')
    c.execute('''
        CREATE TABLE albums (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            artist_id INTEGER NOT NULL,
            name      TEXT NOT NULL,
            year      INTEGER,
            FOREIGN KEY (artist_id) REFERENCES artists(id),
            UNIQUE(artist_id, name)
        )
    ''')
    c.execute('''
        CREATE TABLE songs (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            album_id     INTEGER NOT NULL,
            title        TEXT NOT NULL,
            path         TEXT NOT NULL UNIQUE,
            track_number INTEGER,
            duration     INTEGER,
            file_size    INTEGER,
            mdat_start   INTEGER DEFAULT 0,
            stsz_offset  INTEGER DEFAULT 0,
            sample_count INTEGER DEFAULT 0,
            fixed_size   INTEGER DEFAULT 0,
            aac_profile  INTEGER DEFAULT 2,
            aac_sr_idx   INTEGER DEFAULT 4,
            aac_ch_cfg   INTEGER DEFAULT 2,
            covr_offset  INTEGER DEFAULT 0,
            covr_size    INTEGER DEFAULT 0,
            FOREIGN KEY (album_id) REFERENCES albums(id)
        )
    ''')

    c.execute('CREATE INDEX idx_songs_album   ON songs(album_id)')
    c.execute('CREATE INDEX idx_albums_artist ON albums(artist_id)')
    c.execute('CREATE INDEX idx_songs_title   ON songs(title)')
    c.execute('CREATE INDEX idx_artists_name  ON artists(name)')

    conn.commit()
    print("✅ Database schema created")
    return conn


def get_or_create_artist(cursor, name):
    cursor.execute('SELECT id FROM artists WHERE name = ?', (name,))
    row = cursor.fetchone()
    if row:
        return row[0]
    cursor.execute('INSERT INTO artists (name) VALUES (?)', (name,))
    return cursor.lastrowid


def get_or_create_album(cursor, artist_id, name, year=None):
    cursor.execute('SELECT id FROM albums WHERE artist_id = ? AND name = ?',
                   (artist_id, name))
    row = cursor.fetchone()
    if row:
        return row[0]
    cursor.execute('INSERT INTO albums (artist_id, name, year) VALUES (?, ?, ?)',
                   (artist_id, name, year))
    return cursor.lastrowid


# ============================================================================
# FILE FILTERING
# ============================================================================

def should_skip_file(filename):
    """Return True for Mac resource forks, hidden files, and system metadata."""
    return (filename.startswith('._') or
            filename.startswith('.') or
            filename in ('.DS_Store', '.Spotlight-V100', '.Trashes'))


# ============================================================================
# METADATA EXTRACTION — ID3 / TAG MODE
# ============================================================================

def _parse_track(raw):
    """Parse a track string like '3' or '3/12' → int."""
    try:
        return int(str(raw).split('/')[0])
    except Exception:
        return 0


def _parse_year(raw):
    """Parse a year string → int or None."""
    if not raw:
        return None
    try:
        return int(str(raw)[:4])
    except Exception:
        return None


def extract_metadata_tags(file_path):
    """
    Read all metadata from audio tags.
      MP3 → ID3 via EasyID3
      M4A → iTunes atoms via EasyMP4
    Returns a metadata dict or None on failure.
    """
    ext = Path(file_path).suffix.lower()
    try:
        if ext == '.mp3':
            audio   = MP3(file_path, ID3=EasyID3)
            title   = sanitize_text(audio.get('title',  [None])[0]) or Path(file_path).stem
            artist  = sanitize_text(audio.get('artist', ['Unknown Artist'])[0])
            album   = sanitize_text(audio.get('album',  ['Unknown Album'])[0])
            track   = _parse_track(audio.get('tracknumber', ['0'])[0])
            year    = _parse_year(audio.get('date', [None])[0])
            dur     = int(audio.info.length)

        elif ext == '.m4a':
            audio   = EasyMP4(file_path)
            title   = sanitize_text(audio.get('title',  [None])[0]) or Path(file_path).stem
            artist  = sanitize_text(audio.get('artist', ['Unknown Artist'])[0])
            album   = sanitize_text(audio.get('album',  ['Unknown Album'])[0])
            track   = _parse_track(audio.get('tracknumber', ['0'])[0])
            year    = _parse_year(audio.get('date', [None])[0])
            dur     = int(audio.info.length)

        else:
            return None

        return {
            'title':        title   or Path(file_path).stem,
            'artist':       artist  or 'Unknown Artist',
            'album':        album   or 'Unknown Album',
            'track_number': track,
            'year':         year,
            'duration':     dur,
            'sample_rate':  audio.info.sample_rate,
            'file_size':    os.path.getsize(file_path),
        }

    except Exception as e:
        print(f"⚠️  Error reading tags from {file_path}: {e}")
        return None


# ============================================================================
# METADATA EXTRACTION — FOLDER / FILENAME MODE
# ============================================================================

def _title_from_stem(stem):
    """
    Strip a leading track-number prefix from a filename stem.
    Handles patterns like:
        "01 - Song Name"  →  "Song Name"
        "01. Song Name"   →  "Song Name"
        "1 Song Name"     →  "Song Name"
        "Song Name"       →  "Song Name"  (unchanged)
    """
    stripped = re.sub(r'^\d+[\s.\-_]+', '', stem).strip()
    return stripped if stripped else stem


def extract_metadata_folder(file_path, music_folder):
    """
    Derive artist/album/title from folder structure and filename.
    Audio tags are still read for duration, track number, and year.

    Expected folder layout:
        <music_folder>/Artist Name/Album Name/01 - Track Title.mp3

    Falls back gracefully for shallower structures (no album folder, etc.).
    """
    path  = Path(file_path)
    parts = path.relative_to(music_folder).parts
    # parts = ('Artist', 'Album', '01 - Track.mp3')  for a 3-level layout

    # --- Artist and Album from folder names ---
    if len(parts) >= 3:
        artist = sanitize_text(parts[-3]) or 'Unknown Artist'
        album  = sanitize_text(parts[-2]) or 'Unknown Album'
    elif len(parts) == 2:
        artist = sanitize_text(parts[-2]) or 'Unknown Artist'
        album  = 'Unknown Album'
    else:
        artist = 'Unknown Artist'
        album  = 'Unknown Album'

    # --- Title from filename ---
    raw_title = _title_from_stem(path.stem)
    title = sanitize_text(raw_title) or path.stem

    # --- Track number: filename prefix first, tag fallback ---
    track = 0
    m = re.match(r'^(\d+)', path.stem)
    if m:
        try:
            track = int(m.group(1))
        except Exception:
            pass

    # --- Duration and year always from tags ---
    year = None
    dur  = 0
    ext  = path.suffix.lower()
    try:
        if ext == '.mp3':
            audio = MP3(file_path, ID3=EasyID3)
            dur   = int(audio.info.length)
            if track == 0:
                track = _parse_track(audio.get('tracknumber', ['0'])[0])
            year = _parse_year(audio.get('date', [None])[0])

        elif ext == '.m4a':
            audio = EasyMP4(file_path)
            dur   = int(audio.info.length)
            if track == 0:
                track = _parse_track(audio.get('tracknumber', ['0'])[0])
            year = _parse_year(audio.get('date', [None])[0])

    except Exception as e:
        print(f"⚠️  Could not read audio info from {file_path}: {e}")

    return {
        'title':        title,
        'artist':       artist,
        'album':        album,
        'track_number': track,
        'year':         year,
        'duration':     dur,
        'sample_rate':  audio.info.sample_rate if dur > 0 else 44100,
        'file_size':    os.path.getsize(file_path),
    }


# ============================================================================
# MAIN SCAN
# ============================================================================

def scan_music_folder(music_folder, db_path, source='id3', verbose=False):
    """Scan music folder and populate the database."""
    music_folder = os.path.abspath(music_folder)

    source_label = ('Audio tags (ID3/iTunes)' if source == 'id3'
                    else 'Folder & filename structure (tags used for duration/track/year)')
    print(f"📁 Scanning:  {music_folder}")
    print(f"💾 Database:  {db_path}")
    print(f"🏷️  Metadata:  {source_label}")
    print()

    conn   = create_database(db_path)
    cursor = conn.cursor()

    song_count = mp3_count = m4a_count = 0
    m4a_meta_count = m4a_meta_missing = m4a_art_count = 0
    error_count = skipped_count = mac_skipped = other_skipped = sr_warning_count = 0

    # Collect all supported audio files
    audio_files = []
    for root, dirs, files in os.walk(music_folder):
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        for fname in files:
            if should_skip_file(fname):
                mac_skipped += 1
                if verbose:
                    print(f"⏭️  Skipping system file: {fname}")
                continue
            if Path(fname).suffix.lower() in SUPPORTED_EXTENSIONS:
                audio_files.append(os.path.join(root, fname))
            else:
                other_skipped += 1
                if verbose:
                    print(f"⏭️  Skipping unsupported: {fname}")

    total = len(audio_files)
    print(f"🔍 Found {total} audio files")
    if mac_skipped:
        print(f"   Skipped {mac_skipped} Mac system files")
    if other_skipped:
        print(f"   Skipped {other_skipped} unsupported files")
    print()

    for idx, file_path in enumerate(audio_files, 1):
        relative_path = os.path.relpath(file_path, music_folder)
        # Ensure forward slashes for ESP32 paths regardless of host OS
        esp32_path = "Music/" + relative_path.replace(os.sep, '/')
        ext = Path(file_path).suffix.lower()

        if verbose:
            print(f"[{idx}/{total}] {relative_path}")

        if source == 'folder':
            metadata = extract_metadata_folder(file_path, music_folder)
        else:
            metadata = extract_metadata_tags(file_path)

        if not metadata:
            error_count += 1
            continue

        # Warn about non-44.1kHz files — A2DP is fixed at 44100Hz, so these will sound sped up
        sr = metadata.get('sample_rate', 44100)
        if sr != 44100:
            print(f"   ⚠️  Non-standard sample rate ({sr} Hz, expected 44100): {relative_path}")
            print(f"       Fix: ffmpeg -i \"{file_path}\" -ar 44100 \"{file_path}\"")
            sr_warning_count += 1

        # Parse M4A box layout for fast ESP32 startup (no file scanning at runtime)
        m4a_meta = None
        if ext == '.m4a':
            m4a_meta = parse_m4a_boxes(file_path)
            if m4a_meta:
                m4a_meta_count += 1
                if m4a_meta['covr_offset'] > 0:
                    m4a_art_count += 1
                if verbose:
                    sr_table = {0:96000,1:88200,2:64000,3:48000,4:44100,5:32000,
                                6:24000,7:22050,8:16000,9:12000,10:11025,11:8000}
                    sr_hz = sr_table.get(m4a_meta['aac_sr_idx'], '?')
                    covr_info = (f"  covr@{m4a_meta['covr_offset']} ({m4a_meta['covr_size']}B JPEG)"
                                 if m4a_meta['covr_offset'] > 0 else "  no cover art")
                    print(f"   🎵 M4A meta: mdat@{m4a_meta['mdat_start']}, "
                          f"stsz@{m4a_meta['stsz_offset']}, "
                          f"{m4a_meta['sample_count']} samples, "
                          f"profile={m4a_meta['aac_profile']} "
                          f"sr={sr_hz}Hz ch={m4a_meta['aac_ch_cfg']}"
                          f"{covr_info}")
            else:
                m4a_meta_missing += 1
                print(f"   ⚠️  M4A layout parse failed (will fall back to runtime scan): {relative_path}")

        try:
            artist_id = get_or_create_artist(cursor, metadata['artist'])
            album_id  = get_or_create_album(cursor, artist_id,
                                             metadata['album'], metadata['year'])
            if m4a_meta:
                cursor.execute('''
                    INSERT INTO songs (album_id, title, path, track_number, duration, file_size,
                                      mdat_start, stsz_offset, sample_count, fixed_size,
                                      aac_profile, aac_sr_idx, aac_ch_cfg,
                                      covr_offset, covr_size)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ''', (album_id, metadata['title'], esp32_path,
                      metadata['track_number'], metadata['duration'], metadata['file_size'],
                      m4a_meta['mdat_start'], m4a_meta['stsz_offset'],
                      m4a_meta['sample_count'], m4a_meta['fixed_size'],
                      m4a_meta['aac_profile'], m4a_meta['aac_sr_idx'], m4a_meta['aac_ch_cfg'],
                      m4a_meta['covr_offset'], m4a_meta['covr_size']))
            else:
                cursor.execute('''
                    INSERT INTO songs (album_id, title, path, track_number, duration, file_size)
                    VALUES (?, ?, ?, ?, ?, ?)
                ''', (album_id, metadata['title'], esp32_path,
                      metadata['track_number'], metadata['duration'],
                      metadata['file_size']))

            song_count += 1
            if ext == '.mp3':
                mp3_count += 1
            elif ext == '.m4a':
                m4a_count += 1

            if song_count % 100 == 0:
                conn.commit()
                print(f"  💾 Saved {song_count} songs...")

        except sqlite3.IntegrityError:
            skipped_count += 1
            if verbose:
                print(f"⚠️  Duplicate: {relative_path}")
        except Exception as e:
            error_count += 1
            print(f"❌ Error with {relative_path}: {e}")

    conn.commit()

    cursor.execute('SELECT COUNT(*) FROM artists')
    artist_count = cursor.fetchone()[0]
    cursor.execute('SELECT COUNT(*) FROM albums')
    album_count = cursor.fetchone()[0]

    print()
    print("=" * 50)
    print("✅ Indexing complete!")
    print("=" * 50)
    print(f"   Artists:  {artist_count}")
    print(f"   Albums:   {album_count}")
    print(f"   Songs:    {song_count}")
    if mp3_count:
        print(f"   MP3:      {mp3_count}")
    if m4a_count:
        print(f"   M4A:      {m4a_count}")
        if m4a_meta_count:
            print(f"   M4A meta: {m4a_meta_count}/{m4a_count} files parsed ✅ (fast startup)")
        if m4a_art_count:
            print(f"   M4A art:  {m4a_art_count}/{m4a_meta_count} files have JPEG cover art 🖼️")
        if m4a_meta_missing:
            print(f"   M4A meta: {m4a_meta_missing} files missing layout data ⚠️  (runtime scan fallback)")
    if sr_warning_count:
        print(f"   ⚠️  Non-44.1kHz: {sr_warning_count} file(s) — will sound sped up on device")
    if error_count:
        print(f"   Errors:   {error_count}")
    if skipped_count:
        print(f"   Skipped:  {skipped_count} (duplicates)")
    print("=" * 50)
    print()
    print(f"📋 Database saved to: {db_path}")
    print(f"📏 Database size: {os.path.getsize(db_path) / 1024:.1f} KB")

    conn.close()


# ============================================================================
# VERIFICATION
# ============================================================================

def verify_database(db_path):
    """Verify database integrity and show sample data."""
    print()
    print("🔍 Verifying database...")

    conn   = sqlite3.connect(db_path)
    cursor = conn.cursor()

    cursor.execute("SELECT name FROM sqlite_master WHERE type='table'")
    tables = [row[0] for row in cursor.fetchall()]

    for table in ('artists', 'albums', 'songs'):
        if table in tables:
            print(f"   ✅ Table '{table}' exists")
        else:
            print(f"   ❌ Table '{table}' missing!")
            return False

    print()
    print("📊 Sample data:")

    cursor.execute('SELECT name FROM artists LIMIT 5')
    print("   Artists:")
    for row in cursor.fetchall():
        print(f"      • {row[0]}")

    cursor.execute('''
        SELECT albums.name, artists.name
        FROM albums JOIN artists ON albums.artist_id = artists.id
        LIMIT 5
    ''')
    print("   Albums:")
    for row in cursor.fetchall():
        print(f"      • {row[0]} — {row[1]}")

    cursor.execute('''
        SELECT songs.title, artists.name, songs.path
        FROM songs
        JOIN albums  ON songs.album_id    = albums.id
        JOIN artists ON albums.artist_id  = artists.id
        LIMIT 5
    ''')
    print("   Songs:")
    for row in cursor.fetchall():
        print(f"      • {row[0]} — {row[1]}  ({row[2]})")

    conn.close()
    print()
    print("✅ Database verification complete!")
    return True


# ============================================================================
# CLI
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Index MP3/M4A files and create SQLite database for Rouge Audio Player',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  # Default — use audio tags for all metadata
  python music_indexer.py /Volumes/SD/Music /Volumes/SD/music.db

  # Use folder/filename structure (good when tags are unreliable)
  python music_indexer.py /Volumes/SD/Music /Volumes/SD/music.db --source folder

  # Verbose + verify
  python music_indexer.py /Volumes/SD/Music /Volumes/SD/music.db -v --verify

  # Windows
  python music_indexer.py D:\\Music D:\\music.db --source folder
        '''
    )

    parser.add_argument('music_folder', help='Path to Music folder on SD card')
    parser.add_argument('output_db',    help='Path for output database (music.db)')
    parser.add_argument(
        '--source', choices=['id3', 'folder'], default='id3',
        metavar='MODE',
        help=('"id3" (default) reads title/artist/album/track/year from audio tags; '
              '"folder" uses folder names for artist/album and filename for title '
              '(tags still used for duration, track number, and year)')
    )
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show each file as it is processed')
    parser.add_argument('--verify', action='store_true',
                        help='Verify database after creation')

    args = parser.parse_args()

    if not os.path.exists(args.music_folder):
        print(f"❌ Music folder not found: {args.music_folder}")
        sys.exit(1)
    if not os.path.isdir(args.music_folder):
        print(f"❌ Not a directory: {args.music_folder}")
        sys.exit(1)

    try:
        scan_music_folder(args.music_folder, args.output_db,
                          source=args.source, verbose=args.verbose)
        if args.verify:
            verify_database(args.output_db)

        print()
        print("🎵 Ready to use with Rouge Audio Player!")
        print("   1. Eject SD card safely")
        print("   2. Insert into ESP32")
        print("   3. Power on and enjoy!")

    except KeyboardInterrupt:
        print()
        print("⚠️  Interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"❌ Error: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
