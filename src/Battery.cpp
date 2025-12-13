#include "Battery.h"
#include "State.h"

// Battery voltage divider on Feather V2
// VBAT --- 100K --- GPIO35 --- 100K --- GND
// So GPIO35 reads VBAT/2

#define BATTERY_PIN 35  // A13 on Feather V2

// Moving average for stable readings
#define BATTERY_SAMPLES 10
static float voltageHistory[BATTERY_SAMPLES] = {0};
static int historyIndex = 0;
static bool historyFilled = false;

void initBattery() {
    pinMode(BATTERY_PIN, INPUT);
    
    // ADC settings for better accuracy
    analogSetAttenuation(ADC_11db);  // Full range 0-3.3V
    analogReadResolution(12);         // 12-bit resolution (0-4095)
    
    Serial.println("✅ Battery monitoring initialized");
    
    // Take initial reading
    updateBattery();
}

float getBatteryVoltage() {
    // Read ADC (0-4095 for 12-bit)
    int rawValue = analogRead(BATTERY_PIN);
    
    // Convert to voltage (0-3.3V range)
    float voltage = (rawValue / 4095.0) * 3.3;
    
    // Multiply by 2 because of voltage divider
    voltage *= 2.0;
    
    // Add calibration offset (Feather V2 typically needs +0.1V correction)
    voltage += 0.1;
    
    return voltage;
}

int getBatteryPercent() {
    // LiPo voltage to percentage conversion
    // 4.2V = 100%, 3.7V = 50%, 3.0V = 0%
    
    if (batteryVoltage >= 4.2) return 100;
    if (batteryVoltage <= 3.0) return 0;
    
    // Linear approximation (good enough for most cases)
    // Better: use a lookup table for real LiPo discharge curve
    float percent = ((batteryVoltage - 3.0) / (4.2 - 3.0)) * 100.0;
    
    return (int)percent;
}

bool isBatteryCharging() {
    // On Feather V2, if voltage is very high (>4.3V) while USB connected,
    // battery is likely charging. This is a simple heuristic.
    // A more accurate method would require checking USB voltage separately.
    
    // If voltage is rising rapidly, probably charging
    static float lastVoltage = 0.0;
    bool charging = (batteryVoltage > 4.15 && batteryVoltage > lastVoltage + 0.05);
    lastVoltage = batteryVoltage;
    
    return charging;
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
    
    #ifdef DEBUG
    Serial.printf("🔋 Battery: %.2fV (%d%%)", batteryVoltage, batteryPercent);
    if (batteryCharging) {
        Serial.print(" ⚡ CHARGING");
    }
    Serial.println();
    #endif
}