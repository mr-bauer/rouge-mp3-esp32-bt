#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

// Battery hardware
#define BATTERY_PIN 35                   // A13 on Feather V2
#define BATTERY_SAMPLES 10               // Moving average sample count

// Battery voltage thresholds (LiPo)
#define BATTERY_VOLTAGE_FULL 4.2f        // Full charge voltage
#define BATTERY_VOLTAGE_EMPTY 3.2f       // Minimum safe voltage
#define BATTERY_VOLTAGE_CHARGING 4.25f   // Definitely charging/USB connected
#define BATTERY_VOLTAGE_HIGH 4.0f        // High voltage (stable = charging)

// Charging detection
#define BATTERY_CHARGE_RATE_THRESHOLD 0.00005f  // Rising voltage threshold (V/s) — LiPo CC charge ~0.0001 V/s
#define BATTERY_STABLE_RATE_THRESHOLD -0.00005f // Not dropping = charging/plugged in

void initBattery();
void updateBattery();
float getBatteryVoltage();
int getBatteryPercent();
bool isBatteryCharging();

#endif