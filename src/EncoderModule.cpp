#include "EncoderModule.h"
#include "State.h"
#include "Display.h"
#include <RotaryEncoder.h>

#define ENCODER_PIN_A 12
#define ENCODER_PIN_B 13

static RotaryEncoder encoder(ENCODER_PIN_A, ENCODER_PIN_B, RotaryEncoder::LatchMode::TWO03);
static int lastPos = 0;
static int lastValidPos = 0;

// Tuning parameters
static unsigned long lastEncoderUpdate = 0;
const unsigned long ENCODER_UPDATE_INTERVAL = 90;

// Anti-jump threshold
const int ENCODER_JUMP_THRESHOLD = 3;

// Track scroll direction
int lastScrollDirection = 0;  // -1 = up, 1 = down, 0 = none

void IRAM_ATTR encoderISR()
{
  encoder.tick();
}

void initEncoder()
{
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);

  digitalWrite(ENCODER_PIN_A, HIGH);
  digitalWrite(ENCODER_PIN_B, HIGH);

  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), encoderISR, CHANGE);
  
  lastPos = encoder.getPosition();
  lastValidPos = lastPos;
  
  Serial.println("✅ Encoder initialized");
}

void updateEncoder()
{
  int newPos = encoder.getPosition();
  
  if (newPos != lastPos)
  {
    int delta = newPos - lastPos;
    
    // Anti-jump protection
    if (abs(delta) > ENCODER_JUMP_THRESHOLD) {
      Serial.printf("⚠️ Encoder jump detected: %d steps, ignoring\n", delta);
      encoder.setPosition(lastValidPos);
      lastPos = lastValidPos;
      return;
    }
    
    // Throttle updates
    unsigned long now = millis();
    if (now - lastEncoderUpdate < ENCODER_UPDATE_INTERVAL) {
      return;
    }
    lastEncoderUpdate = now;
    
    // Normalize to single step and track direction
    int step = (delta > 0) ? 1 : -1;
    lastScrollDirection = step;
    
    lastPos = newPos;
    lastValidPos = newPos;

    // Take mutex to safely update shared variables
    if (xSemaphoreTake(displayMutex, 10)) {
      
      // Handle based on current menu
      if (currentMenu == MENU_MAIN || currentMenu == MENU_MUSIC || 
          currentMenu == MENU_SETTINGS || currentMenu == MENU_BLUETOOTH)
      {
        // Generic menu navigation
        int oldIndex = menuIndex;
        int listSize = currentMenuItems.size();
        
        menuIndex += step;
        
        if (menuIndex < 0) {
          menuIndex = 0;
        } else if (menuIndex >= listSize) {
          menuIndex = listSize - 1;
        }
        
        if (oldIndex != menuIndex) {
          displayNeedsUpdate = true;
          
          #ifdef DEBUG
          Serial.printf("Menu: %d -> %d\n", oldIndex, menuIndex);
          #endif
        }
      }
      else if (currentMenu == MENU_ARTIST_LIST && !artists.empty())
      {
        int oldIndex = artistIndex;
        int listSize = artists.size();
        
        artistIndex += step;
        
        if (artistIndex < 0) {
          artistIndex = 0;
        } else if (artistIndex >= listSize) {
          artistIndex = listSize - 1;
        }
        
        if (oldIndex != artistIndex) {
          displayNeedsUpdate = true;
          
          #ifdef DEBUG
          Serial.printf("Artist: %d -> %d (%s)\n", 
                       oldIndex, artistIndex, artists[artistIndex].c_str());
          #endif
        }
      }
      else if (currentMenu == MENU_ALBUM_LIST && !albums.empty())
      {
        int oldIndex = albumIndex;
        int listSize = albums.size();
        
        albumIndex += step;
        
        if (albumIndex < 0) {
          albumIndex = 0;
        } else if (albumIndex >= listSize) {
          albumIndex = listSize - 1;
        }
        
        if (oldIndex != albumIndex) {
          displayNeedsUpdate = true;
          
          #ifdef DEBUG
          Serial.printf("Album: %d -> %d (%s)\n", 
                       oldIndex, albumIndex, albums[albumIndex].c_str());
          #endif
        }
      }
      else if (currentMenu == MENU_SONG_LIST && !songs.empty())
      {
        int oldIndex = songIndex;
        int listSize = songs.size();
        
        songIndex += step;
        
        if (songIndex < 0) {
          songIndex = 0;
        } else if (songIndex >= listSize) {
          songIndex = listSize - 1;
        }
        
        if (oldIndex != songIndex) {
          displayNeedsUpdate = true;
          
          #ifdef DEBUG
          Serial.printf("Song: %d -> %d (%s)\n", 
                       oldIndex, songIndex, songs[songIndex].title.c_str());
          #endif
        }
      }
      
      xSemaphoreGive(displayMutex);
    }
  }
}