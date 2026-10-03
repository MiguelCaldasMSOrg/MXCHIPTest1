#pragma once

#include "VirtualWireProtocol.h"

class VirtualWireDecoder {
  public:
  static constexpr size_t kMaxPayloadLength = VirtualWireProtocol::kMaxPayloadLength;
  static constexpr unsigned int kBitRate = VirtualWireProtocol::kBitRate;
  static constexpr unsigned int kSamplePeriodUs = VirtualWireProtocol::kSamplePeriodUs;

  struct Statistics {
    uint32_t received;
    uint32_t rejected;
    uint32_t dropped;
    uint32_t transitions;
  };

  VirtualWireDecoder() {
    reset(false);
  }

  void reset(bool initialLevel) {
    history = initialLevel ? 7 : 0;
    lastRaw = initialLevel;
    filteredLevel = initialLevel;
    phase = 0;
    syncWindow = 0;
    receiving = false;
    encodedWord = 0;
    encodedBits = 0;
    frameLength = 0;
    frameUsed = 0;
    crc = 0xFFFF;
    pendingLength = 0;
    pending = false;
    received = rejected = dropped = transitions = 0;
  }

  void sample(bool level) {
    if (level != lastRaw) {
      transitions++;
      lastRaw = level;
    }
    history = static_cast<uint8_t>(((history << 1) | (level ? 1 : 0)) & 7);
    const bool filtered = ((history & 1) + ((history >> 1) & 1) + ((history >> 2) & 1)) >= 2;
    if (filtered != filteredLevel) {
      filteredLevel = filtered;
      phase = 0;
    } else {
      phase++;
      if (phase == kSamplesPerBit) {
        phase = 0;
      }
    }
    if (phase == kSamplesPerBit / 2) {
      acceptBit(filteredLevel);
    }
  }

  bool available() const {
    return pending;
  }

  // The caller masks the sampling interrupt while reading the pending message or statistics.
  bool read(uint8_t (&payload)[kMaxPayloadLength], uint8_t &length) {
    if (!pending) {
      return false;
    }
    length = pendingLength;
    for (size_t i = 0; i < length; i++) {
      payload[i] = pendingPayload[i];
    }
    pending = false;
    return true;
  }

  Statistics statistics() const {
    return {received, rejected, dropped, transitions};
  }

  private:
  static constexpr unsigned int kSamplesPerBit = VirtualWireProtocol::kSamplesPerBit;
  static constexpr size_t kFrameCapacity = VirtualWireProtocol::kFrameCapacity;

  uint8_t history;
  bool lastRaw;
  bool filteredLevel;
  unsigned int phase;
  uint32_t syncWindow;
  bool receiving;
  uint16_t encodedWord;
  uint8_t encodedBits;
  uint8_t frameLength;
  uint8_t frameUsed;
  uint16_t crc;
  uint8_t frame[kFrameCapacity];
  volatile uint8_t pendingPayload[kMaxPayloadLength];
  volatile uint8_t pendingLength;
  volatile bool pending;
  volatile uint32_t received;
  volatile uint32_t rejected;
  volatile uint32_t dropped;
  volatile uint32_t transitions;

  static int decodeSymbol(uint8_t symbol) {
    for (size_t i = 0; i < sizeof(VirtualWireProtocol::kSymbols); i++) {
      if (VirtualWireProtocol::kSymbols[i] == symbol) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  void rejectFrame() {
    rejected++;
    receiving = false;
  }

  void acceptBit(bool bit) {
    syncWindow = (syncWindow >> 1) | (bit ? 0x800000UL : 0);
    if (!receiving) {
      // Require the end of the alternating preamble followed by the 0xB38 start word.
      if (syncWindow == VirtualWireProtocol::kSyncWindow) {
        receiving = true;
        encodedWord = 0;
        encodedBits = 0;
        frameUsed = 0;
        frameLength = 0;
        crc = 0xFFFF;
      }
      return;
    }

    if (bit) {
      encodedWord |= static_cast<uint16_t>(1U << encodedBits);
    }
    encodedBits++;
    if (encodedBits != VirtualWireProtocol::kBitsPerEncodedByte) {
      return;
    }
    const int high = decodeSymbol(encodedWord & 0x3F);
    const int low = decodeSymbol((encodedWord >> VirtualWireProtocol::kBitsPerSymbol) & 0x3F);
    encodedBits = 0;
    encodedWord = 0;
    if (high < 0 || low < 0) {
      rejectFrame();
      return;
    }
    acceptByte(static_cast<uint8_t>((high << 4) | low));
  }

  void acceptByte(uint8_t value) {
    if (frameUsed == 0) {
      if (value <= VirtualWireProtocol::kFrameOverhead || value > kFrameCapacity) {
        rejectFrame();
        return;
      }
      frameLength = value;
    }
    frame[frameUsed++] = value;
    crc = VirtualWireProtocol::updateCrc(crc, value);
    if (frameUsed != frameLength) {
      return;
    }

    receiving = false;
    if (crc != VirtualWireProtocol::kCrcResidue) {
      rejected++;
      return;
    }
    received++;
    if (pending) {
      dropped++;
      return;
    }
    pendingLength = static_cast<uint8_t>(frameLength - VirtualWireProtocol::kFrameOverhead);
    for (size_t i = 0; i < pendingLength; i++) {
      pendingPayload[i] = frame[i + 1];
    }
    pending = true;
  }
};
