#pragma once

// P2 is shared by RF TX and the audio codec's I2C bus.
#ifndef MXCHIP_ENABLE_AUDIO_TESTS
#define MXCHIP_ENABLE_AUDIO_TESTS 0
#endif

namespace AppConfig {
  static_assert(MXCHIP_ENABLE_AUDIO_TESTS == 0 || MXCHIP_ENABLE_AUDIO_TESTS == 1, "MXCHIP_ENABLE_AUDIO_TESTS must be 0 or 1.");
  constexpr bool kAudioEnabled = MXCHIP_ENABLE_AUDIO_TESTS != 0;
  constexpr bool kRadioTransmitEnabled = !kAudioEnabled;
}
