#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "stubs/DiagnosticHardware.h"
#include <Az3166StSafe.h>
#include "SuppliedKeySetup.h"

namespace {
  bool hostPresent = false;
  bool envelopePresent = false;
  uint8_t actualKeys[32] = {};
  unsigned int hostWrites = 0;
  unsigned int envelopeCreates = 0;
  unsigned int flashWrites = 0;
  unsigned int flashErases = 0;
  bool failHostWrite = false;
  bool failProof = false;
  bool failFlash = false;
  TestFlashRegisters registers = {0x0FFFAA00};
  uint8_t *flash = nullptr;
  uint8_t *identityPage = nullptr;

  uint8_t *allocate(uintptr_t address, size_t size) {
#ifdef _WIN32
    void *result = VirtualAlloc(reinterpret_cast<void *>(address), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void *result = mmap(reinterpret_cast<void *>(address), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (result == MAP_FAILED) {
      result = nullptr;
    }
#endif
    check(result == reinterpret_cast<void *>(address), "isolated host-memory fixture");
    return static_cast<uint8_t *>(result);
  }

  bool validSession(stse_Handler_t *handler) {
    return hostPresent && handler->pActive_host_session != nullptr && memcmp(handler->pActive_host_session->context.host.pHost_MAC_key, actualKeys, 16) == 0 && memcmp(handler->pActive_host_session->context.host.pHost_cypher_key, actualKeys + 16, 16) == 0;
  }

  void resetFixture() {
    memset(flash, 0xA5, 0x100000);
    memset(flash + 0x8000, 255, 0x4000);
    hostPresent = envelopePresent = failHostWrite = failProof = failFlash = false;
    hostWrites = envelopeCreates = flashWrites = flashErases = 0;
    registers.OPTCR = 0x0FFFAA00;
  }
}

TestFlashRegisters *FLASH = &registers;
HAL_StatusTypeDef HAL_FLASH_Unlock() {
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Lock() {
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t value) {
  check(type == FLASH_TYPEPROGRAM_BYTE && address >= 0x08008000 && address < 0x08008058, "only the 88 legacy key-loader bytes are programmed");
  ++flashWrites;
  if (failFlash) {
    return HAL_ERROR;
  }
  flash[address - 0x08000000] = static_cast<uint8_t>(value);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *operation, uint32_t *) {
  check(operation->TypeErase == FLASH_TYPEERASE_SECTORS && operation->Sector == 2 && operation->NbSectors == 1, "only host-key sector erased");
  ++flashErases;
  memset(flash + 0x8000, 255, 0x4000);
  return HAL_OK;
}

namespace Az3166StSafe {
  stse_ReturnCode_t begin(stse_Handler_t &handler) {
    handler = {};
    handler.device_type = STSAFE_A100;
    return STSE_OK;
  }
}

extern "C" {
  void stse_frame_push_element(stse_frame_t *frame, stse_frame_element_t *element) {
    if (frame->last_element) {
      frame->last_element->next = element;
    } else {
      frame->first_element = element;
    }
    frame->last_element = element;
    element->next = nullptr;
    frame->length += element->length;
    ++frame->element_count;
  }
  stse_ReturnCode_t stsafea_frame_raw_transfer(stse_Handler_t *, stse_frame_t *request, stse_frame_t *response, uint16_t) {
    check(request->first_element->pData[0] == STSAFEA_CMD_QUERY && request->first_element->pData[1] == 7, "only envelope-key metadata queried");
    *response->first_element->pData = 0;
    stse_frame_element_t *payload = response->first_element->next;
    const uint8_t empty[] = {2, 0, 0, 1, 0};
    const uint8_t populated[] = {2, 0, 1, 0, 1, 0};
    const size_t size = envelopePresent ? sizeof(populated) : sizeof(empty);
    memcpy(payload->pData, envelopePresent ? populated : empty, size);
    payload->length = static_cast<uint16_t>(size);
    return STSE_OK;
  }
  stse_ReturnCode_t stsafea_query_host_key(stse_Handler_t *, stsafea_host_key_slot_t *keys) {
    *keys = {};
    keys->key_presence_flag = hostPresent ? 1 : 0;
    return STSE_OK;
  }
  void stsafea_perso_info_set_cmd_AC(stse_perso_info_t *info, uint8_t code, stse_cmd_access_conditions_t ac) {
    info->cmd_AC_status = (info->cmd_AC_status & ~(uint64_t(3) << (code * 2))) | (uint64_t(ac) << (code * 2));
  }
  void stsafea_perso_info_set_cmd_encrypt_flag(stse_perso_info_t *info, uint8_t code, uint8_t enabled) {
    info->cmd_encryption_status |= uint32_t(enabled) << code;
  }
  void stsafea_perso_info_set_rsp_encrypt_flag(stse_perso_info_t *info, uint8_t code, uint8_t enabled) {
    info->rsp_encryption_status |= uint32_t(enabled) << code;
  }
  stse_ReturnCode_t stsafea_generate_wrap_unwrap_key(stse_Handler_t *, uint8_t slot, stse_aes_key_type_t type) {
    check(slot == 0 && type == STSE_AES_128_KT && !envelopePresent, "only missing device-local envelope key created");
    ++envelopeCreates;
    envelopePresent = true;
    return STSE_OK;
  }
  stse_ReturnCode_t stse_host_key_provisioning(stse_Handler_t *, stsafea_host_key_type_t type, stsafea_host_keys_t *keys) {
    check(!hostPresent && type == STSAFEA_AES_128_HOST_KEY, "supplied AES-128 host bundle");
    ++hostWrites;
    if (failHostWrite) {
      return STSE_COMMUNICATION_ERROR;
    }
    memcpy(actualKeys, keys->aes_128_key.host_mac_key, 16);
    memcpy(actualKeys + 16, keys->aes_128_key.host_cipher_key, 16);
    hostPresent = true;
    return STSE_OK;
  }
  stse_ReturnCode_t stsafea_open_host_session(stse_Handler_t *handler, stse_session_t *session, uint8_t *mac, uint8_t *cipher) {
    *session = {};
    session->context.host.pSTSE = handler;
    session->context.host.pHost_MAC_key = mac;
    session->context.host.pHost_cypher_key = cipher;
    handler->pActive_host_session = session;
    return STSE_OK;
  }
  void stsafea_close_host_session(stse_session_t *session) {
    session->context.host.pSTSE->pActive_host_session = nullptr;
    *session = {};
  }
  stse_ReturnCode_t stsafea_wrap_payload(stse_Handler_t *handler, uint8_t slot, uint8_t *data, uint16_t size, uint8_t *wrapped, uint16_t total) {
    check(validSession(handler) && slot == 0 && size == 16 && total == 24, "only a volatile proof block is wrapped");
    check(((handler->perso_info.cmd_AC_status >> (STSAFEA_CMD_WRAP_LOCAL_ENVELOPE * 2)) & 3) == STSE_CMD_AC_HOST, "proof command is host-authenticated");
    check((handler->perso_info.cmd_encryption_status & (uint32_t(1) << STSAFEA_CMD_WRAP_LOCAL_ENVELOPE)) != 0, "proof payload encrypted in transit");
    memset(wrapped, 0, 8);
    memcpy(wrapped + 8, data, size);
    return STSE_OK;
  }
  stse_ReturnCode_t stsafea_unwrap_payload(stse_Handler_t *handler, uint8_t slot, uint8_t *wrapped, uint16_t total, uint8_t *data, uint16_t size) {
    check(validSession(handler) && slot == 0 && total == 24 && size == 16, "only volatile proof is unwrapped");
    check(((handler->perso_info.cmd_AC_status >> (STSAFEA_CMD_UNWRAP_LOCAL_ENVELOPE * 2)) & 3) == STSE_CMD_AC_HOST, "proof response is host-authenticated");
    check((handler->perso_info.rsp_encryption_status & (uint32_t(1) << STSAFEA_CMD_UNWRAP_LOCAL_ENVELOPE)) != 0, "proof response encrypted in transit");
    memcpy(data, wrapped + 8, size);
    if (failProof) {
      data[0] ^= 1;
    }
    return STSE_OK;
  }
}

int main() {
  flash = allocate(0x08000000, 0x100000);
  identityPage = allocate(0x1FFF0000, 0x10000);
  uint8_t uid[12];
  uint8_t keys[32];
  for (size_t i = 0; i < sizeof(uid); ++i) {
    uid[i] = static_cast<uint8_t>(i + 1);
  }
  memcpy(identityPage + 0x7A10, uid, sizeof(uid));
  for (size_t i = 0; i < sizeof(keys); ++i) {
    keys[i] = static_cast<uint8_t>(i + 1);
  }
  resetFixture();
  SuppliedKeySetup::Status status;
  check(SuppliedKeySetup::status(status) && status.hostFlashEmpty && !status.hostKeysPresent && hostWrites == 0 && flashErases == 0, "status does not mutate anything");
  check(SuppliedKeySetup::install(uid, keys), "supplied-key setup");
  check(hostWrites == 1 && envelopeCreates == 1 && flashErases == 1 && flashWrites == 88 && memcmp(keys, actualKeys, sizeof(keys)) == 0, "only requested key objects and legacy loaders written");
  uint8_t expected[88];
  HostKeyBlock::encode(keys, expected);
  check(memcmp(flash + 0x8000, expected, sizeof(expected)) == 0, "legacy loader bytes exactly match supplied keys");
  for (size_t i = 0; i < 0x100000; ++i) {
    if (i < 0x8000 || i >= 0xC000) {
      check(flash[i] == 0xA5, "bootloader/application untouched");
    } else if (i >= 0x8000 + 88) {
      check(flash[i] == 255, "no journal, snapshot, or RDP request is stored");
    }
  }
  check(!SuppliedKeySetup::install(uid, keys) && hostWrites == 1 && flashErases == 1, "no re-provision or repair of existing host keys");
  resetFixture();
  envelopePresent = true;
  check(SuppliedKeySetup::install(uid, keys) && envelopeCreates == 0, "existing chip-bound envelope key is retained");
  resetFixture();
  uid[0] ^= 1;
  check(!SuppliedKeySetup::install(uid, keys) && hostWrites == 0 && envelopeCreates == 0, "wrong target rejected");
  uid[0] ^= 1;
  registers.OPTCR = 0x0FFF5500;
  check(!SuppliedKeySetup::install(uid, keys) && hostWrites == 0 && envelopeCreates == 0 && registers.OPTCR == 0x0FFF5500, "RDP is not changed");
  registers.OPTCR = 0x8FFFAA00;
  check(!SuppliedKeySetup::install(uid, keys) && hostWrites == 0 && envelopeCreates == 0 && registers.OPTCR == 0x8FFFAA00, "PCROP is not changed");
  registers.OPTCR = 0x0FFBAA00;
  check(!SuppliedKeySetup::install(uid, keys) && hostWrites == 0 && envelopeCreates == 0 && registers.OPTCR == 0x0FFBAA00, "sector write protection is checked before creating any keys");
  resetFixture();
  memset(flash + 0x8000, 0, 0x4000);
  check(SuppliedKeySetup::install(uid, keys), "uniformly zero-filled factory host sector accepted");
  resetFixture();
  flash[0x8100] = 1;
  check(!SuppliedKeySetup::install(uid, keys) && hostWrites == 0 && flashErases == 0, "occupied host sector is not repaired or overwritten");
  resetFixture();
  failHostWrite = true;
  check(!SuppliedKeySetup::install(uid, keys) && hostWrites == 1 && envelopeCreates == 1 && flashErases == 0, "write error returns partial outcome without rollback or automatic retry");
  resetFixture();
  failProof = true;
  check(!SuppliedKeySetup::install(uid, keys) && hostWrites == 1 && flashErases == 0, "authentication failure stops setup");
  resetFixture();
  failFlash = true;
  check(!SuppliedKeySetup::install(uid, keys) && hostPresent && flashWrites == 1, "flash failure is reported without a repair path");
  for (unsigned int seed = 0; seed < 256; ++seed) {
    for (size_t i = 0; i < 32; ++i) {
      keys[i] = static_cast<uint8_t>(seed + i * 73);
    }
    HostKeyBlock::encode(keys, expected);
    for (size_t half = 0; half < 2; ++half) {
      const uint8_t *code = expected + half * 44;
      check(
        HostKeyBlock::get16(code) == 0xB4F0 && HostKeyBlock::get16(code + 34) == 0xE880 && HostKeyBlock::get16(code + 36) == 0x00F0 && HostKeyBlock::get16(code + 38) == 0xBCF0 && HostKeyBlock::get16(code + 40) == 0x4770 && HostKeyBlock::get16(code + 42) == 0,
        "legacy getter entry, store, register restore, return and padding"
      );
      for (size_t word = 0; word < 4; ++word) {
        for (size_t part = 0; part < 2; ++part) {
          const uint8_t *instruction = expected + half * 44 + 2 + word * 8 + part * 4;
          const uint16_t first = HostKeyBlock::get16(instruction);
          const uint16_t second = HostKeyBlock::get16(instruction + 2);
          check((first & 0xFBF0) == (part == 0 ? 0xF240 : 0xF2C0) && (second & 0x8F00) == ((4 + word) << 8), "MOVW/MOVT opcodes and r4-r7 destinations");
          const uint16_t value = static_cast<uint16_t>(((first & 15) << 12) | ((first & 0x400) << 1) | ((second & 0x7000) >> 4) | (second & 255));
          check(value == HostKeyBlock::get16(keys + (1 - half) * 16 + word * 4 + part * 2), "cipher-first/MAC-second loader ABI and immediate bits");
        }
      }
      uint8_t instruction[4];
      const uint8_t knownMove[] = {0x41, 0xF2, 0x34, 0x24};
      HostKeyBlock::moveImmediate(instruction, 0xF240, 4, 0x1234);
      check(memcmp(instruction, knownMove, sizeof(instruction)) == 0, "known MOVW r4,#0x1234 little-endian encoding");
    }
  }
  memset(keys, 0, sizeof(keys));
  check(!HostKeyBlock::valid(nullptr) && !HostKeyBlock::valid(keys), "missing or zero supplied keys rejected");
  for (size_t i = 0; i < sizeof(keys); ++i) {
    keys[i] = static_cast<uint8_t>(i + 1);
  }
  memcpy(keys + 16, keys, 16);
  check(!HostKeyBlock::valid(keys), "identical MAC/cipher keys rejected");
  memset(keys, 255, 16);
  check(!HostKeyBlock::valid(keys), "all-FF MAC key rejected");
  memset(keys, 0, 16);
  check(!HostKeyBlock::valid(keys), "all-zero MAC key rejected");
  for (size_t i = 0; i < 16; ++i) {
    keys[i] = static_cast<uint8_t>(i + 1);
  }
  memset(keys + 16, 255, 16);
  check(!HostKeyBlock::valid(keys), "all-FF cipher key rejected");
  memset(keys + 16, 0, 16);
  check(!HostKeyBlock::valid(keys), "all-zero cipher key rejected");
  resetFixture();
  check(!SuppliedKeySetup::install(uid, keys) && hostWrites == 0 && envelopeCreates == 0 && flashErases == 0, "invalid supplied key material has no persistent effects");
#ifdef _WIN32
  VirtualFree(flash, 0, MEM_RELEASE);
  VirtualFree(identityPage, 0, MEM_RELEASE);
#else
  munmap(flash, 0x100000);
  munmap(identityPage, 0x10000);
#endif
  std::cout << "PASS: supplied keys only, existing envelope-key retention, exact loader ABI, no data-zone APIs/journal/RDP code and explicit partial-failure reporting\n";
}
