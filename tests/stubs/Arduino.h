#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <sstream>
#include <string>

namespace FakeHardware {
  extern uint64_t nowUs;
  extern bool loopback;
  extern bool outputLevel;
  extern unsigned int outputConstructions;
  extern unsigned int outputWrites;
  extern void (*timerCallback)();
  extern unsigned int timerPeriodUs;
}

inline unsigned long millis() {
  return static_cast<unsigned long>(FakeHardware::nowUs / 1000);
}

class TestSerial {
  public:
  std::string output;
  std::deque<uint8_t> input;

  template<typename T> void print(const T &value) {
    std::ostringstream stream;
    stream << value;
    output += stream.str();
  }
  template<typename T> void println(const T &value) {
    print(value);
    println();
  }
  void println() {
    output += '\n';
  }
  size_t write(const uint8_t *data, size_t size) {
    output.append(data, data + size);
    return size;
  }
  int available() const {
    return static_cast<int>(input.size());
  }
  int read() {
    if (input.empty()) {
      return -1;
    }
    const int value = input.front();
    input.pop_front();
    return value;
  }
};

class TestScreen {
  public:
  std::string lines[4];
  bool invalidWrite = false;

  void print(unsigned int line, const char *text) {
    if (line >= 4 || strlen(text) > 16) {
      invalidWrite = true;
      return;
    }
    lines[line] = text;
  }
};

extern TestSerial Serial;
extern TestScreen Screen;

enum PinName {
  PB_0,
  PB_7
};

enum PinMode {
  PullNone
};

class DigitalIn {
  public:
  explicit DigitalIn(PinName) {
  }
  void mode(PinMode) {
  }
  int read() const {
    return FakeHardware::loopback && FakeHardware::outputLevel;
  }
};

class DigitalOut {
  public:
  DigitalOut(PinName, int value) {
    FakeHardware::outputConstructions++;
    write(value);
  }
  void write(int value) {
    FakeHardware::outputWrites++;
    FakeHardware::outputLevel = value != 0;
  }
};

class Ticker {
  public:
  void attach_us(void (*callback)(), unsigned int period) {
    FakeHardware::timerCallback = callback;
    FakeHardware::timerPeriodUs = period;
  }
};
