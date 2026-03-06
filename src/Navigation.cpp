#include "Navigation.h"
#include "Indexer.h"
#include "Display.h"
#include "AudioManager.h"
#include "State.h"
#include "Haptics.h"
#include "Preferences.h"
#include <esp_random.h>

static void savePlayingContext() {
  playingSongIndex   = songIndex;
  playingAlbumIndex  = albumIndex;
  playingArtistIndex = artistIndex;
  playingArtist      = currentArtist;
  playingAlbum       = currentAlbum;
  rougePrefs.saveLastPlayed(artistIndex, albumIndex, songIndex, currentArtist, currentAlbum);
}

void handleButtonPress(int buttonIndex)
{
  switch (buttonIndex)
  {
  case 0:
    handleCenter();
    break;
  case 1:
    handleLeft();    // Previous track
    break;
  case 2:
    handleTop();     // Menu/Back
    break;
  case 3:
    handleBottom();  // Play/Pause
    break;
  case 4:
    handleRight();   // Next track
    break;
  default:
    Serial.printf("Unhandled button: %d\n", buttonIndex);
    break;
  }
}

void handleCenter()
{
  // Handle menu selections
  if (currentMenu == MENU_MAIN || currentMenu == MENU_MUSIC ||
      currentMenu == MENU_SETTINGS || currentMenu == MENU_BLUETOOTH ||
      currentMenu == MENU_BT_SCAN)
  {
    if (menuIndex >= 0 && menuIndex < (int)currentMenuItems.size()) {
      MenuItem& item = currentMenuItems[menuIndex];
      
      if (!item.enabled) {
        Serial.println("⚠️ Menu item disabled");
        hapticError();
        return;
      }
      
      Serial.printf("Selected: %s -> %d\n", item.label.c_str(), item.action);
      
      hapticSelection();
      
      // Handle Bluetooth menu actions
      if (currentMenu == MENU_BLUETOOTH) {
        if (item.label == "Disconnect") {
          Serial.println("User requested Bluetooth disconnect");
          disconnectBluetooth();
          buildBluetoothMenu();
          displayNeedsUpdate = true;
        } else if (item.action == MENU_BT_SCAN) {
          // "Scan for New..." — start scan and navigate to scan screen
          startBTScan();
          navigateToMenu(MENU_BT_SCAN);
          displayNeedsUpdate = true;
        } else if (item.enabled) {
          // Any enabled item that isn't Disconnect or Scan is a saved device
          Serial.printf("User selected saved BT device: %s\n", item.label.c_str());
          changeBluetoothDevice(String(item.label.c_str()));
          buildBluetoothMenu();
          displayNeedsUpdate = true;
        }
        return;
      }

      // Handle BT scan results
      if (currentMenu == MENU_BT_SCAN) {
        if (!item.enabled) return;  // "Scanning..." or "No devices found" — not selectable
        Serial.printf("User selected BT device: %s\n", item.label.c_str());
        changeBluetoothDevice(String(item.label.c_str()));
        navigateBack();
        displayNeedsUpdate = true;
        return;
      }
      
      if (currentMenu == MENU_SETTINGS) {
        if (item.label == "Brightness") {
          Serial.println("🔆 Entering brightness adjustment");
          brightnessControlActive = true;
          lastBrightnessChange = millis();
          displayNeedsUpdate = true;
          hapticSelection();
          return;
        }

        if (item.label.find("Text Size:") == 0) {
          textSizePreference = (textSizePreference % 3) + 1;  // cycles 1→2→3→1
          Serial.printf("🔤 Text size changed to: %d\n", textSizePreference);
          rougePrefs.saveTextSize(textSizePreference);
          buildSettingsMenu();
          forceDisplayRedraw = true;
          displayNeedsUpdate = true;
          hapticSelection();
          return;
        }

        if (item.label.find("Theme:") == 0) {
          themeIndex = (themeIndex == 0) ? 1 : 0;
          Serial.printf("🎨 Theme changed to: %s\n", themeIndex == 0 ? "Dark" : "Light");
          applyTheme(themeIndex);
          rougePrefs.saveTheme(themeIndex);
          buildSettingsMenu();
          forceDisplayRedraw = true;
          displayNeedsUpdate = true;
          hapticSelection();
          return;
        }

        if (item.label.find("Resume on Boot:") == 0) {
          resumeOnBoot = !resumeOnBoot;
          rougePrefs.saveResumeOnBoot(resumeOnBoot);
          buildSettingsMenu();
          forceDisplayRedraw = true;
          displayNeedsUpdate = true;
          hapticSelection();
          return;
        }

        if (item.label.find("Shuffle:") == 0) {
          shuffleMode = (shuffleMode + 1) % 3;  // cycles 0→1→2→0
          Serial.printf("🔀 Shuffle mode: %d\n", shuffleMode);
          rougePrefs.saveShuffle(shuffleMode);
          buildSettingsMenu();
          forceDisplayRedraw = true;
          displayNeedsUpdate = true;
          hapticSelection();
          return;
        }

        return;
      }
      
      // Navigate to selected menu
      navigateToMenu(item.action);
      displayNeedsUpdate = true;
    }
    return;
  }
  
  // In Now Playing, Center cycles shuffle mode
  if (currentMenu == MENU_NOW_PLAYING) {
    shuffleMode = (shuffleMode + 1) % 3;
    Serial.printf("🔀 Shuffle mode (now playing): %d\n", shuffleMode);
    rougePrefs.saveShuffle(shuffleMode);
    hapticSelection();
    displayNeedsUpdate = true;
    return;
  }
  
  // Handle music browser selections
  if (currentMenu == MENU_ARTIST_LIST)
  {
    if (artistIndex >= 0 && artistIndex < (int)artists.size()) {
      currentArtist = artists[artistIndex];
      
      hapticSelection();
      
      if (buildAlbumList(currentArtist)) {
        buildAlphaIndex(MENU_ALBUM_LIST);
        navigateToMenu(MENU_ALBUM_LIST);
        albumIndex = 0;
      } else {
        Serial.println("⚠️ Failed to load albums");
        hapticError();
      }
    } else {
      Serial.println("❌ Invalid artist index!");
    }
  }
  else if (currentMenu == MENU_ALBUM_LIST)
  {
    if (albumIndex >= 0 && albumIndex < (int)albums.size()) {
      currentAlbum = albums[albumIndex];
      
      hapticSelection();
      
      if (buildSongList(currentArtist, currentAlbum)) {
        buildAlphaIndex(MENU_SONG_LIST);
        navigateToMenu(MENU_SONG_LIST);
        songIndex = 0;
      } else {
        Serial.println("⚠️ Failed to load songs");
        hapticError();
      }
    } else {
      Serial.println("❌ Invalid album index!");
    }
  }
  else if (currentMenu == MENU_SONG_LIST)
  {
    if (songIndex >= 0 && songIndex < (int)songs.size()) {
      hapticSelection();
      
      const Song& selectedSong = songs[songIndex];
      
      // Check if this is the currently playing/paused song
      bool isSameSong = (selectedSong.title == currentTitle);
      
      if (isSameSong && (player_state == STATE_PLAYING || player_state == STATE_PAUSED)) {
        // Same song is already loaded
        if (player_state == STATE_PAUSED) {
          Serial.println("Same song paused, resuming playback");
          resumePlayback();
        } else {
          Serial.println("Same song playing, navigating to Now Playing");
        }
        navigateToMenu(MENU_NOW_PLAYING);
      } else {
        // Different song OR nothing playing - need to start fresh
        if (player_state != STATE_STOPPED) {
          // CRITICAL: Stop current playback first if anything is playing/paused
          Serial.println("Stopping current playback before starting new song");
          stopPlayback();  // This properly stops and resets everything
          delay(100);      // Give it time to clean up
        }

        // Save playing context so autoNext/autoPrevious advance from this position
        savePlayingContext();

        Serial.println("Starting new song");
        playCurrentSong(false);
        navigateToMenu(MENU_NOW_PLAYING);
      }
    } else {
      Serial.println("❌ Invalid song index!");
    }
  }
  
  displayNeedsUpdate = true;
}

void handleTop()
{
  // Top button = Menu/Back (like iPod)
  Serial.println("Top button - Menu/Back");
  
  // If in brightness mode, save and exit
  if (brightnessControlActive) {
    Serial.println("🔆 Exiting brightness control (back button), saving...");
    brightnessControlActive = false;
    rougePrefs.saveBrightness(screenBrightness);
    
    // Force full redraw - UPDATED
    forceDisplayRedraw = true;
    displayNeedsUpdate = true;
    hapticBack();
    return;
  }
  
  navigateBack();
  displayNeedsUpdate = true;
}

void handleBottom()
{
  // Bottom button = Play/Pause
  Serial.println("Bottom button - Play/Pause");
  
  if (player_state == STATE_PLAYING) {
    pausePlayback();
    hapticSelection();
  } else if (player_state == STATE_PAUSED) {
    resumePlayback();
    hapticSelection();
  } else if (player_state == STATE_STOPPED) {
    // If stopped and we have songs, start playing
    if (!songs.empty()) {
      // Save playing context so autoNext advances from this position
      savePlayingContext();
      startPlayback();
      hapticSelection();
      navigateToMenu(MENU_NOW_PLAYING);
    } else {
      Serial.println("No songs loaded");
      hapticError();
    }
  }
  
  displayNeedsUpdate = true;
}

void handleLeft()
{
  // Left button = Previous track
  Serial.println("Left button - Previous track");
  
  if (player_state != STATE_STOPPED && !songs.empty()) {
    autoPrevious();
    hapticSelection();
  } else {
    Serial.println("Not playing or no songs loaded");
    hapticError();
  }
}

void handleRight()
{
  // Right button = Next track
  Serial.println("Right button - Next track");
  
  if (player_state != STATE_STOPPED && !songs.empty()) {
    autoNext();
    hapticSelection();
  } else {
    Serial.println("Not playing or no songs loaded");
    hapticError();
  }
}

void handleTopLongPress()
{
  // Long press Top → jump to Home (main menu), clearing nav stack
  Serial.println("🏠 Top LONG PRESS → Home");
  hapticBack();
  fastScrollActive = false;
  menuStack.clear();
  currentMenu = MENU_MAIN;
  buildMainMenu();
  menuIndex = 0;
  forceDisplayRedraw = true;
  displayNeedsUpdate = true;
}

void handleBottomLongPress()
{
  // Long press Bottom → jump to Now Playing
  Serial.println("🎵 Bottom LONG PRESS → Now Playing");
  hapticSelection();
  navigateToMenu(MENU_NOW_PLAYING);
  displayNeedsUpdate = true;
}

static void navigateInDirection(int dir)
{
  const bool goForward = (dir > 0);
  const char* label = goForward ? "next" : "previous";

  // --- Shuffle mode ---
  if (shuffleMode == 1 && goForward) {
    // Song-level: random song within the currently playing album
    if (playingArtist != currentArtist || playingAlbum != currentAlbum) {
      artistIndex   = playingArtistIndex;
      currentArtist = playingArtist;
      buildAlbumList(currentArtist);
      albumIndex    = playingAlbumIndex;
      currentAlbum  = playingAlbum;
      buildSongList(currentArtist, currentAlbum);
    }
    if (!songs.empty()) {
      songIndex        = (int)(esp_random() % (uint32_t)songs.size());
      playingSongIndex = songIndex;
      playCurrentSong(true);
      displayNeedsUpdate = true;
      Serial.printf("🔀 Shuffle song: %d/%d\n", songIndex, (int)songs.size());
    }
    return;
  }

  if (shuffleMode == 2 && goForward) {
    // Library-wide: random artist → random album → random song
    if (!artists.empty()) {
      artistIndex   = (int)(esp_random() % (uint32_t)artists.size());
      currentArtist = artists[artistIndex];
      if (buildAlbumList(currentArtist) && !albums.empty()) {
        albumIndex   = (int)(esp_random() % (uint32_t)albums.size());
        currentAlbum = albums[albumIndex];
        if (buildSongList(currentArtist, currentAlbum) && !songs.empty()) {
          songIndex = (int)(esp_random() % (uint32_t)songs.size());
          savePlayingContext();
          playCurrentSong(true);
          displayNeedsUpdate = true;
          Serial.printf("🔀 Shuffle library: artist %d, album %d, song %d\n",
                        artistIndex, albumIndex, songIndex);
          return;
        }
      }
    }
    // Fallback to sequential if random pick failed
  }

  // Restore playing context if the user has browsed to a different artist/album
  if (playingArtist != currentArtist || playingAlbum != currentAlbum) {
    Serial.printf("📀 Restoring playing context before auto-%s\n", label);
    artistIndex   = playingArtistIndex;
    currentArtist = playingArtist;
    buildAlbumList(currentArtist);
    albumIndex    = playingAlbumIndex;
    currentAlbum  = playingAlbum;
    buildSongList(currentArtist, currentAlbum);
  }
  songIndex = playingSongIndex;

  // Try next/previous song in current album
  int nextSong = songIndex + dir;
  if (nextSong >= 0 && nextSong < (int)songs.size())
  {
    songIndex        = nextSong;
    playingSongIndex = songIndex;
    playCurrentSong(true);
    displayNeedsUpdate = true;
    logRamSpace(goForward ? "auto next - same album" : "auto previous - same album");
    return;
  }

  // Try next/previous album
  int nextAlbum = albumIndex + dir;
  if (nextAlbum >= 0 && nextAlbum < (int)albums.size())
  {
    albumIndex   = nextAlbum;
    if (goForward) songIndex = 0;
    currentAlbum = albums[albumIndex];

    if (buildSongList(currentArtist, currentAlbum))
    {
      if (!songs.empty())
      {
        songIndex         = goForward ? 0 : (int)songs.size() - 1;
        playingSongIndex  = songIndex;
        playingAlbumIndex = albumIndex;
        playingAlbum      = currentAlbum;
        playCurrentSong(true);
        displayNeedsUpdate = true;
        logRamSpace(goForward ? "auto next - next album" : "auto previous - previous album");
        return;
      }
      else
      {
        Serial.printf("⚠️ Album has no songs, trying %s\n", label);
        playingSongIndex  = songIndex;
        playingAlbumIndex = albumIndex;
        playingAlbum      = currentAlbum;
        navigateInDirection(dir);
        return;
      }
    }
    else
    {
      Serial.println("⚠️ Failed to load album songs");
      playingSongIndex  = songIndex;
      playingAlbumIndex = albumIndex;
      playingAlbum      = currentAlbum;
      navigateInDirection(dir);
      return;
    }
  }

  // Try next/previous artist
  int nextArtist = artistIndex + dir;
  if (nextArtist >= 0 && nextArtist < (int)artists.size())
  {
    artistIndex   = nextArtist;
    if (goForward) { albumIndex = 0; songIndex = 0; }
    currentArtist = artists[artistIndex];

    if (buildAlbumList(currentArtist))
    {
      if (!albums.empty())
      {
        if (!goForward) albumIndex = (int)albums.size() - 1;
        currentAlbum = albums[albumIndex];

        if (buildSongList(currentArtist, currentAlbum))
        {
          if (!songs.empty())
          {
            songIndex = goForward ? 0 : (int)songs.size() - 1;
            savePlayingContext();
            playCurrentSong(true);
            displayNeedsUpdate = true;
            logRamSpace(goForward ? "auto next - next artist" : "auto previous - previous artist");
            return;
          }
          else
          {
            Serial.println("⚠️ No songs found");
            savePlayingContext();
            navigateInDirection(dir);
            return;
          }
        }
        // buildSongList failed — fall through to boundary
      }
      else
      {
        Serial.println("⚠️ Artist has no albums");
        playingArtistIndex = artistIndex;
        playingArtist      = currentArtist;
        navigateInDirection(dir);
        return;
      }
    }
  }

  // Boundary of library
  if (goForward)
  {
    Serial.println("📀 Reached end of library");
    stopPlayback();
    navigateToMenu(MENU_NOW_PLAYING);
    displayNeedsUpdate = true;
    logRamSpace("auto next - end");
  }
  else
  {
    Serial.println("📀 At beginning of library");
    playCurrentSong(true);
    displayNeedsUpdate = true;
    logRamSpace("auto previous - restart");
  }
}

void autoPrevious() { navigateInDirection(-1); }

void autoNext()     { navigateInDirection(+1); }