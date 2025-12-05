#include "Buttons.h"
#include "Navigation.h"
#include "Haptics.h"
#include "EncoderModule.h"

#define BTN_CENTER 4
#define BTN_LEFT 36

volatile bool btnPressed[2] = { false, false };
unsigned long lastPressTime[2] = { 0, 0 };
unsigned long pressStartTime[2] = { 0, 0 };
const unsigned long debounceDelay = 300;
const unsigned long minPressDurationLeft = 10;  // Only for left button (GPIO36)

void IRAM_ATTR handleInterrupt(int index) {
  unsigned long now = millis();
  
  if (now - lastPressTime[index] > debounceDelay) {
    pressStartTime[index] = now;
    btnPressed[index] = true;
    lastPressTime[index] = now;
  }
}

void IRAM_ATTR onCenterButton() { handleInterrupt(0); }
void IRAM_ATTR onLeftButton() { handleInterrupt(1); }

void initButtons() {
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT);

  attachInterrupt(digitalPinToInterrupt(BTN_CENTER), onCenterButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_LEFT), onLeftButton, FALLING);
  
  Serial.println("✅ Buttons initialized (Center: GPIO4, Left: GPIO36 with external pull-up)");
}

void pollButtons() {
  bool scrolling = isEncoderScrolling();
  
  // Center button - NO FILTERING, immediate response
  if (btnPressed[0]) {
    btnPressed[0] = false;
    
    if (scrolling) {
      Serial.println("🔇 Center button suppressed (scrolling)");
    } else {
      Serial.println("🔘 Center button pressed");
      hapticButtonPress();
      handleButtonPress(0);
    }
  }
  
  // Left button - WITH FILTERING for BT glitch
  if (btnPressed[1]) {
    if (digitalRead(BTN_LEFT) == LOW) {
      unsigned long pressDuration = millis() - pressStartTime[1];
      
      if (pressDuration >= minPressDurationLeft) {
        btnPressed[1] = false;
        
        if (scrolling) {
          Serial.println("🔇 Left button suppressed (scrolling)");
        } else {
          Serial.println("🔘 Left button pressed");
          hapticBack();
          handleButtonPress(3);
        }
      }
    } else {
      btnPressed[1] = false;
      Serial.println("⚠️ Left button glitch filtered");
    }
  }
}