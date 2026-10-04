#include <Arduino.h>
#include <platform/mbed_critical.h>
#include "AppConfig.h"
#include "LoRaBridge.h"
#include "LoRaE5.h"
#include "RadioDisplay.h"
#include "SoftwareUart.h"

namespace {
  struct Module {
    DigitalIn rx;
    DigitalOut tx;
    SoftwareUart uart;
    LoRaE5 modem;
    const char *name;
    bool faultReported = false;
    uint32_t lastTransmitMs = 0;
    uint32_t transmittedPackets = 0;
    uint32_t receivedPackets = 0;

    Module(PinName receivePin, PinName transmitPin, const char *label): rx(receivePin), tx(transmitPin, 1), name(label) {
      rx.mode(PullUp);
    }

    void sample() {
      tx.write(uart.sample(rx.read() != 0) ? 1 : 0);
    }

    bool canTransmit(uint32_t now) const {
      return modem.ready() && now - lastTransmitMs >= LoRaE5::kTransmitIntervalMs;
    }

    void update(uint32_t now) {
      if (uart.errors() != 0 && modem.error() == nullptr) {
        modem.fail("UART framing/overflow error; check Grove wiring and factory 9600-baud firmware");
      }
      for (unsigned int count = 0; count < 128; count++) {
        core_util_critical_section_enter();
        const int value = uart.read();
        core_util_critical_section_exit();
        if (value < 0) {
          break;
        }
        modem.input(static_cast<uint8_t>(value), now);
      }
      modem.update(now);
      if (modem.completedTransmissions() != transmittedPackets) {
        transmittedPackets = modem.completedTransmissions();
        lastTransmitMs = now;
        Serial.print("LoRa ");
        Serial.print(name);
        Serial.println(" TX complete; returning to receive mode.");
      }
      const char *command = modem.command();
      if (command != nullptr) {
        core_util_critical_section_enter();
        const bool written = uart.write(command, strlen(command));
        core_util_critical_section_exit();
        if (!written) {
          modem.fail("UART command queue full; command was not sent");
        } else {
          modem.commandSent(now);
          Serial.print("LoRa ");
          Serial.print(name);
          Serial.print(" command: ");
          Serial.print(command);
        }
      }
      if (modem.error() != nullptr && !faultReported) {
        faultReported = true;
        core_util_critical_section_enter();
        uart.discardTransmit();
        core_util_critical_section_exit();
        Serial.print("LoRa ");
        Serial.print(name);
        Serial.print(" error: ");
        Serial.println(modem.error());
        Serial.println("LoRa transmission stopped. Correct the problem and reset the DevKit; modules are not reflashed.");
        char status[17];
        snprintf(status, sizeof(status), "LoRa %s error", name);
        Screen.print(1, status);
      }
    }
  };

  Module &moduleA() {
    // Grove pin 1 is module TX; pin 2 is module RX.
    static Module module(PB_0, PB_14, "A");
    return module;
  }

  Module &moduleB() {
    static Module module(PB_7, PC_6, "B");
    return module;
  }

  Module &module(unsigned int index) {
    return index == 0 ? moduleA() : moduleB();
  }

  enum class Phase {
    Idle,
    Request,
    Reply
  };

  struct RoundTrip {
    Phase phase = Phase::Idle;
    uint32_t id = 0;
    uint32_t startedMs = 0;
    unsigned int initiator = 1;
    uint8_t payload[SerialRadioInput::kMaxPayloadLength] = {};
    uint8_t length = 0;
    uint8_t replyFrame[LoRaFrame::kMaxSize] = {};
    uint8_t replyLength = 0;
    uint32_t initialTxCounts[2] = {};
    bool requestReceived = false;
    bool replyReceived = false;
  };

  constexpr uint32_t kRoundTripTimeoutMs = 12000;
  Ticker uartTicker;
  SerialRadioInput serialInput;
  RoundTrip roundTrip;
  unsigned int nextInitiator = 1;
  uint32_t passedRoundTrips = 0;
  uint32_t failedRoundTrips = 0;
  uint32_t ignoredPackets = 0;
  uint32_t lastStatsMs = 0;
  bool initialized = false;
  bool readinessReported = false;

  void sampleUarts() {
    moduleA().sample();
    moduleB().sample();
  }

  void readConsole() {
    for (unsigned int processed = 0; processed < 32 && Serial.available() > 0; processed++) {
      const int value = Serial.read();
      if (value < 0) {
        Serial.println("LoRa TX error: USB serial read failed.");
        break;
      }
      switch (serialInput.push(static_cast<uint8_t>(value))) {
        case SerialRadioInput::Result::None:
          break;
        case SerialRadioInput::Result::Queued:
          Serial.println("LoRa round trip queued (16-second per-radio duty-cycle pacing).");
          break;
        case SerialRadioInput::Result::Empty:
          Serial.println("LoRa TX skipped: empty line.");
          break;
        case SerialRadioInput::Result::TooLong:
          Serial.println("LoRa TX error: line exceeds 77 bytes; entire line discarded.");
          break;
        case SerialRadioInput::Result::InvalidCharacter:
          Serial.println("LoRa TX error: use printable ASCII or tabs; entire line discarded.");
          break;
        case SerialRadioInput::Result::QueueFull:
          Serial.println("LoRa TX error: four-message queue full; line discarded.");
          break;
      }
    }
  }

  void failRoundTrip(const char *reason) {
    if (roundTrip.phase == Phase::Idle) {
      return;
    }
    char status[144];
    snprintf(status, sizeof(status), "LoRa round trip %lu failed: %s", static_cast<unsigned long>(roundTrip.id), reason);
    Serial.println(status);
    Screen.print(1, "LoRa test failed");
    failedRoundTrips++;
    roundTrip.phase = Phase::Idle;
  }

  bool sendFrame(unsigned int index, const uint8_t *frame, uint8_t length, LoRaFrame::Kind kind, uint32_t now) {
    if (!module(index).canTransmit(now) || !module(index).modem.transmit(frame, length)) {
      failRoundTrip("could not begin transmission");
      return false;
    }
    module(index).lastTransmitMs = now;
    char status[64];
    snprintf(status, sizeof(status), "LoRa %s %s TX #%lu", module(index).name, kind == LoRaFrame::Kind::Request ? "request" : "reply", static_cast<unsigned long>(roundTrip.id));
    Serial.println(status);
    return true;
  }

  void receiveFrame(unsigned int index) {
    uint8_t frame[LoRaE5::kMaxPayloadLength];
    uint8_t length = 0;
    if (!module(index).modem.receive(frame, length)) {
      return;
    }
    LoRaFrame::Message message;
    if (!LoRaFrame::decode(frame, length, message)) {
      ignoredPackets++;
      Serial.println("LoRa RX ignored: invalid or unrelated round-trip frame.");
      return;
    }
    const bool request = roundTrip.phase == Phase::Request && !roundTrip.requestReceived && index != roundTrip.initiator && message.kind == LoRaFrame::Kind::Request;
    const bool reply = roundTrip.phase == Phase::Reply && !roundTrip.replyReceived && index == roundTrip.initiator && message.kind == LoRaFrame::Kind::Reply;
    if (message.id != roundTrip.id || (!request && !reply)) {
      ignoredPackets++;
      Serial.println("LoRa RX ignored: stale, duplicate, or unexpected transaction/direction.");
      return;
    }
    if (message.length != roundTrip.length || memcmp(message.payload, roundTrip.payload, message.length) != 0) {
      failRoundTrip("received bytes differ from the serial message");
      return;
    }
    module(index).receivedPackets++;
    char status[64];
    snprintf(status, sizeof(status), "LoRa %s %s RX #%lu", module(index).name, request ? "request" : "reply", static_cast<unsigned long>(message.id));
    Serial.println(status);
    RadioDisplay::show(message.payload, message.length, index == 0 ? "LoRa A" : "LoRa B");
    if (request) {
      // Echo only bytes actually received over RF, never the locally queued string.
      if (!LoRaFrame::encode(LoRaFrame::Kind::Reply, message.id, message.payload, message.length, roundTrip.replyFrame, roundTrip.replyLength)) {
        failRoundTrip("could not encode the received echo");
        return;
      }
      roundTrip.requestReceived = true;
    } else {
      roundTrip.replyReceived = true;
    }
  }

  void updateRoundTrip(uint32_t now) {
    if (moduleA().modem.error() != nullptr || moduleB().modem.error() != nullptr) {
      failRoundTrip("module error; no automatic retransmission");
      uint8_t discarded[SerialRadioInput::kMaxPayloadLength];
      uint8_t length = 0;
      while (serialInput.read(discarded, length)) {
        Serial.println("LoRa TX error: a module is unavailable; queued line discarded.");
      }
      return;
    }
    if (roundTrip.phase != Phase::Idle) {
      if (now - roundTrip.startedMs >= kRoundTripTimeoutMs) {
        failRoundTrip(roundTrip.requestReceived ? "return echo timed out" : "forward reception timed out");
        return;
      }
      const unsigned int responder = 1 - roundTrip.initiator;
      if (roundTrip.phase == Phase::Request && roundTrip.requestReceived && module(roundTrip.initiator).modem.ready() && module(responder).canTransmit(now)) {
        // Wait for the first sender's RXLRPKT acknowledgement before sending the echo.
        if (sendFrame(responder, roundTrip.replyFrame, roundTrip.replyLength, LoRaFrame::Kind::Reply, now)) {
          roundTrip.phase = Phase::Reply;
          Screen.print(1, "LoRa echo...");
        }
      } else if (roundTrip.phase == Phase::Reply && roundTrip.replyReceived && moduleA().modem.ready() && moduleB().modem.ready() && moduleA().transmittedPackets == roundTrip.initialTxCounts[0] + 1 && moduleB().transmittedPackets == roundTrip.initialTxCounts[1] + 1) {
        char status[80];
        snprintf(status, sizeof(status), "LoRa round trip %lu OK (%s -> %s -> %s): ", static_cast<unsigned long>(roundTrip.id), module(roundTrip.initiator).name, module(responder).name, module(roundTrip.initiator).name);
        Serial.print(status);
        Serial.write(roundTrip.payload, roundTrip.length);
        Serial.println();
        Screen.print(1, "LoRa loopback OK");
        passedRoundTrips++;
        roundTrip.phase = Phase::Idle;
      }
      return;
    }
    if (!moduleA().canTransmit(now) || !moduleB().canTransmit(now)) {
      return;
    }
    if (!serialInput.read(roundTrip.payload, roundTrip.length)) {
      return;
    }
    if (++roundTrip.id == 0) {
      roundTrip.id = 1;
    }
    roundTrip.initiator = nextInitiator;
    nextInitiator = 1 - nextInitiator;
    roundTrip.startedMs = now;
    roundTrip.requestReceived = false;
    roundTrip.replyReceived = false;
    roundTrip.replyLength = 0;
    roundTrip.initialTxCounts[0] = moduleA().transmittedPackets;
    roundTrip.initialTxCounts[1] = moduleB().transmittedPackets;
    roundTrip.phase = Phase::Request;
    uint8_t frame[LoRaFrame::kMaxSize];
    uint8_t frameLength = 0;
    if (!LoRaFrame::encode(LoRaFrame::Kind::Request, roundTrip.id, roundTrip.payload, roundTrip.length, frame, frameLength)) {
      failRoundTrip("could not encode the request");
      return;
    }
    if (sendFrame(roundTrip.initiator, frame, frameLength, LoRaFrame::Kind::Request, now)) {
      Screen.print(1, "LoRa testing...");
    }
  }
}

namespace LoRaBridge {
  void begin() {
    if (!AppConfig::kLoRaEnabled) {
      Serial.println("LoRa inactive in the selected firmware mode.");
      return;
    }
    if (initialized) {
      return;
    }
    const uint32_t now = millis();
    for (unsigned int index = 0; index < 2; index++) {
      module(index).modem.begin(now);
      module(index).lastTransmitMs = now;
    }
    uartTicker.attach_us(&sampleUarts, SoftwareUart::kSamplePeriodUs);
    initialized = true;
    RadioDisplay::begin();
    Screen.print(1, "LoRa starting");
    Serial.println("LoRa mode: ASK RF and audio are inactive. USB console remains at 115200 baud.");
    Serial.println("Grove P0/P14: E5 A; P2/P16: E5 B. Both module UARTs: 9600 8N1.");
    Serial.println("EU868 P2P: 868.1 MHz, SF7/BW125, 10 dBm, CRC on, private sync word.");
    Serial.println("Use attached 868 MHz antennas. Send 1-77 ASCII bytes with CR/LF; no LoRaWAN gateway is used.");
    Serial.println("Each line tests B -> A -> B, then A -> B -> A; success requires the matching received echo.");
  }

  void update() {
    if (!AppConfig::kLoRaEnabled || !initialized) {
      return;
    }
    const uint32_t now = millis();
    moduleA().update(now);
    moduleB().update(now);
    if (!readinessReported && moduleA().modem.ready() && moduleB().modem.ready()) {
      readinessReported = true;
      Serial.println("LoRa ready: both transceivers configured and listening.");
      Screen.print(1, "LoRa 868.1 MHz");
    }
    receiveFrame(0);
    receiveFrame(1);
    readConsole();
    updateRoundTrip(now);
    RadioDisplay::update();
    if (now - lastStatsMs >= 5000) {
      lastStatsMs = now;
      char status[192];
      snprintf(
        status,
        sizeof(status),
        "LoRa stats: A-TX=%lu A-RX=%lu B-TX=%lu B-RX=%lu passed=%lu failed=%lu",
        static_cast<unsigned long>(moduleA().transmittedPackets),
        static_cast<unsigned long>(moduleA().receivedPackets),
        static_cast<unsigned long>(moduleB().transmittedPackets),
        static_cast<unsigned long>(moduleB().receivedPackets),
        static_cast<unsigned long>(passedRoundTrips),
        static_cast<unsigned long>(failedRoundTrips)
      );
      Serial.println(status);
      snprintf(
        status,
        sizeof(status),
        "LoRa link stats: ignored=%lu A-UART-errors=%lu B-UART-errors=%lu A-dropped=%lu B-dropped=%lu",
        static_cast<unsigned long>(ignoredPackets),
        static_cast<unsigned long>(moduleA().uart.errors()),
        static_cast<unsigned long>(moduleB().uart.errors()),
        static_cast<unsigned long>(moduleA().modem.droppedPackets()),
        static_cast<unsigned long>(moduleB().modem.droppedPackets())
      );
      Serial.println(status);
    }
  }
}
