# Rouge MP3 Player — Features & Roadmap

## Completed Features

### Audio & Playback

| Feature | Details |
|---|---|
| MP3 playback | arduino-audio-tools + Helix decoder; streams from SD card |
| Bluetooth A2DP source | Connects to wireless speakers/headphones as an audio source |
| 128KB PSRAM audio buffer | Ring buffer in PSRAM feeds the BT stack; keeps playback smooth |
| Auto-advance (next track) | Walks forward through songs → albums → artists automatically |
| Auto-previous (prev track) | Walks backward through songs → albums → artists |
| Play / Pause / Resume / Stop | Full playback state machine with clean buffer resets |
| Volume control | Encoder-activated from Now Playing; saved to NVS with 3s debounce |
| Auto-fade between tracks | Enabled via arduino-audio-tools `setAutoFade(true)` |
| Bluetooth reconnect / disconnect | Manual reconnect and disconnect from the Bluetooth menu |

### Library & Browsing

| Feature | Details |
|---|---|
| Desktop indexer tool | Scans SD card MP3s and writes a SQLite `music.db` with artist/album/song/track metadata |
| SQLite music database | `music.db` on SD card, queried on-device via Sqlite3Esp32 |
| Artist browser | Sorted artist list loaded from DB |
| Album browser (per artist) | Albums for the selected artist, sorted |
| Song browser (per album) | Songs sorted by track number |
| Menu navigation stack | Back button restores exact scroll position at each level |
| Cross-boundary auto-advance | Next/prev skips across album and artist boundaries seamlessly |

### Display & UI

| Feature | Details |
|---|---|
| ST7789 240×240 TFT display | Driven via HSPI at 60 MHz using LovyanGFX |
| Flicker-free sprite rendering | Full-screen 240×240 sprite in PSRAM; entire frame composed off-screen then DMA-pushed in one burst |
| Loading spinner | Animated dot-ring spinner runs on a FreeRTOS task during startup |
| Now Playing screen | Shows current artist, album, song title, and album art |
| JPEG album art | Parses ID3v2 APIC frames directly from MP3 files; renders via TJpg_Decoder into the sprite |
| Header bar | Displays current context title, battery percentage, and charging indicator |
| Control bar | Shows play/pause state icon and scroll position indicator |
| Adjustable text size | Small (size 1) and Large (size 2) modes, toggled from Settings and saved to NVS |
| Screen brightness control | PWM-controlled via LovyanGFX; encoder-adjusted from Settings; saved to NVS |
| Scroll position indicator | Shows current index within a list |

### Hardware

| Feature | Details |
|---|---|
| Adafruit Feather ESP32 V2 | ESP32-PICO-MINI-02, ECO3 silicon, 2MB PSRAM, USB-C |
| Rotary encoder navigation | Direction filtering and anti-jump protection; drives all list scrolling |
| 4 physical buttons | Menu/Back, Play/Pause, Previous track, Next track |
| Haptic feedback | DRV2605L with 6 distinct effects: scroll tick, click, confirm double-click, menu transition, error buzz, back |
| Battery monitoring | GPIO35 voltage divider; LiPo discharge curve with linear interpolation; charging detection; moving average smoothing |

### Settings & Persistence

| Feature | Details |
|---|---|
| NVS preferences | `RougePreferences` class stores volume, brightness, and text size across power cycles |
| Watchdog timer | 30-second hardware watchdog prevents lockup |
| FreeRTOS display task | Display runs on Core 0 at 50ms intervals; audio processing runs on Core 1 |
| Display mutex | `displayMutex` protects the display bus between the spinner task and the main display task |

---

## Known Bugs

- [ ] **Audio crash on certain songs** — some MP3 files cause a crash during playback; needs investigation. Enable verbose AudioTools logging (`AudioLogger::instance().begin(Serial, AudioLogger::Debug)`) to capture the offending frame/metadata, then narrow down whether it's a malformed ID3 tag, a Helix decoder edge case, or a buffer overrun

---

## Roadmap

### Stubbed / Placeholder Features (Already in UI, Not Yet Wired)

- [ ] **Shuffle mode** — toggle exists in Settings menu (`Shuffle: Off`) but has no effect on playback; needs state var, NVS persistence, and randomized `autoNext()` path
- [ ] **Repeat mode** — toggle exists in Settings menu (`Repeat: Off`) but has no effect; needs single-track and full-library repeat modes, wired into `autoNext()`
- [ ] **Album browser** — "Albums" in Music menu navigates nowhere; intended to browse all albums across artists without picking an artist first
- [ ] **All Songs browser** — "All Songs" in Music menu navigates nowhere; intended as a flat list of every song in the library
- [ ] **Playlists** — "Playlists" in Music menu is a placeholder; would require a playlist format (e.g. M3U) and indexer support
- [ ] **About screen** — "About" in Settings menu does nothing; could show firmware version, library stats (artist/album/song count), and free memory

### Audio & Playback

- [ ] **Additional audio formats** — currently only `.mp3` is supported (hardcoded `ext = "mp3"` in `AudioManager.cpp`); candidates are FLAC, AAC, OGG Vorbis, and WAV — each requires a matching arduino-audio-tools codec and potentially more PSRAM buffer headroom
- [ ] **Song progress bar** — Now Playing screen has no seek indicator; would require ID3 duration metadata and tracking playback position from the audio buffer
- [ ] **Elapsed / remaining time** — display MM:SS elapsed and/or MM:SS remaining in Now Playing
- [ ] **Sleep timer** — auto-pause/stop playback after N minutes; configurable from Settings

### Bluetooth

- [ ] **Bluetooth device discovery** — scan for nearby A2DP sink devices and present a selectable list on screen; store the chosen device name in NVS (`rougePrefs`) so the player auto-connects to it on next boot without hardcoding `headphoneName`
- [ ] **Make Bluetooth device name configurable** — interim step: move `headphoneName` out of `AudioManager.cpp` into NVS so it survives re-flash without code changes (blocked by device discovery above for the full solution)

### Library / Browsing

- [ ] **Off-device library cleanup tool** — a desktop script (Python or Node) that scans the SD card's SQLite database and flags or merges artists with similar spellings (e.g. `"Beatles"` vs `"The Beatles"`); could use fuzzy matching and write corrections back to the DB without touching the MP3 files
- [ ] **Search / filter** — type-ahead or alphabetic jump for large artist/song lists (encoder fast-scroll to first letter?)
- [ ] **Song count badge** — show number of songs or albums next to each artist/album in list view
- [ ] **"Jump to Now Playing"** — shortcut from any list screen back to Now Playing without pressing Menu repeatedly (e.g. long-press Bottom)

### Display / UI

- [ ] **Themes** — define a `ColorTheme` struct in `Display.h` and swap the current `COLOR_*` constants at runtime; start with a Dark theme (current) and a Light theme (white background, dark text), with room to add more; save active theme to NVS
- [ ] **Better fonts via LovyanGFX** — LovyanGFX supports custom bitmap fonts (converted with `fontconvert`) and the U8g2 font library; replacing the default 5×7 bitmap font with a proportional or anti-aliased font would significantly improve readability
- [ ] **PNG album art support** — `AlbumArt.cpp` only decodes JPEG (`FF D8` check); PNG (`89 50 4E 47`) embedded art is silently skipped
- [ ] **Scrolling song title** — long titles in Now Playing are truncated; a marquee scroll would show the full title
- [ ] **Now Playing layout with album art** — currently art fills most of the screen; consider a compact layout showing art + title + artist + progress bar together
- [ ] **Animated playback icon** — the play/pause triangle in the control bar could animate (pulsing or spinning) while buffering

### Settings / Persistence

- [ ] **Deep sleep mode** — enter ESP32 deep sleep (via `esp_deep_sleep_start()`) after a configurable period of inactivity; wake on button press using GPIO wakeup; saves significant battery vs. display dim alone
- [ ] **Volume in Settings** — volume is encoder-activated from Now Playing, but not visible or adjustable from the Settings menu directly
- [ ] **Restore last-played position** — remember `artistIndex`, `albumIndex`, `songIndex` across power cycles (save to NVS on track change)
- [ ] **Auto-dim / sleep display** — dim or blank screen after N seconds of inactivity; wake on button press (lighter alternative to full deep sleep)

### Infrastructure / Code Health

- [ ] **Remove duplicate `#define` blocks in `State.h`** — `VOLUME_TIMEOUT`, `VOLUME_ACTIVATION_TICKS`, `VOLUME_SAVE_DELAY`, `BATTERY_CHECK_INTERVAL`, and `BRIGHTNESS_TIMEOUT` are each defined twice (lines 12-18 and lines 117-131)
- [ ] **Unit / integration test harness** — native PlatformIO env for testing Database queries and Navigation logic off-device
