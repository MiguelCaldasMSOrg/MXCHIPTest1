#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr int WL_CONNECTED = 3;
constexpr int WL_DISCONNECTED = 6;

struct IPAddress {
  uint8_t octets[4];
  uint8_t operator[](size_t index) const {
    return octets[index];
  }
};

class TestWiFi {
  public:
  int begin();
  int status();
  int scanNetworks();
  IPAddress localIP();
  const char *SSID();
  const char *SSID(unsigned char index);
  int RSSI();
  int RSSI(unsigned char index);
};

extern TestWiFi WiFi;
