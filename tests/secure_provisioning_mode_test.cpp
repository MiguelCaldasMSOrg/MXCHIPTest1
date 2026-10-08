#include "stubs/DiagnosticHardware.h"
#include "AppConfig.h"
#include "SecureProvisioningMode.h"
#include "SuppliedKeySetup.h"

namespace {
  unsigned int installs = 0;
  bool failInstall = false;
  void send(const std::string &text) {
    Serial.input.insert(Serial.input.end(), text.begin(), text.end());
    SecureProvisioningMode::update();
  }
}

namespace SuppliedKeySetup {
  bool status(Status &value) {
    value = {};
    memset(value.uid, 0x12, sizeof(value.uid));
    value.hostFlashEmpty = installs == 0;
    value.hostKeysPresent = installs != 0;
    value.envelopeKeyPresent = installs != 0;
    return true;
  }
  bool install(const uint8_t *uid, const uint8_t *keys) {
    check(uid[0] == 0x12 && HostKeyBlock::valid(keys), "only validated supplied keys reach setup");
    ++installs;
    return !failInstall;
  }
  const char *lastError() {
    return "FIXTURE_ERROR";
  }
}

int main() {
  SecureProvisioningMode::begin();
  if (!AppConfig::kSecureProvisioningEnabled) {
    send("SK2 STATUS\n");
    SecureProvisioningMode::authorize();
    SecureProvisioningMode::abort();
    check(installs == 0 && Serial.output.empty(), "inactive mode does nothing");
    std::cout << "PASS: supplied-key mode isolation\n";
    return 0;
  }
  check(Serial.output.find("MXCHIP_SUPPLIED_KEYS_MODE15_V1") != std::string::npos && installs == 0, "startup only reports status");
  const std::string actualUid = "121212121212121212121212";
  const std::string keys = "0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20";
  const std::string command = "SK2 SET " + actualUid + " " + keys + "\r\n";
  send(command);
  check(installs == 0 && Serial.output.find("PHYSICAL_CONFIRMATION_REQUIRED") != std::string::npos, "confirmation before a key write");
  send("a\n");
  send(command);
  check(installs == 0, "serial input cannot emulate the physical button");
  SecureProvisioningMode::authorize();
  FakeHardware::nowUs += 60000000;
  send(command);
  check(installs == 0, "confirmation expiry");
  SecureProvisioningMode::authorize();
  send("SK2 SET " + actualUid + " XYZ\n");
  check(installs == 0, "invalid key format");
  SecureProvisioningMode::authorize();
  send(command);
  check(installs == 1 && Serial.output.find("SK2 INSTALLED") != std::string::npos, "one explicitly confirmed supplied-key operation");
  send(command);
  check(installs == 1, "one confirmation cannot authorize a second operation");
  check(Serial.output.find(keys) == std::string::npos, "keys never echoed");
  for (const char *removed: {"SP1 BEGIN3", "SP1 COMMIT", "SK2 BEGIN3", "SK2 RESUME", "SK2 RESTORE", "SK2 PREFIX", "SK2 GET", "SK2 BACKUP", "SK2 ARM", "SK2 CANCEL"}) {
    SecureProvisioningMode::authorize();
    send(std::string(removed) + "\n");
    check(installs == 1, "removed commands have no implementation");
  }
  send(std::string(200, 'X') + "\nSK2 STATUS\r\n");
  check(Serial.output.find("INVALID_INPUT") != std::string::npos, "bounded parser");
  SecureProvisioningMode::authorize();
  SecureProvisioningMode::abort();
  send(command);
  check(installs == 1, "abort only clears transient authorization");
  failInstall = true;
  SecureProvisioningMode::authorize();
  send(command);
  check(installs == 2 && Serial.output.find("SK2 ERR FIXTURE_ERROR") != std::string::npos, "hardware failure is reported without retry");
  check(!Screen.invalidWrite, "OLED text bounds");
  std::cout << "PASS: supplied-key-only protocol, confirmation, input limits, removed-command rejection and no secret echo\n";
}
