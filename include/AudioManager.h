#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include "AudioTools.h"

extern AudioPlayer player;

void initAudio();
void audioLoop();
void checkConnectionWatchdog() ;
void startPlayback();
void pausePlayback();
void resumePlayback();
void stopPlayback();
void playCurrentSong(bool updateDisplay);
void reconnectBluetooth();
void disconnectBluetooth();
void changeBluetoothDevice(const String& new_device_name);

#endif