#include <Arduino.h>
#include <platform/mbed_critical.h>
#include "AppConfig.h"
#include "NfcUart.h"

namespace {
  class Uart: public NfcProtocol::Link {
    public:
    Uart(): rx(PB_14), tx(PB_0, 1) {
      rx.mode(PullUp);
    }
    void write(uint8_t value) {
      const uint32_t start = micros();
      tx.write(0);
      for (unsigned int bit = 0; bit < 8; ++bit) {
        wait(start + (bit + 1) * 1000000UL / 115200);
        tx.write((value >> bit) & 1);
      }
      wait(start + 78);
      tx.write(1);
      wait(start + 87);
    }
    bool exchange(const uint8_t *command, size_t size, uint8_t *response, size_t capacity, size_t &length) override {
      length = 0;
      uint8_t frame[NfcProtocol::kFrameBytes];
      const size_t count = NfcProtocol::encode(command, size, frame);
      if (!AppConfig::kNfcEnabled || count == 0) {
        return false;
      }
      core_util_critical_section_enter();
      for (size_t i = 0; i < count; ++i) {
        write(frame[i]);
      }
      core_util_critical_section_exit();
      const uint8_t ack[] = {0, 0, 255, 0, 255, 0};
      if (!startByte(250)) {
        return false;
      }
      core_util_critical_section_enter();
      bool ok = true;
      for (size_t i = 0; ok && i < sizeof(ack); ++i) {
        ok = read(frame[i], i == 0);
      }
      ok = ok && memcmp(frame, ack, sizeof(ack)) == 0;
      core_util_critical_section_exit();
      if (!ok || !startByte(1500)) {
        return false;
      }
      core_util_critical_section_enter();
      for (size_t i = 0; ok && i < 5; ++i) {
        ok = read(frame[i], i == 0);
      }
      size_t total = 0;
      if (ok) {
        total = static_cast<size_t>(frame[3]) + 7;
        ok = frame[0] == 0 && frame[1] == 0 && frame[2] == 255 && frame[3] >= 2 && static_cast<uint8_t>(frame[3] + frame[4]) == 0 && total <= sizeof(frame);
      }
      for (size_t i = 5; ok && i < total; ++i) {
        ok = read(frame[i], false);
      }
      core_util_critical_section_exit();
      return ok && NfcProtocol::decode(frame, total, command[0], response, capacity, length);
    }

    private:
    DigitalIn rx;
    DigitalOut tx;
    static void wait(uint32_t target) {
      while (static_cast<int32_t>(target - micros()) > 0) {}
    }
    bool startByte(uint32_t timeoutMs) {
      const uint32_t start = millis();
      while (rx.read()) {
        if (static_cast<uint32_t>(millis()) - start >= timeoutMs) {
          return false;
        }
      }
      return true;
    }
    bool read(uint8_t &value, bool started) {
      const uint32_t waiting = micros();
      if (!started) {
        while (rx.read()) {
          if (static_cast<uint32_t>(micros() - waiting) > 2000) {
            return false;
          }
        }
      }
      const uint32_t start = micros();
      value = 0;
      wait(start + 4);
      if (rx.read()) {
        return false;
      }
      for (unsigned int bit = 0; bit < 8; ++bit) {
        wait(start + (3 + bit * 2) * 1000000UL / 230400);
        if (rx.read()) {
          value |= static_cast<uint8_t>(1U << bit);
        }
      }
      wait(start + 82);
      return rx.read() != 0;
    }
  };
  Uart &uart() {
    static Uart value;
    return value;
  }
}

namespace NfcUart {
  void wake() {
    if (!AppConfig::kNfcEnabled) {
      return;
    }
    core_util_critical_section_enter();
    const uint8_t wakeBytes[] = {0x55, 0x55, 0, 0, 0};
    for (uint8_t byte: wakeBytes) {
      uart().write(byte);
    }
    core_util_critical_section_exit();
    delay(10);
  }
  NfcProtocol::Link &link() {
    return uart();
  }
}
