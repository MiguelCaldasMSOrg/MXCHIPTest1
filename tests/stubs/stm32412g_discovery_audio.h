#pragma once

#include <stdint.h>

enum HAL_StatusTypeDef {
  HAL_OK,
  HAL_ERROR
};

enum HAL_I2S_StateTypeDef {
  HAL_I2S_STATE_READY,
  HAL_I2S_STATE_BUSY_TX
};

struct TestDmaHandle {
  uint32_t remaining;
};

struct I2S_HandleTypeDef {
  uint16_t TxXferSize;
  TestDmaHandle *hdmatx;
};

constexpr uint8_t AUDIO_OK = 0;
constexpr uint8_t AUDIO_ERROR = 1;
constexpr uint32_t OUTPUT_DEVICE_AUTO = 0;
constexpr uint32_t I2S_DATAFORMAT_16B = 16;
constexpr uint32_t HAL_I2S_ERROR_NONE = 0;
constexpr uint32_t AUDIO_MUTE_OFF = 0;

#define __HAL_DMA_GET_COUNTER(handle) ((handle)->remaining)

extern "C" {
  extern I2S_HandleTypeDef haudio_i2s;
  uint8_t BSP_AUDIO_IN_OUT_Init(uint16_t device, uint32_t bits, uint32_t rate);
  uint8_t BSP_AUDIO_OUT_SetVolume(uint8_t volume);
  uint8_t BSP_AUDIO_OUT_SetMute(uint32_t command);
  HAL_I2S_StateTypeDef HAL_I2S_GetState(I2S_HandleTypeDef *handle);
  uint32_t HAL_I2S_GetError(I2S_HandleTypeDef *handle);
  HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *handle, uint16_t *data, uint16_t words);
  HAL_StatusTypeDef HAL_I2S_DMAStop(I2S_HandleTypeDef *handle);
}
