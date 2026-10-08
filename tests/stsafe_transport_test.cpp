#include "stubs/DiagnosticHardware.h"
#include <mico.h>
#include <Az3166StSafe.h>
#include <vector>
#include <algorithm>

namespace {
  std::vector<uint8_t> response;
  unsigned int transmissions = 0;
  unsigned int receiveAttempts = 0;
  unsigned int nackReads = 0;
  bool failWrite = false;
  bool corruptCrc = false;
  bool wrongIdentity = false;
  bool overflowLength = false;
  uint8_t deviceStatus = 0;
  uint8_t randomCounter = 0;

  uint16_t referenceCrc(const std::vector<uint8_t> &bytes) {
    uint16_t value = 0xFFFF;
    for (uint8_t byte: bytes) {
      for (unsigned int bit = 0; bit < 8; ++bit) {
        const bool feedback = ((value ^ (byte >> bit)) & 1) != 0;
        value >>= 1;
        if (feedback) {
          value ^= 0x8408;
        }
      }
    }
    return static_cast<uint16_t>(~value);
  }

  void answer(const std::vector<uint8_t> &payload) {
    std::vector<uint8_t> protectedBytes(1, deviceStatus);
    if (deviceStatus == 0) {
      protectedBytes.insert(protectedBytes.end(), payload.begin(), payload.end());
    }
    const uint16_t crc = referenceCrc(protectedBytes);
    const size_t size = protectedBytes.size() - 1 + 2;
    response = {deviceStatus, static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size)};
    response.insert(response.end(), protectedBytes.begin() + 1, protectedBytes.end());
    response.push_back(static_cast<uint8_t>((crc >> 8) ^ (corruptCrc ? 1 : 0)));
    response.push_back(static_cast<uint8_t>(crc));
    if (overflowLength) {
      response[1] = 0xFF;
      response[2] = 0xFF;
    }
  }
}

OSStatus MicoI2cInitialize(mico_i2c_device_t *device) {
  check(device->address == 0x20 && device->port == Arduino_I2C && device->speed_mode == I2C_STANDARD_SPEED_MODE, "native shared 100 kHz bus");
  return kNoErr;
}

bool MicoI2cProbeDevice(mico_i2c_device_t *, int) {
  return true;
}

OSStatus MicoI2cBuildTxMessage(mico_i2c_message_t *message, const void *data, uint16_t size, uint16_t retries) {
  check(retries == 1, "middleware owns retry policy");
  *message = {data, nullptr, size, 0, retries, false};
  return kNoErr;
}

OSStatus MicoI2cBuildRxMessage(mico_i2c_message_t *message, void *data, uint16_t size, uint16_t retries) {
  *message = {nullptr, data, 0, size, retries, false};
  return kNoErr;
}

OSStatus MicoI2cTransfer(mico_i2c_device_t *, mico_i2c_message_t *message, uint16_t count) {
  check(count == 1 && !message->combined, "whole-frame transaction, never register-address emulation");
  if (message->tx_length != 0) {
    if (failWrite) {
      return -1;
    }
    ++transmissions;
    const uint8_t *data = static_cast<const uint8_t *>(message->tx_buffer);
    const size_t size = message->tx_length;
    check(size >= 3 && size <= 512, "bounded command frame");
    const std::vector<uint8_t> bytes(data, data + size - 2);
    const uint16_t crc = referenceCrc(bytes);
    check(data[size - 2] == (crc >> 8) && data[size - 1] == (crc & 0xFF), "command CRC byte order and coverage");
    std::vector<uint8_t> payload;
    if (data[0] == STSAFEA_CMD_QUERY) {
      check(data[1] == STSAFEA_SUBJECT_TAG_PRODUCT_DATA, "A100 initialization does not issue the unsupported 0x24 query");
      payload.assign(STSAFEA_MASK_ID_SIZE + 2, 0);
      payload[0] = 1;
      payload[1] = STSAFEA_MASK_ID_SIZE;
      payload[payload.size() - 2] = wrongIdentity ? 0x46 : 0x40;
    } else if (data[0] == STSAFEA_CMD_ECHO) {
      payload.assign(data + 1, data + size - 2);
    } else if (data[0] == STSAFEA_CMD_GENERATE_RANDOM) {
      check(data[1] == 0 && size == 5, "exact A100 hardware RNG command shape");
      payload.assign(data[2], ++randomCounter);
    } else {
      check(false, "read-only test must not send personalization, update, key-generation or private-key commands");
    }
    answer(payload);
    return kNoErr;
  }
  ++receiveAttempts;
  if (nackReads > 0) {
    --nackReads;
    return -1;
  }
  check(message->rx_length <= response.size(), "no receive beyond complete response");
  memcpy(message->rx_buffer, response.data(), message->rx_length);
  return kNoErr;
}

extern "C" {
  stse_ReturnCode_t stse_platform_generate_random_init() {
    return STSE_OK;
  }
  stse_ReturnCode_t stse_platform_crypto_init() {
    return STSE_OK;
  }
  stse_ReturnCode_t stse_platform_aes_cmac_init(const uint8_t *, uint16_t, uint16_t) {
    check(false, "no host keys in read-only transport tests");
    return STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
  }
  stse_ReturnCode_t stse_platform_aes_cmac_append(uint8_t *, uint16_t) {
    return STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
  }
  stse_ReturnCode_t stse_platform_aes_cmac_compute_finish(uint8_t *, uint8_t *) {
    return STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
  }
  stse_ReturnCode_t stse_platform_aes_cmac_verify_finish(uint8_t *) {
    return STSE_PLATFORM_AES_CMAC_VERIFY_ERROR;
  }
  stse_ReturnCode_t stse_platform_aes_ecb_enc(const uint8_t *, uint16_t, const uint8_t *, uint16_t, uint8_t *, uint16_t *) {
    return STSE_PLATFORM_AES_ECB_ENCRYPT_ERROR;
  }
  stse_ReturnCode_t stse_platform_aes_cbc_enc(const uint8_t *, uint16_t, uint8_t *, const uint8_t *, uint16_t, uint8_t *, uint16_t *) {
    return STSE_PLATFORM_AES_CBC_ENCRYPT_ERROR;
  }
  stse_ReturnCode_t stse_platform_aes_cbc_dec(const uint8_t *, uint16_t, uint8_t *, const uint8_t *, uint16_t, uint8_t *, uint16_t *) {
    return STSE_PLATFORM_AES_CBC_DECRYPT_ERROR;
  }
}

int main() {
  uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  check(stse_platform_Crc16_Calculate(digits, sizeof(digits)) == 0x906E, "CRC-16/X-25 reference vector");
  stse_platform_Crc16_Calculate(digits, 4);
  check(stse_platform_Crc16_Accumulate(digits + 4, 5) == 0x906E, "CRC accumulation over frame fragments");
  stse_Handler_t handler = {};
  check(Az3166StSafe::begin(handler) == STSE_OK && handler.device_type == STSAFE_A100, "typed upstream initialization and real product-data query");
  check(transmissions == 1 && Az3166StSafe::lastCommand() == 0x1411, "only identity queried at initialization");
  stse_cmd_access_conditions_t ac;
  stsafea_perso_info_get_cmd_AC(&handler.perso_info, STSAFEA_CMD_GENERATE_SIGNATURE, &ac);
  check(ac == STSE_CMD_AC_HOST, "private-key commands require an explicit host session");
  uint8_t data[400];
  uint8_t echoed[400];
  for (size_t index = 0; index < sizeof(data); ++index) {
    data[index] = static_cast<uint8_t>(index * 73);
  }
  check(stse_device_echo(&handler, data, echoed, sizeof(data)) == STSE_OK && memcmp(data, echoed, sizeof(data)) == 0, "full upstream two-phase response framing and binary echo");
  nackReads = 2;
  const unsigned int beforeRead = receiveAttempts;
  check(stse_generate_random(&handler, data, 32) == STSE_OK && receiveAttempts - beforeRead == 4, "busy/NACK retries followed by full response");
  corruptCrc = true;
  check(stse_generate_random(&handler, data, 32) == STSE_SERVICE_FRAME_CRC_ERROR, "corrupted response cannot pass");
  corruptCrc = false;
  deviceStatus = STSE_ACCESS_CONDITION_NOT_SATISFIED;
  check(stse_generate_random(&handler, data, 32) == STSE_ACCESS_CONDITION_NOT_SATISFIED, "device access denial is preserved, not retried as success");
  deviceStatus = 0;
  overflowLength = true;
  check(stse_generate_random(&handler, data, 32) == STSE_SERVICE_FRAME_SIZE_ERROR, "oversized wire length rejected");
  overflowLength = false;
  failWrite = true;
  const uint64_t beforeTimeout = FakeHardware::nowUs;
  check(stse_generate_random(&handler, data, 32) == STSE_PLATFORM_BUS_ACK_ERROR, "transmit failure surfaced");
  check(FakeHardware::nowUs - beforeTimeout <= 1000000, "retry time is bounded");
  uint8_t mutation[3] = {STSAFEA_CMD_PUT_ATTRIBUTE, 0, 0};
  check(stse_platform_i2c_send_start(0, 0x20, 100, sizeof(mutation)) == STSE_OK, "stage a simulated mutating command");
  check(stse_platform_i2c_send_stop(0, 0x20, 100, mutation, sizeof(mutation)) == STSE_PLATFORM_BUS_ERR, "uncertain mutation is never marked retryable");
  mutation[0] = 0xE0 | STSAFEA_CMD_READ;
  check(stse_platform_i2c_send_start(0, 0x20, 100, sizeof(mutation)) == STSE_OK, "stage a simulated authenticated read");
  check(stse_platform_i2c_send_stop(0, 0x20, 100, mutation, sizeof(mutation)) == STSE_PLATFORM_BUS_ERR, "authenticated counter-changing commands are never blindly retransmitted");
  failWrite = false;
  wrongIdentity = true;
  check(Az3166StSafe::begin(handler) == STSE_SERVICE_INCOMPATIBLE_DEVICE_TYPE, "wrong chip family rejected");
  wrongIdentity = false;
  check(stse_platform_i2c_send_start(1, 0x20, 100, 4) == STSE_PLATFORM_INVALID_PARAMETER, "unknown bus rejected");
  check(stse_platform_i2c_send_start(0, 0x3C, 100, 4) == STSE_PLATFORM_INVALID_PARAMETER, "cannot address the OLED");
  check(stse_platform_i2c_send_start(0, 0x20, 400, 4) == STSE_PLATFORM_INVALID_PARAMETER, "unsupported bus speed rejected");
  check(stse_platform_i2c_send_start(0, 0x20, 100, 513) == STSE_PLATFORM_BUFFER_ERR, "frame capacity enforced");
  check(stse_platform_i2c_send_start(0, 0x20, 100, 4) == STSE_OK, "start bounded staged write");
  check(stse_platform_i2c_send_continue(0, 0x20, 100, data, 5) == STSE_PLATFORM_BUFFER_ERR, "fragment cannot overrun staged frame");
  check(stse_platform_i2c_send_start(0, 0x20, 100, 4) == STSE_OK, "recover after rejected fragment");
  check(stse_platform_i2c_send_stop(0, 0x20, 100, data, 3) == STSE_PLATFORM_BUFFER_ERR, "incomplete frame never transmitted");
  check(stse_platform_power_off(0, 0x20) == STSE_PLATFORM_POWER_ERROR, "nonexistent power switch is explicitly unsupported");
  memset(data, 0xAB, sizeof(data));
  Az3166StSafe::clear(data, sizeof(data));
  check(
    std::all_of(
      data,
      data + sizeof(data),
      [](uint8_t value) {
        return value == 0;
      }
    ),
    "temporary buffers can be wiped"
  );
  std::cout << "PASS: real STSELib C API/frames, native I2C binding, CRC, identity, static A100 profile, access denial, retry limits and buffer guards\n";
}
