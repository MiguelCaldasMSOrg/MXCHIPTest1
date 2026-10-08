#pragma once

#include <Arduino.h>
#include <cstdlib>
#include <iostream>

namespace FakeHardware {
  uint64_t nowUs = 0;
  bool loopback = false;
  bool outputLevel = false;
  unsigned int outputConstructions = 0;
  unsigned int outputWrites = 0;
  unsigned int inputConstructions = 0;
  bool pinLevels[4] = {};
  unsigned int pinOutputConstructions[4] = {};
  void (*timerCallback)() = nullptr;
  unsigned int timerPeriodUs = 0;
}

TestSerial Serial;
TestScreen Screen;

inline void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
