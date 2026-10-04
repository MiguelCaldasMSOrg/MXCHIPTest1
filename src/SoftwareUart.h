#pragma once

#include <stddef.h>
#include <stdint.h>

// A 9600-baud, 8N1 UART sampled at 26 us (nominal baud error +0.16%).
// Main-context queue operations must mask the sampling interrupt.
class SoftwareUart {
  public:
  static constexpr unsigned int kSamplePeriodUs = 26;
  static constexpr unsigned int kSamplesPerBit = 4;
  static constexpr size_t kRxCapacity = 512;
  static constexpr size_t kTxCapacity = 256;

  bool write(const char *text, size_t length) {
    if (text == nullptr || length == 0 || length > kTxCapacity - (txWritten - txRead)) {
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      txBuffer[(txWritten + i) % kTxCapacity] = static_cast<uint8_t>(text[i]);
    }
    txWritten += length;
    return true;
  }

  int read() {
    if (rxRead == rxWritten) {
      return -1;
    }
    const uint8_t value = rxBuffer[rxRead % kRxCapacity];
    rxRead++;
    return value;
  }

  bool transmitting() const {
    return txBits != 0 || txWritten != txRead;
  }

  uint32_t errors() const {
    return framingErrors + overflows;
  }

  void discardTransmit() {
    txRead = txWritten;
    txBits = 0;
    txTicks = 0;
  }

  bool sample(bool rxHigh) {
    receiveSample(rxHigh);
    if (txBits == 0) {
      if (txRead == txWritten) {
        return true;
      }
      txWord = static_cast<uint16_t>((txBuffer[txRead % kTxCapacity] << 1) | 0x200);
      txRead++;
      txBits = 10;
      txTicks = kSamplesPerBit;
    }
    const bool level = (txWord & 1) != 0;
    if (--txTicks == 0) {
      txWord >>= 1;
      txBits--;
      txTicks = kSamplesPerBit;
    }
    return level;
  }

  private:
  enum class RxState {
    Idle,
    Start,
    Data,
    Stop,
    Recover
  };

  uint8_t rxBuffer[kRxCapacity] = {};
  uint8_t txBuffer[kTxCapacity] = {};
  volatile uint32_t rxWritten = 0;
  volatile uint32_t rxRead = 0;
  volatile uint32_t txWritten = 0;
  volatile uint32_t txRead = 0;
  volatile uint32_t framingErrors = 0;
  volatile uint32_t overflows = 0;
  RxState rxState = RxState::Idle;
  unsigned int rxTicks = 0;
  unsigned int rxBit = 0;
  uint8_t rxByte = 0;
  uint16_t txWord = 0;
  volatile unsigned int txBits = 0;
  unsigned int txTicks = 0;

  void receiveSample(bool high) {
    if (rxState == RxState::Recover) {
      if (high) {
        rxState = RxState::Idle;
      }
      return;
    }
    if (rxState == RxState::Idle) {
      if (!high) {
        rxState = RxState::Start;
        rxTicks = kSamplesPerBit / 2;
      }
      return;
    }
    if (--rxTicks != 0) {
      return;
    }
    rxTicks = kSamplesPerBit;
    switch (rxState) {
      case RxState::Start:
        if (high) {
          rxState = RxState::Idle;
        } else {
          rxByte = 0;
          rxBit = 0;
          rxState = RxState::Data;
        }
        break;
      case RxState::Data:
        if (high) {
          rxByte |= static_cast<uint8_t>(1U << rxBit);
        }
        if (++rxBit == 8) {
          rxState = RxState::Stop;
        }
        break;
      case RxState::Stop:
        if (!high) {
          framingErrors++;
          rxState = RxState::Recover;
        } else {
          if (rxWritten - rxRead == kRxCapacity) {
            overflows++;
          } else {
            rxBuffer[rxWritten % kRxCapacity] = rxByte;
            rxWritten++;
          }
          rxState = RxState::Idle;
        }
        break;
      case RxState::Idle:
      case RxState::Recover:
        break;
    }
  }
};
