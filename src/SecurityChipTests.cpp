#include <Arduino.h>
#include <EEPROMInterface.h>
#include <HAL_STSAFE-A100.h>
#include <mico.h>
#include <Az3166StSafe.h>
#include "AppConfig.h"
#include "OnboardTests.h"

namespace {
  bool checkResult(stse_ReturnCode_t result, const char *operation) {
    if (result == STSE_OK) {
      return true;
    }
    char text[112];
    snprintf(text, sizeof(text), "STSELib FAIL: %s (0x%04X). No provisioning or policy changes attempted.", operation, static_cast<unsigned int>(result));
    Serial.println(text);
    snprintf(text, sizeof(text), "Last command header/query tag: 0x%04X.", static_cast<unsigned int>(Az3166StSafe::lastCommand()));
    Serial.println(text);
    Screen.print(1, "STSELib FAIL");
    Screen.print(2, "See USB serial");
    return false;
  }

  void clear(uint8_t *data, size_t size) {
    volatile uint8_t *bytes = data;
    while (size-- > 0) {
      *bytes++ = 0;
    }
  }

  bool middlewareTests() {
    stse_Handler_t handler = {};
    if (!checkResult(Az3166StSafe::begin(handler), "A100 identity and platform initialization")) {
      return false;
    }
    if (handler.device_type != STSAFE_A100) {
      return checkResult(STSE_SERVICE_INCOMPATIBLE_DEVICE_TYPE, "expected onboard STSAFE-A100");
    }
    Serial.println(F("PASS: STSELib v1.1.11 verified the A100 identity. Using a conservative static host profile; A100 does not expose the newer command-policy query."));
    if (!checkResult(Az3166StSafe::cryptoSelfTest(), "host AES/CMAC/HKDF known-answer tests")) {
      return false;
    }
    Serial.println(F("PASS: host AES-128/256 ECB/CBC, CMAC (including fragments/truncation), HKDF and invalid-input/tag rejection."));

    stsafea_life_cycle_state_t life = {};
    stsafea_host_key_slot_t host = {};
    uint8_t partitionCount = 0;
    if (!checkResult(stsafea_query_life_cycle_state(&handler, &life), "life-cycle query") || !checkResult(stsafea_query_host_key(&handler, &host), "host-key slot query") || !checkResult(stse_data_storage_get_total_partition_count(&handler, &partitionCount), "partition count")) {
      return false;
    }
    char text[128];
    snprintf(text, sizeof(text), "STSAFE metadata: lifecycle=%u, host keys present=%u, partitions=%u (no key material read).", static_cast<unsigned int>(life), static_cast<unsigned int>(host.key_presence_flag), static_cast<unsigned int>(partitionCount));
    Serial.println(text);
    if (partitionCount == 0 || partitionCount > 16) {
      return checkResult(STSE_SERVICE_FRAME_SIZE_ERROR, "partition count outside bounded buffer");
    }
    stsafea_data_partition_record_t partitions[16] = {};
    if (!checkResult(stse_data_storage_get_partitioning_table(&handler, partitionCount, partitions, sizeof(partitions)), "partition metadata")) {
      return false;
    }
    for (size_t index = 0; index < partitionCount; ++index) {
      const stsafea_data_partition_record_t &partition = partitions[index];
      snprintf(
        text,
        sizeof(text),
        "Zone %u: %u bytes, type=%u, read AC=%u, update AC=%u.",
        static_cast<unsigned int>(partition.index),
        static_cast<unsigned int>(partition.data_segment_length),
        static_cast<unsigned int>(partition.zone_type),
        static_cast<unsigned int>(partition.read_ac),
        static_cast<unsigned int>(partition.update_ac)
      );
      Serial.println(text);
    }
    uint8_t keySlotCount = 0;
    if (!checkResult(stsafea_query_private_key_slots_count(&handler, &keySlotCount), "private-key slot metadata count")) {
      return false;
    }
    Serial.print(F("Private-key slots reported: "));
    Serial.println(static_cast<unsigned int>(keySlotCount));

    uint8_t outgoing[] = {0x00, 0xFF, 0x55, 0xAA, 'M', 'X', 'C', 'H', 'I', 'P', '-', 'S', 'T', 'S', 'E', 1};
    uint8_t incoming[sizeof(outgoing)] = {};
    if (!checkResult(stse_device_echo(&handler, outgoing, incoming, sizeof(outgoing)), "binary echo") || memcmp(outgoing, incoming, sizeof(outgoing)) != 0) {
      return checkResult(STSE_SERVICE_INVALID_FRAME, "binary echo mismatch");
    }
    Serial.println(F("PASS: binary echo and middleware CRC-checked frame transport."));

    uint8_t first[32] = {};
    uint8_t second[32] = {};
    const stse_ReturnCode_t firstResult = stse_generate_random(&handler, first, sizeof(first));
    const stse_ReturnCode_t secondResult = firstResult == STSE_OK ? stse_generate_random(&handler, second, sizeof(second)) : firstResult;
    bool firstNonzero = false;
    bool firstNotAllOnes = false;
    bool secondNonzero = false;
    bool secondNotAllOnes = false;
    for (size_t index = 0; index < sizeof(first); ++index) {
      firstNonzero = firstNonzero || first[index] != 0;
      firstNotAllOnes = firstNotAllOnes || first[index] != 0xFF;
      secondNonzero = secondNonzero || second[index] != 0;
      secondNotAllOnes = secondNotAllOnes || second[index] != 0xFF;
    }
    const bool randomVaries = firstNonzero && firstNotAllOnes && secondNonzero && secondNotAllOnes && memcmp(first, second, sizeof(first)) != 0;
    clear(first, sizeof(first));
    clear(second, sizeof(second));
    if (!checkResult(firstResult, "first hardware RNG block") || !checkResult(secondResult, "second hardware RNG block")) {
      return false;
    }
    if (!randomVaries) {
      return checkResult(STSE_UNEXPECTED_ERROR, "hardware RNG repeated/constant test output");
    }
    Serial.println(F("PASS: two distinct hardware RNG blocks. Output discarded; this functional check does not certify entropy quality."));

    // RFC 6979 A.2.5, SHA-256("sample"): public test key and signature only.
    const uint8_t publicKey[] = {0x60,
      0xFE,
      0xD4,
      0xBA,
      0x25,
      0x5A,
      0x9D,
      0x31,
      0xC9,
      0x61,
      0xEB,
      0x74,
      0xC6,
      0x35,
      0x6D,
      0x68,
      0xC0,
      0x49,
      0xB8,
      0x92,
      0x3B,
      0x61,
      0xFA,
      0x6C,
      0xE6,
      0x69,
      0x62,
      0x2E,
      0x60,
      0xF2,
      0x9F,
      0xB6,
      0x79,
      0x03,
      0xFE,
      0x10,
      0x08,
      0xB8,
      0xBC,
      0x99,
      0xA4,
      0x1A,
      0xE9,
      0xE9,
      0x56,
      0x28,
      0xBC,
      0x64,
      0xF2,
      0xF1,
      0xB2,
      0x0C,
      0x2D,
      0x7E,
      0x9F,
      0x51,
      0x77,
      0xA3,
      0xC2,
      0x94,
      0xD4,
      0x46,
      0x22,
      0x99};
    uint8_t signature[] = {0xEF,
      0xD4,
      0x8B,
      0x2A,
      0xAC,
      0xB6,
      0xA8,
      0xFD,
      0x11,
      0x40,
      0xDD,
      0x9C,
      0xD4,
      0x5E,
      0x81,
      0xD6,
      0x9D,
      0x2C,
      0x87,
      0x7B,
      0x56,
      0xAA,
      0xF9,
      0x91,
      0xC3,
      0x4D,
      0x0E,
      0xA8,
      0x4E,
      0xAF,
      0x37,
      0x16,
      0xF7,
      0xCB,
      0x1C,
      0x94,
      0x2D,
      0x65,
      0x7C,
      0x41,
      0xD4,
      0x36,
      0xC7,
      0xA1,
      0xB6,
      0xE2,
      0x9F,
      0x65,
      0xF3,
      0xE9,
      0x00,
      0xDB,
      0xB9,
      0xAF,
      0xF4,
      0x06,
      0x4D,
      0xC4,
      0xAB,
      0x2F,
      0x84,
      0x3A,
      0xCD,
      0xA8};
    uint8_t message[] = {'s', 'a', 'm', 'p', 'l', 'e'};
    uint8_t digest[32];
    uint16_t digestSize = sizeof(digest);
    if (!checkResult(stse_platform_hash_compute(STSE_SHA_256, message, sizeof(message), digest, &digestSize), "host SHA-256") || !checkResult(stse_platform_ecc_verify(STSE_ECC_KT_NIST_P_256, publicKey, digest, digestSize, signature), "host ECDSA test vector")) {
      return false;
    }
    uint8_t valid = 0;
    if (!checkResult(stse_ecc_verify_signature(&handler, STSE_ECC_KT_NIST_P_256, publicKey, signature, digest, digestSize, 0, &valid), "hardware ECDSA verification")) {
      return false;
    }
    if (valid != 1) {
      return checkResult(STSE_API_INVALID_SIGNATURE, "hardware rejected the valid public test signature");
    }
    signature[0] ^= 1;
    valid = 1;
    if (!checkResult(stse_ecc_verify_signature(&handler, STSE_ECC_KT_NIST_P_256, publicKey, signature, digest, digestSize, 0, &valid), "hardware negative ECDSA check")) {
      return false;
    }
    if (valid != 0 || stse_platform_ecc_verify(STSE_ECC_KT_NIST_P_256, publicKey, digest, digestSize, signature) == STSE_OK) {
      return checkResult(STSE_API_INVALID_SIGNATURE, "modified signature was accepted");
    }
    Serial.println(F("PASS: host and STSAFE hardware ECDSA P-256 verify the public test signature and reject the modified signature."));
    Serial.println(F("No private chip key was used, generated or replaced. Personalization, stored credentials and access policies are unchanged."));
    return true;
  }
}

namespace SecurityChipTests {
  void run() {
    if (!AppConfig::kSecurityChipEnabled) {
      return;
    }
    Screen.print(0, "STSAFE full SDK");
    Screen.print(1, "Checking...");
    Screen.print(2, "No writes/keys");
    Screen.print(3, "A:run B:help");
    Serial.println(F("STSAFE-A100 read-only test: I2C 0x20; no provisioning, key generation, credential reads or option-byte changes."));
    mico_i2c_device_t device = {Arduino_I2C, 0x20, I2C_ADDRESS_WIDTH_7BIT, I2C_STANDARD_SPEED_MODE};
    if (MicoI2cInitialize(&device) != kNoErr || !MicoI2cProbeDevice(&device, 3)) {
      Screen.print(1, "STSAFE absent");
      Serial.println(F("STSAFE FAIL: no I2C response."));
      return;
    }

    void *handle = nullptr;
    const uint8_t initialized = Init_HAL(0x20, &handle);
    // The core 2.0.0 binary returns 10 for missing host/envelope keys, with a usable read handle.
    constexpr uint8_t kNotPersonalized = 10;
    Serial.print(F("STSAFE HAL initialization status: "));
    Serial.println(static_cast<unsigned int>(initialized));
    if ((initialized != 0 && initialized != kNotPersonalized) || handle == nullptr) {
      Screen.print(1, "STSAFE init FAIL");
      Serial.println(F("STSAFE FAIL: HAL initialization/query failed; personalization was NOT attempted."));
      if (handle != nullptr && Free_HAL(handle) != 0) {
        Serial.println(F("STSAFE FAIL: HAL cleanup failed."));
      }
      return;
    }
    if (initialized == kNotPersonalized) {
      Screen.print(2, "Not personalized");
      Serial.println(F("WARNING: STSAFE host/envelope keys are not personalized (HAL 10). Testing only accessible reads; no keys will be created."));
    }
    uint8_t first[32] = {};
    uint8_t second[32] = {};
    // Zone 0 is the SDK's certificate storage, not its Wi-Fi/password/key zones.
    const bool readOk = HAL_Get_Data_Zone(handle, STSAFE_ZONE_0_IDX, sizeof(first), first, 0) == 0 && HAL_Get_Data_Zone(handle, STSAFE_ZONE_0_IDX, sizeof(second), second, 0) == 0;
    const bool same = readOk && memcmp(first, second, sizeof(first)) == 0;
    const bool freed = Free_HAL(handle) == 0;
    clear(first, sizeof(first));
    clear(second, sizeof(second));
    if (!readOk || !same || !freed) {
      Screen.print(1, "STSAFE read FAIL");
      Serial.println(!readOk ? F("STSAFE FAIL: certificate-zone read/access denied.") : (!same ? F("STSAFE FAIL: repeated read mismatch.") : F("STSAFE FAIL: HAL cleanup.")));
      return;
    }
    Serial.println(F("PASS: direct STSAFE HAL initialization and two consistent 32-byte certificate-zone reads. Contents were not printed and temporary buffers were cleared."));
    if (middlewareTests()) {
      Screen.print(1, "STSELib PASS");
      Screen.print(2, "RNG / ECDSA OK");
    }
  }
}
