#ifndef NAVIGATION_H
#define NAVIGATION_H

void handleButtonPress(int buttonIndex);
void handleCenter();
void handleTop();         // NEW - Menu/Back
void handleBottom();      // NEW - Play/Pause
void handleLeft();        // NEW - Previous track
void handleRight();       // NEW - Next track
void autoNext();
void autoPrevious();      // NEW - Go to previous track

#endif