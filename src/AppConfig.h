#pragma once

// 0: ASK RF, 1: audio, 2: two Grove LoRa-E5 modules. Their pin assignments overlap.
#ifndef MXCHIP_TEST_MODE
#ifdef MXCHIP_ENABLE_AUDIO_TESTS
#define MXCHIP_TEST_MODE MXCHIP_ENABLE_AUDIO_TESTS
#else
#define MXCHIP_TEST_MODE 2
#endif
#endif

namespace AppConfig {
  enum class Mode {
    AskRadio = 0,
    Audio = 1,
    LoRa = 2
  };

  static_assert(MXCHIP_TEST_MODE >= 0 && MXCHIP_TEST_MODE <= 2, "MXCHIP_TEST_MODE must be 0, 1, or 2.");
#ifdef MXCHIP_ENABLE_AUDIO_TESTS
  static_assert(MXCHIP_ENABLE_AUDIO_TESTS == 0 || MXCHIP_ENABLE_AUDIO_TESTS == 1, "MXCHIP_ENABLE_AUDIO_TESTS must be 0 or 1.");
  static_assert(MXCHIP_TEST_MODE == MXCHIP_ENABLE_AUDIO_TESTS, "Conflicting legacy audio flag and MXCHIP_TEST_MODE.");
#endif
  constexpr Mode kMode = static_cast<Mode>(MXCHIP_TEST_MODE);
  constexpr bool kAudioEnabled = kMode == Mode::Audio;
  constexpr bool kLoRaEnabled = kMode == Mode::LoRa;
  constexpr bool kRadioEnabled = !kLoRaEnabled;
  constexpr bool kRadioTransmitEnabled = kMode == Mode::AskRadio;
}
