#include "Buttons.h"
#include "Navigation.h"
#include "Haptics.h"
#include "EncoderModule.h"

// Button pin definitions
#define BTN_CENTER 4
#define BTN_LEFT 37
#define BTN_TOP 39      // NEW - Menu/Back
#define BTN_BOTTOM 34   // NEW - Play/Pause
#define BTN_RIGHT 36    // NEW - Next

// Track button states
volatile bool btnPressed[5] = { false, false, false, false, false };
unsigned long lastPressTime[5] = { 0, 0, 0, 0, 0 };
unsigned long pressStartTime[5] = { 0, 0, 0, 0, 0 };
const unsigned long debounceDelay = 300;
const unsigned long minPressDurationADC = 10;  // For ADC pins (GPIO34/36/37/39)

// Button indices
#define BTN_IDX_CENTER 0
#define BTN_IDX_LEFT 1
#define BTN_IDX_TOP 2
#define BTN_IDX_BOTTOM 3
#define BTN_IDX_RIGHT 4

void IRAM_ATTR handleInterrupt(int index) {
  unsigned long now = millis();
  
  if (now - lastPressTime[index] > debounceDelay) {
    pressStartTime[index] = now;
    btnPressed[index] = true;
    lastPressTime[index] = now;
  }
}

void IRAM_ATTR onCenterButton() { handleInterrupt(BTN_IDX_CENTER); }
void IRAM_ATTR onLeftButton() { handleInterrupt(BTN_IDX_LEFT); }
void IRAM_ATTR onTopButton() { handleInterrupt(BTN_IDX_TOP); }
void IRAM_ATTR onBottomButton() { handleInterrupt(BTN_IDX_BOTTOM); }
void IRAM_ATTR onRightButton() { handleInterrupt(BTN_IDX_RIGHT); }

void initButtons() {
  // Center button - has internal pull-up
  pinMode(BTN_CENTER, INPUT_PULLUP);
  
  // All others need external pull-ups (ADC pins)
  pinMode(BTN_LEFT, INPUT);
  pinMode(BTN_TOP, INPUT);
  pinMode(BTN_BOTTOM, INPUT);
  pinMode(BTN_RIGHT, INPUT);

  // Attach interrupts
  attachInterrupt(digitalPinToInterrupt(BTN_CENTER), onCenterButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_LEFT), onLeftButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_TOP), onTopButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_BOTTOM), onBottomButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_RIGHT), onRightButton, FALLING);
  
  Serial.println("✅ Buttons initialized");
  Serial.println("   Center: GPIO4 (internal pull-up)");
  Serial.println("   Left: GPIO36 (external pull-up)");
  Serial.println("   Top: GPIO34 (external pull-up)");
  Serial.println("   Bottom: GPIO39 (external pull-up)");
  Serial.println("   Right: GPIO37 (external pull-up)");
}

void pollButtons() {
  bool scrolling = isEncoderScrolling();
  
  // CENTER button - NO FILTERING, immediate response (has internal pull-up)
  if (btnPressed[BTN_IDX_CENTER]) {
    btnPressed[BTN_IDX_CENTER] = false;
    
    if (scrolling) {
      Serial.println("🔇 Center button suppressed (scrolling)");
    } else {
      Serial.println("🔘 Center button pressed");
      hapticButtonPress();
      handleButtonPress(0);  // Keep existing index for handleCenter()
    }
  }
  
  // LEFT button - Previous track (WITH FILTERING - ADC pin)
  if (btnPressed[BTN_IDX_LEFT]) {
    if (digitalRead(BTN_LEFT) == LOW) {
      unsigned long pressDuration = millis() - pressStartTime[BTN_IDX_LEFT];
      
      if (pressDuration >= minPressDurationADC) {
        btnPressed[BTN_IDX_LEFT] = false;
        
        if (scrolling) {
          Serial.println("🔇 Left button suppressed (scrolling)");
        } else {
          Serial.println("🔘 Left button pressed (Previous)");
          hapticButtonPress();
          handleButtonPress(1);  // Previous track
        }
      }
    } else {
      btnPressed[BTN_IDX_LEFT] = false;
      Serial.println("⚠️ Left button glitch filtered");
    }
  }
  
  // TOP button - Menu/Back (WITH FILTERING - ADC pin)
  if (btnPressed[BTN_IDX_TOP]) {
    if (digitalRead(BTN_TOP) == LOW) {
      unsigned long pressDuration = millis() - pressStartTime[BTN_IDX_TOP];
      
      if (pressDuration >= minPressDurationADC) {
        btnPressed[BTN_IDX_TOP] = false;
        
        if (scrolling) {
          Serial.println("🔇 Top button suppressed (scrolling)");
        } else {
          Serial.println("🔘 Top button pressed (Menu/Back)");
          hapticBack();
          handleButtonPress(2);  // Menu/Back
        }
      }
    } else {
      btnPressed[BTN_IDX_TOP] = false;
      Serial.println("⚠️ Top button glitch filtered");
    }
  }
  
  // BOTTOM button - Play/Pause (WITH FILTERING - ADC pin)
  if (btnPressed[BTN_IDX_BOTTOM]) {
    if (digitalRead(BTN_BOTTOM) == LOW) {
      unsigned long pressDuration = millis() - pressStartTime[BTN_IDX_BOTTOM];
      
      if (pressDuration >= minPressDurationADC) {
        btnPressed[BTN_IDX_BOTTOM] = false;
        
        if (scrolling) {
          Serial.println("🔇 Bottom button suppressed (scrolling)");
        } else {
          Serial.println("🔘 Bottom button pressed (Play/Pause)");
          hapticButtonPress();
          handleButtonPress(3);  // Play/Pause
        }
      }
    } else {
      btnPressed[BTN_IDX_BOTTOM] = false;
      Serial.println("⚠️ Bottom button glitch filtered");
    }
  }
  
  // RIGHT button - Next track (WITH FILTERING - ADC pin)
  if (btnPressed[BTN_IDX_RIGHT]) {
    if (digitalRead(BTN_RIGHT) == LOW) {
      unsigned long pressDuration = millis() - pressStartTime[BTN_IDX_RIGHT];
      
      if (pressDuration >= minPressDurationADC) {
        btnPressed[BTN_IDX_RIGHT] = false;
        
        if (scrolling) {
          Serial.println("🔇 Right button suppressed (scrolling)");
        } else {
          Serial.println("🔘 Right button pressed (Next)");
          hapticButtonPress();
          handleButtonPress(4);  // Next track
        }
      }
    } else {
      btnPressed[BTN_IDX_RIGHT] = false;
      Serial.println("⚠️ Right button glitch filtered");
    }
  }
}