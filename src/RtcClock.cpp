#include "RtcClock.h"

#include <Arduino.h>
#include <mico.h>

namespace {
  namespace Pcf = RtcClock::Pcf85063;
  namespace Ds = RtcClock::Ds1307;
  using RtcClock::DateTime;
  using RtcClock::ReadResult;

  mico_i2c_device_t device = {Arduino_I2C, RtcClock::kAddress, I2C_ADDRESS_WIDTH_7BIT, I2C_STANDARD_SPEED_MODE};
  bool connected = false;

  uint8_t decimalToBcd(uint8_t value) {
    return static_cast<uint8_t>((value / 10) << 4 | (value % 10));
  }

  bool decodeBcd(uint8_t value, uint8_t &decoded) {
    if ((value & 0x0F) > 9 || (value >> 4) > 9) {
      return false;
    }
    decoded = static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0F));
    return true;
  }

  uint8_t daysInMonth(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return month == 2 && leap ? 29 : days[month - 1];
  }

  uint8_t weekdayForDate(const DateTime &value) {
    int year = value.year;
    int month = value.month;
    if (month < 3) {
      month += 12;
      --year;
    }
    const int weekday = (value.day + (13 * (month + 1)) / 5 + year + year / 4 - year / 100 + year / 400) % 7;
    return static_cast<uint8_t>((weekday + 6) % 7);
  }

  uint16_t parseDecimal(const uint8_t *text, size_t length) {
    uint16_t value = 0;
    for (size_t index = 0; index < length; ++index) {
      value = static_cast<uint16_t>(value * 10 + text[index] - '0');
    }
    return value;
  }

  ReadResult decodeDateTime(const uint8_t *data, uint8_t control1, DateTime &value) {
    value = {};
    value.oscillatorStopped = (data[0] & 0x80) != 0;
    value.stopped = AppConfig::kDs1307Enabled ? value.oscillatorStopped : (control1 & Pcf::kStop) != 0;
    const bool twelveHour = AppConfig::kDs1307Enabled ? (data[2] & Ds::k12Hour) != 0 : (control1 & Pcf::k12Hour) != 0;
    uint8_t hour;
    uint8_t year;
    if (!decodeBcd(data[0] & 0x7F, value.second) || !decodeBcd(data[1] & 0x7F, value.minute) || !decodeBcd(data[2] & (twelveHour ? 0x1F : 0x3F), hour) || !decodeBcd(data[AppConfig::kDs1307Enabled ? 4 : 3] & 0x3F, value.day) || !decodeBcd(data[5] & 0x1F, value.month) || !decodeBcd(data[6], year)) {
      return ReadResult::InvalidTime;
    }
    if (twelveHour && (hour == 0 || hour > 12)) {
      return ReadResult::InvalidTime;
    }
    value.hour = twelveHour ? static_cast<uint8_t>(hour % 12 + ((data[2] & 0x20) != 0 ? 12 : 0)) : hour;
    value.year = static_cast<uint16_t>(2000 + year);
    const uint8_t weekday = data[AppConfig::kDs1307Enabled ? 3 : 4] & 0x07;
    if (AppConfig::kDs1307Enabled && weekday == 0) {
      return ReadResult::InvalidTime;
    }
    // Match Seeed's DS1307 convention: Monday=1 through Sunday=7.
    value.weekday = AppConfig::kDs1307Enabled ? weekday % 7 : weekday;
    return RtcClock::isValid(value) ? ReadResult::Valid : ReadResult::InvalidTime;
  }

  bool verifyStoppedTime(const DateTime &expected) {
    DateTime actual = {};
    return RtcClock::readDateTime(actual) == ReadResult::Valid && actual.stopped && actual.oscillatorStopped == AppConfig::kDs1307Enabled && RtcClock::sameDateTime(expected, actual);
  }
}

namespace RtcClock {
  bool begin() {
    connected = MicoI2cInitialize(&device) == kNoErr && MicoI2cProbeDevice(&device, 3);
    return connected;
  }

  bool present() {
    return connected;
  }

  bool writeRegisters(uint8_t firstRegister, const uint8_t *data, size_t length) {
    if (data == nullptr || length == 0 || firstRegister >= kRegisterCount || length > kRegisterCount - firstRegister) {
      Serial.println(F("RTC write rejected: register range or buffer is invalid."));
      return false;
    }
    if (!connected) {
      return false;
    }
    uint8_t buffer[kRegisterCount + 1];
    buffer[0] = firstRegister;
    memcpy(buffer + 1, data, length);
    mico_i2c_message_t message;
    return MicoI2cBuildTxMessage(&message, buffer, static_cast<uint16_t>(length + 1), 3) == kNoErr && MicoI2cTransfer(&device, &message, 1) == kNoErr;
  }

  bool readRegisters(uint8_t firstRegister, uint8_t *data, size_t length) {
    if (data == nullptr || length == 0 || firstRegister >= kRegisterCount || length > kRegisterCount - firstRegister) {
      Serial.println(F("RTC read rejected: register range or buffer is invalid."));
      return false;
    }
    if (!connected) {
      return false;
    }
    mico_i2c_message_t message;
    return MicoI2cBuildCombinedMessage(&message, &firstRegister, data, 1, static_cast<uint16_t>(length), 3) == kNoErr && MicoI2cTransfer(&device, &message, 1) == kNoErr;
  }

  bool readRegister(uint8_t address, uint8_t &value) {
    return readRegisters(address, &value, 1);
  }

  bool writeRegister(uint8_t address, uint8_t value) {
    return writeRegisters(address, &value, 1);
  }

  bool writeVerified(uint8_t address, uint8_t value, uint8_t mask) {
    uint8_t actual;
    return writeRegister(address, value) && readRegister(address, actual) && (actual & mask) == (value & mask);
  }

  bool isValid(const DateTime &value) {
    return value.year >= 2000 && value.year <= 2099 && value.month >= 1 && value.month <= 12 && value.day >= 1 && value.day <= daysInMonth(value.year, value.month) && value.weekday <= 6 && value.hour <= 23 && value.minute <= 59 && value.second <= 59;
  }

  bool sameDateTime(const DateTime &left, const DateTime &right) {
    return left.year == right.year && left.month == right.month && left.day == right.day && left.weekday == right.weekday && left.hour == right.hour && left.minute == right.minute && left.second == right.second;
  }

  bool parseDateTime(const uint8_t *text, size_t length, DateTime &value) {
    static const char format[] = " YYYY-MM-DD HH:MM:SS";
    static_assert(sizeof(format) - 1 == kTimeInputLength, "RTC input buffer must fit one date/time.");
    if (text == nullptr || length != kTimeInputLength) {
      return false;
    }
    for (size_t index = 0; index < length; ++index) {
      if (format[index] == ' ' || format[index] == '-' || format[index] == ':') {
        if (text[index] != format[index]) {
          return false;
        }
      } else if (text[index] < '0' || text[index] > '9') {
        return false;
      }
    }
    value = {};
    value.year = parseDecimal(text + 1, 4);
    value.month = static_cast<uint8_t>(parseDecimal(text + 6, 2));
    value.day = static_cast<uint8_t>(parseDecimal(text + 9, 2));
    value.hour = static_cast<uint8_t>(parseDecimal(text + 12, 2));
    value.minute = static_cast<uint8_t>(parseDecimal(text + 15, 2));
    value.second = static_cast<uint8_t>(parseDecimal(text + 18, 2));
    if (!isValid(value)) {
      return false;
    }
    value.weekday = weekdayForDate(value);
    return true;
  }

  bool buildDateTime(const char *date, const char *time, DateTime &value) {
    if (date == nullptr || time == nullptr || strlen(date) != 11 || strlen(time) != 8 || date[3] != ' ' || date[6] != ' ') {
      return false;
    }
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    uint8_t month = 1;
    while (month <= 12 && strncmp(date, months + (month - 1) * 3, 3) != 0) {
      ++month;
    }
    if (month > 12) {
      return false;
    }
    uint8_t text[kTimeInputLength] = {};
    text[0] = ' ';
    memcpy(text + 1, date + 7, 4);
    text[5] = '-';
    text[6] = static_cast<uint8_t>('0' + month / 10);
    text[7] = static_cast<uint8_t>('0' + month % 10);
    text[8] = '-';
    text[9] = date[4] == ' ' ? '0' : date[4];
    text[10] = date[5];
    text[11] = ' ';
    memcpy(text + 12, time, 8);
    return parseDateTime(text, sizeof(text), value);
  }

  bool addSeconds(DateTime &value, uint32_t seconds) {
    if (!isValid(value)) {
      return false;
    }
    const uint64_t total = static_cast<uint64_t>(value.hour) * 3600 + value.minute * 60 + value.second + seconds;
    value.hour = static_cast<uint8_t>((total % 86400) / 3600);
    value.minute = static_cast<uint8_t>((total % 3600) / 60);
    value.second = static_cast<uint8_t>(total % 60);
    uint32_t days = static_cast<uint32_t>(total / 86400);
    value.weekday = static_cast<uint8_t>((value.weekday + days) % 7);
    while (days != 0) {
      const uint8_t remaining = static_cast<uint8_t>(daysInMonth(value.year, value.month) - value.day);
      if (days <= remaining) {
        value.day = static_cast<uint8_t>(value.day + days);
        break;
      }
      days -= remaining + 1;
      value.day = 1;
      if (++value.month > 12) {
        value.month = 1;
        value.year = value.year == 2099 ? 2000 : static_cast<uint16_t>(value.year + 1);
      }
    }
    return true;
  }

  ReadResult readDateTime(DateTime &value) {
    uint8_t control1 = 0;
    uint8_t data[7];
    if ((!AppConfig::kDs1307Enabled && !readRegister(Pcf::kControl1, control1)) || !readRegisters(kSeconds, data, sizeof(data))) {
      return ReadResult::IoError;
    }
    return decodeDateTime(data, control1, value);
  }

  bool setStopped(bool stopped) {
    const uint8_t address = AppConfig::kDs1307Enabled ? kSeconds : Pcf::kControl1;
    const uint8_t mask = AppConfig::kDs1307Enabled ? Ds::kClockHalt : Pcf::kStop;
    uint8_t control;
    if (!readRegister(address, control)) {
      return false;
    }
    control = stopped ? static_cast<uint8_t>(control | mask) : static_cast<uint8_t>(control & ~mask);
    return writeVerified(address, control, mask);
  }

  bool writeDateTime(const DateTime &value) {
    if (!isValid(value)) {
      Serial.println(F("RTC time write rejected: invalid date/time."));
      return false;
    }
    bool twelveHour;
    bool stopped;
    if (AppConfig::kDs1307Enabled) {
      uint8_t current[3];
      if (!readRegisters(kSeconds, current, sizeof(current))) {
        return false;
      }
      stopped = (current[0] & Ds::kClockHalt) != 0;
      twelveHour = (current[2] & Ds::k12Hour) != 0;
    } else {
      uint8_t control1;
      if (!readRegister(Pcf::kControl1, control1)) {
        return false;
      }
      stopped = (control1 & Pcf::kStop) != 0;
      twelveHour = (control1 & Pcf::k12Hour) != 0;
    }
    if (!stopped) {
      Serial.println(F("RTC time write rejected: clock must be stopped."));
      return false;
    }
    uint8_t hour = decimalToBcd(value.hour);
    if (twelveHour) {
      hour = decimalToBcd(static_cast<uint8_t>(value.hour % 12 == 0 ? 12 : value.hour % 12));
      if (value.hour >= 12) {
        hour |= 0x20;
      }
      if (AppConfig::kDs1307Enabled) {
        hour |= Ds::k12Hour;
      }
    }
    const uint8_t weekday = AppConfig::kDs1307Enabled && value.weekday == 0 ? 7 : value.weekday;
    const uint8_t data[] = {static_cast<uint8_t>(decimalToBcd(value.second) | (AppConfig::kDs1307Enabled ? Ds::kClockHalt : 0)),
      decimalToBcd(value.minute),
      hour,
      AppConfig::kDs1307Enabled ? weekday : decimalToBcd(value.day),
      AppConfig::kDs1307Enabled ? decimalToBcd(value.day) : weekday,
      decimalToBcd(value.month),
      decimalToBcd(static_cast<uint8_t>(value.year - 2000))};
    return writeRegisters(kSeconds, data, sizeof(data));
  }

  bool setDateTime(const DateTime &value) {
    if (!connected || !isValid(value)) {
      Serial.println(F("RTC set failed: device unavailable or invalid date/time."));
      return false;
    }
    if (!setStopped(true)) {
      Serial.println(F("RTC set failed: unable to stop the clock."));
      if (!setStopped(false)) {
        Serial.println(F("RTC set failed: restart failed; clock may be stopped."));
      }
      return false;
    }
    const bool written = writeDateTime(value);
    const bool verified = written && verifyStoppedTime(value);
    // Always attempt restart, even when a write or its verification failed.
    const bool restarted = setStopped(false);
    if (!written) {
      Serial.println(F("RTC set failed: time write failed; time may be invalid."));
    } else if (!verified) {
      Serial.println(F("RTC set failed: date/time readback or oscillator check failed."));
    }
    if (!restarted) {
      Serial.println(F("RTC set failed: restart failed; clock may be stopped."));
    }
    return written && verified && restarted;
  }

  ReadResult capture(Snapshot &snapshot) {
    snapshot.capturedMs = static_cast<uint32_t>(millis());
    if (!readRegisters(0, snapshot.registers, sizeof(snapshot.registers))) {
      return ReadResult::IoError;
    }
    return decodeDateTime(snapshot.registers + kSeconds, snapshot.registers[0], snapshot.dateTime);
  }

  bool restore(const Snapshot &snapshot) {
    if (!isValid(snapshot.dateTime)) {
      Serial.println(F("RTC restore rejected: snapshot has invalid time."));
      return false;
    }
    DateTime time = snapshot.dateTime;
    const uint32_t elapsedMs = static_cast<uint32_t>(millis()) - snapshot.capturedMs;
    if (!time.stopped && !addSeconds(time, elapsedMs / 1000 + (elapsedMs % 1000 >= 500 ? 1 : 0))) {
      Serial.println(F("RTC restore failed: elapsed time could not be applied."));
      return false;
    }
    bool restored = setStopped(true);
    bool modeRestored;
    if (AppConfig::kDs1307Enabled) {
      modeRestored = writeVerified(Ds::kHours, snapshot.registers[Ds::kHours]);
      restored = writeVerified(Ds::kControl, snapshot.registers[Ds::kControl]) && restored;
      uint8_t ram[Ds::kRamSize];
      const bool ramRestored = writeRegisters(Ds::kRamStart, snapshot.registers + Ds::kRamStart, sizeof(ram)) && readRegisters(Ds::kRamStart, ram, sizeof(ram)) && memcmp(ram, snapshot.registers + Ds::kRamStart, sizeof(ram)) == 0;
      restored = ramRestored && restored;
    } else {
      modeRestored = writeVerified(Pcf::kControl1, static_cast<uint8_t>(snapshot.registers[Pcf::kControl1] | Pcf::kStop), Pcf::kControl1Mask);
      // Restore independent settings even after an earlier failure. TF is a latched event, not configuration.
      restored = writeVerified(Pcf::kControl2, snapshot.registers[Pcf::kControl2], Pcf::kControl2SettingsMask) && restored;
      restored = writeVerified(Pcf::kOffset, snapshot.registers[Pcf::kOffset]) && restored;
      restored = writeVerified(Pcf::kRam, snapshot.registers[Pcf::kRam]) && restored;
    }
    const bool timeRestored = modeRestored && writeDateTime(time) && verifyStoppedTime(time);
    const bool clockRestored = AppConfig::kDs1307Enabled ? setStopped(snapshot.dateTime.stopped) : writeVerified(Pcf::kControl1, snapshot.registers[Pcf::kControl1], Pcf::kControl1Mask);
    return restored && timeRestored && clockRestored;
  }
}
