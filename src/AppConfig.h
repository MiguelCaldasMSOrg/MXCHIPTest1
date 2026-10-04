#pragma once

// 0: ASK RF, 1: audio, 2: LoRa-E5, 3: PCF85063TP RTC, 4: DS1307 RTC.
#ifndef MXCHIP_TEST_MODE
#ifdef MXCHIP_ENABLE_AUDIO_TESTS
#define MXCHIP_TEST_MODE MXCHIP_ENABLE_AUDIO_TESTS
#else
#define MXCHIP_TEST_MODE 3
#endif
#endif

namespace AppConfig {
  enum class Mode {
    AskRadio = 0,
    Audio = 1,
    LoRa = 2,
    HighPrecisionRtc = 3,
    Ds1307Rtc = 4
  };

  static_assert(MXCHIP_TEST_MODE >= 0 && MXCHIP_TEST_MODE <= 4, "MXCHIP_TEST_MODE must be 0, 1, 2, 3, or 4.");
#ifdef MXCHIP_ENABLE_AUDIO_TESTS
  static_assert(MXCHIP_ENABLE_AUDIO_TESTS == 0 || MXCHIP_ENABLE_AUDIO_TESTS == 1, "MXCHIP_ENABLE_AUDIO_TESTS must be 0 or 1.");
  static_assert(MXCHIP_TEST_MODE == MXCHIP_ENABLE_AUDIO_TESTS, "Conflicting legacy audio flag and MXCHIP_TEST_MODE.");
#endif
  constexpr Mode kMode = static_cast<Mode>(MXCHIP_TEST_MODE);
  constexpr bool kAudioEnabled = kMode == Mode::Audio;
  constexpr bool kLoRaEnabled = kMode == Mode::LoRa;
  constexpr bool kDs1307Enabled = kMode == Mode::Ds1307Rtc;
  constexpr bool kRtcEnabled = kMode == Mode::HighPrecisionRtc || kDs1307Enabled;
  constexpr bool kRadioEnabled = kMode == Mode::AskRadio || kMode == Mode::Audio;
  constexpr bool kRadioTransmitEnabled = kMode == Mode::AskRadio;
}
