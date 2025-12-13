#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

void initBattery();
void updateBattery();
float getBatteryVoltage();
int getBatteryPercent();
bool isBatteryCharging();

#endif