#include "Battery.h"
#include "State.h"
#include "Display.h"

// Battery voltage divider on Feather V2
// VBAT --- 200K --- GPIO35 --- 200K --- GND
// So GPIO35 reads VBAT/2

#define BATTERY_PIN 35  // A13 on Feather V2

// Moving average for stable readings
#define BATTERY_SAMPLES 10
static float voltageHistory[BATTERY_SAMPLES] = {0};
static int historyIndex = 0;
static bool historyFilled = false;

// Charging detection
static float lastVoltageReading = 0.0f;
static float voltageChangeRate = 0.0f;
static unsigned long lastVoltageTime = 0;

void initBattery() {
    pinMode(BATTERY_PIN, INPUT);
    
    // ADC settings for better accuracy
    analogSetAttenuation(ADC_11db);  // Full range 0-3.3V (actually reads 0-3.6V)
    analogReadResolution(12);         // 12-bit resolution (0-4095)
    
    Serial.println("✅ Battery monitoring initialized (200K divider)");
    
    // Take initial reading
    updateBattery();
}

float getBatteryVoltage() {
    // Read ADC (0-4095 for 12-bit)
    int rawValue = analogRead(BATTERY_PIN);
    
    // ESP32 ADC is not perfectly linear, especially with 11db attenuation
    // The actual voltage range is closer to 0-3.6V despite 3.3V spec
    float voltage = (rawValue / 4095.0) * 3.6;
    
    // Multiply by 2 because of voltage divider (200K + 200K)
    voltage *= 2.0;
    
    // ADC reference calibration (adjust this based on actual measurements)
    // If you have a multimeter, measure your battery and adjust this offset
    voltage += 0.0;  // Start with no offset, tune if needed
    
    return voltage;
}

int getBatteryPercent() {
    // LiPo discharge curve approximation
    // Based on typical LiPo battery discharge characteristics
    
    if (batteryVoltage >= 4.2) return 100;
    if (batteryVoltage <= 3.2) return 0;  // Don't drain below 3.2V
    
    // More accurate curve fitting for LiPo
    // These breakpoints match real-world LiPo discharge better than linear
    if (batteryVoltage >= 4.1) {
        // 4.1V - 4.2V = 90-100%
        return 90 + (int)((batteryVoltage - 4.1) / 0.1 * 10);
    } else if (batteryVoltage >= 3.9) {
        // 3.9V - 4.1V = 70-90%
        return 70 + (int)((batteryVoltage - 3.9) / 0.2 * 20);
    } else if (batteryVoltage >= 3.7) {
        // 3.7V - 3.9V = 40-70%
        return 40 + (int)((batteryVoltage - 3.7) / 0.2 * 30);
    } else if (batteryVoltage >= 3.5) {
        // 3.5V - 3.7V = 20-40%
        return 20 + (int)((batteryVoltage - 3.5) / 0.2 * 20);
    } else if (batteryVoltage >= 3.3) {
        // 3.3V - 3.5V = 5-20%
        return 5 + (int)((batteryVoltage - 3.3) / 0.2 * 15);
    } else {
        // 3.2V - 3.3V = 0-5%
        return (int)((batteryVoltage - 3.2) / 0.1 * 5);
    }
}

bool isBatteryCharging() {
    // Improved charging detection with three methods:
    // 1. If voltage is very high (>4.25V), definitely charging/full with USB
    // 2. If voltage is rising over time, actively charging
    // 3. If voltage is stable and high (>4.0V and not dropping), plugged in but full/maintaining
    
    // Method 1: Very high voltage = definitely USB connected
    if (batteryVoltage > 4.25) {
        return true;
    }
    
    // Method 2: Voltage actively rising = charging
    if (voltageChangeRate > 0.005) {  // Rising by >5mV per check (5 seconds)
        return true;
    }
    
    // Method 3: Stable at high voltage = plugged in (full or maintaining)
    // On battery, voltage would be dropping due to consumption
    // If voltage is stable (not dropping significantly) at >4.0V, USB must be connected
    if (batteryVoltage > 4.0 && voltageChangeRate > -0.002) {  // Not dropping more than 2mV per check
        return true;
    }
    
    return false;
}

void updateBattery() {
    unsigned long now = millis();
    
    // Only update every X seconds
    if (now - lastBatteryCheck < BATTERY_CHECK_INTERVAL) {
        return;
    }
    lastBatteryCheck = now;
    
    // Get current voltage
    float voltage = getBatteryVoltage();
    
    // Calculate voltage change rate for charging detection
    if (lastVoltageTime > 0) {
        unsigned long timeDiff = now - lastVoltageTime;
        float voltageDiff = voltage - lastVoltageReading;
        
        // Calculate rate in V/second, then smooth it
        float instantRate = (voltageDiff * 1000.0) / timeDiff;
        voltageChangeRate = (voltageChangeRate * 0.7) + (instantRate * 0.3);  // Smooth with 70/30 filter
    }
    lastVoltageReading = voltage;
    lastVoltageTime = now;
    
    // Add to moving average
    voltageHistory[historyIndex] = voltage;
    historyIndex = (historyIndex + 1) % BATTERY_SAMPLES;
    
    if (historyIndex == 0) {
        historyFilled = true;
    }
    
    // Calculate average
    float sum = 0;
    int count = historyFilled ? BATTERY_SAMPLES : historyIndex;
    
    for (int i = 0; i < count; i++) {
        sum += voltageHistory[i];
    }
    
    batteryVoltage = sum / count;
    batteryPercent = getBatteryPercent();
    batteryCharging = isBatteryCharging();
    
    // Trigger display update for battery status - NEW
    displayNeedsUpdate = true;
    
    #ifdef DEBUG
    Serial.printf("🔋 Battery: %.2fV (%d%%) Rate: %.4fV/s", 
                  batteryVoltage, batteryPercent, voltageChangeRate);
    if (batteryCharging) {
        Serial.print(" ⚡ CHARGING");
    }
    Serial.println();
    #endif
}