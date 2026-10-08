#pragma once

#include <stddef.h>
#include <stdint.h>

namespace SensitiveMemory {
  inline void clear(void *data, size_t size) {
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(data);
    while (size-- != 0) {
      *bytes++ = 0;
    }
  }
}
