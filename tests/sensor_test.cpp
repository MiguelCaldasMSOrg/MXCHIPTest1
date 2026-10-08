#include "stubs/DiagnosticHardware.h"
#include <mico.h>
#include "AppConfig.h"
#include "OnboardTests.h"
#include <array>
#include <map>

namespace {
  std::map<uint16_t, std::array<uint8_t, 256>> registers;
  unsigned int transfers = 0;
  uint16_t failedAddress = 0;
  bool failConfiguration = false;

  void word(uint16_t address, uint8_t reg, int value) {
    registers[address][reg] = static_cast<uint8_t>(value);
    registers[address][reg + 1] = static_cast<uint8_t>(value >> 8);
  }

  void prepare() {
    registers.clear();
    registers[0x5F][0x0F] = 0xBC;
    registers[0x5F][0x27] = 3;
    registers[0x5F][0x30] = 40;
    registers[0x5F][0x31] = 160;
    registers[0x5F][0x32] = 0;
    registers[0x5F][0x33] = 64;
    registers[0x5F][0x35] = 4;
    word(0x5F, 0x36, 1000);
    word(0x5F, 0x3A, 5000);
    word(0x5F, 0x3C, -1000);
    word(0x5F, 0x3E, 3000);
    word(0x5F, 0x28, 3000);
    word(0x5F, 0x2A, 1000);
    registers[0x5C][0x0F] = 0xB1;
    registers[0x5C][0x27] = 3;
    const uint32_t pressure = 4150272;
    word(0x5C, 0x28, pressure);
    registers[0x5C][0x2A] = static_cast<uint8_t>(pressure >> 16);
    word(0x5C, 0x2B, 2500);
    registers[0x6A][0x0F] = 0x6A;
    registers[0x6A][0x1E] = 3;
    word(0x6A, 0x22, 800);
    word(0x6A, 0x2C, 16393);
    registers[0x1E][0x4F] = 0x40;
    registers[0x1E][0x67] = 8;
    word(0x1E, 0x68, 100);
    word(0x1E, 0x6A, -200);
    word(0x1E, 0x6C, 300);
    failedAddress = 0;
    failConfiguration = false;
    Serial.output.clear();
  }
}

OSStatus MicoI2cInitialize(mico_i2c_device_t *device) {
  check(device->port == Arduino_I2C && device->speed_mode == I2C_STANDARD_SPEED_MODE, "reuse native shared I2C at 100 kHz");
  ++transfers;
  return kNoErr;
}

bool MicoI2cProbeDevice(mico_i2c_device_t *device, int) {
  return registers.count(device->address) != 0;
}

OSStatus MicoI2cBuildTxMessage(mico_i2c_message_t *message, const void *data, uint16_t size, uint16_t retries) {
  *message = {data, nullptr, size, 0, retries, false};
  return kNoErr;
}

OSStatus MicoI2cBuildCombinedMessage(mico_i2c_message_t *message, const void *tx, void *rx, uint16_t txSize, uint16_t rxSize, uint16_t retries) {
  *message = {tx, rx, txSize, rxSize, retries, true};
  return kNoErr;
}

OSStatus MicoI2cTransfer(mico_i2c_device_t *device, mico_i2c_message_t *message, uint16_t count) {
  ++transfers;
  check(count == 1, "one bounded I2C transaction");
  if (device->address == failedAddress) {
    return -1;
  }
  const uint8_t *data = static_cast<const uint8_t *>(message->tx_buffer);
  const uint8_t reg = device->address == 0x5F ? data[0] & 0x7F : data[0];
  if (message->combined) {
    uint8_t *result = static_cast<uint8_t *>(message->rx_buffer);
    for (size_t index = 0; index < message->rx_length; ++index) {
      result[index] = registers[device->address][reg + index];
    }
  } else if (!failConfiguration) {
    check(message->tx_length == 2, "single-register configuration writes");
    registers[device->address][reg] = data[1];
  }
  return kNoErr;
}

int main() {
  prepare();
  SensorTests::begin();
  if (!AppConfig::kSensorsEnabled) {
    SensorTests::sampleNow();
    SensorTests::update();
    SensorTests::nextPage();
    check(transfers == 0 && Screen.writes[0] == 0, "inactive sensor mode leaves the bus and screen alone");
    std::cout << "PASS: sensor mode exclusion\n";
    return 0;
  }
  FakeHardware::nowUs = 1999000;
  SensorTests::update();
  check(Serial.output.find("humidity") == std::string::npos, "no sample before two seconds");
  FakeHardware::nowUs = 2000000;
  SensorTests::update();
  check(Serial.output.find("humidity 50.00 %RH, temperature 20.00 C") != std::string::npos, "HTS221 factory calibration and signed raw values");
  check(Serial.output.find("pressure 1013.25 hPa, temperature 25.00 C") != std::string::npos, "LPS22HB true pressure/temperature scale");
  check(Serial.output.find("gyro 7.00") != std::string::npos && Serial.output.find("150.0 -300.0 450.0 mG") != std::string::npos, "motion/magnetometer scales");
  for (size_t index = 0; index < 5; ++index) {
    SensorTests::nextPage();
  }
  check(!Screen.invalidWrite && FakeHardware::inputConstructions == 0 && FakeHardware::outputConstructions == 0, "bounded display and no Grove GPIO ownership");
  word(0x5F, 0x28, 3100);
  Serial.output.clear();
  SensorTests::sampleNow();
  check(Serial.output.find("changed samples=1") != std::string::npos, "live changes are observed");
  failedAddress = 0x5F;
  SensorTests::sampleNow();
  check(Screen.lines[1] == "No fresh reading", "I2C failure clears stale displayed measurements");
  prepare();
  registers[0x5F][0x0F] = 0;
  SensorTests::begin();
  SensorTests::sampleNow();
  check(Serial.output.find("unexpected device identity") != std::string::npos && Serial.output.find("humidity") == std::string::npos, "wrong sensor ID blocks readings");
  prepare();
  word(0x5F, 0x3A, 1000);
  SensorTests::begin();
  check(Serial.output.find("invalid factory calibration") != std::string::npos, "divide-by-zero calibration rejected");
  prepare();
  SensorTests::begin();
  word(0x5F, 0x28, 20000);
  SensorTests::sampleNow();
  check(Serial.output.find("outside sensor limits") != std::string::npos, "out-of-range measurements are not reported as passing");
  prepare();
  failConfiguration = true;
  SensorTests::begin();
  check(Serial.output.find("configuration readback") != std::string::npos, "ignored configuration writes are detected");
  prepare();
  SensorTests::begin();
  registers[0x5F][0x27] = 0;
  SensorTests::sampleNow();
  check(Serial.output.find("waiting for fresh data") != std::string::npos, "data-ready status checked");
  std::cout << "PASS: four sensor IDs, configuration readback, calibrated units/ranges, live changes, all pages and I2C/data-ready failures\n";
}
