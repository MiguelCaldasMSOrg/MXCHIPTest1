#pragma once

#include <Arduino.h>

namespace RtcTests {
  enum class Status {
    NotReady,
    Testing,
    Passed,
    Failed
  };

  struct Result {
    uint16_t passed;
    uint16_t failed;
  };

  bool begin();
  Status status();
  Result runFull();
  void updateDisplay();
  bool setToBuildTime();
  void printHelp();
  void handleSerial(char command);
}
