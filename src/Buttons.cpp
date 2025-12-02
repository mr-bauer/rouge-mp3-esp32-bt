#include "Buttons.h"
#include "Navigation.h"
#include "Haptics.h"  // NEW

#define BTN_CENTER 4
#define BTN_LEFT 36

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
  pinMode(BTN_LEFT, INPUT);

  attachInterrupt(digitalPinToInterrupt(BTN_CENTER), onCenterButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_LEFT), onLeftButton, FALLING);
  
  Serial.println("✅ Buttons initialized (Center: GPIO4, Left: GPIO36 with external pull-up)");
}

void pollButtons() {
  if (btnPressed[0]) {
    btnPressed[0] = false;
    Serial.println("🔘 Center button pressed");
    hapticButtonPress();  // NEW: Haptic feedback
    handleButtonPress(0);
  }
  
  if (btnPressed[1]) {
    btnPressed[1] = false;
    Serial.println("🔘 Left button pressed");
    hapticBack();  // NEW: Stronger feedback for back
    handleButtonPress(3);
  }
}