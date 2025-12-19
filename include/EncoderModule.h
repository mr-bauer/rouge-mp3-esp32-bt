#ifndef ENCODER_MODULE_H
#define ENCODER_MODULE_H

#include <Arduino.h>

// Encoder hardware pins
#define ENCODER_PIN_A 26
#define ENCODER_PIN_B 25

// Note: Timing constants moved to State.h

void initEncoder();
void updateEncoder();
bool isEncoderScrolling();

#endif