#include "Buttons.h"
#include "Navigation.h"

#define BTN_CENTER 4
#define BTN_LEFT 15

volatile bool btnPressed[2] = { false, false };
unsigned long lastPressTime[2] = { 0, 0 };
const unsigned long debounceDelay = 150;

void IRAM_ATTR handleInterrupt(int index) {
  unsigned long now = millis();
  if (now - lastPressTime[index] > debounceDelay) {
    btnPressed[index] = true;
    lastPressTime[index] = now;
  }
}

void IRAM_ATTR onCenterButton() { handleInterrupt(0); }
void IRAM_ATTR onLeftButton() { handleInterrupt(1); }

void initButtons() {
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BTN_CENTER), onCenterButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_LEFT), onLeftButton, FALLING);
  
  Serial.println("✅ Buttons initialized");
}

void pollButtons() {
  if (btnPressed[0]) {
    btnPressed[0] = false;
    handleButtonPress(0);  // Center button
  }
  
  if (btnPressed[1]) {
    btnPressed[1] = false;
    handleButtonPress(3);  // Left button (mapped to index 3 for compatibility)
  }
}