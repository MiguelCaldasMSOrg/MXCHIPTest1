#pragma once

#include <stdint.h>

namespace RadioDisplay {
  void begin();
  void show(const uint8_t *payload, uint8_t length, const char *source);
  void update();
}
