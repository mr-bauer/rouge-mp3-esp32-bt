#include "Battery.h"
#include "State.h"
#include "Display.h"

// Battery voltage divider on Feather V2
// VBAT --- 200K --- GPIO35 --- 200K --- GND
// So GPIO35 reads VBAT/2

// Moving average for stable readings
static float voltageHistory[BATTERY_SAMPLES] = {0};
static int historyIndex = 0;
static bool historyFilled = false;

// Charging detection
static float lastVoltageReading = 0.0f;
static float voltageChangeRate = 0.0f;
static unsigned long lastVoltageTime = 0;

// LiPo discharge curve lookup table
struct BatteryPoint {
    float voltage;
    int percent;
};

static const BatteryPoint dischargeCurve[] = {
    {4.2, 100},
    {4.1, 90},
    {3.9, 70},
    {3.7, 40},
    {3.5, 20},
    {3.3, 5},
    {3.2, 0}
};

static const int curveSize = sizeof(dischargeCurve) / sizeof(BatteryPoint);

void initBattery() {
    pinMode(BATTERY_PIN, INPUT);
    
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);
    
    Serial.println("✅ Battery monitoring initialized (200K divider)");
    
    updateBattery();
}

float getBatteryVoltage() {
    int rawValue = analogRead(BATTERY_PIN);
    
    // ESP32 ADC with 11db attenuation reads 0-3.6V
    float voltage = (rawValue / 4095.0) * 3.6;
    
    // Multiply by 2 because of voltage divider (200K + 200K)
    voltage *= 2.0;
    
    // ADC reference calibration (adjust based on multimeter measurements)
    voltage += 0.0;  // Start with no offset, tune if needed
    
    return voltage;
}

int getBatteryPercent() {
    // Handle out-of-range cases
    if (batteryVoltage >= BATTERY_VOLTAGE_FULL) return 100;
    if (batteryVoltage <= BATTERY_VOLTAGE_EMPTY) return 0;
    
    // Find the appropriate segment and interpolate
    for (int i = 0; i < curveSize - 1; i++) {
        if (batteryVoltage >= dischargeCurve[i + 1].voltage) {
            float voltageRange = dischargeCurve[i].voltage - dischargeCurve[i + 1].voltage;
            if (voltageRange <= 0) return dischargeCurve[i + 1].percent;
            float voltageInSegment = batteryVoltage - dischargeCurve[i + 1].voltage;
            float percentRange = dischargeCurve[i].percent - dischargeCurve[i + 1].percent;
            return dischargeCurve[i + 1].percent + (int)((voltageInSegment / voltageRange) * percentRange);
        }
    }
    
    // Should never reach here, but return 0 as fallback
    return 0;
}

bool isBatteryCharging() {
    // Method 1: Very high voltage = definitely USB connected
    if (batteryVoltage > BATTERY_VOLTAGE_CHARGING) {
        return true;
    }

    // Method 2: Voltage actively rising = charging
    // Method 3: Stable at high voltage = plugged in (full or maintaining)
    bool conditionsMet = (voltageChangeRate > BATTERY_CHARGE_RATE_THRESHOLD) ||
                         (batteryVoltage > BATTERY_VOLTAGE_HIGH &&
                          voltageChangeRate > BATTERY_STABLE_RATE_THRESHOLD);

    // Hysteresis: once charging detected, require negative rate to clear it
    // prevents flickering from ADC noise near the threshold
    if (batteryCharging) {
        return voltageChangeRate > -BATTERY_CHARGE_RATE_THRESHOLD;
    }

    return conditionsMet;
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
        voltageChangeRate = (voltageChangeRate * 0.7) + (instantRate * 0.3);
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

    #ifdef DEBUG
    Serial.printf("🔋 Battery: %.2fV (%d%%) Rate: %.4fV/s", 
                  batteryVoltage, batteryPercent, voltageChangeRate);
    if (batteryCharging) {
        Serial.print(" ⚡ CHARGING");
    }
    Serial.println();
    #endif
}