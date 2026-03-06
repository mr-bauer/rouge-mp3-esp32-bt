#ifndef NAVIGATION_H
#define NAVIGATION_H

void handleButtonPress(int buttonIndex);
void handleCenter();
void handleTop();
void handleBottom();
void handleLeft();
void handleRight();
void handleTopLongPress();     // Hold Top → jump to Home
void handleBottomLongPress();  // Hold Bottom → jump to Now Playing
void autoNext();
void autoPrevious();

#endif