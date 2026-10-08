#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace HostKeyBlock {
  constexpr size_t kKeyBytes = 32;
  constexpr size_t kLoaderBytes = 88;

  inline bool valid(const uint8_t *keys) {
    if (keys == nullptr || memcmp(keys, keys + 16, 16) == 0) {
      return false;
    }
    for (size_t half = 0; half < 2; ++half) {
      uint8_t bits = 0;
      uint8_t inverted = 0;
      for (size_t index = 0; index < 16; ++index) {
        bits |= keys[half * 16 + index];
        inverted |= static_cast<uint8_t>(~keys[half * 16 + index]);
      }
      if (bits == 0 || inverted == 0) {
        return false;
      }
    }
    return true;
  }

  inline void put16(uint8_t *bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
  }

  inline uint16_t get16(const uint8_t *bytes) {
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
  }

  inline void moveImmediate(uint8_t *instruction, uint16_t base, uint8_t reg, uint16_t value) {
    put16(instruction, static_cast<uint16_t>(base | ((value >> 12) & 15) | ((value & 0x0800) >> 1)));
    put16(instruction + 2, static_cast<uint16_t>(((value & 0x0700) << 4) | (reg << 8) | (value & 255)));
  }

  inline void encode(const uint8_t *keys, uint8_t *block) {
    // Input: MAC then cipher. Core 2.0.0 getters: cipher at 0x08008001, MAC at 0x0800802D.
    for (size_t half = 0; half < 2; ++half) {
      uint8_t *code = block + half * 44;
      put16(code, 0xB4F0); // push {r4-r7}
      for (size_t word = 0; word < 4; ++word) {
        const uint8_t *key = keys + (1 - half) * 16 + word * 4;
        moveImmediate(code + 2 + word * 8, 0xF240, static_cast<uint8_t>(4 + word), get16(key));
        moveImmediate(code + 6 + word * 8, 0xF2C0, static_cast<uint8_t>(4 + word), get16(key + 2));
      }
      put16(code + 34, 0xE880);
      put16(code + 36, 0x00F0); // stm.w r0, {r4-r7}
      put16(code + 38, 0xBCF0);
      put16(code + 40, 0x4770);
      put16(code + 42, 0);
    }
  }
}
