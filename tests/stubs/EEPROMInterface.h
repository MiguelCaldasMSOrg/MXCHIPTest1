#pragma once

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PWD_MAX_LEN 64
#define STSAFE_ZONE_0_IDX 0

class EEPROMInterface {
  public:
  int readWiFiSetting(char *ssid, int ssidSize, char *password, int passwordSize);
  int saveWiFiSetting(char *ssid, char *password);
};
