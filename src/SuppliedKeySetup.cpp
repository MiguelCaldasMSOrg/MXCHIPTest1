#include <Arduino.h>
#include <Az3166StSafe.h>
#include <platform/mbed_critical.h>
#include "SensitiveMemory.h"
#include "SuppliedKeySetup.h"

namespace {
  constexpr uint32_t kHostAddress = 0x08008000;
  constexpr size_t kHostSectorBytes = 0x4000;
  const char *failure = "NONE";

  bool fail(const char *code) {
    failure = code;
    return false;
  }

  bool hostFlashEmpty() {
    const volatile uint8_t *bytes = reinterpret_cast<volatile const uint8_t *>(kHostAddress);
    bool zero = true;
    bool erased = true;
    for (size_t index = 0; index < kHostSectorBytes; ++index) {
      const uint8_t value = bytes[index];
      zero = zero && value == 0;
      erased = erased && value == 255;
    }
    return zero || erased;
  }

  class Chip {
    public:
    stse_Handler_t handler = {};
    stse_session_t session = {};
    bool sessionOpen = false;

    ~Chip() {
      if (sessionOpen) {
        stsafea_close_host_session(&session);
      }
    }

    bool inspect(SuppliedKeySetup::Status &value) {
      value = {};
      memcpy(value.uid, reinterpret_cast<const void *>(0x1FFF7A10), sizeof(value.uid));
      const uint32_t options = FLASH->OPTCR;
      const uint8_t rdp = static_cast<uint8_t>((options >> 8) & 255);
      value.rdp = rdp == OB_RDP_LEVEL_0 ? 0 : (rdp == OB_RDP_LEVEL_2 ? 2 : 1);
      value.pcrop = (options & 0x80000000U) != 0;
      value.hostFlashEmpty = !value.pcrop && hostFlashEmpty();
      if (Az3166StSafe::begin(handler) != STSE_OK) {
        return fail("CHIP_CONNECT");
      }
      stsafea_host_key_slot_t host = {};
      if (stsafea_query_host_key(&handler, &host) != STSE_OK || host.key_presence_flag > 1) {
        return fail("HOST_KEY_QUERY");
      }
      value.hostKeysPresent = host.key_presence_flag != 0;
      return envelopeKey(value.envelopeKeyPresent);
    }

    bool envelopeKey(bool &present) {
      uint8_t command[] = {STSAFEA_CMD_QUERY, STSAFEA_SUBJECT_TAG_LOCAL_ENVELOPE_KEY_TABLE};
      uint8_t status = 0;
      uint8_t payload[7] = {};
      STSE_FRAME_ALLOCATE(request);
      STSE_FRAME_ELEMENT_ALLOCATE_PUSH(&request, commandElement, sizeof(command), command);
      STSE_FRAME_ALLOCATE(response);
      STSE_FRAME_ELEMENT_ALLOCATE_PUSH(&response, statusElement, 1, &status);
      STSE_FRAME_ELEMENT_ALLOCATE_PUSH(&response, payloadElement, sizeof(payload), payload);
      if (stsafea_frame_raw_transfer(&handler, &request, &response, 5) != STSE_OK || payloadElement.length < 5 || payload[0] != 2) {
        return fail("ENVELOPE_KEY_QUERY");
      }
      size_t offset = 1;
      present = false;
      for (uint8_t slot = 0; slot < 2; ++slot) {
        if (offset + 2 > payloadElement.length || payload[offset] != slot || payload[offset + 1] > 1) {
          return fail("ENVELOPE_KEY_LAYOUT");
        }
        const bool exists = payload[offset + 1] != 0;
        offset += 2;
        if (exists) {
          if (offset >= payloadElement.length || (slot == 0 && payload[offset] != 0)) {
            return fail("ENVELOPE_KEY_TYPE");
          }
          ++offset;
        }
        if (slot == 0) {
          present = exists;
        }
      }
      return offset == payloadElement.length || fail("ENVELOPE_KEY_LENGTH");
    }

    bool createEnvelopeKey() {
      const stse_perso_info_t saved = handler.perso_info;
      stsafea_perso_info_set_cmd_AC(&handler.perso_info, STSAFEA_CMD_GENERATE_KEY, STSE_CMD_AC_FREE);
      const stse_ReturnCode_t result = stsafea_generate_wrap_unwrap_key(&handler, 0, STSE_AES_128_KT);
      handler.perso_info = saved;
      bool present = false;
      return (result == STSE_OK && envelopeKey(present) && present) || fail("ENVELOPE_KEY_CREATE");
    }

    bool setHostKeys(const uint8_t *keys) {
      stsafea_host_keys_t material = {};
      memcpy(material.aes_128_key.host_mac_key, keys, 16);
      memcpy(material.aes_128_key.host_cipher_key, keys + 16, 16);
      const stse_ReturnCode_t result = stse_host_key_provisioning(&handler, STSAFEA_AES_128_HOST_KEY, &material);
      SensitiveMemory::clear(&material, sizeof(material));
      return result == STSE_OK || fail("HOST_KEY_WRITE");
    }

    bool verify(uint8_t *keys) {
      if (stsafea_open_host_session(&handler, &session, keys, keys + 16) != STSE_OK) {
        return fail("HOST_SESSION");
      }
      sessionOpen = true;
      uint8_t challenge[] = {'M', 'X', 'C', 'H', 'I', 'P', '-', 'H', 'O', 'S', 'T', '-', 'K', 'E', 'Y', 'S'};
      stsafea_perso_info_set_cmd_AC(&handler.perso_info, STSAFEA_CMD_WRAP_LOCAL_ENVELOPE, STSE_CMD_AC_HOST);
      stsafea_perso_info_set_cmd_encrypt_flag(&handler.perso_info, STSAFEA_CMD_WRAP_LOCAL_ENVELOPE, 1);
      stsafea_perso_info_set_cmd_AC(&handler.perso_info, STSAFEA_CMD_UNWRAP_LOCAL_ENVELOPE, STSE_CMD_AC_HOST);
      stsafea_perso_info_set_rsp_encrypt_flag(&handler.perso_info, STSAFEA_CMD_UNWRAP_LOCAL_ENVELOPE, 1);
      uint8_t wrapped[sizeof(challenge) + 8] = {};
      uint8_t returned[sizeof(challenge)] = {};
      const bool valid = stsafea_wrap_payload(&handler, 0, challenge, sizeof(challenge), wrapped, sizeof(wrapped)) == STSE_OK && stsafea_unwrap_payload(&handler, 0, wrapped, sizeof(wrapped), returned, sizeof(returned)) == STSE_OK && memcmp(challenge, returned, sizeof(challenge)) == 0;
      SensitiveMemory::clear(wrapped, sizeof(wrapped));
      SensitiveMemory::clear(returned, sizeof(returned));
      return valid || fail("HOST_KEYS_VERIFY");
    }
  };

  bool writeLoaders(const uint8_t *keys) {
    uint8_t code[HostKeyBlock::kLoaderBytes];
    HostKeyBlock::encode(keys, code);
    if (HAL_FLASH_Unlock() != HAL_OK) {
      SensitiveMemory::clear(code, sizeof(code));
      return fail("FLASH_UNLOCK");
    }
    FLASH_EraseInitTypeDef erase = {};
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = FLASH_SECTOR_2;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    uint32_t error = 0;
    core_util_critical_section_enter();
    bool written = HAL_FLASHEx_Erase(&erase, &error) == HAL_OK;
    for (size_t index = 0; written && index < sizeof(code); ++index) {
      written = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, kHostAddress + static_cast<uint32_t>(index), code[index]) == HAL_OK;
    }
    core_util_critical_section_exit();
    const bool locked = HAL_FLASH_Lock() == HAL_OK;
    const bool matches = written && memcmp(reinterpret_cast<const void *>(kHostAddress), code, sizeof(code)) == 0;
    SensitiveMemory::clear(code, sizeof(code));
    return (written && locked && matches) || fail("HOST_FLASH_WRITE_VERIFY");
  }
}

namespace SuppliedKeySetup {
  bool status(Status &value) {
    failure = "NONE";
    Chip chip;
    return chip.inspect(value);
  }

  bool install(const uint8_t *uid, const uint8_t *keys) {
    failure = "NONE";
    if (uid == nullptr || !HostKeyBlock::valid(keys)) {
      return fail("INVALID_SUPPLIED_KEYS");
    }
    Status current;
    uint8_t keyCopy[HostKeyBlock::kKeyBytes];
    memcpy(keyCopy, keys, sizeof(keyCopy));
    bool installed = false;
    {
      Chip chip;
      if (chip.inspect(current)) {
        if (memcmp(uid, current.uid, sizeof(current.uid)) != 0) {
          fail("WRONG_DEVICE");
        } else if (current.rdp != 0 || current.pcrop || (FLASH->OPTCR & FLASH_OPTCR_nWRP_2) == 0) {
          fail("HOST_FLASH_PROTECTED");
        } else if (current.hostKeysPresent) {
          fail("HOST_KEYS_ALREADY_PRESENT");
        } else if (!current.hostFlashEmpty) {
          fail("HOST_FLASH_NOT_EMPTY");
        } else if ((current.envelopeKeyPresent || chip.createEnvelopeKey()) && chip.setHostKeys(keyCopy) && chip.verify(keyCopy)) {
          installed = writeLoaders(keyCopy);
        }
      }
    }
    SensitiveMemory::clear(keyCopy, sizeof(keyCopy));
    return installed;
  }

  const char *lastError() {
    return failure;
  }
}
