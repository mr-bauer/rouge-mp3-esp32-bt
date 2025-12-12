#include "Navigation.h"
#include "Indexer.h"
#include "Display.h"
#include "AudioManager.h"
#include "State.h"
#include "Haptics.h"

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
      currentMenu == MENU_SETTINGS || currentMenu == MENU_BLUETOOTH)
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
        if (item.label == "Reconnect") {
          Serial.println("User requested Bluetooth reconnect");
          reconnectBluetooth();
        } else if (item.label == "Disconnect") {
          Serial.println("User requested Bluetooth disconnect");
          disconnectBluetooth();
        }
        
        if (item.label.find("Status:") == 0) {
          return;
        }
        
        buildBluetoothMenu();
        displayNeedsUpdate = true;
      }
      
      if (currentMenu == MENU_SETTINGS) {
        Serial.println("Settings action (not implemented)");
        return;
      }
      
      // Navigate to selected menu
      navigateToMenu(item.action);
      displayNeedsUpdate = true;
    }
    return;
  }
  
  // In Now Playing, Center does nothing (Play/Pause is Bottom button now)
  if (currentMenu == MENU_NOW_PLAYING) {
    Serial.println("Center button - no action in Now Playing (use Bottom for Play/Pause)");
    return;
  }
  
  // Handle music browser selections
  if (currentMenu == MENU_ARTIST_LIST)
  {
    if (artistIndex >= 0 && artistIndex < (int)artists.size()) {
      currentArtist = artists[artistIndex];
      
      hapticSelection();
      
      if (buildAlbumList(currentArtist)) {
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

void autoPrevious()
{
  Serial.println("Going to previous track...");

  // Try previous song in current album
  if (songIndex - 1 >= 0)
  {
    songIndex--;
    playCurrentSong(true);
    displayNeedsUpdate = true;
    logRamSpace("auto previous - same album");
    return;
  }

  // Try previous album
  if (albumIndex - 1 >= 0)
  {
    albumIndex--;
    
    currentAlbum = albums[albumIndex];

    if (buildSongList(currentArtist, currentAlbum))
    {
      if (!songs.empty())
      {
        songIndex = songs.size() - 1;  // Go to last song of previous album
        playCurrentSong(true);
        displayNeedsUpdate = true;
        logRamSpace("auto previous - previous album");
        return;
      }
      else
      {
        Serial.println("⚠️ Album has no songs, trying previous");
        autoPrevious();
        return;
      }
    }
    else
    {
      Serial.println("⚠️ Failed to load album songs");
      autoPrevious();
      return;
    }
  }

  // Try previous artist
  if (artistIndex - 1 >= 0)
  {
    artistIndex--;
    
    currentArtist = artists[artistIndex];

    if (buildAlbumList(currentArtist))
    {
      if (!albums.empty())
      {
        albumIndex = albums.size() - 1;  // Go to last album of previous artist
        currentAlbum = albums[albumIndex];

        if (buildSongList(currentArtist, currentAlbum))
        {
          if (!songs.empty())
          {
            songIndex = songs.size() - 1;  // Go to last song
            playCurrentSong(true);
            displayNeedsUpdate = true;
            logRamSpace("auto previous - previous artist");
            return;
          }
          else
          {
            Serial.println("⚠️ No songs found");
            autoPrevious();
            return;
          }
        }
      }
      else
      {
        Serial.println("⚠️ Artist has no albums");
        autoPrevious();
        return;
      }
    }
  }

  // Already at beginning of library
  Serial.println("📀 At beginning of library");
  // Restart current song
  playCurrentSong(true);
  displayNeedsUpdate = true;
  logRamSpace("auto previous - restart");
}

void autoNext()
{
  Serial.println("Auto-advancing to next track...");

  // Try next song in current album
  if (songIndex + 1 < (int)songs.size())
  {
    songIndex++;
    playCurrentSong(true);
    displayNeedsUpdate = true;
    logRamSpace("auto next - same album");
    return;
  }

  // Try next album
  if (albumIndex + 1 < (int)albums.size())
  {
    albumIndex++;
    songIndex = 0;

    if (albumIndex < (int)albums.size())
    {
      currentAlbum = albums[albumIndex];

      if (buildSongList(currentArtist, currentAlbum))
      {
        if (!songs.empty())
        {
          playCurrentSong(true);
          displayNeedsUpdate = true;
          logRamSpace("auto next - next album");
          return;
        }
        else
        {
          Serial.println("⚠️ Album has no songs, trying next");
          autoNext();
          return;
        }
      }
      else
      {
        Serial.println("⚠️ Failed to load album songs");
        autoNext();
        return;
      }
    }
  }

  // Try next artist
  if (artistIndex + 1 < (int)artists.size())
  {
    artistIndex++;
    albumIndex = 0;
    songIndex = 0;

    if (artistIndex < (int)artists.size())
    {
      currentArtist = artists[artistIndex];

      if (buildAlbumList(currentArtist))
      {
        if (!albums.empty())
        {
          currentAlbum = albums[0];

          if (buildSongList(currentArtist, currentAlbum))
          {
            if (!songs.empty())
            {
              playCurrentSong(true);
              displayNeedsUpdate = true;
              logRamSpace("auto next - next artist");
              return;
            }
            else
            {
              Serial.println("⚠️ No songs found");
              autoNext();
              return;
            }
          }
        }
        else
        {
          Serial.println("⚠️ Artist has no albums");
          autoNext();
          return;
        }
      }
    }
  }

  // End of library
  Serial.println("📀 Reached end of library");
  stopPlayback();
  navigateToMenu(MENU_NOW_PLAYING);
  displayNeedsUpdate = true;
  logRamSpace("auto next - end");
}