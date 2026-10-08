#include <Arduino.h>
#include <IrDASensor.h>
#include "AppConfig.h"
#include "OnboardTests.h"

namespace {
  bool ready = false;
  bool sent = false;
  uint32_t lastSentMs = 0;
  uint32_t count = 0;

  IRDASensor &emitter() {
    static IRDASensor sensor;
    return sensor;
  }
}

namespace IrdaTests {
  void begin() {
    if (!AppConfig::kIrdaEnabled) {
      return;
    }
    Screen.print(0, "IrDA TX test");
    ready = emitter().init() == 0;
    if (!ready) {
      Screen.print(1, "IrDA init FAIL");
      Serial.println(F("IrDA FAIL: USART3/PB_10 initialization."));
      return;
    }
    Screen.print(1, "38400 baud SIR");
    Screen.print(2, "A:send B:help");
    Screen.print(3, "Need IR receiver");
    Serial.println(F("IrDA initialized; no automatic transmission. API completion cannot verify emitted light or received bytes."));
  }

  void transmit() {
    if (!AppConfig::kIrdaEnabled) {
      return;
    }
    if (!ready) {
      Serial.println(F("IrDA FAIL: initialize successfully before transmitting."));
      return;
    }
    if (sent && static_cast<uint32_t>(millis()) - lastSentMs < 1000) {
      Serial.println(F("IrDA request rejected: wait one second between bursts."));
      return;
    }
    unsigned char pattern[] = {0x55, 0xAA, 0x00, 0xFF, 0x4D, 0x58, 0x43, 0x48};
    const unsigned char result = emitter().IRDATransmit(pattern, sizeof(pattern), 500);
    lastSentMs = millis();
    sent = true;
    if (result != 0) {
      Screen.print(1, "IrDA TX FAIL");
      Serial.print(F("IrDA FAIL: transmit returned "));
      Serial.println(static_cast<unsigned int>(result));
      return;
    }
    char text[17];
    snprintf(text, sizeof(text), "Sent %lu bursts", static_cast<unsigned long>(++count));
    Screen.print(1, text);
    Serial.println(F("IrDA TX API completed: 55 AA 00 FF 4D 58 43 48 at 38400 baud. Optical/receiver verification remains pending."));
  }
}
