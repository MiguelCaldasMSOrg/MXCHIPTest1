#include "WiFiTests.h"

#include <Arduino.h>
#include <AZ3166WiFi.h>
#include <SystemWiFi.h>
#include "AppConfig.h"

namespace {
  constexpr unsigned long kReportIntervalMs = 5000;
  unsigned long lastReportMs = 0;

  void formatIp(char *buffer, size_t capacity, const IPAddress &address) {
    snprintf(buffer, capacity, "%u.%u.%u.%u", address[0], address[1], address[2], address[3]);
  }

  void diagnoseFailedConnection() {
    const char *savedSsid = SystemWiFiSSID();
    Serial.print(F("Wi-Fi diagnostic: scanning for saved SSID \""));
    Serial.print(savedSsid);
    Serial.println(F("\"."));
    Screen.print(2, "Scanning SSID...");
    const int networkCount = WiFi.scanNetworks();
    if (networkCount <= 0) {
      Screen.print(2, "Scan found none");
      Serial.println(F("Wi-Fi diagnostic failed: no networks were returned by the scan."));
      return;
    }

    for (int index = 0; index < networkCount; ++index) {
      if (strcmp(WiFi.SSID(static_cast<unsigned char>(index)), savedSsid) == 0) {
        const int rssi = WiFi.RSSI(static_cast<unsigned char>(index));
        char status[32];
        snprintf(status, sizeof(status), "Visible %d dBm", rssi);
        Screen.print(2, status);
        Serial.print(F("Wi-Fi diagnostic: saved SSID is visible at "));
        Serial.print(rssi);
        Serial.println(F(" dBm; verify the saved password and WPA2 compatibility."));
        return;
      }
    }

    Screen.print(2, "SSID not visible");
    Serial.println(F("Wi-Fi diagnostic: saved SSID was not visible in the scan."));
  }
}

namespace WiFiTests {
  bool begin() {
    if (!AppConfig::kWiFiEnabled) {
      return false;
    }
    Screen.print(1, "Wi-Fi connecting");
    Serial.println(F("Wi-Fi test: connecting with credentials saved on the board."));
    if (WiFi.begin() != WL_CONNECTED) {
      Screen.print(1, "Wi-Fi failed");
      Serial.println(F("Wi-Fi test failed: saved-network connection was not established."));
      diagnoseFailedConnection();
      lastReportMs = millis();
      return false;
    }

    Screen.print(1, "Wi-Fi connected");
    report();
    return true;
  }

  void report() {
    if (!AppConfig::kWiFiEnabled) {
      return;
    }
    lastReportMs = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Screen.print(1, "Wi-Fi offline");
      Screen.print(2, "");
      Screen.print(3, "");
      Serial.println(F("Wi-Fi status: not connected."));
      return;
    }

    Screen.print(1, "Wi-Fi connected");
    const IPAddress ip = WiFi.localIP();
    const int rssi = WiFi.RSSI();
    char ipText[17];
    char status[32];
    formatIp(ipText, sizeof(ipText), ip);
    snprintf(status, sizeof(status), "RSSI %d dBm", rssi);
    Screen.print(2, ipText);
    Screen.print(3, status);
    Serial.print(F("Wi-Fi connected: SSID=\""));
    Serial.print(WiFi.SSID());
    Serial.print(F("\", IP="));
    Serial.print(ipText);
    Serial.print(F(", RSSI="));
    Serial.print(rssi);
    Serial.println(F(" dBm."));
  }

  void update() {
    if (AppConfig::kWiFiEnabled && millis() - lastReportMs >= kReportIntervalMs) {
      report();
    }
  }
}
