#include "Buttons.h"
#include "Navigation.h"
#include "Haptics.h"
#include "EncoderModule.h"
#include "State.h"
#include "Display.h"

// Button pin definitions
#define BTN_CENTER 4
#define BTN_LEFT   39
#define BTN_TOP    36
#define BTN_RIGHT  34
#define BTN_BOTTOM 37

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

void handleInterrupt(int index) {
  unsigned long now = millis();

  if (now - lastPressTime[index] > BUTTON_DEBOUNCE_MS) {
    pressStartTime[index] = now;
    btnPressed[index] = true;
    lastPressTime[index] = now;
    // NOTE: lastActivityTime is set after glitch filtering in pollButtons(),
    // not here, so noise-induced ISR triggers don't reset the inactivity timer.
  }
}

void onCenterButton() { handleInterrupt(BTN_IDX_CENTER); }
void onLeftButton()   { handleInterrupt(BTN_IDX_LEFT);   }
void onTopButton()    { handleInterrupt(BTN_IDX_TOP);    }
void onBottomButton() { handleInterrupt(BTN_IDX_BOTTOM); }
void onRightButton()  { handleInterrupt(BTN_IDX_RIGHT);  }

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
  Serial.println("   Left: GPIO39 (external pull-up)");
  Serial.println("   Top: GPIO36 (external pull-up)");
  Serial.println("   Bottom: GPIO37 (external pull-up)");
  Serial.println("   Right: GPIO34 (external pull-up)");
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
      lastActivityTime = millis();  // confirmed press — reset inactivity timer

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
      lastActivityTime = millis();  // confirmed press — reset inactivity timer
      hapticButtonPress();
      handleButtonPress(0);
    }
  }
  
  // LEFT: short press = Previous track, long press (700ms) = toggle button lock
  if (btnPressed[BTN_IDX_LEFT]) {
    unsigned long dur = millis() - pressStartTime[BTN_IDX_LEFT];
    bool pinLow = (digitalRead(BTN_LEFT) == LOW);
    if (pinLow && dur >= LONG_PRESS_MS) {
      btnPressed[BTN_IDX_LEFT] = false;
      if (!scrolling) {
        lastActivityTime   = millis();
        displayNeedsUpdate = true;
        buttonsLocked = !buttonsLocked;
        Serial.printf("🔒 Button lock %s\n", buttonsLocked ? "ON" : "OFF");
        hapticSelection();
      }
    } else if (!pinLow && dur >= BUTTON_MIN_DURATION_ADC) {
      btnPressed[BTN_IDX_LEFT] = false;
      if (!scrolling) {
        lastActivityTime = millis();
        hapticButtonPress();
        handleButtonPress(1);
      }
    } else if (!pinLow) {
      btnPressed[BTN_IDX_LEFT] = false;
      Serial.println("⚠️ Left button glitch filtered");
    }
    // else: still held, duration < LONG_PRESS_MS → wait
  }

  // RIGHT: short press = Next track
  processADCButton(BTN_IDX_RIGHT, BTN_RIGHT, "Right", handleButtonPress, 4);

  // TOP: short press = Back/Menu, long press (700ms) = Home
  if (btnPressed[BTN_IDX_TOP]) {
    unsigned long dur = millis() - pressStartTime[BTN_IDX_TOP];
    bool pinLow = (digitalRead(BTN_TOP) == LOW);
    if (pinLow && dur >= LONG_PRESS_MS) {
      // Long press fires while still held
      btnPressed[BTN_IDX_TOP] = false;
      if (!scrolling) {
        lastActivityTime = millis();
        handleTopLongPress();
      }
    } else if (!pinLow && dur >= BUTTON_MIN_DURATION_ADC) {
      // Released within long-press threshold → normal short press
      btnPressed[BTN_IDX_TOP] = false;
      if (!scrolling) {
        lastActivityTime = millis();
        hapticBack();
        handleButtonPress(2);
      }
    } else if (!pinLow) {
      // Released too quickly → glitch
      btnPressed[BTN_IDX_TOP] = false;
      Serial.println("⚠️ Top button glitch filtered");
    }
    // else: still held, duration < LONG_PRESS_MS → wait
  }

  // BOTTOM: short press = Play/Pause, long press (700ms) = Now Playing
  if (btnPressed[BTN_IDX_BOTTOM]) {
    unsigned long dur = millis() - pressStartTime[BTN_IDX_BOTTOM];
    bool pinLow = (digitalRead(BTN_BOTTOM) == LOW);
    if (pinLow && dur >= LONG_PRESS_MS) {
      // Long press fires while still held
      btnPressed[BTN_IDX_BOTTOM] = false;
      if (!scrolling) {
        lastActivityTime = millis();
        handleBottomLongPress();
      }
    } else if (!pinLow && dur >= BUTTON_MIN_DURATION_ADC) {
      // Released within long-press threshold → normal short press
      btnPressed[BTN_IDX_BOTTOM] = false;
      if (!scrolling) {
        lastActivityTime = millis();
        hapticButtonPress();
        handleButtonPress(3);
      }
    } else if (!pinLow) {
      // Released too quickly → glitch
      btnPressed[BTN_IDX_BOTTOM] = false;
      Serial.println("⚠️ Bottom button glitch filtered");
    }
    // else: still held, duration < LONG_PRESS_MS → wait
  }
}