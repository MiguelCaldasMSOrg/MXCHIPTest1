#pragma once
#include <Arduino.h>

enum HAL_StatusTypeDef {
  HAL_OK,
  HAL_ERROR
};
struct FLASH_EraseInitTypeDef {
  uint32_t TypeErase, Banks, Sector, NbSectors, VoltageRange;
};
struct TestFlashRegisters {
  uint32_t OPTCR;
};
extern TestFlashRegisters *FLASH;
constexpr uint32_t FLASH_TYPEPROGRAM_BYTE = 0;
constexpr uint32_t FLASH_TYPEERASE_SECTORS = 0;
constexpr uint32_t FLASH_SECTOR_2 = 2;
constexpr uint32_t FLASH_VOLTAGE_RANGE_3 = 3;
constexpr uint32_t FLASH_OPTCR_nWRP_2 = 0x00040000;
constexpr uint32_t OB_RDP_LEVEL_0 = 0xAA;
constexpr uint32_t OB_RDP_LEVEL_2 = 0xCC;
HAL_StatusTypeDef HAL_FLASH_Unlock();
HAL_StatusTypeDef HAL_FLASH_Lock();
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t value);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *operation, uint32_t *error);
