#include "Haptics.h"

Adafruit_DRV2605 drv;
bool hapticsEnabled = true;

void initHaptics()
{
    Serial.println("🔊 Initializing DRV2605L haptics...");
    
    pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
    delay(10);
  
    // Initialize I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    
    if (!drv.begin()) {
        Serial.println("❌ Could not find DRV2605L!");
        hapticsEnabled = false;
        return;
    }
    
    Serial.println("✅ DRV2605L found");
    
    // Set mode for ERM (Eccentric Rotating Mass) motor
    drv.selectLibrary(1);
    
    // Set mode to internal trigger (we control when effects play)
    drv.setMode(DRV2605_MODE_INTTRIG);
    
    Serial.println("✅ Haptics initialized");
    
    // Test with a gentle bump
    playHaptic(HAPTIC_SOFT_BUMP);
}

void playHaptic(HapticEffect effect)
{
    if (!hapticsEnabled) return;
    
    drv.setWaveform(0, effect);  // Set effect in slot 0
    drv.setWaveform(1, 0);       // End waveform
    drv.go();                     // Play the effect
}

void stopHaptic()
{
    if (!hapticsEnabled) return;
    drv.stop();
}

// Convenience functions
void hapticEncoderTick()
{
    playHaptic(HAPTIC_SOFT_BUMP);  // Very subtle for encoder
}

void hapticButtonPress()
{
    playHaptic(HAPTIC_CLICK);  // Crisp click for button
}

void hapticSelection()
{
    playHaptic(HAPTIC_DOUBLE_CLICK);  // Double click for confirm
}

void hapticMenuTransition()
{
    playHaptic(HAPTIC_TRANSITION);  // Smooth for menu change
}

void hapticError()
{
    playHaptic(HAPTIC_ALERT_750);  // 750ms buzz for errors
}

void hapticBack()
{
    playHaptic(HAPTIC_STRONG_CLICK);  // Stronger for back action
}