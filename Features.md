# Rouge MP3 Player — Features & Roadmap

## Completed Features

### Audio & Playback

| Feature | Details |
|---|---|
| MP3 playback | arduino-audio-tools + Helix MP3 decoder; streams from SD card |
| M4A (AAC) playback | Helix AAC decoder with VolumeStream; M4A box layout metadata pre-stored in `music.db` so playback begins instantly (no runtime file scan) |
| Bluetooth A2DP source | Connects to wireless speakers/headphones as an audio source |
| 128KB PSRAM audio buffer | Ring buffer in PSRAM feeds the BT stack; keeps playback smooth |
| Auto-advance (next track) | Walks forward through songs → albums → artists automatically |
| Auto-previous (prev track) | Walks backward through songs → albums → artists |
| Play / Pause / Resume / Stop | Full playback state machine with clean buffer resets |
| Volume control | Encoder-activated from Now Playing; applies to both MP3 and M4A; saved to NVS with 3s debounce |
| Auto-fade between tracks | Enabled via arduino-audio-tools `setAutoFade(true)` |
| Bluetooth reconnect / disconnect | MAC-based reconnect using stored device addresses; disconnect and reconnect without restarting; uses `reconnect()` directly (no A2DP stack reinit) |
| Bluetooth device scanning | Scans for nearby A2DP sink devices and presents a selectable list on screen; scan state and results update in real time |
| Saved BT device list | Up to 5 most-recently-connected devices stored in NVS with MAC addresses; player pre-populates MAC cache from NVS on boot for reliable reconnect; shown in Bluetooth menu with connect/disconnect controls |

### Library & Browsing

| Feature | Details |
|---|---|
| Desktop indexer tool | Scans SD card MP3 and M4A files and writes a SQLite `music.db` with artist/album/song/track metadata; for M4A files also parses the MP4 box structure and stores 7 AAC layout fields (`mdat_start`, `stsz_offset`, `sample_count`, `fixed_size`, `aac_profile`, `aac_sr_idx`, `aac_ch_cfg`) plus JPEG cover art location (`covr_offset`, `covr_size`) for zero-scan fast startup and instant album art display |
| SQLite music database | `music.db` on SD card, queried on-device via Sqlite3Esp32 |
| Artist browser | Sorted artist list loaded from DB |
| Album browser (per artist) | Albums for the selected artist, sorted |
| Song browser (per album) | Songs sorted by track number |
| Menu navigation stack | Back button restores exact scroll position at each level |
| Cross-boundary auto-advance | Next/prev skips across album and artist boundaries seamlessly |

### Display & UI

| Feature | Details |
|---|---|
| ST7789 TFT display | Driven via HSPI at 60 MHz using LovyanGFX; supports 240×320 panel in landscape (320×240 logical, default) and 240×240 square panel — selected at compile time via `-DDISPLAY_240WIDE` |
| Flicker-free sprite rendering | Full-screen sprite in PSRAM (~150 KB for 320-wide, ~112 KB for 240-wide); entire frame composed off-screen then DMA-pushed in one burst |
| Loading spinner | Animated dot-ring spinner runs on a FreeRTOS task during startup |
| Now Playing screen | Shows current artist, album, song title, and album art |
| JPEG album art | Parses ID3v2 APIC frames from MP3 files and `covr` box data from M4A files; renders via TJpg_Decoder into the sprite |
| Song progress bar | Full-width bar at bottom of Now Playing; fills green proportional to elapsed/total; resets on track change |
| Elapsed / total time | MM:SS elapsed (left) and MM:SS total (right) below the progress bar; freezes while paused; shows `--:--` if duration missing from DB |
| Header bar | Displays current context title, battery percentage, and charging indicator |
| Control bar | Shows play/pause state icon and scroll position indicator |
| Adjustable text size | Small, Medium, and Large modes using DejaVu9 / DejaVu12 / DejaVu18 fonts respectively; toggled from Settings and saved to NVS |
| Fast alphabetic scroll | On artist and album lists with ≥ 12 items: spin encoder quickly (8 ticks < 60 ms apart) to enter alpha-jump mode; encoder then steps through first letters present in the list; large centered letter overlay shows current position; 600 ms of idle exits alpha mode and leaves cursor at the selected letter's first item |
| Header bar font | Header rendered with DejaVu18 for improved readability |
| Screen brightness control | PWM-controlled via LovyanGFX; encoder-adjusted from Settings; saved to NVS |
| Scroll position indicator | Shows current index within a list |
| Themes | Dark theme (default) and Light theme (white-based); toggled from Settings; `applyTheme()` swaps 7 runtime color globals; saved to NVS |

### Hardware

| Feature | Details |
|---|---|
| Adafruit Feather ESP32 V2 | ESP32-PICO-MINI-02, ECO3 silicon, 2MB PSRAM, USB-C |
| Rotary encoder navigation | Direction filtering and anti-jump protection; drives all list scrolling |
| 4 physical buttons | Menu/Back (tap), Play/Pause, Previous track, Next track; long-press Top (Back) → jump to Home menu; long-press Bottom (Play/Pause) → jump to Now Playing screen |
| Haptic feedback | DRV2605L with 6 distinct effects: scroll tick, click, confirm double-click, menu transition, error buzz, back |
| Battery monitoring | GPIO35 voltage divider; LiPo discharge curve with linear interpolation; charging detection; moving average smoothing |

### Settings & Persistence

| Feature | Details |
|---|---|
| NVS preferences | `RougePreferences` class stores volume, brightness, text size, theme, shuffle mode, and last-played position across power cycles |
| Watchdog timer | 30-second hardware watchdog prevents lockup |
| FreeRTOS display task | Display runs on Core 0 at 50ms intervals; audio processing runs on Core 1 |
| Display mutex | `displayMutex` protects the display bus between the spinner task and the main display task |
| Auto-dim + screen-off | Dims to ~6% brightness after 30s of inactivity; turns screen fully off after 5 min of no input (all player states); smooth fade in/out; wakes on button press |
| 1-hour auto-stop | After 1 hour of no input while playing, playback stops automatically |
| BT sleep disconnect | After 15 min screen-off, A2DP connection is dropped (~10–15 mA savings); auto-reconnects on wake |
| Deep sleep | After 15 min screen-off with no active playback, enters ESP32 deep sleep (<1 mA); backlight held LOW via GPIO hold; CENTER button (GPIO4, EXT0 wakeup) wakes the device; full boot on wake with GPIO holds released before display init |
| Button lock | Long-press LEFT toggles button lock; all button/encoder input suppressed when locked; padlock icon shown in header; locked state visible even when screen is dimmed |
| Resume on boot | Restores last-played artist/album/song position from NVS on startup so Play works immediately; toggleable in Settings |
| Shuffle mode | Three modes: Off / Song-level (random within current album) / Library-wide (random artist→album→song); toggled from Settings or Center button on Now Playing; indicator shown on Now Playing screen; saved to NVS |

---

## Roadmap

### Stubbed / Placeholder Features (Already in UI, Not Yet Wired)

- [ ] **Repeat mode** — toggle exists in Settings menu (`Repeat: Off`) but has no effect; needs single-track and full-library repeat modes, wired into `autoNext()`
- [ ] **Album browser** — "Albums" in Music menu navigates nowhere; intended to browse all albums across artists without picking an artist first
- [ ] **All Songs browser** — "All Songs" in Music menu navigates nowhere; intended as a flat list of every song in the library
- [ ] **Playlists** — "Playlists" in Music menu is a placeholder; would require a playlist format (e.g. M3U) and indexer support
- [ ] **About screen** — "About" in Settings menu does nothing; could show firmware version, library stats (artist/album/song count), and free memory

### Audio & Playback

- [ ] **Additional audio formats** — MP3 and M4A/AAC are supported; candidates for future formats are FLAC, OGG Vorbis, and WAV — each requires a matching arduino-audio-tools codec and potentially more PSRAM buffer headroom
- [ ] **Sleep timer** — auto-pause/stop playback after N minutes; configurable from Settings

### Library / Browsing

- [ ] **Off-device library cleanup tool** — a desktop script (Python or Node) that scans the SD card's SQLite database and flags or merges artists with similar spellings (e.g. `"Beatles"` vs `"The Beatles"`); could use fuzzy matching and write corrections back to the DB without touching the MP3 files
- [ ] **Search / filter** — type-ahead filtering for large lists (alphabetic jump is implemented; type-ahead would require on-screen keyboard or rotary-based character input)
- [ ] **Song count badge** — show number of songs or albums next to each artist/album in list view

### Display / UI

- [ ] **Additional font options** — DejaVu9/12/18 are now in use for content and headers; candidates for further improvement include proportional fonts or U8g2 anti-aliased fonts for a more polished look
- [ ] **PNG album art support** — `AlbumArt.cpp` only decodes JPEG (`FF D8` check); PNG (`89 50 4E 47`) embedded art is silently skipped
- [ ] **Scrolling song title** — long titles in Now Playing are truncated; a marquee scroll would show the full title
- [ ] **Now Playing layout with album art** — currently art fills most of the screen; consider a compact layout showing art + title + artist + progress bar together
- [ ] **Animated playback icon** — the play/pause triangle in the control bar could animate (pulsing or spinning) while buffering
- [ ] **Button lock mode** — a button combination (e.g. double-tap Top, or hold Left+Right simultaneously) toggles a locked state that ignores all playback controls (play/pause, skip, volume); a lock icon overlay or header indicator shows when locked so the user knows why buttons aren't responding. When the screen is dimmed and locked, any button press should still wake the display to show the lock indicator rather than silently doing nothing. Unlock uses the same combination.

### Settings / Persistence

- [ ] **Volume in Settings** — volume is encoder-activated from Now Playing, but not visible or adjustable from the Settings menu directly

### Infrastructure / Code Health

- [ ] **Remove duplicate `#define` blocks in `State.h`** — `VOLUME_TIMEOUT`, `VOLUME_ACTIVATION_TICKS`, `VOLUME_SAVE_DELAY`, `BATTERY_CHECK_INTERVAL`, and `BRIGHTNESS_TIMEOUT` are each defined twice (lines 12-18 and lines 117-131)
- [ ] **Unit / integration test harness** — native PlatformIO env for testing Database queries and Navigation logic off-device
