#pragma once

#include <stdio.h>
#include <string.h>
#include "LoRaFrame.h"

// Factory LoRa-E5 AT firmware, direct LoRa TEST mode (not LoRaWAN).
class LoRaE5 {
  public:
  static constexpr uint32_t kBootDelayMs = 2000;
  static constexpr uint32_t kCommandTimeoutMs = 3000;
  static constexpr uint32_t kTransmitTimeoutMs = 5000;
  // SF7/BW125, 12 preamble symbols, CRC and <=85 on-air bytes: <160 ms airtime.
  static constexpr uint32_t kTransmitIntervalMs = 16000;
  static constexpr size_t kMaxPayloadLength = LoRaFrame::kMaxSize;

  void begin(uint32_t now) {
    state = State::Boot;
    sinceMs = now;
    waiting = false;
    commandPending = false;
    inputLength = 0;
    receivedLength = 0;
    declaredLength = -1;
    failure[0] = '\0';
    completed = 0;
    dropped = 0;
  }

  void update(uint32_t now) {
    if (state == State::Boot && now - sinceMs >= kBootDelayMs) {
      queue(State::Probe, "AT\r\n");
    } else if (waiting && now - sinceMs >= (state == State::Sending ? kTransmitTimeoutMs : kCommandTimeoutMs)) {
      fail(state == State::Sending ? "TX DONE timeout; delivery is unknown; no automatic retry" : "AT response timeout; check power, Grove wiring and 9600-baud factory firmware");
    }
  }

  const char *command() const {
    return commandPending ? outgoing : nullptr;
  }

  void commandSent(uint32_t now) {
    commandPending = false;
    waiting = true;
    sinceMs = now;
  }

  bool ready() const {
    return state == State::Ready;
  }

  bool transmitting() const {
    return state == State::StopForTransmit || state == State::Sending;
  }

  const char *error() const {
    return failure[0] == '\0' ? nullptr : failure;
  }

  void fail(const char *message) {
    if (state != State::Fault) {
      snprintf(failure, sizeof(failure), "%s", message);
    }
    state = State::Fault;
    commandPending = false;
    waiting = false;
  }

  void input(uint8_t value, uint32_t now) {
    if (state == State::Fault) {
      return;
    }
    if (value == '\r' || value == '\n') {
      if (inputLength != 0) {
        incoming[inputLength] = '\0';
        inputLength = 0;
        acceptLine(now);
      }
      return;
    }
    if (value == ' ' || value == '\t') {
      return;
    }
    if (value < 32 || value > 126) {
      fail("Non-ASCII AT response; check module UART baud rate");
    } else if (inputLength == sizeof(incoming) - 1) {
      fail("AT response exceeds the bounded line buffer");
    } else {
      incoming[inputLength++] = static_cast<char>(value);
    }
  }

  bool transmit(const uint8_t *payload, size_t length) {
    if (!ready() || payload == nullptr || length == 0 || length > kMaxPayloadLength) {
      return false;
    }
    static const char digits[] = "0123456789ABCDEF";
    strcpy(transmitCommand, "AT+TEST=TXLRPKT,\"");
    size_t position = strlen(transmitCommand);
    for (size_t i = 0; i < length; i++) {
      transmitCommand[position++] = digits[payload[i] >> 4];
      transmitCommand[position++] = digits[payload[i] & 15];
    }
    strcpy(transmitCommand + position, "\"\r\n");
    queue(State::StopForTransmit, "AT+TEST=STOP\r\n");
    return true;
  }

  bool receive(uint8_t (&payload)[kMaxPayloadLength], uint8_t &length) {
    if (receivedLength == 0) {
      return false;
    }
    length = receivedLength;
    memcpy(payload, receivedPayload, length);
    receivedLength = 0;
    return true;
  }

  uint32_t completedTransmissions() const {
    return completed;
  }

  uint32_t droppedPackets() const {
    return dropped;
  }

  private:
  enum class State {
    Boot,
    Probe,
    TestMode,
    Stop,
    Configure,
    Listen,
    Ready,
    StopForTransmit,
    Sending,
    Fault
  };

  State state = State::Boot;
  bool waiting = false;
  bool commandPending = false;
  uint32_t sinceMs = 0;
  char outgoing[192] = {};
  char transmitCommand[192] = {};
  char incoming[256] = {};
  size_t inputLength = 0;
  char failure[128] = {};
  uint8_t receivedPayload[kMaxPayloadLength] = {};
  uint8_t receivedLength = 0;
  int declaredLength = -1;
  uint32_t completed = 0;
  uint32_t dropped = 0;
  static_assert(kMaxPayloadLength * 2 + sizeof("AT+TEST=TXLRPKT,\"\"\r\n") <= sizeof(transmitCommand), "Framed LoRa payload must fit the AT command.");

  void queue(State next, const char *text) {
    state = next;
    waiting = false;
    snprintf(outgoing, sizeof(outgoing), "%s", text);
    commandPending = true;
  }

  bool validConfiguration() const {
    // Validate returned settings, not just the RFCFG command echo.
    const char *expected = "+TEST:RFCFGF:868100000,SF7,BW125K,TXPR:12,RXPR:15,POW:10dBm,CRC:ON,IQ:OFF,";
    if (strncmp(incoming, expected, strlen(expected)) != 0) {
      return false;
    }
    const char *network = incoming + strlen(expected);
    return strcmp(network, "NET:OFF") == 0 || strcmp(network, "PNET:OFF") == 0;
  }

  static int hexDigit(char value) {
    if (value >= '0' && value <= '9') {
      return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
      return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
      return value - 'a' + 10;
    }
    return -1;
  }

  void acceptPacket() {
    const char *hex = incoming + strlen("+TEST:RX");
    size_t size = strlen(hex);
    if (size >= 2 && hex[0] == '"' && hex[size - 1] == '"') {
      hex++;
      size -= 2;
    }
    if (size == 0 || size % 2 != 0 || size / 2 > kMaxPayloadLength || (declaredLength >= 0 && size / 2 != static_cast<size_t>(declaredLength))) {
      fail("Invalid RX hexadecimal payload or inconsistent reported length");
      return;
    }
    uint8_t payload[kMaxPayloadLength];
    for (size_t i = 0; i < size / 2; i++) {
      const int high = hexDigit(hex[i * 2]);
      const int low = hexDigit(hex[i * 2 + 1]);
      if (high < 0 || low < 0) {
        fail("RX payload contains invalid hexadecimal digits");
        return;
      }
      payload[i] = static_cast<uint8_t>((high << 4) | low);
    }
    declaredLength = -1;
    if (receivedLength != 0) {
      dropped++;
      return;
    }
    receivedLength = static_cast<uint8_t>(size / 2);
    memcpy(receivedPayload, payload, receivedLength);
  }

  void acceptLine(uint32_t now) {
    if (strstr(incoming, "ERROR") != nullptr || strstr(incoming, "FATAL") != nullptr || strcmp(incoming, "+INFO:Inputtimeout") == 0) {
      fail(incoming);
      return;
    }
    if (ready()) {
      if (strncmp(incoming, "+TEST:LEN:", 10) == 0) {
        unsigned int length = 0;
        const char *digit = incoming + 10;
        while (*digit >= '0' && *digit <= '9') {
          length = length * 10 + static_cast<unsigned int>(*digit++ - '0');
          if (length > kMaxPayloadLength) {
            fail("RX length is outside the supported 1-85 byte range");
            return;
          }
        }
        if (length == 0 || (*digit != '\0' && *digit != ',')) {
          fail("Invalid RX length metadata");
          return;
        }
        declaredLength = static_cast<int>(length);
        return;
      }
      if (strncmp(incoming, "+TEST:RX", 8) == 0 && strcmp(incoming, "+TEST:RXLRPKT") != 0) {
        acceptPacket();
        return;
      }
    }
    if (!waiting) {
      return;
    }
    if (state == State::Probe && strcmp(incoming, "+AT:OK") == 0) {
      queue(State::TestMode, "AT+MODE=TEST\r\n");
    } else if (state == State::TestMode && strcmp(incoming, "+MODE:TEST") == 0) {
      queue(State::Stop, "AT+TEST=STOP\r\n");
    } else if (state == State::Stop && strcmp(incoming, "+TEST:STOP") == 0) {
      queue(State::Configure, "AT+TEST=RFCFG,868.1,SF7,125,12,15,10,ON,OFF,OFF\r\n");
    } else if (state == State::Configure && strncmp(incoming, "+TEST:RFCFG", 11) == 0) {
      if (!validConfiguration()) {
        fail("Radio settings do not match 868.1 MHz/SF7/BW125/CRC/private-network configuration");
      } else {
        queue(State::Listen, "AT+TEST=RXLRPKT\r\n");
      }
    } else if (state == State::Listen && strcmp(incoming, "+TEST:RXLRPKT") == 0) {
      state = State::Ready;
      waiting = false;
      declaredLength = -1;
    } else if (state == State::StopForTransmit && strcmp(incoming, "+TEST:STOP") == 0) {
      queue(State::Sending, transmitCommand);
    } else if (state == State::Sending && strcmp(incoming, "+TEST:TXDONE") == 0) {
      completed++;
      sinceMs = now;
      queue(State::Listen, "AT+TEST=RXLRPKT\r\n");
    }
  }
};
