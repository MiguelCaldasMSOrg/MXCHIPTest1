#pragma once

#include <stdint.h>

typedef int OSStatus;
constexpr OSStatus kNoErr = 0;

enum mico_i2c_t {
  Arduino_I2C
};

enum mico_i2c_bus_address_width_t {
  I2C_ADDRESS_WIDTH_7BIT
};

enum mico_i2c_speed_mode_t {
  I2C_STANDARD_SPEED_MODE
};

struct mico_i2c_device_t {
  mico_i2c_t port;
  uint16_t address;
  mico_i2c_bus_address_width_t address_width;
  mico_i2c_speed_mode_t speed_mode;
};

struct mico_i2c_message_t {
  const void *tx_buffer;
  void *rx_buffer;
  uint16_t tx_length;
  uint16_t rx_length;
  uint16_t retries;
  bool combined;
};

OSStatus MicoI2cInitialize(mico_i2c_device_t *device);
bool MicoI2cProbeDevice(mico_i2c_device_t *device, int retries);
OSStatus MicoI2cBuildTxMessage(mico_i2c_message_t *message, const void *buffer, uint16_t length, uint16_t retries);
OSStatus MicoI2cBuildCombinedMessage(mico_i2c_message_t *message, const void *tx, void *rx, uint16_t txLength, uint16_t rxLength, uint16_t retries);
OSStatus MicoI2cTransfer(mico_i2c_device_t *device, mico_i2c_message_t *message, uint16_t count);
