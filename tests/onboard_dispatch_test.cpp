#include "stubs/DiagnosticHardware.h"
#include "AppConfig.h"
#include "OnboardTests.h"

namespace {
  unsigned int hardwareCalls = 0;
  unsigned int formats = 0;

  void called(AppConfig::Mode expected) {
    check(AppConfig::kMode == expected, "dispatch cannot invoke an inactive mode's hardware");
    ++hardwareCalls;
  }
}

namespace SensorTests {
  void begin() {
    called(AppConfig::Mode::OnboardSensors);
  }
  void update() {
    called(AppConfig::Mode::OnboardSensors);
  }
  void nextPage() {
    called(AppConfig::Mode::OnboardSensors);
  }
  void sampleNow() {
    called(AppConfig::Mode::OnboardSensors);
  }
}
namespace MicrophoneTests {
  void begin() {
    called(AppConfig::Mode::Microphone);
  }
  void update() {
    called(AppConfig::Mode::Microphone);
  }
  void record() {
    called(AppConfig::Mode::Microphone);
  }
  void play() {
    called(AppConfig::Mode::Microphone);
  }
}
namespace FileSystemTests {
  void run() {
    called(AppConfig::Mode::FileSystem);
  }
  void verify() {
    called(AppConfig::Mode::FileSystem);
  }
  void cleanup() {
    called(AppConfig::Mode::FileSystem);
  }
  void format() {
    called(AppConfig::Mode::FileSystem);
    ++formats;
  }
}
namespace NetworkTests {
  void run() {
    called(AppConfig::Mode::NetworkServices);
  }
}
namespace IrdaTests {
  void begin() {
    called(AppConfig::Mode::Irda);
  }
  void transmit() {
    called(AppConfig::Mode::Irda);
  }
}
namespace SecurityChipTests {
  void run() {
    called(AppConfig::Mode::SecurityChip);
  }
}

int main() {
  OnboardTests::begin();
  OnboardTests::update();
  OnboardTests::buttonA();
  OnboardTests::buttonB();
  for (char value: std::string("a\nb\r\nhc?\r\n")) {
    OnboardTests::handleSerial(value);
  }
  check(formats == 0, "normal controls never format");
  if (!AppConfig::kOnboardTestsEnabled) {
    check(hardwareCalls == 0, "older modes are left completely unchanged by the new dispatcher");
  } else {
    check(hardwareCalls > 0, "new mode routes commands to its implementation");
  }
  if (AppConfig::kFileSystemEnabled) {
    for (const char *invalid: {"FORMAT\r\n", "FORMAT FILESYS extra\n", "FORMAT FILESYX\n", "FORMAT\tFILESYS\n"}) {
      for (const char *value = invalid; *value != 0; ++value) {
        OnboardTests::handleSerial(*value);
      }
    }
    check(formats == 0, "partial, overlong, malformed and non-exact confirmation rejected");
    for (char value: std::string("FORMAT FILESYS")) {
      OnboardTests::handleSerial(value);
    }
    check(formats == 0, "full confirmation still requires a line terminator");
    OnboardTests::handleSerial('\r');
    OnboardTests::handleSerial('\n');
    check(formats == 1, "exact confirmation triggers exactly one format");
  }
  std::cout << "PASS: compile-time diagnostic dispatch, legacy exclusion, controls and exact destructive confirmation\n";
}
