#include <Arduino.h>
#include <platform/mbed_critical.h>
#include "AppConfig.h"
#include "RadioBridge.h"
#include "RadioDisplay.h"
#include "rf_receiver.h"
#include "rf_transmitter.h"

namespace {
  constexpr unsigned long kStatsMs = 5000;
  constexpr unsigned long kTxGapMs = 100;

  Ticker ticker;
  VirtualWireDecoder decoder;
  VirtualWireTransmitter transmitter;
  SerialRadioInput serialInput;
  unsigned long lastStatsMs = 0;
  unsigned long lastTxMs = 0;
  unsigned long txCompleted = 0;
  bool txInProgress = false;
  bool initialized = false;

  DigitalIn &input() {
    static DigitalIn pin(PB_0);
    return pin;
  }

  DigitalOut &output() {
    // Construct only for ASK TX; other modes must not claim the shared P2 pin.
    static DigitalOut pin(PB_7, 0);
    return pin;
  }

  void sampleRadio() {
    if (AppConfig::kRadioTransmitEnabled) {
      output().write(transmitter.sample() ? 1 : 0);
    }
    decoder.sample(input().read() != 0);
  }

  void receiveMessage() {
    if (!decoder.available()) {
      return;
    }
    uint8_t payload[VirtualWireProtocol::kMaxPayloadLength];
    uint8_t length = 0;
    core_util_critical_section_enter();
    const bool received = decoder.read(payload, length);
    core_util_critical_section_exit();
    if (received) {
      RadioDisplay::show(payload, length, "RF");
    }
  }

  void readSerialLines() {
    for (unsigned int processed = 0; processed < 32 && Serial.available() > 0; processed++) {
      const int value = Serial.read();
      if (value < 0) {
        Serial.println("RF TX error: serial read failed.");
        break;
      }
      switch (serialInput.push(static_cast<uint8_t>(value))) {
        case SerialRadioInput::Result::None:
          break;
        case SerialRadioInput::Result::Queued:
          if (AppConfig::kRadioTransmitEnabled) {
            Serial.println("RF TX queued.");
          } else {
            uint8_t discarded[VirtualWireProtocol::kMaxPayloadLength];
            uint8_t length = 0;
            if (!serialInput.read(discarded, length)) {
              Serial.println("RF TX error: inconsistent serial queue.");
            }
            Serial.println("RF TX disabled by audio mode; line discarded to protect P2.");
          }
          break;
        case SerialRadioInput::Result::Empty:
          Serial.println("RF TX skipped: empty line.");
          break;
        case SerialRadioInput::Result::TooLong:
          Serial.println("RF TX error: line exceeds 77 bytes; entire line discarded.");
          break;
        case SerialRadioInput::Result::InvalidCharacter:
          Serial.println("RF TX error: use printable ASCII or tabs; entire line discarded.");
          break;
        case SerialRadioInput::Result::QueueFull:
          Serial.println("RF TX error: four-message queue full; line discarded.");
          break;
      }
    }
  }

  void finishTransmission(unsigned long now) {
    if (txInProgress && !transmitter.active()) {
      txInProgress = false;
      txCompleted++;
      lastTxMs = now;
      Serial.println("RF TX complete (not a reception acknowledgement).");
    }
  }

  void startTransmission(unsigned long now) {
    if (!AppConfig::kRadioTransmitEnabled || txInProgress || now - lastTxMs < kTxGapMs) {
      return;
    }
    uint8_t payload[VirtualWireProtocol::kMaxPayloadLength];
    uint8_t length = 0;
    if (!serialInput.read(payload, length)) {
      return;
    }
    VirtualWirePacket packet;
    if (!packet.encode(payload, length)) {
      Serial.println("RF TX error: could not encode packet.");
      return;
    }
    Serial.print("RF transmitting: ");
    Serial.write(payload, length);
    Serial.println();
    core_util_critical_section_enter();
    const bool started = transmitter.start(packet);
    core_util_critical_section_exit();
    if (!started) {
      Serial.println("RF TX error: transmitter could not start.");
      return;
    }
    txInProgress = true;
  }

  void reportStatistics(unsigned long now) {
    if (now - lastStatsMs < kStatsMs) {
      return;
    }
    lastStatsMs = now;
    core_util_critical_section_enter();
    const VirtualWireDecoder::Statistics stats = decoder.statistics();
    core_util_critical_section_exit();
    char status[144];
    snprintf(
      status,
      sizeof(status),
      "RF stats: received=%lu rejected=%lu dropped=%lu transitions=%lu transmitted=%lu",
      static_cast<unsigned long>(stats.received),
      static_cast<unsigned long>(stats.rejected),
      static_cast<unsigned long>(stats.dropped),
      static_cast<unsigned long>(stats.transitions),
      txCompleted
    );
    Serial.println(status);
  }
}

namespace RadioBridge {
  void begin() {
    if (!AppConfig::kRadioEnabled) {
      Serial.println("ASK radio inactive in the selected firmware mode.");
      return;
    }
    input().mode(PullNone);
    decoder.reset(input().read() != 0);
    if (AppConfig::kRadioTransmitEnabled) {
      output().write(0);
    }
    ticker.attach_us(&sampleRadio, VirtualWireProtocol::kSamplePeriodUs);
    initialized = true;
    RadioDisplay::begin();
    Serial.println("RF RX: Grove P0/P14 (PB_0), VirtualWire 2000 bit/s.");
    if (AppConfig::kRadioTransmitEnabled) {
      Serial.println("RF TX: Grove P2/P16 (PB_7). Send 1-77 ASCII bytes followed by CR, LF, or CRLF.");
    } else {
      Serial.println("RF TX disabled: audio owns P1/P2. RF reception remains active.");
    }
  }

  void update() {
    if (!AppConfig::kRadioEnabled || !initialized) {
      return;
    }
    receiveMessage();
    finishTransmission(millis());
    readSerialLines();
    startTransmission(millis());
    RadioDisplay::update();
    reportStatistics(millis());
  }
}
