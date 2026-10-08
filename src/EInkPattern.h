#pragma once

#include <stddef.h>
#include <stdint.h>

namespace EInkPattern {
  constexpr size_t kWidth = 152;
  constexpr size_t kHeight = 152;
  constexpr size_t kBytesPerRow = kWidth / 8;
  constexpr size_t kPlaneBytes = kBytesPerRow * kHeight;

  enum class Plane {
    Black,
    Red
  };

  inline uint8_t byteAt(Plane plane, size_t index) {
    if (index >= kPlaneBytes) {
      return 0xFF;
    }
    const size_t row = index / kBytesPerRow;
    if (plane == Plane::Black && row < kHeight / 3) {
      return 0x00;
    }
    if (plane == Plane::Red && row >= (kHeight * 2) / 3) {
      return 0x00;
    }
    return 0xFF;
  }
}
