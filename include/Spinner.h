#ifndef SPINNER_H
#define SPINNER_H

#include <Arduino.h>

extern volatile bool animationRunning;
extern TaskHandle_t spinnerHandle;

void startLoadingAnimation();
void stopLoadingAnimation();

#endif