#pragma once

#include <stdint.h>

extern "C" {
  uint8_t Init_HAL(uint8_t address, void **handle);
  uint8_t Free_HAL(void *handle);
  uint8_t HAL_Get_Data_Zone(void *handle, uint8_t zone, uint16_t size, uint8_t *buffer, uint16_t offset);
}
