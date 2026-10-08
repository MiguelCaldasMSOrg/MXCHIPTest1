#pragma once

namespace OnboardTests {
  void begin();
  void update();
  void buttonA();
  void buttonB();
  void handleSerial(char value);
}

namespace SensorTests {
  void begin();
  void update();
  void nextPage();
  void sampleNow();
}

namespace MicrophoneTests {
  void begin();
  void update();
  void record();
  void play();
}

namespace FileSystemTests {
  void run();
  void verify();
  void cleanup();
  void format();
}

namespace NetworkTests {
  void run();
}

namespace IrdaTests {
  void begin();
  void transmit();
}

namespace SecurityChipTests {
  void run();
}
