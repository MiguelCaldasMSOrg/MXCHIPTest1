#pragma once

#include "NfcProtocol.h"

namespace NfcUart {
  void wake();
  NfcProtocol::Link &link();
}
