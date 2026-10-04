#include <Arduino.h>
#include <AudioClassV2.h>
#include <stm32412g_discovery_audio.h>
#include "AppConfig.h"
#include "AudioTests.h"
#include "generated/audio_test_sample.h"

extern "C" {
  extern I2S_HandleTypeDef haudio_i2s;
}

namespace {
  enum class ClipKind {
    Sine,
    Triangle,
    SoftSquare,
    Melody,
    Speech
  };

  struct Clip {
    ClipKind kind;
    const char *name;
  };

  constexpr unsigned int kSampleRate = 16000;
  constexpr size_t kToneFrames = kSampleRate;
  constexpr size_t kMelodyFrames = kSampleRate * 3 / 2;
  constexpr size_t kMaxFrames = kMelodyFrames;
  constexpr unsigned long kTimeoutMs = 2000;
  constexpr int kFadeFrames = kSampleRate / 100;
  constexpr uint32_t kTonePhaseStep = static_cast<uint32_t>(440.0 * 4294967296.0 / kSampleRate);
  constexpr uint8_t kVolumes[] = {25, 50, 75};
  constexpr size_t kVolumeCount = sizeof(kVolumes) / sizeof(kVolumes[0]);
  constexpr Clip kClips[] = {{ClipKind::Sine, "Sine"}, {ClipKind::Triangle, "Triangle"}, {ClipKind::SoftSquare, "Soft square"}, {ClipKind::Melody, "Melody"}, {ClipKind::Speech, "Speech"}};
  constexpr size_t kClipCount = sizeof(kClips) / sizeof(kClips[0]);
  constexpr uint32_t kMelodyPhaseSteps[] = {static_cast<uint32_t>(261.63 * 4294967296.0 / kSampleRate), static_cast<uint32_t>(329.63 * 4294967296.0 / kSampleRate), static_cast<uint32_t>(392.0 * 4294967296.0 / kSampleRate), static_cast<uint32_t>(523.25 * 4294967296.0 / kSampleRate)};
  static_assert(kSpeechSampleRate == kSampleRate, "Speech sample rate must match playback.");
  static_assert(kSpeechSampleCount > 0 && kSpeechSampleCount <= kMaxFrames, "Speech must fit the playback buffer.");
  static_assert(kMaxFrames * 2 <= 0xFFFF, "Audio must fit a single finite DMA transfer.");

  alignas(4) uint16_t samples[kMaxFrames * 2];
  size_t nextStep = 0;
  size_t currentClip = 0;
  unsigned long startedMs = 0;
  unsigned long expectedMs = 0;
  bool ready = false;
  bool playing = false;

  void reportError(const char *message) {
    Serial.print("Audio error: ");
    Serial.println(message);
    Screen.print(1, "Audio error");
  }

  int32_t applyFade(int32_t sample, size_t frame, size_t count) {
    const size_t remaining = count - 1 - frame;
    const size_t edge = frame < remaining ? frame : remaining;
    if (edge < kFadeFrames) {
      return sample * static_cast<int32_t>(edge) / kFadeFrames;
    }
    return sample;
  }

  int32_t sampleWaveform(size_t waveform, uint32_t phase) {
    const size_t index = phase >> 24;
    const int32_t fraction = (phase >> 8) & 0xFFFF;
    const int32_t first = kAudioWaveforms[waveform][index];
    const int32_t next = kAudioWaveforms[waveform][(index + 1) & 0xFF];
    return first + (next - first) * fraction / 65536;
  }

  size_t prepare(ClipKind kind) {
    size_t frames = kToneFrames;
    if (kind == ClipKind::Melody) {
      frames = kMelodyFrames;
    } else if (kind == ClipKind::Speech && kSpeechSampleCount > frames) {
      frames = kSpeechSampleCount;
    }

    const size_t waveform = kind == ClipKind::Triangle ? 1 : (kind == ClipKind::SoftSquare ? 2 : 0);
    uint32_t phase = 0;
    uint32_t phaseStep = kTonePhaseStep;
    for (size_t frame = 0; frame < frames; frame++) {
      int32_t value = 0;
      size_t localFrame = frame;
      size_t localCount = frames;
      if (kind == ClipKind::Speech) {
        if (frame < kSpeechSampleCount) {
          value = kSpeechSamples[frame];
          localCount = kSpeechSampleCount;
        }
      } else {
        if (kind == ClipKind::Melody) {
          localCount = kMelodyFrames / 4;
          localFrame = frame % localCount;
          if (localFrame == 0) {
            phase = 0;
            phaseStep = kMelodyPhaseSteps[frame / localCount];
          }
        }
        value = sampleWaveform(waveform, phase);
        phase += phaseStep;
      }

      const int16_t sample = static_cast<int16_t>(applyFade(value, localFrame, localCount));
      // The mono codec uses two I2S slots; send the same signal in both.
      samples[frame * 2] = static_cast<uint16_t>(sample);
      samples[frame * 2 + 1] = static_cast<uint16_t>(sample);
    }
    return frames;
  }

  bool stopPlayback() {
    const HAL_StatusTypeDef stopped = HAL_I2S_DMAStop(&haudio_i2s);
    const uint8_t muted = BSP_AUDIO_OUT_SetVolume(0);
    playing = false;
    if (stopped != HAL_OK || muted != AUDIO_OK) {
      ready = false;
      reportError("could not stop and mute playback");
      return false;
    }
    return true;
  }
}

namespace AudioTests {
  bool begin() {
    if (playing && !stopPlayback()) {
      return false;
    }
    ready = false;
    if (!AppConfig::kAudioEnabled) {
      Serial.println("Audio suspended in this firmware mode; select mode 1 to enable it.");
      return false;
    }
    // The BSP exposes initialization errors hidden by the higher-level wrapper.
    if (BSP_AUDIO_IN_OUT_Init(OUTPUT_DEVICE_AUTO, I2S_DATAFORMAT_16B, kSampleRate) != AUDIO_OK) {
      reportError("codec initialization failed");
      return false;
    }
    if (HAL_I2S_GetState(&haudio_i2s) != HAL_I2S_STATE_READY || HAL_I2S_GetError(&haudio_i2s) != HAL_I2S_ERROR_NONE) {
      reportError("I2S initialization failed");
      return false;
    }
    if (BSP_AUDIO_OUT_SetVolume(0) != AUDIO_OK) {
      reportError("initial mute failed");
      return false;
    }
    ready = true;
    Screen.print(1, "A:RGB B:Audio");
    Serial.println("Audio ready: 16 kHz, 16-bit mono signal; output muted until button B.");
    return true;
  }

  void advance() {
    if (!AppConfig::kAudioEnabled || !ready) {
      reportError("output is unavailable; check the build mode and reset to retry initialization");
      return;
    }
    if (playing) {
      Serial.println("Button B ignored: audio is already playing.");
      return;
    }

    const size_t index = nextStep / kVolumeCount;
    const Clip &clip = kClips[index];
    const uint8_t volume = kVolumes[nextStep % kVolumeCount];
    const unsigned long prepareStartedMs = millis();
    const size_t frames = prepare(clip.kind);
    const unsigned long prepareMs = millis() - prepareStartedMs;
    if (BSP_AUDIO_OUT_SetVolume(volume) != AUDIO_OK) {
      stopPlayback();
      reportError("could not set playback volume");
      return;
    }

    // The BSP playback helper truncates at 4095 words; the HAL accepts a finite 16-bit count.
    const uint16_t words = static_cast<uint16_t>(frames * 2);
    if (BSP_AUDIO_OUT_SetMute(AUDIO_MUTE_OFF) != AUDIO_OK || HAL_I2S_Transmit_DMA(&haudio_i2s, samples, words) != HAL_OK || HAL_I2S_GetState(&haudio_i2s) != HAL_I2S_STATE_BUSY_TX || HAL_I2S_GetError(&haudio_i2s) != HAL_I2S_ERROR_NONE) {
      stopPlayback();
      reportError("DMA playback did not start");
      return;
    }

    currentClip = index;
    startedMs = millis();
    expectedMs = frames * 1000 / kSampleRate;
    playing = true;
    nextStep = (nextStep + 1) % (kClipCount * kVolumeCount);

    char status[96];
    snprintf(status, sizeof(status), "Audio preparation: %lu ms; DMA words: %u, remaining: %lu", prepareMs, static_cast<unsigned int>(haudio_i2s.TxXferSize), static_cast<unsigned long>(__HAL_DMA_GET_COUNTER(haudio_i2s.hdmatx)));
    Serial.println(status);
    snprintf(status, sizeof(status), "Button B: %s, volume %u/100, expected %lu ms", clip.name, static_cast<unsigned int>(volume), expectedMs);
    Serial.println(status);
    snprintf(status, sizeof(status), "%s v%u", clip.name, static_cast<unsigned int>(volume));
    Screen.print(1, status);
  }

  void update() {
    if (!AppConfig::kAudioEnabled || !playing) {
      return;
    }
    const unsigned long elapsed = millis() - startedMs;
    if (HAL_I2S_GetError(&haudio_i2s) != HAL_I2S_ERROR_NONE) {
      stopPlayback();
      reportError("I2S/DMA transfer failed");
      return;
    }
    if (elapsed >= kTimeoutMs) {
      stopPlayback();
      reportError("two-second playback limit reached");
      return;
    }
    if (HAL_I2S_GetState(&haudio_i2s) != HAL_I2S_STATE_READY || !stopPlayback()) {
      return;
    }
    if (elapsed + 100 < expectedMs || elapsed > expectedMs + 150) {
      char status[80];
      snprintf(status, sizeof(status), "duration mismatch: expected %lu ms, observed %lu ms", expectedMs, elapsed);
      reportError(status);
      return;
    }
    char status[80];
    snprintf(status, sizeof(status), "Audio finished: %s after %lu ms; output muted.", kClips[currentClip].name, elapsed);
    Serial.println(status);
  }
}
