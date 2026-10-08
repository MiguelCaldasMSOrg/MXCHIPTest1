#pragma once

#include "HostKeyBlock.h"

namespace SuppliedKeySetup {
  struct Status {
    uint8_t uid[12] = {};
    unsigned int rdp = 0;
    bool pcrop = false;
    bool hostKeysPresent = false;
    bool envelopeKeyPresent = false;
    bool hostFlashEmpty = false;
  };

  bool status(Status &value);
  bool install(const uint8_t *uid, const uint8_t *keys);
  const char *lastError();
}
