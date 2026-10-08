#include "WiFiProvisioning.h"

#include <Arduino.h>
#include <EEPROMInterface.h>
#include "AppConfig.h"
#include "SensitiveMemory.h"
#include "SerialLineInput.h"

namespace {
  enum class State {
    AwaitConfirmation,
    AwaitSsid,
    AwaitPassword,
    Complete
  };

  SerialLineInput<WIFI_PWD_MAX_LEN, 1> input;
  State state = State::AwaitConfirmation;
  char ssid[WIFI_SSID_MAX_LEN + 1] = {};

  void handleLine(uint8_t *line, uint8_t length) {
    for (size_t index = 0; index < length; ++index) {
      if (line[index] < 32 || line[index] > 126) {
        Serial.println(F("Provisioning accepts printable ASCII characters only; re-enter this field."));
        return;
      }
    }
    line[length] = 0;
    if (state == State::AwaitConfirmation) {
      if (strcmp(reinterpret_cast<char *>(line), "PROVISION") != 0) {
        Serial.println(F("Provisioning not started. Type PROVISION exactly."));
        return;
      }
      state = State::AwaitSsid;
      Serial.println(F("SSID:"));
      return;
    }
    if (state == State::AwaitSsid) {
      if (length > WIFI_SSID_MAX_LEN) {
        Serial.println(F("SSID is too long; maximum is 32 characters. SSID:"));
        return;
      }
      memcpy(ssid, line, length + 1);
      state = State::AwaitPassword;
      Serial.println(F("Password (type - for an open network; input is not echoed by firmware):"));
      return;
    }
    if (state != State::AwaitPassword) {
      return;
    }

    char *password = reinterpret_cast<char *>(line);
    if (length == 1 && password[0] == '-') {
      password[0] = 0;
    }
    EEPROMInterface eeprom;
    const int result = eeprom.saveWiFiSetting(ssid, password);
    SensitiveMemory::clear(ssid, sizeof(ssid));
    if (result != 0) {
      state = State::AwaitConfirmation;
      Screen.print(1, "Wi-Fi save FAIL");
      Serial.println(F("Wi-Fi provisioning failed; settings may be partially updated. Type PROVISION to re-enter both values."));
      return;
    }

    state = State::Complete;
    Screen.print(1, "Wi-Fi saved");
    Screen.print(2, "Reflash mode 5");
    Serial.println(F("Wi-Fi credentials saved and verified in STSAFE EEPROM."));
    Serial.println(F("They survive normal application reflashing. Reflash mode 5 to test."));
  }
}

namespace WiFiProvisioning {
  void begin() {
    if (!AppConfig::kWiFiProvisioningEnabled) {
      return;
    }
    input.reset();
    state = State::AwaitConfirmation;
    SensitiveMemory::clear(ssid, sizeof(ssid));
    Screen.print(1, "Wi-Fi provision");
    Screen.print(2, "Use USB serial");
    Serial.println(F("Wi-Fi provisioning mode. No credentials are compiled into this firmware."));
    Serial.println(F("Type PROVISION to begin, then send the SSID and password as separate lines."));
  }

  void update() {
    if (!AppConfig::kWiFiProvisioningEnabled) {
      return;
    }
    while (state != State::Complete && Serial.available() > 0) {
      const int value = Serial.read();
      if (value < 0) {
        Serial.println(F("Provisioning USB serial read failed."));
        return;
      }
      const SerialLineInput<WIFI_PWD_MAX_LEN, 1>::Result result = input.push(static_cast<uint8_t>(value));
      if (result == SerialLineInput<WIFI_PWD_MAX_LEN, 1>::Result::Queued) {
        uint8_t payload[WIFI_PWD_MAX_LEN] = {};
        uint8_t length = 0;
        if (!input.read(payload, length)) {
          Serial.println(F("Provisioning input queue error."));
          continue;
        }
        uint8_t line[WIFI_PWD_MAX_LEN + 1] = {};
        memcpy(line, payload, length);
        SensitiveMemory::clear(payload, sizeof(payload));
        handleLine(line, length);
        SensitiveMemory::clear(line, sizeof(line));
      } else if (result == SerialLineInput<WIFI_PWD_MAX_LEN, 1>::Result::TooLong) {
        Serial.println(F("Provisioning line is too long; maximum is 64 characters."));
      } else if (result == SerialLineInput<WIFI_PWD_MAX_LEN, 1>::Result::InvalidCharacter) {
        Serial.println(F("Provisioning accepts printable ASCII characters only."));
      } else if (result == SerialLineInput<WIFI_PWD_MAX_LEN, 1>::Result::QueueFull) {
        Serial.println(F("Provisioning input queue is full; re-enter this field."));
      }
    }
  }
}
