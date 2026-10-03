#pragma once

#include <stddef.h>
#include <stdint.h>

namespace VirtualWireProtocol {
  constexpr size_t kMaxPayloadLength = 77;
  constexpr size_t kFrameOverhead = 3;
  constexpr size_t kFrameCapacity = kMaxPayloadLength + kFrameOverhead;
  constexpr unsigned int kBitRate = 2000;
  constexpr unsigned int kSamplePeriodUs = 50;
  constexpr unsigned int kSamplesPerBit = 1000000 / (kBitRate * kSamplePeriodUs);
  constexpr unsigned int kBitsPerSymbol = 6;
  constexpr unsigned int kBitsPerEncodedByte = 2 * kBitsPerSymbol;
  constexpr size_t kTrainingSymbols = 6;
  constexpr size_t kHeaderSymbols = kTrainingSymbols + 2;
  constexpr uint8_t kTrainingSymbol = 0x2A;
  constexpr uint16_t kStartSymbol = 0xB38;
  constexpr uint16_t kCrcResidue = 0xF0B8;
  constexpr uint32_t kSyncWindow = (static_cast<uint32_t>(kStartSymbol) << 12) | 0xAAA;
  const uint8_t kSymbols[] = {
    0x0D, 0x0E, 0x13, 0x15, 0x16, 0x19, 0x1A, 0x1C,
    0x23, 0x25, 0x26, 0x29, 0x2A, 0x2C, 0x32, 0x34
  };
  static_assert(kFrameCapacity <= 255, "Frame length must fit its one-byte count.");
  static_assert(kBitRate * kSamplePeriodUs * kSamplesPerBit == 1000000, "Radio sampling must divide the bit interval exactly.");
  static_assert(kSamplesPerBit >= 4 && kSamplesPerBit % 2 == 0, "The receiver needs an even oversampling factor.");

  inline uint16_t updateCrc(uint16_t crc, uint8_t value) {
    crc ^= value;
    for (unsigned int bit = 0; bit < 8; bit++) {
      crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1) ? 0x8408 : 0));
    }
    return crc;
  }
}
