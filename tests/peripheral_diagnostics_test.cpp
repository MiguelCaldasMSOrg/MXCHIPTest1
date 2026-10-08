#include "stubs/DiagnosticHardware.h"
#include <mico.h>
#include <IrDASensor.h>
#include <HAL_STSAFE-A100.h>
#include <Az3166StSafe.h>
#include "AppConfig.h"
#include "OnboardTests.h"

namespace {
  unsigned int irInitializations = 0;
  unsigned int irTransmissions = 0;
  unsigned int chipInitializations = 0;
  unsigned int reads = 0;
  unsigned int frees = 0;
  unsigned int probes = 0;
  int irResult = 0;
  uint8_t chipResult = 0;
  uint8_t readResult = 0;
  bool different = false;
  bool present = true;
  int token = 0;
  unsigned int middlewareInitializations = 0;
  unsigned int randomReads = 0;
  std::string middlewareFailure;
}

namespace Az3166StSafe {
  stse_ReturnCode_t cryptoSelfTest() {
    return middlewareFailure == "crypto" ? STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR : STSE_OK;
  }
  stse_ReturnCode_t begin(stse_Handler_t &handler) {
    ++middlewareInitializations;
    handler.device_type = STSAFE_A100;
    return middlewareFailure == "init" ? STSE_COMMUNICATION_ERROR : STSE_OK;
  }
  uint16_t lastCommand() {
    return 0x1411;
  }
}

extern "C" {
  stse_ReturnCode_t stsafea_query_life_cycle_state(stse_Handler_t *, stsafea_life_cycle_state_t *state) {
    *state = static_cast<stsafea_life_cycle_state_t>(3);
    return STSE_OK;
  }
  stse_ReturnCode_t stsafea_query_host_key(stse_Handler_t *, stsafea_host_key_slot_t *slot) {
    memset(slot, 0, sizeof(*slot));
    return STSE_OK;
  }
  stse_ReturnCode_t stse_data_storage_get_total_partition_count(stse_Handler_t *, uint8_t *count) {
    *count = middlewareFailure == "partition-count" ? 255 : 12;
    return STSE_OK;
  }
  stse_ReturnCode_t stse_data_storage_get_partitioning_table(stse_Handler_t *, uint8_t count, stsafea_data_partition_record_t *table, uint16_t size) {
    check(count == 12 && size >= count * sizeof(*table), "bounded partition metadata table");
    for (uint8_t index = 0; index < count; ++index) {
      table[index] = {};
      table[index].index = index;
    }
    return STSE_OK;
  }
  stse_ReturnCode_t stsafea_query_private_key_slots_count(stse_Handler_t *, uint8_t *count) {
    *count = 2;
    return STSE_OK;
  }
  stse_ReturnCode_t stse_device_echo(stse_Handler_t *, uint8_t *input, uint8_t *output, uint16_t size) {
    memcpy(output, input, size);
    if (middlewareFailure == "echo") {
      output[0] ^= 1;
    }
    return STSE_OK;
  }
  stse_ReturnCode_t stse_generate_random(stse_Handler_t *, uint8_t *output, uint16_t size) {
    ++randomReads;
    check(size == 32, "bounded hardware random request");
    memset(output, middlewareFailure == "random" ? 0 : (randomReads % 2 == 0 ? 0xA5 : 0x5A), size);
    return STSE_OK;
  }
  stse_ReturnCode_t stse_platform_hash_compute(stse_hash_algorithm_t type, uint8_t *, uint16_t size, uint8_t *output, uint16_t *outputSize) {
    check(type == STSE_SHA_256 && size == 6, "public test message only");
    memset(output, 0xA5, 32);
    *outputSize = 32;
    return STSE_OK;
  }
  stse_ReturnCode_t stse_platform_ecc_verify(stse_ecc_key_type_t, const uint8_t *, uint8_t *, uint16_t, uint8_t *signature) {
    return signature[0] == 0xEF ? STSE_OK : STSE_PLATFORM_ECC_VERIFY_ERROR;
  }
  stse_ReturnCode_t stse_ecc_verify_signature(stse_Handler_t *, stse_ecc_key_type_t type, const uint8_t *, const uint8_t *signature, const uint8_t *, uint16_t digestSize, uint8_t, uint8_t *valid) {
    check(type == STSE_ECC_KT_NIST_P_256 && digestSize == 32, "hardware verifies only the public vector; no private slot is used");
    *valid = middlewareFailure == "signature" ? 1 : (signature[0] == 0xEF ? 1 : 0);
    return STSE_OK;
  }
}

int IRDASensor::init() {
  ++irInitializations;
  return irResult;
}

unsigned char IRDASensor::IRDATransmit(unsigned char *data, int size, int timeout) {
  const unsigned char expected[] = {0x55, 0xAA, 0, 0xFF, 0x4D, 0x58, 0x43, 0x48};
  check(size == 8 && memcmp(data, expected, 8) == 0 && timeout == 500, "known bounded IR pattern");
  ++irTransmissions;
  return static_cast<unsigned char>(irResult);
}

OSStatus MicoI2cInitialize(mico_i2c_device_t *device) {
  ++probes;
  check(device->address == 0x20, "STSAFE uses its own I2C address");
  return kNoErr;
}

bool MicoI2cProbeDevice(mico_i2c_device_t *, int) {
  return present;
}

extern "C" {
  uint8_t Init_HAL(uint8_t address, void **handle) {
    check(address == 0x20, "STSAFE HAL address");
    ++chipInitializations;
    *handle = &token;
    return chipResult;
  }
  uint8_t Free_HAL(void *handle) {
    check(handle == &token, "free only an allocated HAL handle");
    ++frees;
    return 0;
  }
  uint8_t HAL_Get_Data_Zone(void *handle, uint8_t zone, uint16_t size, uint8_t *buffer, uint16_t offset) {
    check(handle == &token && zone == 0 && offset == 0 && size == 32, "only 32 certificate-zone bytes read; never credential zones");
    ++reads;
    memset(buffer, different && reads % 2 == 0 ? 0x44 : 0x30, size);
    return readResult;
  }
}

int main() {
  IrdaTests::begin();
  IrdaTests::transmit();
  SecurityChipTests::run();
  if (AppConfig::kIrdaEnabled) {
    check(irInitializations == 1 && irTransmissions == 1 && probes == 0, "IR/STSAFE mode isolation");
    IrdaTests::transmit();
    check(irTransmissions == 1, "IR rate limit");
    FakeHardware::nowUs = 999000;
    IrdaTests::transmit();
    check(irTransmissions == 1, "IR interval boundary");
    FakeHardware::nowUs = 1000000;
    IrdaTests::transmit();
    check(irTransmissions == 2, "IR ready at one second");
    for (unsigned int burst = 3; burst <= 10000; ++burst) {
      FakeHardware::nowUs += 1000000;
      IrdaTests::transmit();
    }
    check(irTransmissions == 10000 && Screen.lines[1] == "Sent 10000", "five-digit burst count is displayed without truncation");
    irResult = 1;
    FakeHardware::nowUs += 1000000;
    IrdaTests::transmit();
    check(Serial.output.find("IrDA FAIL: transmit") != std::string::npos, "IR failure reported");
    IrdaTests::begin();
    IrdaTests::transmit();
    check(irTransmissions == 10001, "failed init blocks TX");
  } else if (AppConfig::kSecurityChipEnabled) {
    check(irInitializations == 0 && chipInitializations == 1 && reads == 2 && frees == 1, "direct read-only STSAFE operations and cleanup");
    check(Serial.output.find("Contents were not printed") != std::string::npos, "no certificate data dump");
    check(middlewareInitializations == 1 && randomReads == 2 && Serial.output.find("reject the modified signature") != std::string::npos, "complete non-destructive middleware diagnostic path");
    different = true;
    SecurityChipTests::run();
    check(Serial.output.find("repeated read mismatch") != std::string::npos && frees == 2, "read mismatch detected and handle freed");
    different = false;
    chipResult = 10;
    SecurityChipTests::run();
    check(Serial.output.find("not personalized (HAL 10)") != std::string::npos && reads == 6 && frees == 3, "unpersonalized status is explicit; accessible reads need no key changes");
    chipResult = 0;
    readResult = 1;
    SecurityChipTests::run();
    check(Serial.output.find("read/access denied") != std::string::npos && frees == 4, "access denial does not trigger personalization");
    chipResult = 1;
    SecurityChipTests::run();
    check(Serial.output.find("personalization was NOT attempted") != std::string::npos && frees == 5, "no destructive fallback for an uninitialized chip");
    present = false;
    SecurityChipTests::run();
    check(chipInitializations == 5 && Serial.output.find("no I2C response") != std::string::npos, "absent chip blocks HAL");
    present = true;
    chipResult = readResult = 0;
    for (const char *fault: {"init", "crypto", "partition-count", "echo", "random", "signature"}) {
      middlewareFailure = fault;
      Serial.output.clear();
      SecurityChipTests::run();
      check(Serial.output.find("STSELib FAIL:") != std::string::npos && Screen.lines[1] == "STSELib FAIL", "middleware errors cannot masquerade as success");
    }
  } else {
    check(irInitializations == 0 && irTransmissions == 0 && probes == 0 && chipInitializations == 0 && middlewareInitializations == 0, "inactive modes have no peripheral side effects");
  }
  check(!Screen.invalidWrite, "IR/STSAFE status fits OLED");
  std::cout << "PASS: IR/STSAFE mode isolation, bounded transmit, rate limit, read-only operations and failure cleanup\n";
}
