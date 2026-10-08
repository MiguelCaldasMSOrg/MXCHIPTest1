#include "stubs/DiagnosticHardware.h"
#include <mico.h>
#include "AppConfig.h"
#include "GroveOledTests.h"

namespace {
  unsigned int initializations = 0;
  unsigned int transfers = 0;
  unsigned int failTransfer = 0;
  bool present = true;
  uint8_t pixels[16][128] = {};
  uint8_t page = 0;
  uint8_t column = 0;

  void reset() {
    initializations = transfers = failTransfer = 0;
    present = true;
    Serial.output.clear();
  }

  void checkPattern(unsigned int selected) {
    for (unsigned int row = 0; row < 16; ++row) {
      for (unsigned int col = 0; col < 128; ++col) {
        bool lit = false;
        switch (selected) {
          case 0:
            lit = (row + col / 8) % 2 == 0;
            break;
          case 1:
            lit = col / 8 % 2 == 0;
            break;
          case 2:
            lit = row % 2 == 0;
            break;
          case 3:
            lit = row == 0 || row == 15 || col < 8 || col >= 120;
            break;
        }
        check(pixels[row][col] == (lit ? 255 : 0), "all 2048 OLED bytes match the intended pattern and zero offset");
      }
    }
  }
}

OSStatus MicoI2cInitialize(mico_i2c_device_t *device) {
  check(device->address == 0x3C, "SH1107 address retained");
  ++initializations;
  return kNoErr;
}
bool MicoI2cProbeDevice(mico_i2c_device_t *, int) {
  return present;
}
OSStatus MicoI2cBuildTxMessage(mico_i2c_message_t *message, const void *buffer, uint16_t length, uint16_t retries) {
  *message = {buffer, nullptr, length, 0, retries, false};
  return kNoErr;
}
OSStatus MicoI2cTransfer(mico_i2c_device_t *, mico_i2c_message_t *message, uint16_t count) {
  check(count == 1, "one bounded OLED transaction");
  if (++transfers == failTransfer) {
    return -1;
  }
  const uint8_t *data = static_cast<const uint8_t *>(message->tx_buffer);
  if (data[0] == 0 && message->tx_length > 4) {
    const uint8_t initialization[] = {0xAE, 0xDC, 0x00, 0x81, 0x2F, 0x20, 0xA0, 0xC0, 0xA8, 0x7F, 0xD5, 0x50, 0xD9, 0x22, 0xDB, 0x35, 0xB0, 0xDA, 0x12, 0xA4, 0xA6, 0xAF};
    check(message->tx_length == sizeof(initialization) + 1 && memcmp(data + 1, initialization, sizeof(initialization)) == 0, "original initialization sequence retained");
  } else if (data[0] == 0 && message->tx_length == 4) {
    column = static_cast<uint8_t>(((data[1] & 15) << 4) | (data[2] & 15));
    page = data[3] & 15;
  } else {
    check(data[0] == 0x40 && message->tx_length == 17 && column <= 112, "16-byte bounded pixel transfer");
    memcpy(pixels[page] + column, data + 1, 16);
  }
  return kNoErr;
}

int main() {
  const bool started = GroveOledTests::begin();
  if (!AppConfig::kGroveOledEnabled) {
    GroveOledTests::nextPattern();
    FakeHardware::nowUs = 3000000;
    GroveOledTests::update();
    check(!started && initializations == 0 && transfers == 0 && Serial.output.empty(), "inactive Grove OLED does not touch either display");
    std::cout << "PASS: Grove OLED mode isolation\n";
    return 0;
  }
  check(started && transfers == 257, "initialization includes a successful full first pattern");
  for (unsigned int selected = 0; selected < 4; ++selected) {
    if (selected != 0) {
      GroveOledTests::nextPattern();
    }
    checkPattern(selected);
  }
  const unsigned int before = transfers;
  FakeHardware::nowUs = 2999000;
  GroveOledTests::update();
  check(transfers == before, "three-second pattern interval");
  FakeHardware::nowUs = 3000000;
  GroveOledTests::update();
  check(transfers == before + 256, "timed refresh draws exactly one pattern");
  checkPattern(0);
  reset();
  present = false;
  check(!GroveOledTests::begin() && transfers == 0, "missing device does not attempt initialization");
  reset();
  failTransfer = 1;
  check(!GroveOledTests::begin(), "initialization failure reported");
  reset();
  failTransfer = 2;
  check(!GroveOledTests::begin() && transfers == 2, "first pattern failure is not reported as successful initialization");
  GroveOledTests::nextPattern();
  check(transfers == 2, "failed display is not blindly retried");
  check(!Screen.invalidWrite, "OLED status respects onboard display bounds");
  std::cout << "PASS: four exact OLED patterns, refresh pacing, transport errors and truthful initialization status\n";
}
