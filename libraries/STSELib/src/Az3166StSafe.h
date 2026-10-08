#pragma once

#include "stselib.h"

namespace Az3166StSafe {
  constexpr size_t kTransportCapacity = 512;
  stse_ReturnCode_t begin(stse_Handler_t &handler);
  stse_ReturnCode_t cryptoSelfTest();
  void clear(void *buffer, size_t size);
  uint16_t lastCommand();
}
