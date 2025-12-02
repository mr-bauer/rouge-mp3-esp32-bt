#ifndef HAPTICS_H
#define HAPTICS_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_DRV2605.h>

#define I2C_SDA 22
#define I2C_SCL 20
#define NEOPIXEL_I2C_POWER 2

// Haptic effect types (subtle feedback)
enum HapticEffect {
    HAPTIC_SOFT_BUMP = 7,       // Soft bump (encoder tick)
    HAPTIC_CLICK = 1,           // Sharp click (button press)
    HAPTIC_DOUBLE_CLICK = 10,   // Double click (selection confirm)
    HAPTIC_TRANSITION = 47,     // Smooth transition (menu change)
    HAPTIC_ALERT_750 = 18,      // Alert buzz (error - 750ms)
    HAPTIC_STRONG_CLICK = 14    // Strong click (back button)
};

extern Adafruit_DRV2605 drv;
extern bool hapticsEnabled;

void initHaptics();
void playHaptic(HapticEffect effect);
void stopHaptic();

// Convenience functions
void hapticEncoderTick();    // Encoder rotation
void hapticButtonPress();    // Button press
void hapticSelection();      // Item selected
void hapticMenuTransition(); // Menu change
void hapticError();          // Error feedback
void hapticBack();           // Back button

#endif