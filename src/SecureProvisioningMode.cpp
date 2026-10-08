#include <Arduino.h>
#include "AppConfig.h"
#include "SecureProvisioningMode.h"
#include "SensitiveMemory.h"
#include "SuppliedKeySetup.h"

namespace {
  constexpr uint32_t kConfirmationMs = 60000;
  __attribute__((section(".rodata.mxchip_supplied_keys"))) const char kMarker[] = "MXCHIP_SUPPLIED_KEYS_MODE15_V1";
  char line[128] = {};
  size_t used = 0;
  bool invalid = false;
  bool skipLf = false;
  bool confirmed = false;
  uint32_t confirmedMs = 0;

  void reportError(const char *code) {
    Serial.print(F("SK2 ERR "));
    Serial.println(code);
    Screen.print(1, "Key setup error");
    Screen.print(2, "See host tool");
  }

  void hex(const uint8_t *bytes, size_t size, char *text) {
    const char digits[] = "0123456789ABCDEF";
    for (size_t index = 0; index < size; ++index) {
      text[index * 2] = digits[bytes[index] >> 4];
      text[index * 2 + 1] = digits[bytes[index] & 15];
    }
    text[size * 2] = 0;
  }

  int nibble(char value) {
    if (value >= '0' && value <= '9') {
      return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
      return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
      return value - 'a' + 10;
    }
    return -1;
  }

  bool unhex(const char *text, uint8_t *bytes, size_t size) {
    if (strlen(text) != size * 2) {
      return false;
    }
    for (size_t index = 0; index < size; ++index) {
      const int high = nibble(text[index * 2]);
      const int low = nibble(text[index * 2 + 1]);
      if (high < 0 || low < 0) {
        return false;
      }
      bytes[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
  }

  void printStatus() {
    SuppliedKeySetup::Status value;
    if (!SuppliedKeySetup::status(value)) {
      reportError(SuppliedKeySetup::lastError());
      return;
    }
    char uid[25];
    char reply[80];
    hex(value.uid, sizeof(value.uid), uid);
    snprintf(reply, sizeof(reply), "SK2 STATUS %s %u %u %u %u %u", uid, value.rdp, value.pcrop ? 1U : 0U, value.hostKeysPresent ? 1U : 0U, value.envelopeKeyPresent ? 1U : 0U, value.hostFlashEmpty ? 1U : 0U);
    Serial.println(reply);
  }

  void process() {
    if (strcmp(line, "SK2 STATUS") == 0) {
      printStatus();
      return;
    }
    if (strcmp(line, "SK2 ABORT") == 0) {
      confirmed = false;
      Serial.println(F("SK2 ABORTED"));
      return;
    }
    if (strncmp(line, "SK2 SET ", 8) != 0) {
      confirmed = false;
      reportError("UNSUPPORTED_COMMAND");
      return;
    }
    const bool authorized = confirmed && static_cast<uint32_t>(millis()) - confirmedMs < kConfirmationMs;
    confirmed = false;
    if (!authorized) {
      reportError("PHYSICAL_CONFIRMATION_REQUIRED");
      return;
    }
    uint8_t uid[12] = {};
    uint8_t keys[HostKeyBlock::kKeyBytes] = {};
    char *separator = strchr(line + 8, ' ');
    bool valid = separator != nullptr;
    if (valid) {
      *separator = 0;
      valid = unhex(line + 8, uid, sizeof(uid)) && unhex(separator + 1, keys, sizeof(keys));
    }
    SensitiveMemory::clear(line, sizeof(line));
    if (!valid) {
      reportError("UID_OR_KEY_FORMAT");
    } else {
      Screen.print(1, "Installing keys");
      Serial.println(F("SK2 WORKING"));
      if (SuppliedKeySetup::install(uid, keys)) {
        Screen.print(1, "Supplied keys OK");
        Screen.print(2, "Data unchanged");
        Serial.println(F("SK2 INSTALLED"));
      } else {
        reportError(SuppliedKeySetup::lastError());
        Serial.println(F("Setup may be incomplete. No rollback or automatic retry is available."));
      }
    }
    SensitiveMemory::clear(keys, sizeof(keys));
  }
}

namespace SecureProvisioningMode {
  void begin() {
    if (!AppConfig::kSecureProvisioningEnabled) {
      return;
    }
    confirmed = false;
    SensitiveMemory::clear(line, sizeof(line));
    used = 0;
    invalid = false;
    skipLf = false;
    Screen.print(0, "Supplied keys");
    Screen.print(1, "Use host tool");
    Screen.print(2, "A:arm B:cancel");
    Screen.print(3, "No auto writes");
    Serial.println(kMarker);
    Serial.println(F("Supplied host keys only. Stored application data is not read, converted or initialized. No random host keys, journal, backup, or lock automation."));
    printStatus();
  }

  void authorize() {
    if (AppConfig::kSecureProvisioningEnabled) {
      confirmed = true;
      confirmedMs = millis();
      Screen.print(1, "Armed 60 seconds");
      Serial.println(F("SK2 PHYSICAL_READY"));
    }
  }

  void abort() {
    if (AppConfig::kSecureProvisioningEnabled) {
      confirmed = false;
      SensitiveMemory::clear(line, sizeof(line));
      used = 0;
      invalid = false;
      skipLf = false;
      Screen.print(1, "Cancelled");
      Serial.println(F("SK2 ABORTED"));
    }
  }

  void update() {
    if (!AppConfig::kSecureProvisioningEnabled) {
      return;
    }
    while (Serial.available() > 0) {
      const int value = Serial.read();
      if (value < 0) {
        abort();
        reportError("SERIAL_READ");
        break;
      }
      if (value == '\n' && skipLf) {
        skipLf = false;
        continue;
      }
      skipLf = false;
      if (value == '\r' || value == '\n') {
        skipLf = value == '\r';
        if (invalid) {
          confirmed = false;
          reportError("INVALID_INPUT");
        } else if (used != 0) {
          line[used] = 0;
          process();
        }
        SensitiveMemory::clear(line, sizeof(line));
        used = 0;
        invalid = false;
      } else if (!invalid) {
        if (value < 32 || value > 126 || used == sizeof(line) - 1) {
          invalid = true;
        } else {
          line[used++] = static_cast<char>(value);
        }
      }
    }
  }
}
