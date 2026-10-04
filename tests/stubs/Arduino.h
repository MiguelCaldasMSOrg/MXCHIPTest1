#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
  extern unsigned int inputConstructions;
  extern bool pinLevels[4];
  extern unsigned int pinOutputConstructions[4];
  extern void (*timerCallback)();
  extern unsigned int timerPeriodUs;
}

inline unsigned long millis() {
  return static_cast<unsigned long>(FakeHardware::nowUs / 1000);
}

inline void delay(unsigned long milliseconds) {
  FakeHardware::nowUs += static_cast<uint64_t>(milliseconds) * 1000;
}

class __FlashStringHelper;
#define F(value) reinterpret_cast<const __FlashStringHelper *>(value)

class TestSerial {
  public:
  std::string output;
  std::deque<uint8_t> input;

  void print(const __FlashStringHelper *value) {
    print(reinterpret_cast<const char *>(value));
  }
  template <typename T> void print(const T &value) {
    std::ostringstream stream;
    stream << value;
    output += stream.str();
  }
  template <typename T> void println(const T &value) {
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
  unsigned int writes[4] = {};
  bool invalidWrite = false;

  void print(unsigned int line, const char *text) {
    if (line >= 4 || strlen(text) > 16) {
      invalidWrite = true;
      return;
    }
    lines[line] = text;
    ++writes[line];
  }
};

extern TestSerial Serial;
extern TestScreen Screen;

enum PinName {
  PB_0,
  PB_7,
  PB_14,
  PC_6
};

enum PinMode {
  PullNone,
  PullUp
};

class DigitalIn {
  public:
  explicit DigitalIn(PinName name): pin(name) {
    FakeHardware::inputConstructions++;
  }
  void mode(PinMode mode) {
    if (mode == PullUp) {
      FakeHardware::pinLevels[pin] = true;
    }
  }
  int read() const {
    return FakeHardware::loopback ? FakeHardware::outputLevel : FakeHardware::pinLevels[pin];
  }

  private:
  PinName pin;
};

class DigitalOut {
  public:
  DigitalOut(PinName name, int value): pin(name) {
    FakeHardware::outputConstructions++;
    FakeHardware::pinOutputConstructions[pin]++;
    write(value);
  }
  void write(int value) {
    FakeHardware::outputWrites++;
    FakeHardware::outputLevel = value != 0;
    FakeHardware::pinLevels[pin] = value != 0;
  }

  private:
  PinName pin;
};

class Ticker {
  public:
  void attach_us(void (*callback)(), unsigned int period) {
    FakeHardware::timerCallback = callback;
    FakeHardware::timerPeriodUs = period;
  }
};
