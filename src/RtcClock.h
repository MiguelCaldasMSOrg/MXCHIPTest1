#pragma once

#include <stddef.h>
#include <stdint.h>
#include "AppConfig.h"

namespace RtcClock {
  namespace Pcf85063 {
    constexpr uint8_t kControl1 = 0x00;
    constexpr uint8_t kControl2 = 0x01;
    constexpr uint8_t kOffset = 0x02;
    constexpr uint8_t kRam = 0x03;
    constexpr uint8_t k12Hour = 0x02;
    constexpr uint8_t kStop = 0x20;
    constexpr uint8_t kSoftwareReset = 0x58;
    constexpr uint8_t kOscillatorStopped = 0x80;
    constexpr uint8_t kMinuteInterrupt = 0x20;
    constexpr uint8_t kHalfMinuteInterrupt = 0x10;
    constexpr uint8_t kTimerFlag = 0x08;
    constexpr uint8_t kControl1Mask = 0xA7;
    constexpr uint8_t kControl2SettingsMask = 0x37;
  }

  namespace Ds1307 {
    constexpr uint8_t kHours = 0x02;
    constexpr uint8_t kControl = 0x07;
    constexpr uint8_t kRamStart = 0x08;
    constexpr size_t kRamSize = 56;
    constexpr uint8_t k12Hour = 0x40;
    constexpr uint8_t kClockHalt = 0x80;
  }

  constexpr uint8_t kAddress = AppConfig::kDs1307Enabled ? 0x68 : 0x51;
  constexpr size_t kRegisterCount = AppConfig::kDs1307Enabled ? 64 : 11;
  constexpr uint8_t kSeconds = AppConfig::kDs1307Enabled ? 0x00 : 0x04;
  constexpr size_t kTimeInputLength = 20;

  // Public dates use years 2000-2099 and weekdays 0=Sunday through 6=Saturday.
  struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    // PCF85063TP OS is a latched integrity flag; DS1307 CH disables its oscillator.
    bool oscillatorStopped;
    bool stopped;
  };

  enum class ReadResult {
    Valid,
    InvalidTime,
    IoError
  };

  struct Snapshot {
    DateTime dateTime;
    uint8_t registers[kRegisterCount];
    uint32_t capturedMs;
  };

  bool begin();
  bool present();
  bool readRegisters(uint8_t firstRegister, uint8_t *data, size_t length);
  bool writeRegisters(uint8_t firstRegister, const uint8_t *data, size_t length);
  bool readRegister(uint8_t address, uint8_t &value);
  bool writeRegister(uint8_t address, uint8_t value);
  bool writeVerified(uint8_t address, uint8_t value, uint8_t mask = 0xFF);
  ReadResult readDateTime(DateTime &value);
  bool setStopped(bool stopped);
  // Low-level test writes require a stopped clock; setDateTime manages stop/start.
  bool writeDateTime(const DateTime &value);
  bool setDateTime(const DateTime &value);
  ReadResult capture(Snapshot &snapshot);
  bool restore(const Snapshot &snapshot);

  bool isValid(const DateTime &value);
  bool sameDateTime(const DateTime &left, const DateTime &right);
  bool parseDateTime(const uint8_t *text, size_t length, DateTime &value);
  bool buildDateTime(const char *date, const char *time, DateTime &value);
  bool addSeconds(DateTime &value, uint32_t seconds);
}
