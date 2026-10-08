#include "GroveEInkTests.h"

#include <Arduino.h>
#include <platform/mbed_critical.h>
#include "EInkPattern.h"

namespace {
  constexpr unsigned long kRefreshIntervalMs = 180000;
  constexpr unsigned long kHandshakeTimeoutMs = 5000;
  constexpr size_t kChunkBytes = 76;
  constexpr unsigned long kChunkDelayMs = 70;
  constexpr uint8_t kBitOffsetsUs[] = {4, 9, 13, 17, 22, 26, 30, 35};
  constexpr uint8_t kReceiveOffsetsUs[] = {7, 11, 15, 20, 24, 28, 33, 37};

  class GroveUart {
    public:
    GroveUart(): rx(PB_0), tx(PB_14, 1) {
      rx.mode(PullUp);
    }

    void write(uint8_t value) {
      core_util_critical_section_enter();
      const uint32_t startedUs = micros();
      tx.write(0);
      for (unsigned int bit = 0; bit < 8; ++bit) {
        waitUntil(startedUs + kBitOffsetsUs[bit]);
        tx.write((value >> bit) & 1);
      }
      waitUntil(startedUs + 39);
      tx.write(1);
      waitUntil(startedUs + 43);
      core_util_critical_section_exit();
    }

    bool read(uint8_t &value, unsigned long timeoutMs) {
      const unsigned long startedMs = millis();
      while (rx.read() != 0) {
        if (millis() - startedMs >= timeoutMs) {
          return false;
        }
      }

      core_util_critical_section_enter();
      const uint32_t startedUs = micros();
      value = 0;
      for (unsigned int bit = 0; bit < 8; ++bit) {
        waitUntil(startedUs + kReceiveOffsetsUs[bit]);
        if (rx.read() != 0) {
          value |= static_cast<uint8_t>(1U << bit);
        }
      }
      waitUntil(startedUs + 41);
      const bool validStop = rx.read() != 0;
      core_util_critical_section_exit();
      return validStop;
    }

    private:
    DigitalIn rx;
    DigitalOut tx;

    static void waitUntil(uint32_t targetUs) {
      while (static_cast<int32_t>(targetUs - micros()) > 0) {}
    }
  };

  GroveUart &uart() {
    static GroveUart instance;
    return instance;
  }

  unsigned long lastRefreshMs = 0;
  bool refreshed = false;

  bool beginTransfer() {
    uart().write('a');
    uint8_t reply = 0;
    if (!uart().read(reply, kHandshakeTimeoutMs)) {
      Serial.println(F("E-ink handshake timed out; check P0/P14 wiring and module power."));
      return false;
    }
    if (reply != 'b') {
      Serial.print(F("E-ink handshake failed: expected 'b', received 0x"));
      Serial.println(reply, BASE_HEX);
      return false;
    }
    return true;
  }

  void writePlane(EInkPattern::Plane plane) {
    for (size_t offset = 0; offset < EInkPattern::kPlaneBytes; offset += kChunkBytes) {
      const size_t count = min(kChunkBytes, EInkPattern::kPlaneBytes - offset);
      for (size_t index = 0; index < count; ++index) {
        uart().write(EInkPattern::byteAt(plane, offset + index));
      }
      delay(kChunkDelayMs);
    }
  }

  bool refresh() {
    Screen.print(1, "E-ink handshake");
    Serial.println(F("E-ink test: requesting a 230400-baud transfer on Grove P0/P14."));
    if (!beginTransfer()) {
      Screen.print(1, "E-ink no reply");
      return false;
    }

    Screen.print(1, "E-ink writing");
    Serial.println(F("E-ink handshake passed; waiting two seconds before image data."));
    delay(2000);
    writePlane(EInkPattern::Plane::Black);
    writePlane(EInkPattern::Plane::Red);
    lastRefreshMs = millis();
    refreshed = true;
    Screen.print(1, "E-ink sent");
    Screen.print(2, "Black White Red");
    Screen.print(3, "Wait 180 seconds");
    Serial.println(F("E-ink image sent: black, white and red horizontal bands."));
    Serial.println(F("Do not refresh again for at least 180 seconds."));
    return true;
  }
}

namespace GroveEInkTests {
  bool begin() {
    Serial.println(F("Grove triple-color e-ink 1.54 v1.0 mode."));
    Serial.println(F("Module TX -> P0/PB_0; module RX <- P14/PB_14; UART 230400."));
    return refresh();
  }

  void requestRefresh() {
    if (refreshed) {
      const unsigned long elapsedMs = millis() - lastRefreshMs;
      if (elapsedMs < kRefreshIntervalMs) {
        const unsigned long remainingSeconds = (kRefreshIntervalMs - elapsedMs + 999) / 1000;
        char status[24];
        snprintf(status, sizeof(status), "Wait %lu seconds", remainingSeconds);
        Screen.print(3, status);
        Serial.print(F("E-ink refresh rejected; wait "));
        Serial.print(remainingSeconds);
        Serial.println(F(" more seconds."));
        return;
      }
    }
    refresh();
  }
}
