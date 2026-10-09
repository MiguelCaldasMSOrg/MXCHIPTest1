#include <Arduino.h>
#include "AppConfig.h"
#include "NfcUart.h"
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
  uint64_t clockUs = 0;
  unsigned int inputs = 0, outputs = 0, critical = 0;
  unsigned int level = 1;
  struct Byte {
    uint64_t start;
    uint8_t value;
    bool goodStop;
  };
  struct Edge {
    uint64_t time;
    int level;
  };
  std::vector<Byte> incoming;
  std::vector<Edge> outgoing;
  void check(bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      std::exit(1);
    }
  }
  void queue(uint64_t start, const uint8_t *bytes, size_t size, bool badStop = false) {
    for (size_t i = 0; i < size; ++i) {
      incoming.push_back({start + i * 90, bytes[i], !(badStop && i == 0)});
    }
  }
  void fixture(bool badAck, bool badStop, bool truncate, bool large) {
    clockUs = 0;
    incoming.clear();
    outgoing.clear();
    const uint8_t ack[] = {0, 0, 255, 0, 255, 0};
    uint8_t response[] = {0, 0, 255, 6, 250, 0xD5, 3, 0x32, 1, 6, 7, 0xE8, 0};
    uint8_t ackCopy[6];
    memcpy(ackCopy, ack, 6);
    if (badAck) {
      ackCopy[4] ^= 1;
    }
    if (large) {
      response[3] = 100;
      response[4] = 156;
    }
    queue(1200, ackCopy, 6, badStop);
    queue(2300, response, truncate ? 6 : sizeof(response));
  }
}
uint32_t micros() {
  return static_cast<uint32_t>(++clockUs);
}
uint32_t millis() {
  return static_cast<uint32_t>(++clockUs / 1000);
}
void delay(uint32_t ms) {
  clockUs += ms * 1000ULL;
}
void nfcRxConstructed(PinName pin) {
  check(pin == PB_14, "module TX goes to host RX P14");
  ++inputs;
}
void nfcTxConstructed(PinName pin, int value) {
  check(pin == PB_0 && value == 1, "host TX P0 starts idle high");
  ++outputs;
}
void nfcTxLevel(int value) {
  level = value;
  outgoing.push_back({clockUs, value});
}
int nfcRxLevel() {
  ++clockUs;
  for (const auto &byte: incoming) {
    if (clockUs < byte.start || clockUs >= byte.start + 87) {
      continue;
    }
    const unsigned int bit = static_cast<unsigned int>((clockUs - byte.start) * 115200 / 1000000);
    if (bit == 0) {
      return 0;
    }
    if (bit <= 8) {
      return (byte.value >> (bit - 1)) & 1;
    }
    return byte.goodStop ? 1 : 0;
  }
  return 1;
}
void core_util_critical_section_enter() {
  ++critical;
}
void core_util_critical_section_exit() {
  check(critical == 1, "balanced critical sections");
  --critical;
}

int main() {
  NfcUart::wake();
  if (!AppConfig::kNfcEnabled) {
    check(inputs == 0 && outputs == 0 && outgoing.empty(), "inactive mode does not construct UART or drive Grove pins");
    std::cout << "PASS: NFC UART inactive pin exclusion\n";
    return 0;
  }
  check(inputs == 1 && outputs == 1 && level == 1 && critical == 0, "wake initializes only NFC Grove pins and restores interrupts");
  const uint8_t command[] = {2};
  uint8_t response[4];
  size_t length = 0;
  fixture(false, false, false, false);
  check(NfcUart::link().exchange(command, 1, response, 4, length) && length == 4 && response[0] == 0x32, "real GPIO UART decodes timed ACK and response");
  check(critical == 0 && level == 1, "completed exchange leaves interrupts and TX idle");
  for (unsigned int fault = 0; fault < 5; ++fault) {
    fixture(fault == 0, fault == 1, fault == 2, fault == 3);
    if (fault == 4) {
      incoming.clear();
    }
    check(!NfcUart::link().exchange(command, 1, response, 4, length) && length == 0 && critical == 0, "bad ACK, stop bit, partial/large frame and timeout fail explicitly");
    check(clockUs < 1600000, "bounded ACK/frame wait");
  }
  std::cout << "PASS: NFC GPIO UART polarity, timed framing, ACK/checksum, receive bounds, timeout and interrupt cleanup\n";
}
