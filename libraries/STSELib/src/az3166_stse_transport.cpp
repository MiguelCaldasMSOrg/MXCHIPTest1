#include <Arduino.h>
#include <mico.h>
#include "Az3166StSafe.h"

namespace {
  enum class Transfer {
    Idle,
    Sending,
    Receiving
  };
  Transfer transfer = Transfer::Idle;
  uint8_t buffer[Az3166StSafe::kTransportCapacity];
  size_t expected = 0;
  size_t offset = 0;
  uint16_t crc = 0xFFFF;
  mico_i2c_device_t device = {Arduino_I2C, 0x20, I2C_ADDRESS_WIDTH_7BIT, I2C_STANDARD_SPEED_MODE};
  bool initialized = false;
  uint16_t command = 0;

  bool validTarget(uint8_t bus, uint8_t address, uint16_t speed) {
    return bus == 0 && address == 0x20 && speed == 100;
  }

  stse_ReturnCode_t reject(stse_ReturnCode_t result) {
    transfer = Transfer::Idle;
    expected = offset = 0;
    Az3166StSafe::clear(buffer, sizeof(buffer));
    return result;
  }

  stse_ReturnCode_t append(uint8_t bus, uint8_t address, uint16_t speed, uint8_t *data, uint16_t size) {
    if (!validTarget(bus, address, speed) || !initialized || transfer != Transfer::Sending) {
      return reject(STSE_PLATFORM_INVALID_PARAMETER);
    }
    if (size > expected - offset) {
      return reject(STSE_PLATFORM_BUFFER_ERR);
    }
    if (size != 0) {
      if (data != nullptr) {
        memcpy(buffer + offset, data, size);
      } else {
        memset(buffer + offset, 0, size);
      }
      offset += size;
    }
    return STSE_OK;
  }

  stse_ReturnCode_t extract(uint8_t bus, uint8_t address, uint16_t speed, uint8_t *data, uint16_t size) {
    if (!validTarget(bus, address, speed) || !initialized || transfer != Transfer::Receiving) {
      return reject(STSE_PLATFORM_INVALID_PARAMETER);
    }
    if (size > expected - offset) {
      return reject(STSE_PLATFORM_BUFFER_ERR);
    }
    if (data != nullptr && size != 0) {
      memcpy(data, buffer + offset, size);
    }
    offset += size;
    return STSE_OK;
  }
}

namespace Az3166StSafe {
  stse_ReturnCode_t begin(stse_Handler_t &handler) {
    stse_ReturnCode_t result = stse_set_default_handler_value(&handler);
    if (result != STSE_OK) {
      return result;
    }
    result = stse_init(&handler);
    if (result != STSE_OK) {
      return result;
    }
    uint8_t mask[STSAFEA_MASK_ID_SIZE] = {};
    result = stsafea_query_mask_id(&handler, mask);
    if (result != STSE_OK) {
      return result;
    }
    const uint16_t number = static_cast<uint16_t>((mask[sizeof(mask) - 2] << 8) | mask[sizeof(mask) - 1]);
    if (number < 0x4000 || number >= 0x4600) {
      return STSE_SERVICE_INCOMPATIBLE_DEVICE_TYPE;
    }
    handler.device_type = STSAFE_A100;
    handler.perso_info.cmd_AC_status = UINT64_MAX;
    handler.perso_info.ext_cmd_AC_status = UINT64_MAX;
    const uint8_t codes[] = {STSAFEA_CMD_ECHO, STSAFEA_CMD_GENERATE_RANDOM, STSAFEA_CMD_VERIFY_SIGNATURE};
    for (uint8_t code: codes) {
      stsafea_perso_info_set_cmd_AC(&handler.perso_info, code, STSE_CMD_AC_FREE);
    }
    return STSE_OK;
  }

  uint16_t lastCommand() {
    return command;
  }

  void clear(void *data, size_t size) {
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(data);
    while (size-- != 0) {
      *bytes++ = 0;
    }
  }
}

extern "C" {
  stse_ReturnCode_t stse_platform_i2c_init(uint8_t bus) {
    reject(STSE_OK);
    initialized = false;
    if (bus != 0) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    if (MicoI2cInitialize(&device) != kNoErr) {
      return STSE_PLATFORM_SERVICES_INIT_ERROR;
    }
    initialized = true;
    return STSE_OK;
  }

  stse_ReturnCode_t stse_platform_i2c_send_start(uint8_t bus, uint8_t address, uint16_t speed, uint16_t size) {
    reject(STSE_OK);
    if (!initialized || !validTarget(bus, address, speed) || size == 0) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    if (size > sizeof(buffer)) {
      return STSE_PLATFORM_BUFFER_ERR;
    }
    transfer = Transfer::Sending;
    expected = size;
    return STSE_OK;
  }

  stse_ReturnCode_t stse_platform_i2c_send_continue(uint8_t bus, uint8_t address, uint16_t speed, uint8_t *data, uint16_t size) {
    return append(bus, address, speed, data, size);
  }

  stse_ReturnCode_t stse_platform_i2c_send_stop(uint8_t bus, uint8_t address, uint16_t speed, uint8_t *data, uint16_t size) {
    const stse_ReturnCode_t appended = append(bus, address, speed, data, size);
    if (appended != STSE_OK) {
      return appended;
    }
    if (offset != expected) {
      return reject(STSE_PLATFORM_BUFFER_ERR);
    }
    command = static_cast<uint16_t>(buffer[0] << 8);
    if ((buffer[0] & 0x1F) == 0x14 && expected >= 4) {
      command |= buffer[1];
    }
    mico_i2c_message_t message;
    if (MicoI2cBuildTxMessage(&message, buffer, static_cast<uint16_t>(expected), 1) != kNoErr) {
      return reject(STSE_PLATFORM_BUS_ERR);
    }
    const OSStatus result = MicoI2cTransfer(&device, &message, 1);
    const uint8_t opcode = static_cast<uint8_t>((command >> 8) & 0x1F);
    const bool retrySafe = (buffer[0] & 0xE0) == 0 && (opcode == STSAFEA_CMD_ECHO || opcode == STSAFEA_CMD_QUERY || opcode == STSAFEA_CMD_GENERATE_RANDOM || opcode == STSAFEA_CMD_READ || opcode == STSAFEA_CMD_VERIFY_SIGNATURE);
    return reject(result == kNoErr ? STSE_OK : (retrySafe ? STSE_PLATFORM_BUS_ACK_ERROR : STSE_PLATFORM_BUS_ERR));
  }

  stse_ReturnCode_t stse_platform_i2c_receive_start(uint8_t bus, uint8_t address, uint16_t speed, uint16_t size) {
    reject(STSE_OK);
    if (!initialized || !validTarget(bus, address, speed) || size == 0) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    if (size > sizeof(buffer)) {
      return STSE_PLATFORM_BUFFER_ERR;
    }
    mico_i2c_message_t message;
    if (MicoI2cBuildRxMessage(&message, buffer, size, 1) != kNoErr) {
      return STSE_PLATFORM_BUS_ERR;
    }
    if (MicoI2cTransfer(&device, &message, 1) != kNoErr) {
      return reject(STSE_PLATFORM_BUS_ACK_ERROR);
    }
    expected = size;
    transfer = Transfer::Receiving;
    return STSE_OK;
  }

  stse_ReturnCode_t stse_platform_i2c_receive_continue(uint8_t bus, uint8_t address, uint16_t speed, uint8_t *data, uint16_t size) {
    return extract(bus, address, speed, data, size);
  }

  stse_ReturnCode_t stse_platform_i2c_receive_stop(uint8_t bus, uint8_t address, uint16_t speed, uint8_t *data, uint16_t size) {
    const stse_ReturnCode_t copied = extract(bus, address, speed, data, size);
    if (copied != STSE_OK) {
      return copied;
    }
    return reject(offset == expected ? STSE_OK : STSE_PLATFORM_BUFFER_ERR);
  }

  stse_ReturnCode_t stse_platform_i2c_send(uint8_t bus, uint8_t address, uint16_t speed, uint8_t *data, uint16_t size) {
    const stse_ReturnCode_t started = stse_platform_i2c_send_start(bus, address, speed, size);
    return started == STSE_OK ? stse_platform_i2c_send_stop(bus, address, speed, data, size) : started;
  }

  stse_ReturnCode_t stse_platform_i2c_receive(uint8_t bus, uint8_t address, uint16_t speed, uint8_t *header, uint8_t *payload, uint16_t *size) {
    if (header == nullptr || payload == nullptr || size == nullptr || *size > sizeof(buffer) - 3) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    const uint16_t capacity = *size;
    stse_ReturnCode_t result = stse_platform_i2c_receive_start(bus, address, speed, static_cast<uint16_t>(capacity + 3));
    if (result != STSE_OK) {
      return result;
    }
    uint8_t length[2];
    result = extract(bus, address, speed, header, 1);
    if (result == STSE_OK) {
      result = extract(bus, address, speed, length, 2);
    }
    if (result != STSE_OK) {
      return result;
    }
    const uint16_t actual = static_cast<uint16_t>((length[0] << 8) | length[1]);
    if (actual > capacity) {
      return reject(STSE_PLATFORM_BUFFER_ERR);
    }
    result = extract(bus, address, speed, payload, actual);
    *size = result == STSE_OK ? actual : 0;
    return reject(result);
  }

  stse_ReturnCode_t stse_platform_i2c_wake(uint8_t bus, uint8_t address, uint16_t speed) {
    if (!initialized || !validTarget(bus, address, speed)) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    return MicoI2cProbeDevice(&device, 3) ? STSE_OK : STSE_PLATFORM_BUS_ACK_ERROR;
  }

  stse_ReturnCode_t stse_platform_delay_init() {
    return STSE_OK;
  }

  void stse_platform_Delay_ms(uint16_t milliseconds) {
    delay(milliseconds);
  }

  stse_ReturnCode_t stse_platform_power_init() {
    return STSE_OK;
  }

  stse_ReturnCode_t stse_platform_power_ctrl_init() {
    return STSE_OK;
  }

  stse_ReturnCode_t stse_platform_power_on(uint8_t bus, uint8_t address) {
    return stse_platform_i2c_wake(bus, address, 100);
  }

  stse_ReturnCode_t stse_platform_power_off(uint8_t, uint8_t) {
    // The onboard STSAFE has no independently switchable supply.
    return STSE_PLATFORM_POWER_ERROR;
  }

  stse_ReturnCode_t stse_platform_crc16_init() {
    crc = 0xFFFF;
    return STSE_OK;
  }

  uint16_t stse_platform_Crc16_Accumulate(uint8_t *data, uint16_t size) {
    for (size_t index = 0; index < size; ++index) {
      crc ^= data[index];
      for (unsigned int bit = 0; bit < 8; ++bit) {
        crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1) != 0 ? 0x8408 : 0));
      }
    }
    return static_cast<uint16_t>(~crc);
  }

  uint16_t stse_platform_Crc16_Calculate(uint8_t *data, uint16_t size) {
    crc = 0xFFFF;
    return stse_platform_Crc16_Accumulate(data, size);
  }
}
