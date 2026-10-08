#pragma once

#include "NetworkHardware.h"

class EMW10xxInterface: public NetworkInterface {
  public:
  int set_interface(int mode);
  int connect(const char *ssid, const char *password, int security, int channel);
};
