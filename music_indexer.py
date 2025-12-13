#!/usr/bin/env python3
"""
Rouge MP3 Player - Music Library Indexer
Scans music folder and creates SQLite database for ESP32

Usage:
    python music_indexer.py <music_folder> <output_db>
    
Example:
    python music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db
"""

import os
import sqlite3
from pathlib import Path
from mutagen.mp3 import MP3
from mutagen.easyid3 import EasyID3
import sys
import argparse
import unicodedata
import re

def sanitize_text(text):
    """
    Clean text to remove special characters and normalize whitespace.
    Ensures only printable ASCII characters for the MP3 player display.
    """
    if not text:
        return text
    
    # Normalize Unicode (decompose accented characters)
    # NFD = Canonical Decomposition
    text = unicodedata.normalize('NFD', text)
    
    # Remove combining characters (accents)
    text = ''.join(char for char in text if unicodedata.category(char) != 'Mn')
    
    # Replace various problematic characters
    replacements = {
        '\u2013': '-',  # en dash
        '\u2014': '-',  # em dash
        '\u2018': "'",  # left single quote
        '\u2019': "'",  # right single quote
        '\u201C': '"',  # left double quote
        '\u201D': '"',  # right double quote
        '\u2026': '...',  # ellipsis
        '\u00A0': ' ',  # non-breaking space
        '\t': ' ',      # tab
        '\n': ' ',      # newline
        '\r': ' ',      # carriage return
    }
    
    for old, new in replacements.items():
        text = text.replace(old, new)
    
    # Keep only printable ASCII (32-126) and convert others to space
    cleaned = ''.join(char if 32 <= ord(char) <= 126 else ' ' for char in text)
    
    # Normalize whitespace (replace multiple spaces with single space)
    cleaned = re.sub(r'\s+', ' ', cleaned)
    
    # Strip leading/trailing whitespace
    cleaned = cleaned.strip()
    
    return cleaned

def create_database(db_path):
    """Create database schema"""
    print(f"📂 Creating database: {db_path}")
    
    # Remove existing database
    if os.path.exists(db_path):
        os.remove(db_path)
        print("   Removed existing database")
    
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # Create tables
    cursor.execute('''
        CREATE TABLE artists (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        )
    ''')
    
    cursor.execute('''
        CREATE TABLE albums (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            artist_id INTEGER NOT NULL,
            name TEXT NOT NULL,
            year INTEGER,
            FOREIGN KEY (artist_id) REFERENCES artists(id),
            UNIQUE(artist_id, name)
        )
    ''')
    
    cursor.execute('''
        CREATE TABLE songs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            album_id INTEGER NOT NULL,
            title TEXT NOT NULL,
            path TEXT NOT NULL UNIQUE,
            track_number INTEGER,
            duration INTEGER,
            file_size INTEGER,
            FOREIGN KEY (album_id) REFERENCES albums(id)
        )
    ''')
    
    # Create indexes for fast queries
    cursor.execute('CREATE INDEX idx_songs_album ON songs(album_id)')
    cursor.execute('CREATE INDEX idx_albums_artist ON albums(artist_id)')
    cursor.execute('CREATE INDEX idx_songs_title ON songs(title)')
    cursor.execute('CREATE INDEX idx_artists_name ON artists(name)')
    
    conn.commit()
    print("✅ Database schema created")
    return conn

def get_or_create_artist(cursor, name):
    """Get artist ID, create if doesn't exist"""
    cursor.execute('SELECT id FROM artists WHERE name = ?', (name,))
    row = cursor.fetchone()
    
    if row:
        return row[0]
    
    cursor.execute('INSERT INTO artists (name) VALUES (?)', (name,))
    return cursor.lastrowid

def get_or_create_album(cursor, artist_id, name, year=None):
    """Get album ID, create if doesn't exist"""
    cursor.execute('SELECT id FROM albums WHERE artist_id = ? AND name = ?', 
                   (artist_id, name))
    row = cursor.fetchone()
    
    if row:
        return row[0]
    
    cursor.execute('INSERT INTO albums (artist_id, name, year) VALUES (?, ?, ?)', 
                   (artist_id, name, year))
    return cursor.lastrowid

def should_skip_file(filename):
    """Check if file should be skipped (Mac system files, etc.)"""
    # Skip Mac resource fork files
    if filename.startswith('._'):
        return True
    
    # Skip Mac metadata files
    if filename == '.DS_Store':
        return True
    
    # Skip Spotlight indexing files
    if filename == '.Spotlight-V100':
        return True
    
    # Skip Trash
    if filename == '.Trashes':
        return True
    
    # Skip hidden files
    if filename.startswith('.'):
        return True
    
    return False

def extract_metadata(file_path):
    """Extract metadata from MP3 file"""
    try:
        audio = MP3(file_path, ID3=EasyID3)
        
        # Get basic info and sanitize
        title = audio.get('title', [os.path.basename(file_path)])[0]
        title = sanitize_text(title)
        
        artist = audio.get('artist', ['Unknown Artist'])[0]
        artist = sanitize_text(artist)
        
        album = audio.get('album', ['Unknown Album'])[0]
        album = sanitize_text(album)
        
        # Track number
        track = audio.get('tracknumber', ['0'])[0]
        try:
            # Handle formats like "3/12" or just "3"
            track_num = int(track.split('/')[0])
        except:
            track_num = 0
        
        # Year
        year = audio.get('date', [None])[0]
        if year:
            try:
                year = int(year[:4])
            except:
                year = None
        
        # Duration and file size
        duration = int(audio.info.length)
        file_size = os.path.getsize(file_path)
        
        return {
            'title': title,
            'artist': artist,
            'album': album,
            'track_number': track_num,
            'year': year,
            'duration': duration,
            'file_size': file_size
        }
    except Exception as e:
        print(f"⚠️  Error reading {file_path}: {e}")
        return None

def scan_music_folder(music_folder, db_path, verbose=False):
    """Scan music folder and populate database"""
    print(f"📁 Scanning: {music_folder}")
    print(f"💾 Database: {db_path}")
    print()
    
    conn = create_database(db_path)
    cursor = conn.cursor()
    
    song_count = 0
    error_count = 0
    skipped_count = 0
    mac_files_skipped = 0
    non_mp3_skipped = 0
    
    # Get base path for relative paths
    music_folder = os.path.abspath(music_folder)
    
    # Scan for MP3 files ONLY
    mp3_files = []
    for root, dirs, files in os.walk(music_folder):
        # Remove hidden directories from search
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        
        for file in files:
            # Skip Mac system files and hidden files
            if should_skip_file(file):
                mac_files_skipped += 1
                if verbose:
                    print(f"⏭️  Skipping system file: {file}")
                continue
            
            # STRICT: Only accept .mp3 files
            if file.lower().endswith('.mp3'):
                mp3_files.append(os.path.join(root, file))
            else:
                # Count non-MP3 files for reporting
                if not file.startswith('.'):
                    non_mp3_skipped += 1
                    if verbose:
                        print(f"⏭️  Skipping non-MP3: {file}")
    
    total_files = len(mp3_files)
    print(f"🔍 Found {total_files} MP3 files")
    if mac_files_skipped > 0:
        print(f"   Skipped {mac_files_skipped} Mac system files")
    if non_mp3_skipped > 0:
        print(f"   Skipped {non_mp3_skipped} non-MP3 files")
    print()
    
    # Process each file
    for idx, file_path in enumerate(mp3_files, 1):
        # Calculate relative path (remove music_folder prefix)
        relative_path = os.path.relpath(file_path, music_folder)
        
        # For ESP32, paths should start with Music/
        esp32_path = f"Music/{relative_path}"
        
        if verbose:
            print(f"[{idx}/{total_files}] Processing: {relative_path}")
        
        # Extract metadata (now includes sanitization)
        metadata = extract_metadata(file_path)
        
        if not metadata:
            error_count += 1
            continue
        
        try:
            # Get or create artist
            artist_id = get_or_create_artist(cursor, metadata['artist'])
            
            # Get or create album
            album_id = get_or_create_album(cursor, artist_id, 
                                          metadata['album'], 
                                          metadata['year'])
            
            # Insert song
            cursor.execute('''
                INSERT INTO songs (album_id, title, path, track_number, duration, file_size)
                VALUES (?, ?, ?, ?, ?, ?)
            ''', (album_id, metadata['title'], esp32_path, 
                  metadata['track_number'], metadata['duration'], 
                  metadata['file_size']))
            
            song_count += 1
            
            # Commit every 100 songs
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
    
    # Final commit
    conn.commit()
    
    # Print summary
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
    if error_count > 0:
        print(f"   Errors:   {error_count}")
    if skipped_count > 0:
        print(f"   Skipped:  {skipped_count} (duplicates)")
    if mac_files_skipped > 0:
        print(f"   Mac files skipped: {mac_files_skipped}")
    if non_mp3_skipped > 0:
        print(f"   Non-MP3 files skipped: {non_mp3_skipped}")
    print("=" * 50)
    print()
    print(f"📋 Database saved to: {db_path}")
    print(f"📏 Database size: {os.path.getsize(db_path) / 1024:.1f} KB")
    
    conn.close()

def verify_database(db_path):
    """Verify database integrity and show sample data"""
    print()
    print("🔍 Verifying database...")
    
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # Check tables exist
    cursor.execute("SELECT name FROM sqlite_master WHERE type='table'")
    tables = [row[0] for row in cursor.fetchall()]
    
    required_tables = ['artists', 'albums', 'songs']
    for table in required_tables:
        if table in tables:
            print(f"   ✅ Table '{table}' exists")
        else:
            print(f"   ❌ Table '{table}' missing!")
            return False
    
    # Show sample data
    print()
    print("📊 Sample data:")
    
    cursor.execute('SELECT name FROM artists LIMIT 5')
    print("   Artists:")
    for row in cursor.fetchall():
        print(f"      • {row[0]}")
    
    cursor.execute('''
        SELECT albums.name, artists.name 
        FROM albums 
        JOIN artists ON albums.artist_id = artists.id 
        LIMIT 5
    ''')
    print("   Albums:")
    for row in cursor.fetchall():
        print(f"      • {row[0]} - {row[1]}")
    
    cursor.execute('''
        SELECT songs.title, artists.name 
        FROM songs 
        JOIN albums ON songs.album_id = albums.id
        JOIN artists ON albums.artist_id = artists.id
        LIMIT 5
    ''')
    print("   Songs:")
    for row in cursor.fetchall():
        print(f"      • {row[0]} - {row[1]}")
    
    conn.close()
    print()
    print("✅ Database verification complete!")
    return True

def main():
    parser = argparse.ArgumentParser(
        description='Index MP3 files and create SQLite database for Rouge MP3 Player',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  # Mac with SD card mounted
  python music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db
  
  # Windows with SD card as D:
  python music_indexer.py D:\\Music D:\\music.db
  
  # Linux
  python music_indexer.py /media/user/SD_CARD/Music /media/user/SD_CARD/music.db
  
  # Verbose output
  python music_indexer.py /Volumes/SD_CARD/Music /Volumes/SD_CARD/music.db -v
        '''
    )
    
    parser.add_argument('music_folder', help='Path to Music folder on SD card')
    parser.add_argument('output_db', help='Path for output database file (music.db)')
    parser.add_argument('-v', '--verbose', action='store_true', 
                       help='Show detailed progress')
    parser.add_argument('--verify', action='store_true',
                       help='Verify database after creation')
    
    args = parser.parse_args()
    
    # Validate music folder
    if not os.path.exists(args.music_folder):
        print(f"❌ Music folder not found: {args.music_folder}")
        sys.exit(1)
    
    if not os.path.isdir(args.music_folder):
        print(f"❌ Not a directory: {args.music_folder}")
        sys.exit(1)
    
    # Scan and create database
    try:
        scan_music_folder(args.music_folder, args.output_db, args.verbose)
        
        if args.verify:
            verify_database(args.output_db)
        
        print()
        print("🎵 Ready to use with Rouge MP3 Player!")
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