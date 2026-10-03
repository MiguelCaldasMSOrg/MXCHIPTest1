#pragma once

#include <string.h>
#include "VirtualWireProtocol.h"

class VirtualWirePacket {
  public:
  static constexpr size_t kCapacity = VirtualWireProtocol::kHeaderSymbols + VirtualWireProtocol::kFrameCapacity * 2;

  size_t symbolCount() const {
    return usedSymbols;
  }

  bool encode(const uint8_t *payload, size_t length) {
    usedSymbols = 0;
    if (payload == nullptr || length == 0 || length > VirtualWireProtocol::kMaxPayloadLength) {
      return false;
    }
    for (size_t i = 0; i < VirtualWireProtocol::kTrainingSymbols; i++) {
      symbols[usedSymbols++] = VirtualWireProtocol::kTrainingSymbol;
    }
    symbols[usedSymbols++] = VirtualWireProtocol::kStartSymbol & 0x3F;
    symbols[usedSymbols++] = VirtualWireProtocol::kStartSymbol >> VirtualWireProtocol::kBitsPerSymbol;

    const uint8_t count = static_cast<uint8_t>(length + VirtualWireProtocol::kFrameOverhead);
    uint16_t crc = VirtualWireProtocol::updateCrc(0xFFFF, count);
    appendByte(count);
    for (size_t i = 0; i < length; i++) {
      appendByte(payload[i]);
      crc = VirtualWireProtocol::updateCrc(crc, payload[i]);
    }
    crc = static_cast<uint16_t>(~crc);
    appendByte(static_cast<uint8_t>(crc));
    appendByte(static_cast<uint8_t>(crc >> 8));
    return true;
  }

  private:
  friend class VirtualWireTransmitter;
  uint8_t symbols[kCapacity] = {};
  size_t usedSymbols = 0;

  void appendByte(uint8_t value) {
    symbols[usedSymbols++] = VirtualWireProtocol::kSymbols[value >> 4];
    symbols[usedSymbols++] = VirtualWireProtocol::kSymbols[value & 15];
  }
};

class VirtualWireTransmitter {
  public:
  // Mask the radio interrupt for start(); encode the packet outside that critical section.
  bool start(const VirtualWirePacket &next) {
    if (transmitting || next.usedSymbols == 0) {
      return false;
    }
    packet = next;
    symbol = 0;
    bit = 0;
    samples = 0;
    transmitting = true;
    return true;
  }

  bool active() const {
    return transmitting;
  }

  bool sample() {
    if (!transmitting) {
      return false;
    }
    if (symbol == packet.usedSymbols) {
      transmitting = false;
      return false;
    }

    const bool level = (packet.symbols[symbol] & (1U << bit)) != 0;
    samples++;
    if (samples == VirtualWireProtocol::kSamplesPerBit) {
      samples = 0;
      bit++;
      if (bit == VirtualWireProtocol::kBitsPerSymbol) {
        bit = 0;
        symbol++;
      }
    }
    return level;
  }

  private:
  VirtualWirePacket packet;
  size_t symbol = 0;
  unsigned int bit = 0;
  unsigned int samples = 0;
  volatile bool transmitting = false;
};

class SerialRadioInput {
  public:
  static constexpr size_t kQueueDepth = 4;

  enum class Result {
    None,
    Queued,
    Empty,
    TooLong,
    InvalidCharacter,
    QueueFull
  };

  Result push(uint8_t value) {
    if (value == '\n' && skipLf) {
      skipLf = false;
      return Result::None;
    }
    skipLf = false;
    if (value == '\r' || value == '\n') {
      skipLf = value == '\r';
      Result result = error;
      if (result == Result::None) {
        if (used == 0) {
          result = Result::Empty;
        } else if (count == kQueueDepth) {
          result = Result::QueueFull;
        } else {
          const size_t tail = (head + count) % kQueueDepth;
          memcpy(messages[tail], line, used);
          lengths[tail] = static_cast<uint8_t>(used);
          count++;
          result = Result::Queued;
        }
      }
      used = 0;
      error = Result::None;
      return result;
    }
    if (error != Result::None) {
      return Result::None;
    }
    if (value != '\t' && (value < 32 || value > 126)) {
      error = Result::InvalidCharacter;
    } else if (used == VirtualWireProtocol::kMaxPayloadLength) {
      error = Result::TooLong;
    } else {
      line[used++] = value;
    }
    return Result::None;
  }

  bool read(uint8_t (&payload)[VirtualWireProtocol::kMaxPayloadLength], uint8_t &length) {
    if (count == 0) {
      return false;
    }
    length = lengths[head];
    memcpy(payload, messages[head], length);
    head = (head + 1) % kQueueDepth;
    count--;
    return true;
  }

  private:
  uint8_t line[VirtualWireProtocol::kMaxPayloadLength] = {};
  size_t used = 0;
  bool skipLf = false;
  Result error = Result::None;
  uint8_t messages[kQueueDepth][VirtualWireProtocol::kMaxPayloadLength] = {};
  uint8_t lengths[kQueueDepth] = {};
  size_t head = 0;
  size_t count = 0;
};
