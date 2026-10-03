#include <Arduino.h>
#include <stm32412g_discovery_audio.h>
#include "AppConfig.h"
#include "AudioTests.h"
#include "RadioBridge.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace FakeHardware {
  uint64_t nowUs = 0;
  bool loopback = true;
  bool outputLevel = false;
  unsigned int outputConstructions = 0;
  unsigned int outputWrites = 0;
  void (*timerCallback)() = nullptr;
  unsigned int timerPeriodUs = 0;
}

TestSerial Serial;
TestScreen Screen;

namespace {
  unsigned int audioInitializations = 0;
  unsigned int audioTransfers = 0;
  unsigned int audioStops = 0;
  uint8_t volume = 0;
  uint32_t audioError = 0;
  bool failInitialization = false;
  bool failTransfer = false;
  HAL_I2S_StateTypeDef audioState = HAL_I2S_STATE_READY;
  TestDmaHandle dma = {};
  std::vector<uint16_t> playedSamples;

  void check(bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      std::exit(1);
    }
  }

  void sendSerial(const std::string &text) {
    Serial.input.insert(Serial.input.end(), text.begin(), text.end());
  }

  void runRadio(unsigned int milliseconds) {
    for (unsigned int tick = 0; tick < milliseconds * 20; tick++) {
      check(FakeHardware::timerCallback != nullptr, "radio timer is installed");
      FakeHardware::timerCallback();
      FakeHardware::nowUs += 50;
      if (tick % 20 == 0) {
        RadioBridge::update();
      }
    }
    RadioBridge::update();
  }

  void testRadioMode() {
    check(!AudioTests::begin() && audioInitializations == 0, "RF mode must not initialize audio hardware");
    Serial.output.clear();
    RadioBridge::begin();
    check(FakeHardware::outputConstructions == 1 && !FakeHardware::outputLevel, "RF mode initializes TX low");
    check(FakeHardware::timerPeriodUs == 50, "RF sampling period is 50 microseconds");

    FakeHardware::loopback = false;
    sendSerial("no local echo\n");
    runRadio(1000);
    check(Serial.output.find("RF transmitting: no local echo") != std::string::npos, "serial input initiates transmission");
    check(Serial.output.find("RF received: no local echo") == std::string::npos, "TX must not masquerade as reception");
    check(Screen.lines[0] == "RF: waiting", "without reception the OLED must not echo serial input");

    Serial.output.clear();
    FakeHardware::loopback = true;
    sendSerial("hello\r\n");
    runRadio(1000);
    check(Serial.output.find("RF received: hello") != std::string::npos, "physical-input samples drive received messages");
    check(Screen.lines[0] == "RF: hello       ", "short messages are padded to the display width");

    const std::string longText = "abcdefghijklmnopqrst";
    sendSerial(longText + "\n");
    runRadio(1500);
    check(Screen.lines[0] != "RF: abcdefghijkl", "long messages scroll");
    sendSerial(longText + "\n");
    runRadio(1000);
    check(Screen.lines[0] != "RF: abcdefghijkl", "duplicate messages do not reset scrolling");
    check(!Screen.invalidWrite, "all OLED writes fit a valid row");
    check(!FakeHardware::outputLevel, "transmitter returns low after packets");
    std::cout << "PASS: RF-mode pin ownership, no local echo, received display, scrolling, and duplicate handling\n";
  }

  void testAudioMode() {
    RadioBridge::begin();
    sendSerial("must not transmit\n");
    runRadio(1000);
    check(FakeHardware::outputConstructions == 0 && FakeHardware::outputWrites == 0, "audio mode must never configure or drive the TX pin");
    check(Serial.output.find("RF TX disabled by audio mode") != std::string::npos, "disabled TX reports a rejected line");
    check(Serial.output.find("RF TX queued.") == std::string::npos, "disabled TX must not claim a line was queued");
    check(Serial.output.find("RF transmitting:") == std::string::npos, "audio mode cannot transmit");
    check(AudioTests::begin() && audioInitializations == 1 && volume == 0, "audio initializes muted");

    const unsigned int durations[] = {1000, 1000, 1000, 1500, 1345};
    const uint8_t volumes[] = {25, 50, 75};
    for (unsigned int step = 0; step < 15; step++) {
      const unsigned int expectedMs = durations[step / 3];
      const unsigned int before = audioTransfers;
      AudioTests::advance();
      check(audioTransfers == before + 1 && volume == volumes[step % 3], "audio clip advances through all volume settings");
      check(playedSamples.size() == expectedMs * 32, "audio DMA receives every intended I2S word");
      check(playedSamples.front() == 0 && playedSamples.back() == 0, "clip edges fade to silence");
      bool nonzero = false;
      for (size_t i = 0; i < playedSamples.size(); i += 2) {
        check(playedSamples[i] == playedSamples[i + 1], "mono signal is duplicated into both I2S slots");
        const int32_t sample = playedSamples[i] < 32768 ? playedSamples[i] : static_cast<int32_t>(playedSamples[i]) - 65536;
        check(sample >= -3000 && sample <= 3000, "PCM amplitude remains bounded");
        nonzero = nonzero || sample != 0;
      }
      check(nonzero, "each clip contains audio data");
      AudioTests::advance();
      check(audioTransfers == before + 1, "busy button presses cannot queue or restart audio");
      FakeHardware::nowUs += expectedMs * 1000;
      audioState = HAL_I2S_STATE_READY;
      AudioTests::update();
      check(volume == 0, "normal completion mutes the output");
    }

    AudioTests::advance();
    const unsigned int beforeStop = audioStops;
    FakeHardware::nowUs += 1999000;
    AudioTests::update();
    check(audioStops == beforeStop, "the timeout does not fire before two seconds");
    FakeHardware::nowUs += 1000;
    AudioTests::update();
    check(audioStops == beforeStop + 1 && volume == 0, "two-second timeout stops and mutes");

    failTransfer = true;
    AudioTests::advance();
    check(volume == 0 && audioState == HAL_I2S_STATE_READY, "failed DMA start is stopped and muted");
    failTransfer = false;
    failInitialization = true;
    check(!AudioTests::begin(), "initialization failures are surfaced");
    const unsigned int beforeFailure = audioTransfers;
    AudioTests::advance();
    check(audioTransfers == beforeFailure, "failed reinitialization cannot leave audio marked ready");
    check(!Screen.invalidWrite, "audio status text fits the OLED");
    check(FakeHardware::outputWrites == 0, "audio playback never drives the RF TX pin");
    std::cout << "PASS: audio-mode pin exclusion, all 15 clips, PCM bounds, mute, busy behavior, timeout boundary, and failures\n";
  }
}

extern "C" {
  I2S_HandleTypeDef haudio_i2s = {0, &dma};

  uint8_t BSP_AUDIO_IN_OUT_Init(uint16_t, uint32_t bits, uint32_t rate) {
    audioInitializations++;
    check(bits == 16 && rate == 16000, "audio format is configured explicitly");
    audioState = HAL_I2S_STATE_READY;
    audioError = 0;
    return failInitialization ? AUDIO_ERROR : AUDIO_OK;
  }

  uint8_t BSP_AUDIO_OUT_SetVolume(uint8_t value) {
    volume = value;
    return AUDIO_OK;
  }

  uint8_t BSP_AUDIO_OUT_SetMute(uint32_t) {
    return AUDIO_OK;
  }

  HAL_I2S_StateTypeDef HAL_I2S_GetState(I2S_HandleTypeDef *) {
    return audioState;
  }

  uint32_t HAL_I2S_GetError(I2S_HandleTypeDef *) {
    return audioError;
  }

  HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *handle, uint16_t *data, uint16_t words) {
    if (failTransfer) {
      return HAL_ERROR;
    }
    audioTransfers++;
    handle->TxXferSize = words;
    dma.remaining = words;
    playedSamples.assign(data, data + words);
    audioState = HAL_I2S_STATE_BUSY_TX;
    audioError = 0;
    return HAL_OK;
  }

  HAL_StatusTypeDef HAL_I2S_DMAStop(I2S_HandleTypeDef *) {
    audioStops++;
    audioState = HAL_I2S_STATE_READY;
    dma.remaining = 0;
    return HAL_OK;
  }
}

int main() {
  if (AppConfig::kAudioEnabled) {
    testAudioMode();
  } else {
    testRadioMode();
  }
}
