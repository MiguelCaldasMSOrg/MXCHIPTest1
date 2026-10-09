#pragma once

// 0: ASK RF, 1: audio, 2: LoRa-E5, 3: PCF85063TP RTC, 4: DS1307 RTC,
// 5: saved Wi-Fi, 6: Grove 1.12-inch OLED v2.0,
// 7: Grove 1.54-inch triple-color e-ink v1.0, 8: Wi-Fi provisioning.
// 9: onboard sensors, 10: microphone, 11: filesystem, 12: network services,
// 13: IrDA transmitter, 14: non-destructive STSAFE middleware diagnostics.
// 15: explicitly confirmed, user-supplied STSAFE host keys only.
// 16: Grove NFC v1.1 factory UART, tag reads and confirmed spare-tag write tests.
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
    Ds1307Rtc = 4,
    WiFi = 5,
    GroveOled = 6,
    GroveEInk = 7,
    WiFiProvisioning = 8,
    OnboardSensors = 9,
    Microphone = 10,
    FileSystem = 11,
    NetworkServices = 12,
    Irda = 13,
    SecurityChip = 14,
    SecureProvisioning = 15,
    GroveNfc = 16
  };

  static_assert(MXCHIP_TEST_MODE >= 0 && MXCHIP_TEST_MODE <= 16, "MXCHIP_TEST_MODE must be from 0 through 16.");
#ifdef MXCHIP_ENABLE_AUDIO_TESTS
  static_assert(MXCHIP_ENABLE_AUDIO_TESTS == 0 || MXCHIP_ENABLE_AUDIO_TESTS == 1, "MXCHIP_ENABLE_AUDIO_TESTS must be 0 or 1.");
  static_assert(MXCHIP_TEST_MODE == MXCHIP_ENABLE_AUDIO_TESTS, "Conflicting legacy audio flag and MXCHIP_TEST_MODE.");
#endif
  constexpr Mode kMode = static_cast<Mode>(MXCHIP_TEST_MODE);
  constexpr bool kAudioEnabled = kMode == Mode::Audio;
  constexpr bool kLoRaEnabled = kMode == Mode::LoRa;
  constexpr bool kDs1307Enabled = kMode == Mode::Ds1307Rtc;
  constexpr bool kRtcEnabled = kMode == Mode::HighPrecisionRtc || kDs1307Enabled;
  constexpr bool kWiFiEnabled = kMode == Mode::WiFi;
  constexpr bool kGroveOledEnabled = kMode == Mode::GroveOled;
  constexpr bool kGroveEInkEnabled = kMode == Mode::GroveEInk;
  constexpr bool kWiFiProvisioningEnabled = kMode == Mode::WiFiProvisioning;
  constexpr bool kSensorsEnabled = kMode == Mode::OnboardSensors;
  constexpr bool kMicrophoneEnabled = kMode == Mode::Microphone;
  constexpr bool kFileSystemEnabled = kMode == Mode::FileSystem;
  constexpr bool kNetworkServicesEnabled = kMode == Mode::NetworkServices;
  constexpr bool kIrdaEnabled = kMode == Mode::Irda;
  constexpr bool kSecurityChipEnabled = kMode == Mode::SecurityChip;
  constexpr bool kSecureProvisioningEnabled = kMode == Mode::SecureProvisioning;
  constexpr bool kNfcEnabled = kMode == Mode::GroveNfc;
  constexpr bool kOnboardTestsEnabled = kSensorsEnabled || kMicrophoneEnabled || kFileSystemEnabled || kNetworkServicesEnabled || kIrdaEnabled || kSecurityChipEnabled;
  constexpr bool kRadioEnabled = kMode == Mode::AskRadio || kMode == Mode::Audio;
  constexpr bool kRadioTransmitEnabled = kMode == Mode::AskRadio;
}
