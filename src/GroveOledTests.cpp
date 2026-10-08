#include "GroveOledTests.h"

#include <Arduino.h>
#include <mico.h>
#include "AppConfig.h"

namespace {
  constexpr uint8_t kAddress = 0x3C;
  constexpr uint8_t kWidth = 128;
  constexpr uint8_t kPages = 16;
  constexpr uint8_t kColumnOffset = 0;
  constexpr uint8_t kChunkSize = 16;
  constexpr unsigned long kPatternIntervalMs = 3000;
  constexpr uint8_t kPatternCount = 4;

  mico_i2c_device_t device = {Arduino_I2C, kAddress, I2C_ADDRESS_WIDTH_7BIT, I2C_STANDARD_SPEED_MODE};
  bool connected = false;
  uint8_t pattern = 0;
  unsigned long lastPatternMs = 0;

  bool writeBuffer(uint8_t *buffer, size_t length) {
    mico_i2c_message_t message;
    return MicoI2cBuildTxMessage(&message, buffer, static_cast<uint16_t>(length), 3) == kNoErr && MicoI2cTransfer(&device, &message, 1) == kNoErr;
  }

  bool writeCommands(const uint8_t *commands, size_t length) {
    uint8_t buffer[24];
    if (length + 1 > sizeof(buffer)) {
      return false;
    }
    buffer[0] = 0x00;
    memcpy(buffer + 1, commands, length);
    return writeBuffer(buffer, length + 1);
  }

  uint8_t pixelPattern(uint8_t selected, uint8_t page, uint8_t column) {
    switch (selected) {
      case 0:
        return ((page + column / 8) & 1) == 0 ? 0xFF : 0x00;
      case 1:
        return (column & 8) == 0 ? 0xFF : 0x00;
      case 2:
        return (page & 1) == 0 ? 0xFF : 0x00;
      default:
        return page == 0 || page == kPages - 1 || column < 8 || column >= kWidth - 8 ? 0xFF : 0x00;
    }
  }

  bool drawPattern(uint8_t selected) {
    uint8_t data[kChunkSize + 1];
    data[0] = 0x40;
    for (uint8_t page = 0; page < kPages; ++page) {
      for (uint8_t column = 0; column < kWidth; column += kChunkSize) {
        const uint8_t controllerColumn = static_cast<uint8_t>((kColumnOffset + column) & 0x7F);
        const uint8_t commands[] = {static_cast<uint8_t>(0x10 | (controllerColumn >> 4)), static_cast<uint8_t>(controllerColumn & 0x0F), static_cast<uint8_t>(0xB0 | page)};
        if (!writeCommands(commands, sizeof(commands))) {
          return false;
        }
        for (uint8_t index = 0; index < kChunkSize; ++index) {
          data[index + 1] = pixelPattern(selected, page, static_cast<uint8_t>(column + index));
        }
        if (!writeBuffer(data, sizeof(data))) {
          return false;
        }
      }
    }
    return true;
  }

  const char *patternName(uint8_t selected) {
    static const char *const names[] = {"checkerboard", "vertical bars", "horizontal bars", "border"};
    return names[selected];
  }
}

namespace GroveOledTests {
  bool begin() {
    if (!AppConfig::kGroveOledEnabled) {
      return false;
    }
    pattern = 0;
    connected = MicoI2cInitialize(&device) == kNoErr && MicoI2cProbeDevice(&device, 3);
    if (!connected) {
      Screen.print(1, "OLED not found");
      Serial.println(F("Grove OLED test failed: no SH1107 response at I2C address 0x3C."));
      return false;
    }

    const uint8_t initialization[] = {0xAE, 0xDC, 0x00, 0x81, 0x2F, 0x20, 0xA0, 0xC0, 0xA8, 0x7F, 0xD5, 0x50, 0xD9, 0x22, 0xDB, 0x35, 0xB0, 0xDA, 0x12, 0xA4, 0xA6, 0xAF};
    if (!writeCommands(initialization, sizeof(initialization))) {
      connected = false;
      Screen.print(1, "OLED init failed");
      Serial.println(F("Grove OLED test failed while sending the SH1107 initialization sequence."));
      return false;
    }

    Screen.print(1, "Grove OLED found");
    Serial.println(F("Grove OLED detected: SH1107 at I2C address 0x3C."));
    nextPattern();
    return connected;
  }

  void nextPattern() {
    if (!AppConfig::kGroveOledEnabled || !connected) {
      return;
    }
    if (!drawPattern(pattern)) {
      connected = false;
      Screen.print(1, "OLED write FAIL");
      Serial.println(F("Grove OLED test failed while writing display data."));
      return;
    }

    char status[32];
    snprintf(status, sizeof(status), "Pattern: %s", patternName(pattern));
    Screen.print(2, status, true);
    Serial.print(F("Grove OLED pattern: "));
    Serial.println(patternName(pattern));
    pattern = static_cast<uint8_t>((pattern + 1) % kPatternCount);
    lastPatternMs = millis();
  }

  void update() {
    if (AppConfig::kGroveOledEnabled && connected && millis() - lastPatternMs >= kPatternIntervalMs) {
      nextPattern();
    }
  }
}
