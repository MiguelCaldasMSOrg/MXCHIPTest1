#pragma once
#include <stdint.h>
#include <string.h>

enum PinName {
  PB_0,
  PB_14
};
enum PinMode {
  PullUp
};
uint32_t micros();
uint32_t millis();
void delay(uint32_t ms);
void nfcRxConstructed(PinName pin);
void nfcTxConstructed(PinName pin, int value);
int nfcRxLevel();
void nfcTxLevel(int value);
class DigitalIn {
  public:
  explicit DigitalIn(PinName pin) {
    nfcRxConstructed(pin);
  }
  void mode(PinMode) {}
  int read() const {
    return nfcRxLevel();
  }
};
class DigitalOut {
  public:
  DigitalOut(PinName pin, int value) {
    nfcTxConstructed(pin, value);
  }
  void write(int value) {
    nfcTxLevel(value);
  }
};
