#pragma once

#include "NetworkHardware.h"

bool InitSystemWiFi();
NetworkInterface *WiFiInterface();
const char *SystemWiFiSSID();
