#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

// Button timing constants - NEW
#define BUTTON_DEBOUNCE_MS 300
#define BUTTON_MIN_DURATION_ADC 10  // For ADC pins

void initButtons();
void pollButtons();

// Helper function - NEW
bool processADCButton(int btnIndex, int gpio, const char* name, 
                      void (*handler)(int), int handlerIndex);

#endif