#include "stubs/DiagnosticHardware.h"
#include <EEPROMInterface.h>
#include <algorithm>
#include "AppConfig.h"
#include "SerialLineInput.h"
#include "WiFiProvisioning.h"

namespace {
  int saves = 0;
  int saveResult = 0;
  std::string savedSsid;
  std::string savedPassword;

  void send(const std::string &text) {
    Serial.input.insert(Serial.input.end(), text.begin(), text.end());
    WiFiProvisioning::update();
  }

  void begin() {
    saves = saveResult = 0;
    savedSsid.clear();
    savedPassword.clear();
    Serial.output.clear();
    Serial.input.clear();
    WiFiProvisioning::begin();
  }

  void parserClearing() {
    SerialLineInput<64, 2> input;
    const std::string secret = "Public-fixture-secret-for-buffer-clearing";
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&input);
    auto retained = [&]() {
      return std::search(bytes, bytes + sizeof(input), secret.begin(), secret.end()) != bytes + sizeof(input);
    };
    for (uint8_t value: secret) {
      input.push(value);
    }
    check(input.push('\r') == decltype(input)::Result::Queued && retained(), "queued input remains available until consumed");
    uint8_t payload[64] = {};
    uint8_t length = 0;
    check(input.read(payload, length) && length == secret.size() && memcmp(payload, secret.data(), length) == 0, "line contents unchanged by clearing");
    check(!retained(), "both staging and consumed queue slot are cleared");
    check(input.push('\n') == decltype(input)::Result::None, "clearing preserves CRLF handling");
    for (uint8_t value: secret) {
      input.push(value);
    }
    input.push(0);
    check(input.push('\n') == decltype(input)::Result::InvalidCharacter && !retained(), "rejected input is cleared");
    for (uint8_t value: secret) {
      input.push(value);
    }
    input.push('\n');
    for (uint8_t value: secret) {
      input.push(value);
    }
    input.reset();
    check(!retained() && !input.read(payload, length), "reset clears pending and partial input");
    SensitiveMemory::clear(payload, sizeof(payload));
  }
}

int EEPROMInterface::saveWiFiSetting(char *ssid, char *password) {
  ++saves;
  savedSsid = ssid;
  savedPassword = password;
  return saveResult;
}

int main() {
  parserClearing();
  begin();
  if (!AppConfig::kWiFiProvisioningEnabled) {
    send("PROVISION\nFixture\nPublic-password\n");
    check(saves == 0 && Serial.output.empty(), "inactive credential mode does nothing");
    std::cout << "PASS: credential mode isolation and serial-buffer clearing\n";
    return 0;
  }
  send("wrong\n");
  check(saves == 0, "explicit confirmation required");
  send("PROVISION\r\n" + std::string(33, 's') + "\n");
  check(Serial.output.find("SSID is too long") != std::string::npos && saves == 0, "33-character SSID rejected");
  send(std::string(32, 's') + "\r\n" + std::string(65, 'p') + "\n");
  check(saves == 0 && Serial.output.find("line is too long") != std::string::npos, "65-character password rejected");
  send(std::string(64, 'p') + "\r\n");
  check(saves == 1 && savedSsid.size() == 32 && savedPassword.size() == 64, "exact 32/64-character SDK boundaries");
  check(Serial.output.find(savedPassword) == std::string::npos, "password not echoed");
  send("PROVISION\nAgain\nAnother-password\n");
  check(saves == 1, "completed session cannot write again");

  begin();
  send("PROVISION\nBad\tSSID\nFixture\nBad\tpassword\n");
  check(saves == 0 && Serial.output.find("printable ASCII") != std::string::npos, "tabs rejected without advancing the current field");
  send("-\n");
  check(saves == 1 && savedSsid == "Fixture" && savedPassword.empty(), "explicit open-network password");

  begin();
  saveResult = -1;
  send("PROVISION\nFixture\nPublic-only-test-password\n");
  check(saves == 1 && Serial.output.find("partially updated") != std::string::npos && Serial.output.find(savedPassword) == std::string::npos, "failed save reports uncertainty, not atomicity or secrets");
  saveResult = 0;
  send("PROVISION\nReplacement\nSecond-public-password\n");
  check(saves == 2 && savedSsid == "Replacement", "failure requires both credentials again");

  begin();
  Serial.failRead = true;
  send("PROVISION\n");
  check(saves == 0 && Serial.output.find("serial read failed") != std::string::npos, "serial failure is explicit");
  Serial.failRead = false;
  check(!Screen.invalidWrite, "all provisioning status text fits the OLED");
  std::cout << "PASS: credential limits, ASCII validation, no secret echo, explicit open network, partial failure and serial-buffer clearing\n";
}
