#include "Navigation.h"
#include "Indexer.h"
#include "Display.h"
#include "AudioManager.h"
#include "State.h"

void handleButtonPress(int buttonIndex)
{
  switch (buttonIndex)
  {
  case 0:
    handleCenter();
    break;
  case 3:
    handleLeft();
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
        return;
      }
      
      Serial.printf("Selected: %s -> %d\n", item.label.c_str(), item.action);
      
      // Handle Bluetooth menu actions
      if (currentMenu == MENU_BLUETOOTH) {
        if (item.label == "Reconnect") {
          Serial.println("User requested Bluetooth reconnect");
          reconnectBluetooth();
        } else if (item.label == "Disconnect") {
          Serial.println("User requested Bluetooth disconnect");
          disconnectBluetooth();
        }
        
        // If it's just a status item, don't navigate
        if (item.label.find("Status:") == 0) {
          // Just show status, don't do anything
          return;
        }
        
        // Rebuild menu to show new state
        buildBluetoothMenu();
        displayNeedsUpdate = true;
      }
      
      // Handle settings menu actions
      if (currentMenu == MENU_SETTINGS) {
        // Toggle settings (future implementation)
        Serial.println("Settings action (not implemented)");
        return;
      }
      
      // Navigate to selected menu
      navigateToMenu(item.action);
      displayNeedsUpdate = true;
    }
    return;
  }
  
  // Handle playback controls in now playing screen
  if (currentMenu == MENU_NOW_PLAYING) {
    if (player_state == STATE_PLAYING) {
      pausePlayback();
    } else if (player_state == STATE_PAUSED) {
      resumePlayback();
    }
    displayNeedsUpdate = true;
    return;
  }
  
  // Handle music browser selections
  if (currentMenu == MENU_ARTIST_LIST)
  {
    if (artistIndex >= 0 && artistIndex < (int)artists.size()) {
      currentArtist = artists[artistIndex];
      
      if (buildAlbumList(currentArtist)) {
        navigateToMenu(MENU_ALBUM_LIST);
        albumIndex = 0;
      } else {
        Serial.println("⚠️ Failed to load albums");
      }
    } else {
      Serial.println("❌ Invalid artist index!");
    }
  }
  else if (currentMenu == MENU_ALBUM_LIST)
  {
    if (albumIndex >= 0 && albumIndex < (int)albums.size()) {
      currentAlbum = albums[albumIndex];
      
      if (buildSongList(currentArtist, currentAlbum)) {
        navigateToMenu(MENU_SONG_LIST);
        songIndex = 0;
      } else {
        Serial.println("⚠️ Failed to load songs");
      }
    } else {
      Serial.println("❌ Invalid album index!");
    }
  }
  else if (currentMenu == MENU_SONG_LIST)
  {
    if (songIndex >= 0 && songIndex < (int)songs.size()) {
      playCurrentSong(false);
      navigateToMenu(MENU_NOW_PLAYING);
    } else {
      Serial.println("❌ Invalid song index!");
    }
  }
  
  displayNeedsUpdate = true;
}

void handleLeft()
{
  // Back button
  navigateBack();
  displayNeedsUpdate = true;
}

void autoNext()
{
  Serial.println("Auto-advancing to next track...");

  // Try next song in current album
  if (songIndex + 1 < (int)songs.size())
  {
    songIndex++;
    playCurrentSong(true);
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