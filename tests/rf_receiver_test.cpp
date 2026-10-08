#include "AppConfig.h"
#include "rf_receiver.h"
#include "rf_transmitter.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

static uint16_t frameCheck(const std::vector<uint8_t> &bytes) {
  uint16_t crc = 0xFFFF;
  for (uint8_t byte: bytes) {
    for (unsigned int bit = 0; bit < 8; bit++) {
      const bool feedback = ((crc ^ (byte >> bit)) & 1) != 0;
      crc >>= 1;
      if (feedback) {
        crc ^= 0x8408;
      }
    }
  }
  return static_cast<uint16_t>(~crc);
}

static std::vector<uint8_t> packet(const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> bytes;
  bytes.push_back(static_cast<uint8_t>(payload.size() + 3));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  const uint16_t crc = frameCheck(bytes);
  bytes.push_back(static_cast<uint8_t>(crc));
  bytes.push_back(static_cast<uint8_t>(crc >> 8));
  return bytes;
}

static void appendBits(std::vector<uint8_t> &bits, uint16_t value, unsigned int count) {
  for (unsigned int bit = 0; bit < count; bit++) {
    bits.push_back((value >> bit) & 1);
  }
}

static std::vector<uint8_t> encode(const std::vector<uint8_t> &bytes) {
  const uint8_t symbols[] = {0x0D, 0x0E, 0x13, 0x15, 0x16, 0x19, 0x1A, 0x1C, 0x23, 0x25, 0x26, 0x29, 0x2A, 0x2C, 0x32, 0x34};
  std::vector<uint8_t> bits;
  for (unsigned int i = 0; i < 36; i++) {
    bits.push_back(i & 1);
  }
  appendBits(bits, 0xB38, 12);
  for (uint8_t byte: bytes) {
    appendBits(bits, symbols[byte >> 4], 6);
    appendBits(bits, symbols[byte & 15], 6);
  }
  return bits;
}

static void feed(VirtualWireDecoder &decoder, const std::vector<uint8_t> &bits, int samplesPerBit = 10, bool jitter = false, bool glitches = false, int initialSamples = 30) {
  for (int i = 0; i < initialSamples; i++) {
    decoder.sample(false);
  }
  for (size_t i = 0; i < bits.size(); i++) {
    const int count = samplesPerBit + (jitter ? static_cast<int>(i % 3) - 1 : 0);
    for (int j = 0; j < count; j++) {
      bool value = bits[i] != 0;
      if (glitches && i % 11 == 5 && j == 5) {
        value = !value;
      }
      decoder.sample(value);
    }
  }
  for (int i = 0; i < 60; i++) {
    decoder.sample(false);
  }
}

static std::vector<uint8_t> read(VirtualWireDecoder &decoder) {
  uint8_t data[VirtualWireDecoder::kMaxPayloadLength];
  uint8_t length = 0;
  check(decoder.read(data, length), "expected a received message");
  return std::vector<uint8_t>(data, data + length);
}

static void expectNoMessage(VirtualWireDecoder &decoder) {
  uint8_t data[VirtualWireDecoder::kMaxPayloadLength];
  uint8_t length = 0;
  check(!decoder.read(data, length), "invalid frame must not produce a message");
}

static SerialRadioInput::Result serialLine(SerialRadioInput &input, const std::string &text) {
  SerialRadioInput::Result result = SerialRadioInput::Result::None;
  for (unsigned char byte: text) {
    const SerialRadioInput::Result current = input.push(byte);
    if (current != SerialRadioInput::Result::None) {
      result = current;
    }
  }
  return result;
}

static std::string readSerialLine(SerialRadioInput &input) {
  uint8_t payload[VirtualWireProtocol::kMaxPayloadLength];
  uint8_t length = 0;
  check(input.read(payload, length), "expected a queued serial line");
  return std::string(payload, payload + length);
}

static void testTransmitter() {
  for (size_t length = 1; length <= VirtualWireProtocol::kMaxPayloadLength; length++) {
    std::vector<uint8_t> payload(length);
    for (size_t i = 0; i < length; i++) {
      payload[i] = static_cast<uint8_t>((i * 71 + length * 13) & 0xFF);
    }
    const std::vector<uint8_t> expected = encode(packet(payload));
    VirtualWirePacket encoded;
    check(encoded.encode(payload.data(), payload.size()), "encode a valid outgoing packet");
    check(encoded.symbolCount() * 6 == expected.size(), "exact encoded packet size");
    VirtualWireTransmitter transmitter;
    check(!transmitter.active() && !transmitter.sample(), "transmitter starts idle and low");
    check(transmitter.start(encoded), "start transmitter");
    check(!transmitter.start(encoded), "a busy transmitter must not be overwritten");
    const uint8_t replacement = 'Z';
    check(encoded.encode(&replacement, 1), "the caller can reuse its packet without altering active transmission");

    VirtualWireDecoder decoder;
    size_t samples = 0;
    while (transmitter.active()) {
      check(samples <= expected.size() * 10, "transmission must terminate at the specified length");
      const bool level = transmitter.sample();
      const bool expectedLevel = samples < expected.size() * 10 ? expected[samples / 10] != 0 : false;
      check(level == expectedLevel, "each transmitted bit matches the independent reference for exactly 500 us");
      decoder.sample(level);
      samples++;
    }
    check(samples == expected.size() * 10 + 1, "exact airtime followed by low idle");
    check(!transmitter.sample(), "transmitter remains low after completion");
    check(read(decoder) == payload, "transmitted waveform decodes to the original payload");
    check(decoder.statistics().received == 1 && decoder.statistics().rejected == 0, "one valid received frame per send");
    check(transmitter.start(encoded), "transmitter can start again after completing a frame");
  }
  {
    VirtualWirePacket encoded;
    VirtualWireTransmitter transmitter;
    const uint8_t byte = 'X';
    check(!transmitter.start(encoded), "uninitialized frame must be rejected");
    check(!encoded.encode(&byte, 0), "empty TX payload must be rejected");
    check(!encoded.encode(nullptr, 1), "null TX payload must be rejected");
    const std::vector<uint8_t> oversized(78, 'X');
    check(!encoded.encode(oversized.data(), oversized.size()), "overlength TX payload must be rejected");
    check(!transmitter.start(encoded), "failed encoding must not be transmitted");
    check(encoded.encode(&byte, 1), "encoding recovers after invalid payloads");
    check(transmitter.start(encoded), "valid frame can start after failed encodes");
  }
  std::cout << "PASS: TX encoding, bit ordering, precise duration, idle state, busy protection, and RX loopback for all 77 lengths\n";
}

static void testSerialInput() {
  using Result = SerialRadioInput::Result;
  {
    SerialRadioInput input;
    uint8_t payload[VirtualWireProtocol::kMaxPayloadLength];
    uint8_t length = 0;
    check(serialLine(input, "hello") == Result::None, "partial serial lines wait for a terminator");
    check(!input.read(payload, length), "partial serial lines are not queued");
    check(input.push('\r') == Result::Queued, "CR terminates one packet");
    check(input.push('\n') == Result::None, "LF after CR must not create another packet");
    check(readSerialLine(input) == "hello", "line terminators are not included in the payload");
    check(serialLine(input, "two\n") == Result::Queued, "LF line ending");
    check(serialLine(input, "three\r\n") == Result::Queued, "CRLF line ending");
    check(serialLine(input, "  four\t  \r") == Result::Queued, "spaces and tabs are preserved");
    check(readSerialLine(input) == "two" && readSerialLine(input) == "three" && readSerialLine(input) == "  four\t  ", "serial line order and whitespace");
    check(input.push('\n') == Result::None, "CRLF suppression survives queue consumption");
    check(input.push('\n') == Result::Empty, "blank lines are reported explicitly");
    check(!input.read(payload, length), "blank lines do not produce transmissions");
  }
  {
    SerialRadioInput input;
    const std::string longest(77, 'Z');
    check(serialLine(input, longest + "\n") == Result::Queued, "exactly 77 bytes accepted");
    check(readSerialLine(input) == longest, "maximum serial line is not truncated");
    check(serialLine(input, std::string(1000, 'X') + "\r\n") == Result::TooLong, "overlong line discarded as a whole");
    check(serialLine(input, std::string("bad\0tail", 8) + "\n") == Result::InvalidCharacter, "embedded NUL rejected");
    check(serialLine(input, std::string(1, static_cast<char>(0xFF)) + "\n") == Result::InvalidCharacter, "non-ASCII byte rejected");
    check(serialLine(input, "valid\n") == Result::Queued, "parser recovers after rejected lines");
    check(readSerialLine(input) == "valid", "rejected lines do not leak prefixes or suffixes");
    uint8_t payload[VirtualWireProtocol::kMaxPayloadLength];
    uint8_t length = 0;
    check(!input.read(payload, length), "rejected data did not enter the queue");
  }
  {
    SerialRadioInput input;
    for (unsigned int i = 0; i < 4; i++) {
      check(serialLine(input, std::to_string(i) + "\n") == Result::Queued, "four pending lines accepted");
    }
    check(serialLine(input, "overflow\n") == Result::QueueFull, "full queue returns an explicit error");
    check(readSerialLine(input) == "0" && readSerialLine(input) == "1", "overflow does not overwrite pending lines");
    check(serialLine(input, "4\n") == Result::Queued && serialLine(input, "5\n") == Result::Queued, "ring queue reuses released slots");
    for (unsigned int i = 2; i < 6; i++) {
      check(readSerialLine(input) == std::to_string(i), "FIFO ordering survives queue wraparound");
    }
    uint8_t payload[VirtualWireProtocol::kMaxPayloadLength];
    uint8_t length = 0;
    check(!input.read(payload, length), "overflowed line was discarded rather than delayed");
  }
  std::cout << "PASS: serial LF/CR/CRLF, partial lines, payload limits, invalid bytes, queue overflow, and recovery\n";
}

int main() {
  check(
    static_cast<int>(AppConfig::kAudioEnabled) + AppConfig::kRadioTransmitEnabled + AppConfig::kLoRaEnabled + AppConfig::kRtcEnabled + AppConfig::kWiFiEnabled + AppConfig::kGroveOledEnabled + AppConfig::kGroveEInkEnabled + AppConfig::kWiFiProvisioningEnabled + AppConfig::kSensorsEnabled +
        AppConfig::kMicrophoneEnabled + AppConfig::kFileSystemEnabled + AppConfig::kNetworkServicesEnabled + AppConfig::kIrdaEnabled + AppConfig::kSecurityChipEnabled + AppConfig::kSecureProvisioningEnabled ==
      1,
    "exactly one firmware mode must be selected"
  );
  check(AppConfig::kRadioEnabled == (AppConfig::kAudioEnabled || AppConfig::kRadioTransmitEnabled), "ASK reception must be active only in ASK and audio modes");
  std::cout << "PASS: firmware mode exclusion\n";
  const std::string crcText = "123456789";
  check(frameCheck(std::vector<uint8_t>(crcText.begin(), crcText.end())) == 0x906E, "CRC-16/X-25 reference check");
  const std::vector<uint8_t> hello = {'h', 'e', 'l', 'l', 'o'};
  const std::vector<uint8_t> helloBits = encode(packet(hello));

  for (int offset = 0; offset < 10; offset++) {
    VirtualWireDecoder decoder;
    feed(decoder, helloBits, 10, false, false, offset);
    check(read(decoder) == hello, "nominal decode at every sampling phase");
    expectNoMessage(decoder);
  }
  std::cout << "PASS: reference CRC, hello, and all sampling phases\n";

  for (int speed: {9, 11}) {
    VirtualWireDecoder decoder;
    feed(decoder, helloBits, speed);
    check(read(decoder) == hello, "decode with transmitter clock variation");
  }
  {
    VirtualWireDecoder decoder;
    feed(decoder, helloBits, 10, true, true);
    check(read(decoder) == hello, "decode with edge jitter and isolated sample glitches");
  }
  std::cout << "PASS: clock variation, jitter, and isolated glitches\n";

  {
    std::vector<uint8_t> payload(VirtualWireDecoder::kMaxPayloadLength);
    for (size_t i = 0; i < payload.size(); i++) {
      payload[i] = static_cast<uint8_t>(32 + i);
    }
    VirtualWireDecoder decoder;
    feed(decoder, encode(packet(payload)));
    check(read(decoder) == payload, "maximum 77-byte payload");
    decoder.reset(false);
    feed(decoder, encode(packet({'X'})));
    check(read(decoder) == std::vector<uint8_t>({'X'}), "minimum one-byte payload");
  }
  std::cout << "PASS: minimum and maximum payload sizes\n";

  {
    uint32_t random = 0xA53C19E7;
    for (size_t length = 1; length <= VirtualWireDecoder::kMaxPayloadLength; length++) {
      std::vector<uint8_t> payload(length);
      for (uint8_t &byte: payload) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        byte = static_cast<uint8_t>(random);
      }
      const std::vector<uint8_t> bits = encode(packet(payload));
      for (int speed: {9, 10, 11}) {
        VirtualWireDecoder decoder;
        feed(decoder, bits, speed, speed == 10, speed == 10, static_cast<int>(length % 10));
        check(read(decoder) == payload, "all payload lengths and byte values at varied timing");
      }
    }
  }
  std::cout << "PASS: all 77 payload lengths with varied bytes, timing, and noise\n";

  {
    VirtualWireDecoder decoder;
    std::vector<uint8_t> corrupt = packet(hello);
    corrupt.back() ^= 1;
    feed(decoder, encode(corrupt));
    expectNoMessage(decoder);
    check(decoder.statistics().rejected == 1, "CRC failure is counted");
    feed(decoder, helloBits);
    check(read(decoder) == hello, "recovery after bad CRC");
  }
  for (uint8_t invalidLength: {0, 3, 81, 255}) {
    VirtualWireDecoder decoder;
    feed(decoder, encode({invalidLength}));
    expectNoMessage(decoder);
    check(decoder.statistics().rejected != 0, "invalid length is counted");
    feed(decoder, helloBits);
    check(read(decoder) == hello, "recovery after invalid length");
  }
  {
    VirtualWireDecoder decoder;
    std::vector<uint8_t> invalid = helloBits;
    for (size_t i = 48; i < 54; i++) {
      invalid[i] = 1;
    }
    feed(decoder, invalid);
    expectNoMessage(decoder);
    feed(decoder, helloBits);
    check(read(decoder) == hello, "recovery after invalid 6-bit symbol");
  }
  {
    VirtualWireDecoder decoder;
    std::vector<uint8_t> truncated = helloBits;
    truncated.resize(truncated.size() - 24);
    feed(decoder, truncated);
    expectNoMessage(decoder);
    feed(decoder, helloBits);
    check(read(decoder) == hello, "recovery after a truncated frame");
  }
  std::cout << "PASS: invalid CRC, length, symbol, and truncated-frame rejection/recovery\n";

  {
    VirtualWireDecoder decoder;
    feed(decoder, helloBits);
    feed(decoder, encode(packet({'n', 'e', 'w'})));
    check(decoder.statistics().received == 2 && decoder.statistics().dropped == 1, "pending-message overflow is counted");
    check(read(decoder) == hello, "unread message is not overwritten");
    feed(decoder, encode(packet({'n', 'e', 'w'})));
    check(read(decoder) == std::vector<uint8_t>({'n', 'e', 'w'}), "reception resumes after consuming the pending message");
  }
  {
    VirtualWireDecoder decoder;
    uint32_t random = 0x12345678;
    for (unsigned int i = 0; i < 50000; i++) {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      decoder.sample((random & 1) != 0);
    }
    expectNoMessage(decoder);
    feed(decoder, helloBits);
    check(read(decoder) == hello, "recovery after idle RF noise");
  }
  std::cout << "PASS: queue protection and noisy-idle recovery\n";
  testTransmitter();
  testSerialInput();
}
