#include "Buttons.h"
#include "Navigation.h"
#include "Haptics.h"
#include "EncoderModule.h"

// Button pin definitions
#define BTN_CENTER 4
#define BTN_LEFT 37
#define BTN_TOP 39
#define BTN_BOTTOM 34
#define BTN_RIGHT 36

// Track button states
volatile bool btnPressed[5] = { false, false, false, false, false };
unsigned long lastPressTime[5] = { 0, 0, 0, 0, 0 };
unsigned long pressStartTime[5] = { 0, 0, 0, 0, 0 };

// Button indices
#define BTN_IDX_CENTER 0
#define BTN_IDX_LEFT 1
#define BTN_IDX_TOP 2
#define BTN_IDX_BOTTOM 3
#define BTN_IDX_RIGHT 4

// ============================================================================
// INTERRUPT HANDLERS
// ============================================================================

void IRAM_ATTR handleInterrupt(int index) {
  unsigned long now = millis();
  
  if (now - lastPressTime[index] > BUTTON_DEBOUNCE_MS) {
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

// ============================================================================
// INITIALIZATION
// ============================================================================

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
  Serial.println("   Left: GPIO37 (external pull-up)");
  Serial.println("   Top: GPIO39 (external pull-up)");
  Serial.println("   Bottom: GPIO34 (external pull-up)");
  Serial.println("   Right: GPIO36 (external pull-up)");
}

// ============================================================================
// ADC BUTTON PROCESSING HELPER - NEW
// ============================================================================

bool processADCButton(int btnIndex, int gpio, const char* name, 
                      void (*handler)(int), int handlerIndex) {
  if (!btnPressed[btnIndex]) return false;
  
  // Verify button is still pressed (filter glitches)
  if (digitalRead(gpio) == LOW) {
    unsigned long pressDuration = millis() - pressStartTime[btnIndex];
    
    if (pressDuration >= BUTTON_MIN_DURATION_ADC) {
      btnPressed[btnIndex] = false;
      
      // Check if encoder is scrolling (suppress button)
      if (isEncoderScrolling()) {
        Serial.printf("🔇 %s button suppressed (scrolling)\n", name);
        return false;
      }
      
      Serial.printf("🔘 %s button pressed\n", name);
      
      // Apply appropriate haptic feedback
      if (strcmp(name, "Top") == 0) {
        hapticBack();  // Special haptic for back button
      } else {
        hapticButtonPress();
      }
      
      handler(handlerIndex);
      return true;
    }
  } else {
    // Button released too quickly - glitch
    btnPressed[btnIndex] = false;
    Serial.printf("⚠️ %s button glitch filtered\n", name);
  }
  
  return false;
}

// ============================================================================
// BUTTON POLLING
// ============================================================================

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
      handleButtonPress(0);
    }
  }
  
  // ADC buttons with filtering - SIMPLIFIED
  processADCButton(BTN_IDX_LEFT, BTN_LEFT, "Left", handleButtonPress, 1);
  processADCButton(BTN_IDX_TOP, BTN_TOP, "Top", handleButtonPress, 2);
  processADCButton(BTN_IDX_BOTTOM, BTN_BOTTOM, "Bottom", handleButtonPress, 3);
  processADCButton(BTN_IDX_RIGHT, BTN_RIGHT, "Right", handleButtonPress, 4);
}