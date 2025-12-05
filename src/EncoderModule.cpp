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

// Direction filtering - INCREASED from 3 to 5 samples
int directionHistory[5] = {0, 0, 0, 0, 0};  // Track last 5 directions
int historyIndex = 0;
int consecutiveSameDirection = 0;  // NEW - count consecutive movements in same direction

// Volume control tracking
int volumeModeTicks = 0;

// Button suppression during scrolling
unsigned long lastEncoderMovement = 0;
#define BUTTON_SUPPRESS_TIME 300

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

// Helper function to determine the dominant scroll direction - UPDATED
int getDominantDirection() {
  int sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += directionHistory[i];
  }
  
  // If majority is positive, scrolling up
  if (sum >= 3) return 1;   // At least 3 out of 5 are positive
  // If majority is negative, scrolling down
  if (sum <= -3) return -1;  // At least 3 out of 5 are negative
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
    lastEncoderMovement = now;
    
    // Normalize to single step
    int step = (delta > 0) ? 1 : -1;
    
    // Update direction history
    directionHistory[historyIndex] = step;
    historyIndex = (historyIndex + 1) % 5;  // Changed from 3 to 5
    
    // Track consecutive movements in same direction - NEW
    if (step == lastScrollDirection) {
      consecutiveSameDirection++;
    } else {
      consecutiveSameDirection = 1;
    }
    
    // Get dominant direction from recent history
    int dominantDirection = getDominantDirection();
    
    // More aggressive filtering once we're in a clear scroll pattern - NEW
    if (dominantDirection != 0) {
      // If we've been scrolling consistently in one direction (3+ steps)
      if (consecutiveSameDirection >= 3) {
        // Strongly reject opposite direction steps
        if (step != dominantDirection) {
          Serial.printf("🔧 Strong filter: locked to direction %d, ignoring %d\n", 
                       dominantDirection, step);
          lastPos = newPos;
          lastValidPos = newPos;
          return;
        }
      } else {
        // Normal filtering for initial direction establishment
        if (step != dominantDirection) {
          int oppositeCount = 0;
          for (int i = 0; i < 5; i++) {
            if (directionHistory[i] == -dominantDirection) {
              oppositeCount++;
            }
          }
          
          // If only 1 out of 5 recent ticks is opposite, ignore it
          if (oppositeCount <= 1) {
            Serial.printf("🔧 Filtered direction glitch: step=%d, dominant=%d\n", 
                         step, dominantDirection);
            lastPos = newPos;
            lastValidPos = newPos;
            return;
          }
        }
      }
      
      // Use dominant direction
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
      
      // SPECIAL HANDLING: Now Playing Volume Control
      if (currentMenu == MENU_NOW_PLAYING) {
        volumeModeTicks++;
        
        if (volumeModeTicks >= VOLUME_ACTIVATION_TICKS) {
          if (!volumeControlActive) {
            Serial.println("🔊 Entering volume control mode");
            volumeControlActive = true;
          }
          
          currentVolume += (step * 2);
          if (currentVolume < 0) currentVolume = 0;
          if (currentVolume > 100) currentVolume = 100;
          
          player.setVolume(currentVolume / 100.0f);
          lastVolumeChange = millis();
          displayNeedsUpdate = true;
          
          xSemaphoreGive(displayMutex);
          return;
        }
      } else {
        volumeModeTicks = 0;
        volumeControlActive = false;
      }
      
      // Handle based on current menu
      if (currentMenu == MENU_MAIN || currentMenu == MENU_MUSIC || 
          currentMenu == MENU_SETTINGS || currentMenu == MENU_BLUETOOTH)
      {
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
    // No encoder movement - check for volume control timeout
    if (volumeControlActive) {
      unsigned long now = millis();
      if (now - lastVolumeChange > VOLUME_TIMEOUT) {
        Serial.println("🔊 Exiting volume control mode");
        volumeControlActive = false;
        volumeModeTicks = 0;
        displayNeedsUpdate = true;
      }
    } else {
      if (millis() - lastEncoderUpdate > 500) {
        volumeModeTicks = 0;
      }
    }
    
    // Reset direction history if stopped scrolling
    if (millis() - lastEncoderUpdate > 500) {
      for (int i = 0; i < 5; i++) {
        directionHistory[i] = 0;
      }
      historyIndex = 0;
      consecutiveSameDirection = 0;  // NEW - reset counter
      lastScrollDirection = 0;
    }
  }
}