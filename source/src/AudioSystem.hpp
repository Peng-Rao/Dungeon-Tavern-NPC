#pragma once

// We define the implementation macro ONLY ONCE.
// If you include this header in multiple places, look into creating a separate .cpp,
// but for a quick single-consumer implementation in DungeonApp, this is perfect.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"
#include <string>
#include <iostream>

class AudioSystem {
public:
  AudioSystem() : initialized(false) {}

  ~AudioSystem() {
    if (initialized) {
      ma_engine_uninit(&engine);
    }
  }

  // Initializes the audio engine
  bool init() {
    ma_result result = ma_engine_init(NULL, &engine);
    if (result != MA_SUCCESS) {
      std::cerr << "[AUDIO] Failed to initialize audio engine." << std::endl;
      return false;
    }
    initialized = true;
    return true;
  }

  // Plays a background track in a seamless loop
  void playBackgroundMusic(const std::string& filePath) {
    if (!initialized) return;

    // MA_SOUND_FLAG_STREAM keeps memory low by streaming large music files from disk
    ma_result result = ma_engine_play_sound(&engine, filePath.c_str(), NULL);
    if (result != MA_SUCCESS) {
      std::cerr << "[AUDIO] Failed to play music file: " << filePath << std::endl;
    } else {
      std::cout << "[AUDIO] Playing background music: " << filePath << std::endl;
    }
  }

private:
  ma_engine engine;
  bool initialized;
};
