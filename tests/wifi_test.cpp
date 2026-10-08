#include "stubs/DiagnosticHardware.h"
#include <AZ3166WiFi.h>
#include "AppConfig.h"
#include "WiFiTests.h"

namespace {
  int state = WL_CONNECTED;
  int starts = 0;
  int queries = 0;
  int scans = 0;
  int networkCount = 1;
}
TestWiFi WiFi;
int TestWiFi::begin() {
  ++starts;
  return state;
}
int TestWiFi::status() {
  ++queries;
  return state;
}
int TestWiFi::scanNetworks() {
  ++scans;
  return networkCount;
}
IPAddress TestWiFi::localIP() {
  return {{192, 0, 2, 17}};
}
const char *TestWiFi::SSID() {
  return "Fixture network";
}
const char *TestWiFi::SSID(unsigned char) {
  return "Fixture network";
}
int TestWiFi::RSSI() {
  return -70;
}
int TestWiFi::RSSI(unsigned char) {
  return -71;
}
const char *SystemWiFiSSID() {
  return "Fixture network";
}

int main() {
  const bool connected = WiFiTests::begin();
  if (!AppConfig::kWiFiEnabled) {
    WiFiTests::report();
    FakeHardware::nowUs = 10000000;
    WiFiTests::update();
    check(!connected && starts == 0 && queries == 0 && scans == 0 && Serial.output.empty(), "inactive Wi-Fi mode has no effects");
    std::cout << "PASS: Wi-Fi mode isolation\n";
    return 0;
  }
  check(connected && starts == 1 && Screen.lines[2] == "192.0.2.17", "saved-network connection and IPv4 display");
  const int before = queries;
  FakeHardware::nowUs = 4999000;
  WiFiTests::update();
  check(queries == before, "five-second reporting interval");
  state = WL_DISCONNECTED;
  FakeHardware::nowUs = 5000000;
  WiFiTests::update();
  check(queries == before + 1 && Screen.lines[1] == "Wi-Fi offline" && Screen.lines[2].empty() && Screen.lines[3].empty(), "disconnect is detected and stale IP/RSSI removed");
  const size_t disconnectedOutput = Serial.output.size();
  WiFiTests::update();
  check(Serial.output.size() == disconnectedOutput, "offline polling does not flood serial");
  state = WL_CONNECTED;
  FakeHardware::nowUs = 10000000;
  WiFiTests::update();
  check(Screen.lines[1] == "Wi-Fi connected" && Screen.lines[2] == "192.0.2.17" && starts == 1, "status recovers without forcing reassociation");
  state = WL_DISCONNECTED;
  check(!WiFiTests::begin() && scans == 1 && Serial.output.find("is visible at") != std::string::npos, "failed connection scans the saved SSID");
  networkCount = 0;
  check(!WiFiTests::begin() && scans == 2 && Serial.output.find("no networks") != std::string::npos, "empty scan reported explicitly");
  check(!Screen.invalidWrite, "Wi-Fi status fits the OLED");
  std::cout << "PASS: live Wi-Fi state, disconnect/reconnect reporting, exact polling interval and failed-connection diagnostics\n";
}
