#include "stubs/DiagnosticHardware.h"
#include <stm32412g_discovery_audio.h>
#include "AppConfig.h"
#include "OnboardTests.h"
#include <vector>

namespace {
  unsigned int initializations = 0;
  unsigned int captures = 0;
  unsigned int plays = 0;
  unsigned int stops = 0;
  uint8_t volume = 0;
  uint32_t errorCode = 0;
  bool failInit = false;
  bool failDma = false;
  HAL_I2S_StateTypeDef state = HAL_I2S_STATE_READY;
  TestDmaHandle tx = {};
  TestDmaHandle rx = {};
  uint16_t *recording = nullptr;
  std::vector<uint16_t> played;

  void complete() {
    tx.remaining = rx.remaining = 0;
    state = HAL_I2S_STATE_READY;
    FakeHardware::nowUs += 1000000;
    MicrophoneTests::update();
  }
}

extern "C" {
  I2S_HandleTypeDef haudio_i2s(0, &tx, &rx);
  uint8_t BSP_AUDIO_IN_OUT_Init(uint16_t, uint32_t bits, uint32_t rate) {
    ++initializations;
    check(bits == 16 && rate == 16000, "microphone format");
    state = HAL_I2S_STATE_READY;
    errorCode = 0;
    return failInit ? AUDIO_ERROR : AUDIO_OK;
  }
  uint8_t BSP_AUDIO_OUT_SetVolume(uint8_t value) {
    volume = value;
    return AUDIO_OK;
  }
  uint8_t BSP_AUDIO_OUT_SetMute(uint32_t) {
    return AUDIO_OK;
  }
  HAL_I2S_StateTypeDef HAL_I2S_GetState(I2S_HandleTypeDef *) {
    return state;
  }
  uint32_t HAL_I2S_GetError(I2S_HandleTypeDef *) {
    return errorCode;
  }
  HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *, uint16_t *data, uint16_t words) {
    ++plays;
    played.assign(data, data + words);
    tx.remaining = words;
    state = HAL_I2S_STATE_BUSY_TX;
    return failDma ? HAL_ERROR : HAL_OK;
  }
  HAL_StatusTypeDef HAL_I2SEx_TransmitReceive_DMA(I2S_HandleTypeDef *, uint16_t *output, uint16_t *input, uint16_t words) {
    check(words == 32000, "one second is exactly 32000 interleaved I2S words");
    for (size_t index = 0; index < words; ++index) {
      check(output[index] == 0, "recording outputs only silence");
    }
    ++captures;
    recording = input;
    tx.remaining = rx.remaining = words;
    state = HAL_I2S_STATE_BUSY_TX_RX;
    return failDma ? HAL_ERROR : HAL_OK;
  }
  HAL_StatusTypeDef HAL_I2S_DMAStop(I2S_HandleTypeDef *) {
    ++stops;
    tx.remaining = rx.remaining = 0;
    state = HAL_I2S_STATE_READY;
    return HAL_OK;
  }
}

int main() {
  MicrophoneTests::begin();
  if (!AppConfig::kMicrophoneEnabled) {
    MicrophoneTests::record();
    MicrophoneTests::play();
    MicrophoneTests::update();
    check(initializations == 0 && captures == 0 && plays == 0, "inactive microphone mode never touches the codec");
    std::cout << "PASS: microphone mode exclusion\n";
    return 0;
  }
  check(initializations == 1 && volume == 0 && captures == 0, "startup muted; recording needs an explicit request");
  MicrophoneTests::play();
  check(plays == 0, "no playback without a completed recording");
  MicrophoneTests::record();
  MicrophoneTests::record();
  MicrophoneTests::play();
  check(captures == 1 && plays == 0, "busy operation rejection");
  for (size_t frame = 0; frame < 16000; ++frame) {
    recording[frame * 2] = static_cast<uint16_t>(frame % 2 == 0 ? -32768 : 32767);
    recording[frame * 2 + 1] = 0;
  }
  tx.remaining = 0;
  MicrophoneTests::update();
  check(stops == 0, "TX completion alone must not finish RX capture");
  complete();
  check(Serial.output.find("samples=16000 peak=32768") != std::string::npos && Serial.output.find("clipped=16000") != std::string::npos, "sample count, negative full-scale and clipping diagnostics");
  check(Serial.output.find("quietest 20ms AC-RMS=32767.50") != std::string::npos && Serial.output.find("quietest 20ms AC-RMS=0.00") != std::string::npos, "noise-floor estimate distinguishes an active slot from a silent one");
  MicrophoneTests::play();
  check(plays == 1 && volume == 25 && played.size() == 32000, "bounded full-length playback");
  for (uint16_t value: played) {
    const int sample = value < 32768 ? value : static_cast<int>(value) - 65536;
    check(sample >= -3000 && sample <= 3000, "playback PCM is limited");
  }
  check(played.front() == 0 && played[played.size() - 2] == 0, "playback fade edges");
  complete();
  check(volume == 0, "playback completion mutes");
  MicrophoneTests::record();
  complete();
  check(Serial.output.find("constant/silent input") != std::string::npos, "all-zero input is not a signal pass");
  MicrophoneTests::record();
  const unsigned int previousStops = stops;
  FakeHardware::nowUs += 1999000;
  MicrophoneTests::update();
  check(stops == previousStops, "timeout does not fire before two seconds");
  FakeHardware::nowUs += 1000;
  MicrophoneTests::update();
  check(stops == previousStops + 1 && volume == 0, "timeout stops and mutes");
  const unsigned int previousPlays = plays;
  MicrophoneTests::play();
  check(plays == previousPlays, "partial capture cannot be played");
  failDma = true;
  MicrophoneTests::record();
  check(volume == 0 && Serial.output.find("record DMA did not start") != std::string::npos, "DMA-start failure");
  failDma = false;
  MicrophoneTests::record();
  errorCode = 1;
  MicrophoneTests::update();
  check(Serial.output.find("I2S/DMA transfer error") != std::string::npos, "DMA transfer failure");
  failInit = true;
  MicrophoneTests::begin();
  const unsigned int previousCaptures = captures;
  MicrophoneTests::record();
  check(captures == previousCaptures && !Screen.invalidWrite, "failed initialization and valid OLED rows");
  std::cout << "PASS: explicit RAM recording, exact DMA size, RX completion, peak/RMS/clipping, bounded playback, busy, timeout and failure handling\n";
}
