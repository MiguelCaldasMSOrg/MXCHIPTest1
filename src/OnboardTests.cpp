#include <Arduino.h>
#include "AppConfig.h"
#include "OnboardTests.h"
#include "SerialLineInput.h"

namespace {
  SerialLineInput<14, 1> formatInput;
  bool readingFormat = false;

  void help() {
    Serial.println(F("USB commands: a = Button A, b = Button B, h/? = help."));
    switch (AppConfig::kMode) {
      case AppConfig::Mode::OnboardSensors:
        Serial.println(F("A: next sensor page. B: sample now. Live readings every two seconds; move the board gently to exercise motion axes."));
        break;
      case AppConfig::Mode::Microphone:
        Serial.println(F("A: record one second into RAM. B: play the last recording at limited level. Disconnect Grove peripherals before using the audio codec."));
        break;
      case AppConfig::Mode::FileSystem:
        Serial.println(F("A: create/verify the dedicated test file. B: verify only. c: remove only a fully verified test file. Reset to verify persistence."));
        Serial.println(F("DESTRUCTIVE: the exact line FORMAT FILESYS erases only the QSPI filesystem partition. Never required for a healthy mounted volume."));
        break;
      case AppConfig::Mode::NetworkServices:
        Serial.println(F("A: rerun DNS/TCP/HTTP/NTP/TLS/HTTPS tests. B: help. Only generic public test requests are sent; no Azure account is required."));
        break;
      case AppConfig::Mode::Irda:
        Serial.println(F("A: transmit 55 AA 00 FF 4D 58 43 48 using 38400-baud IrDA. B: help. Optical verification needs an external IrDA receiver/probe; this is not 38-kHz NEC."));
        break;
      case AppConfig::Mode::SecurityChip:
        Serial.println(F("A: rerun STSAFE middleware tests. B: help. Metadata, public-vector ECDSA verification and RNG only; no credential reads, private-key use, writes or personalization."));
        break;
      default:
        break;
    }
  }
}

namespace OnboardTests {
  void begin() {
    if (!AppConfig::kOnboardTestsEnabled) {
      return;
    }
    help();
    switch (AppConfig::kMode) {
      case AppConfig::Mode::OnboardSensors:
        SensorTests::begin();
        break;
      case AppConfig::Mode::Microphone:
        MicrophoneTests::begin();
        break;
      case AppConfig::Mode::FileSystem:
        FileSystemTests::run();
        break;
      case AppConfig::Mode::NetworkServices:
        NetworkTests::run();
        break;
      case AppConfig::Mode::Irda:
        IrdaTests::begin();
        break;
      case AppConfig::Mode::SecurityChip:
        SecurityChipTests::run();
        break;
      default:
        break;
    }
  }

  void update() {
    if (AppConfig::kSensorsEnabled) {
      SensorTests::update();
    } else if (AppConfig::kMicrophoneEnabled) {
      MicrophoneTests::update();
    }
  }

  void buttonA() {
    switch (AppConfig::kMode) {
      case AppConfig::Mode::OnboardSensors:
        SensorTests::nextPage();
        break;
      case AppConfig::Mode::Microphone:
        MicrophoneTests::record();
        break;
      case AppConfig::Mode::FileSystem:
        FileSystemTests::run();
        break;
      case AppConfig::Mode::NetworkServices:
        NetworkTests::run();
        break;
      case AppConfig::Mode::Irda:
        IrdaTests::transmit();
        break;
      case AppConfig::Mode::SecurityChip:
        SecurityChipTests::run();
        break;
      default:
        break;
    }
  }

  void buttonB() {
    if (AppConfig::kSensorsEnabled) {
      SensorTests::sampleNow();
    } else if (AppConfig::kMicrophoneEnabled) {
      MicrophoneTests::play();
    } else if (AppConfig::kFileSystemEnabled) {
      FileSystemTests::verify();
    } else if (AppConfig::kOnboardTestsEnabled) {
      help();
    }
  }

  void handleSerial(char value) {
    if (!AppConfig::kOnboardTestsEnabled) {
      return;
    }
    if (AppConfig::kFileSystemEnabled && (readingFormat || value == 'F')) {
      readingFormat = true;
      using Result = SerialLineInput<14, 1>::Result;
      const Result result = formatInput.push(static_cast<uint8_t>(value));
      if (result == Result::Queued) {
        uint8_t text[14];
        uint8_t length = 0;
        readingFormat = false;
        if (formatInput.read(text, length) && length == 14 && memcmp(text, "FORMAT FILESYS", 14) == 0) {
          FileSystemTests::format();
        } else {
          Serial.println(F("Format rejected: exact confirmation line required; nothing erased."));
        }
      } else if (result != Result::None) {
        readingFormat = false;
        Serial.println(F("Format rejected: invalid/overlong confirmation; nothing erased."));
      }
      return;
    }
    switch (value) {
      case 'a':
        buttonA();
        break;
      case 'b':
        buttonB();
        break;
      case 'h':
      case '?':
        help();
        break;
      case 'c':
        if (AppConfig::kFileSystemEnabled) {
          FileSystemTests::cleanup();
        } else {
          Serial.println(F("Cleanup is available only in filesystem mode."));
        }
        break;
      case '\r':
      case '\n':
      case ' ':
        break;
      default:
        Serial.println(F("Unknown diagnostic command; send h for help."));
        break;
    }
  }
}
