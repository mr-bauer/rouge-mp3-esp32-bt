#include "EncoderModule.h"
#include "State.h"
#include "Display.h"
#include "Haptics.h"
#include "AudioManager.h"
#include <RotaryEncoder.h>

#define ENCODER_PIN_A 26
#define ENCODER_PIN_B 25

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

// Direction filtering - NEW
int directionHistory[3] = {0, 0, 0};  // Track last 3 directions
int historyIndex = 0;

// Volume control tracking - NEW
int volumeModeTicks = 0;

// Button suppression during scrolling - NEW
unsigned long lastEncoderMovement = 0;
#define BUTTON_SUPPRESS_TIME 300  // Suppress buttons for 300ms after scroll

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

// Helper function to determine the dominant scroll direction - NEW
int getDominantDirection() {
  int sum = directionHistory[0] + directionHistory[1] + directionHistory[2];
  
  // If majority is positive, scrolling up
  if (sum >= 2) return 1;
  // If majority is negative, scrolling down
  if (sum <= -2) return -1;
  // Unclear direction
  return 0;
}

bool isEncoderScrolling() {
  return (millis() - lastEncoderMovement < BUTTON_SUPPRESS_TIME);
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
    lastEncoderMovement = now;  // Track for button suppression
    
    // Normalize to single step
    int step = (delta > 0) ? 1 : -1;
    
    // Update direction history - NEW
    directionHistory[historyIndex] = step;
    historyIndex = (historyIndex + 1) % 3;
    
    // Get dominant direction from recent history - NEW
    int dominantDirection = getDominantDirection();
    
    // If we have a clear dominant direction, use it to filter out glitches - NEW
    if (dominantDirection != 0) {
      // If this step disagrees with dominant direction, it might be a glitch
      if (step != dominantDirection) {
        // Check if this is an isolated opposite tick
        int oppositeCount = 0;
        for (int i = 0; i < 3; i++) {
          if (directionHistory[i] == -dominantDirection) {
            oppositeCount++;
          }
        }
        
        // If only 1 out of 3 recent ticks is opposite, ignore it as a glitch
        if (oppositeCount == 1) {
          Serial.printf("🔧 Filtered direction glitch: step=%d, dominant=%d\n", 
                       step, dominantDirection);
          lastPos = newPos;
          lastValidPos = newPos;
          return;
        }
      }
      
      // Use dominant direction for more stable scrolling
      step = dominantDirection;
      lastScrollDirection = dominantDirection;
    } else {
      lastScrollDirection = step;
    }
    
    lastPos = newPos;
    lastValidPos = newPos;
    
    // Haptic feedback on encoder tick
    hapticEncoderTick();

    // Take mutex to safely update shared variables
    if (xSemaphoreTake(displayMutex, 10)) {
      
      // SPECIAL HANDLING: Now Playing Volume Control - NEW
      if (currentMenu == MENU_NOW_PLAYING) {
        volumeModeTicks++;
        
        // After 3 ticks, enter volume control mode
        if (volumeModeTicks >= VOLUME_ACTIVATION_TICKS) {
          if (!volumeControlActive) {
            Serial.println("🔊 Entering volume control mode");
            volumeControlActive = true;
          }
          
          // Adjust volume
          currentVolume += (step * 2);  // 2% per tick
          if (currentVolume < 0) currentVolume = 0;
          if (currentVolume > 100) currentVolume = 100;
          
          player.setVolume(currentVolume / 100.0f);  // Convert to 0.0-1.0
          lastVolumeChange = millis();
          displayNeedsUpdate = true;
          
          xSemaphoreGive(displayMutex);
          return;
        }
      } else {
        // Reset volume mode tracking when not in Now Playing
        volumeModeTicks = 0;
        volumeControlActive = false;
      }
      
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
  else {
    // No encoder movement - check for volume control timeout - NEW
    if (volumeControlActive) {
      unsigned long now = millis();
      if (now - lastVolumeChange > VOLUME_TIMEOUT) {
        Serial.println("🔊 Exiting volume control mode");
        volumeControlActive = false;
        volumeModeTicks = 0;
        displayNeedsUpdate = true;
      }
    } else {
      // Reset tick counter if no movement
      if (millis() - lastEncoderUpdate > 500) {
        volumeModeTicks = 0;
      }
    }
    
    // Reset direction history if stopped scrolling - NEW
    if (millis() - lastEncoderUpdate > 500) {
      directionHistory[0] = 0;
      directionHistory[1] = 0;
      directionHistory[2] = 0;
      historyIndex = 0;
    }
  }
}