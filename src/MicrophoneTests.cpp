#include <Arduino.h>
#include <AudioClassV2.h>
#include <stm32412g_discovery_audio.h>
#include "AppConfig.h"
#include "DiagnosticChecks.h"
#include "OnboardTests.h"

extern "C" {
  extern I2S_HandleTypeDef haudio_i2s;
}

namespace {
  constexpr uint32_t kSampleRate = 16000;
  constexpr size_t kFrames = kSampleRate;
  constexpr uint16_t kWords = kFrames * 2;
  constexpr uint32_t kTimeoutMs = 2000;
  static_assert(kFrames * 2 <= UINT16_MAX, "Recording must fit one finite DMA transfer.");
  alignas(4) uint16_t captured[kWords];
  alignas(4) uint16_t output[kWords];

  enum class Operation {
    Idle,
    Recording,
    Playing
  };
  Operation operation = Operation::Idle;
  bool ready = false;
  bool haveRecording = false;
  uint32_t startedMs = 0;

  void reportError(const char *message) {
    Screen.print(1, "Microphone FAIL");
    Screen.print(2, "See USB serial");
    Serial.print(F("Microphone FAIL: "));
    Serial.println(message);
  }

  bool stop() {
    const bool stopped = HAL_I2S_DMAStop(&haudio_i2s) == HAL_OK;
    const bool muted = BSP_AUDIO_OUT_SetVolume(0) == AUDIO_OK;
    operation = Operation::Idle;
    if (!stopped || !muted) {
      ready = false;
      reportError("could not stop DMA and mute output; reset required");
      return false;
    }
    return true;
  }

  int16_t sampleAt(size_t index) {
    const int32_t value = captured[index];
    return static_cast<int16_t>(value < 32768 ? value : value - 65536);
  }

  void analyze() {
    bool active = false;
    for (size_t slot = 0; slot < 2; ++slot) {
      DiagnosticChecks::AudioStatistics stats;
      DiagnosticChecks::AudioStatistics window;
      double quietest = 32768.0;
      for (size_t frame = 0; frame < kFrames; ++frame) {
        const int16_t sample = sampleAt(frame * 2 + slot);
        stats.add(sample);
        window.add(sample);
        if (window.count == kSampleRate / 50) {
          const double noise = window.acRms();
          if (noise < quietest) {
            quietest = noise;
          }
          window = DiagnosticChecks::AudioStatistics();
        }
      }
      char text[160];
      snprintf(
        text,
        sizeof(text),
        "Mic I2S slot %u: samples=%lu peak=%lu RMS=%.2f DC=%.2f AC-RMS=%.2f clipped=%lu",
        static_cast<unsigned int>(slot),
        static_cast<unsigned long>(stats.count),
        static_cast<unsigned long>(stats.peak),
        stats.rms(),
        stats.mean(),
        stats.acRms(),
        static_cast<unsigned long>(stats.clipped)
      );
      Serial.println(text);
      snprintf(text, sizeof(text), "Slot %u noise-floor estimate: quietest 20ms AC-RMS=%.2f counts. Use a quiet recording; this is not a calibrated acoustic noise floor.", static_cast<unsigned int>(slot), quietest);
      Serial.println(text);
      active = active || stats.acRms() > 1.0;
      if (stats.clipped != 0) {
        Serial.println(F("WARNING: input clipping; reduce sound level. Recording is retained for inspection."));
      }
    }
    if (!active) {
      reportError("constant/silent input; speak near the microphone and record again");
    } else {
      Screen.print(1, "Capture complete");
      Screen.print(2, "See RMS on USB");
      Serial.println(F("PASS: one-second DMA capture with varying input. RMS is in ADC counts; noise-floor and acoustic accuracy require a controlled quiet/reference recording."));
    }
    Screen.print(3, "A:rec B:play");
  }

  bool canStart() {
    if (!ready) {
      reportError("codec is not ready; reset to retry initialization");
      return false;
    }
    if (operation != Operation::Idle) {
      Serial.println(F("Microphone request rejected: a DMA operation is already active."));
      return false;
    }
    return true;
  }
}

namespace MicrophoneTests {
  void begin() {
    if (!AppConfig::kMicrophoneEnabled) {
      return;
    }
    if (operation != Operation::Idle && !stop()) {
      return;
    }
    ready = haveRecording = false;
    Screen.print(0, "Microphone test");
    if (BSP_AUDIO_IN_OUT_Init(OUTPUT_DEVICE_AUTO, I2S_DATAFORMAT_16B, kSampleRate) != AUDIO_OK || HAL_I2S_GetState(&haudio_i2s) != HAL_I2S_STATE_READY || HAL_I2S_GetError(&haudio_i2s) != HAL_I2S_ERROR_NONE || haudio_i2s.hdmarx == nullptr || haudio_i2s.hdmatx == nullptr) {
      reportError("codec/I2S initialization");
      return;
    }
    if (BSP_AUDIO_OUT_SetVolume(0) != AUDIO_OK) {
      reportError("initial output mute");
      return;
    }
    ready = true;
    Screen.print(1, "16kHz 16-bit");
    Screen.print(2, "Recording in RAM");
    Screen.print(3, "A:rec B:play");
    Serial.println(F("Microphone ready. The NAU88C10 is mono; two I2S slots are measured, not two independent microphones. No recording is saved or uploaded."));
  }

  void record() {
    if (!AppConfig::kMicrophoneEnabled || !canStart()) {
      return;
    }
    haveRecording = false;
    memset(captured, 0, sizeof(captured));
    memset(output, 0, sizeof(output));
    if (BSP_AUDIO_OUT_SetVolume(0) != AUDIO_OK || HAL_I2SEx_TransmitReceive_DMA(&haudio_i2s, output, captured, kWords) != HAL_OK) {
      stop();
      reportError("record DMA did not start");
      return;
    }
    operation = Operation::Recording;
    startedMs = millis();
    Screen.print(1, "Recording 1 sec");
    Serial.println(F("Microphone recording: 16000 frames / 32000 I2S words; output is silent."));
  }

  void play() {
    if (!AppConfig::kMicrophoneEnabled || !canStart()) {
      return;
    }
    if (!haveRecording) {
      reportError("record a complete sample before playback");
      return;
    }
    uint32_t peak = 0;
    for (size_t index = 0; index < kWords; ++index) {
      const int32_t value = sampleAt(index);
      const uint32_t magnitude = static_cast<uint32_t>(value < 0 ? -value : value);
      if (magnitude > peak) {
        peak = magnitude;
      }
    }
    for (size_t frame = 0; frame < kFrames; ++frame) {
      const size_t remaining = kFrames - 1 - frame;
      const size_t edge = frame < remaining ? frame : remaining;
      for (size_t slot = 0; slot < 2; ++slot) {
        int32_t value = sampleAt(frame * 2 + slot);
        if (peak > 3000) {
          value = value * 3000 / static_cast<int32_t>(peak);
        }
        if (edge < 160) {
          value = value * static_cast<int32_t>(edge) / 160;
        }
        output[frame * 2 + slot] = static_cast<uint16_t>(static_cast<int16_t>(value));
      }
    }
    if (BSP_AUDIO_OUT_SetVolume(25) != AUDIO_OK || BSP_AUDIO_OUT_SetMute(AUDIO_MUTE_OFF) != AUDIO_OK || HAL_I2S_Transmit_DMA(&haudio_i2s, output, kWords) != HAL_OK) {
      stop();
      reportError("playback DMA did not start");
      return;
    }
    operation = Operation::Playing;
    startedMs = millis();
    Screen.print(1, "Playing 1 sec");
    Serial.println(F("Playing the retained recording at 25/100 volume, PCM peak limited to 3000."));
  }

  void update() {
    if (!AppConfig::kMicrophoneEnabled || operation == Operation::Idle) {
      return;
    }
    if (HAL_I2S_GetError(&haudio_i2s) != HAL_I2S_ERROR_NONE) {
      stop();
      reportError("I2S/DMA transfer error");
      return;
    }
    if (static_cast<uint32_t>(millis()) - startedMs >= kTimeoutMs) {
      stop();
      reportError("two-second DMA timeout; partial capture discarded");
      return;
    }
    const bool recording = operation == Operation::Recording;
    if (__HAL_DMA_GET_COUNTER(haudio_i2s.hdmatx) != 0 || (recording && __HAL_DMA_GET_COUNTER(haudio_i2s.hdmarx) != 0) || HAL_I2S_GetState(&haudio_i2s) != HAL_I2S_STATE_READY) {
      return;
    }
    if (!stop()) {
      return;
    }
    if (recording) {
      haveRecording = true;
      analyze();
    } else {
      Screen.print(1, "Playback done");
      Serial.println(F("Microphone playback complete; output muted."));
    }
  }
}
