#include <Arduino.h>
#include <mico.h>
#include "AppConfig.h"
#include "RtcClock.h"
#include "RtcTests.h"
#include "SerialRadioInput.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace FakeHardware {
  uint64_t nowUs = 0;
}

TestSerial Serial;
TestScreen Screen;

namespace {
  static_assert(sizeof(SerialLineInput<RtcClock::kTimeInputLength, 1>) < sizeof(SerialRadioInput), "RTC input must not allocate the radio's four-message queue.");
  constexpr size_t kRegisterCount = AppConfig::kDs1307Enabled ? 64 : 11;
  constexpr uint8_t kSeconds = AppConfig::kDs1307Enabled ? 0 : 4;
  constexpr uint8_t kHours = AppConfig::kDs1307Enabled ? 2 : 6;
  constexpr uint8_t kDay = AppConfig::kDs1307Enabled ? 4 : 7;
  constexpr uint8_t kWeekday = AppConfig::kDs1307Enabled ? 3 : 8;
  constexpr uint8_t kMonth = AppConfig::kDs1307Enabled ? 5 : 9;
  constexpr uint8_t kYear = AppConfig::kDs1307Enabled ? 6 : 10;
  constexpr uint8_t kClockMask = AppConfig::kDs1307Enabled ? 0x80 : 0x20;
  std::array<uint8_t, kRegisterCount> registers;
  unsigned int writes = 0;
  unsigned int transfers = 0;
  bool failStop = false;
  bool failWrite = false;
  bool failReadback = false;
  bool failRestart = false;
  bool ignoreWrite = false;
  bool ignoreStop = false;
  bool ignoreRestart = false;
  bool missingDevice = false;
  bool oscillatorStopped = false;
  bool simulateClock = false;
  bool stalledClock = false;
  bool failRamOnce = false;
  bool failSquareWaveOnce = false;
  int failRegisterOnce = -1;
  bool invalidPeriodicMode = false;
  uint64_t nextTickUs = 0;

  void check(bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      std::exit(1);
    }
  }

  void sendSerial(const std::string &text) {
    for (char value: text) {
      RtcTests::handleSerial(value);
    }
  }

  unsigned int fromBcd(uint8_t value) {
    return (value >> 4) * 10 + (value & 0x0F);
  }

  uint8_t toBcd(unsigned int value) {
    return static_cast<uint8_t>((value / 10) * 16 + value % 10);
  }

  bool clockStopped() {
    return (registers[0] & (AppConfig::kDs1307Enabled ? 0x80 : 0xA0)) != 0;
  }

  void tickClock(std::array<uint8_t, kRegisterCount> &state) {
    const unsigned int seconds = fromBcd(state[kSeconds] & 0x7F) + 1;
    state[kSeconds] = static_cast<uint8_t>((state[kSeconds] & 0x80) | toBcd(seconds % 60));
    if (!AppConfig::kDs1307Enabled && (state[2] & 0x80) == 0 && (((state[1] & 0x20) != 0 && seconds == 60) || ((state[1] & 0x10) != 0 && seconds % 30 == 0))) {
      state[1] |= 0x08;
    }
    if (seconds < 60) {
      return;
    }
    const unsigned int minutes = fromBcd(state[kSeconds + 1]) + 1;
    state[kSeconds + 1] = toBcd(minutes % 60);
    if (minutes < 60) {
      return;
    }
    const bool twelveHour = AppConfig::kDs1307Enabled ? (state[kHours] & 0x40) != 0 : (state[0] & 0x02) != 0;
    unsigned int hour = twelveHour ? fromBcd(state[kHours] & 0x1F) % 12 + ((state[kHours] & 0x20) != 0 ? 12 : 0) : fromBcd(state[kHours]);
    hour = (hour + 1) % 24;
    state[kHours] = twelveHour ? static_cast<uint8_t>((AppConfig::kDs1307Enabled ? 0x40 : 0) | (hour >= 12 ? 0x20 : 0) | toBcd(hour % 12 == 0 ? 12 : hour % 12)) : toBcd(hour);
    if (hour != 0) {
      return;
    }
    state[kWeekday] = static_cast<uint8_t>(AppConfig::kDs1307Enabled ? state[kWeekday] % 7 + 1 : (state[kWeekday] + 1) % 7);
    unsigned int day = fromBcd(state[kDay]) + 1;
    unsigned int month = fromBcd(state[kMonth]);
    unsigned int year = fromBcd(state[kYear]);
    const unsigned int days[] = {31, year % 4 == 0 ? 29U : 28U, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day > days[month - 1]) {
      day = 1;
      if (++month == 13) {
        month = 1;
        year = (year + 1) % 100;
      }
    }
    state[kDay] = toBcd(day);
    state[kMonth] = toBcd(month);
    state[kYear] = toBcd(year);
  }

  void advanceClock() {
    if (!simulateClock || stalledClock || clockStopped()) {
      return;
    }
    while (FakeHardware::nowUs >= nextTickUs) {
      tickClock(registers);
      nextTickUs += 1000000;
    }
  }

  void reset() {
    if (AppConfig::kDs1307Enabled) {
      registers = {{0x02, 0x01, 0x08, 0x07, 0x04, 0x10, 0x26, 0x93}};
      for (size_t index = 8; index < registers.size(); ++index) {
        registers[index] = static_cast<uint8_t>(index ^ 0x5A);
      }
    } else {
      registers = {{0x01, 0x07, 0x83, 0xA5, 0x02, 0x01, 0x08, 0x04, 0x00, 0x10, 0x26}};
    }
    writes = 0;
    transfers = 0;
    failStop = failWrite = failReadback = failRestart = ignoreWrite = oscillatorStopped = false;
    ignoreStop = ignoreRestart = missingDevice = invalidPeriodicMode = false;
    simulateClock = stalledClock = failRamOnce = failSquareWaveOnce = false;
    failRegisterOnce = -1;
    nextTickUs = FakeHardware::nowUs + 1000000;
    check(RtcTests::begin(), "RTC initializes with valid saved time");
    Serial.output.clear();
  }

  void checkPreservedSettings(bool twelveHour = false) {
    if (AppConfig::kDs1307Enabled) {
      check((registers[0] & 0x80) == 0 && ((registers[2] & 0x40) != 0) == twelveHour && registers[7] == 0x93, "DS1307 clock runs and hour mode / SQW control are preserved");
      for (size_t index = 8; index < registers.size(); ++index) {
        check(registers[index] == static_cast<uint8_t>(index ^ 0x5A), "DS1307 date setting preserves all 56 RAM bytes");
      }
    } else {
      check(registers[0] == (twelveHour ? 0x03 : 0x01) && registers[1] == 0x07 && registers[2] == 0x83 && registers[3] == 0xA5, "clock runs and 12/24 mode, capacitor, CLKOUT, offset and RAM are preserved");
    }
  }

  void testCommand() {
    for (const char *ending: {"\n", "\r", "\r\n"}) {
      reset();
      sendSerial("t 2026-10-04 ");
      check(writes == 0, "fragmented command does not change time");
      sendSerial("16:25:18");
      check(writes == 0, "time command waits for a line ending");
      sendSerial(ending);
      check(Serial.output == "RTC set from serial: 2026-10-04 W0 16:25:18\n", "acknowledgement reports verified time and computed weekday");
      check(
        registers[kSeconds] == 0x18 && registers[kSeconds + 1] == 0x25 && registers[kHours] == 0x16 && registers[kDay] == 0x04 && registers[kWeekday] == (AppConfig::kDs1307Enabled ? 7 : 0) && registers[kMonth] == 0x10 && registers[kYear] == 0x26,
        "all calendar and time registers use the selected chip's map"
      );
      check(writes == 3, "clock is stopped, time written, and clock restarted exactly once");
      checkPreservedSettings();
      Serial.output.clear();
      sendSerial("s");
      check(Serial.output == "2026-10-04 W0 16:25:18\n", "existing show command remains immediate");
    }
    FakeHardware::nowUs += 300000;
    RtcTests::updateDisplay();
    check(Screen.lines[2] == "2026-10-04 W0" && Screen.lines[3] == "16:25:18" && !Screen.invalidWrite, "OLED shows the new date and time");
    Serial.output.clear();
    sendSerial("h?");
    check(Serial.output.find("t YYYY-MM-DD HH:MM:SS") != std::string::npos, "help advertises the time command");
    Serial.output.clear();
    const auto savedRegisters = registers;
    const unsigned int savedWrites = writes;
    sendSerial("d");
    check(registers == savedRegisters && writes == savedWrites && Serial.output.find(AppConfig::kDs1307Enabled ? "38:" : "08:") != std::string::npos, "register dump includes the final RAM byte without writes");
    std::cout << "PASS: RTC set command, fragments, LF/CR/CRLF, acknowledgement, BCD, preserved settings, display and legacy controls\n";
  }

  void testCalendar() {
    struct Case {
      const char *date;
      uint8_t weekday;
    };
    const Case cases[] = {{"2000-01-01", 6}, {"2000-02-29", 2}, {"2024-02-29", 4}, {"2025-12-31", 3}, {"2026-10-04", 0}, {"2099-12-31", 4}};
    for (const Case &item: cases) {
      reset();
      sendSerial(std::string("T ") + item.date + " 23:59:59\n");
      const uint8_t weekday = AppConfig::kDs1307Enabled && item.weekday == 0 ? 7 : item.weekday;
      check(Serial.output.find("RTC set from serial: ") == 0 && registers[kWeekday] == weekday, "leap dates, year limits and weekday calculation are correct");
      check(registers[kSeconds] == 0x59 && registers[kSeconds + 1] == 0x59 && registers[kHours] == 0x23, "maximum time is valid");
    }
    for (unsigned int hour: {0U, 11U, 12U, 23U}) {
      reset();
      registers[AppConfig::kDs1307Enabled ? kHours : 0] |= AppConfig::kDs1307Enabled ? 0x40 : 0x02;
      char command[32];
      snprintf(command, sizeof(command), "t 2026-10-04 %02u:00:00\n", hour);
      sendSerial(command);
      const uint8_t expected = static_cast<uint8_t>((hour == 0 ? 0x12 : hour == 11 ? 0x11 : hour == 12 ? 0x32 : 0x31) | (AppConfig::kDs1307Enabled ? 0x40 : 0));
      check(registers[kHours] == expected, "24-hour input encodes midnight, morning, noon and evening in 12-hour mode");
      check(Serial.output.find("RTC set from serial: ") == 0, "12-hour time is verified before acknowledgement");
      checkPreservedSettings(true);
    }
    std::cout << "PASS: RTC leap years, date limits, weekdays and 12-hour AM/PM encoding\n";
  }

  void testInvalidInput() {
    const char *invalid[] = {"t",
      "t2026-10-04 16:25:18",
      "t 2026/10/04 16:25:18",
      "t 2026-10-04T16:25:18",
      "t 2026-1-04 16:25:18",
      "t 1999-12-31 23:59:59",
      "t 2100-01-01 00:00:00",
      "t 2026-00-04 16:25:18",
      "t 2026-13-04 16:25:18",
      "t 2026-10-00 16:25:18",
      "t 2026-10-32 16:25:18",
      "t 2026-04-31 16:25:18",
      "t 2026-02-29 16:25:18",
      "t 2024-02-30 16:25:18",
      "t 2026-10-04 24:00:00",
      "t 2026-10-04 16:60:00",
      "t 2026-10-04 16:25:60",
      "t 2026-10-04 -1:25:18",
      "t 2026-10-04 16:25:18 extra",
      "t 2026-10-04 16:25:18b",
      "t 2026-10-04 16:25:18f"};
    for (const char *command: invalid) {
      reset();
      const auto before = registers;
      sendSerial(std::string(command) + "\r\n");
      check(registers == before && writes == 0, "invalid input causes no RTC writes or embedded command execution");
      check(Serial.output.find("Invalid RTC date/time.") == 0, "invalid input is reported");
    }
    for (const std::string &command: {std::string("t") + std::string(100, 'b'), std::string("t 2026-10-04 16:25:18") + '\0', std::string("t 2026-10-04 16:25:18") + '\xFF'}) {
      reset();
      sendSerial(command + "\n");
      check(writes == 0 && Serial.output.find("Invalid RTC date/time.") == 0, "overlong and non-text input are rejected");
      Serial.output.clear();
      sendSerial("t 2026-10-04 16:25:18\r\nt\n");
      check(Serial.output.find("RTC set from serial: ") == 0 && Serial.output.find("Invalid RTC date/time.") != std::string::npos, "parser recovers after rejection and handles consecutive CRLF commands");
    }
    std::cout << "PASS: RTC malformed dates, ranges, overflow, binary input, command isolation and recovery\n";
  }

  void testFailures() {
    for (bool *failure: {&failStop, &failWrite, &failReadback, &failRestart, &ignoreWrite, &ignoreStop, &ignoreRestart, &oscillatorStopped}) {
      reset();
      *failure = true;
      sendSerial("t 2026-10-04 16:25:18\n");
      check(Serial.output.find("RTC set failed: ") == 0 && Serial.output.find("RTC set from serial: ") == std::string::npos, "I2C and readback failures never produce a success acknowledgement");
      if (failure == &failRestart || failure == &ignoreRestart) {
        check(Serial.output.find("clock may be stopped") != std::string::npos, "restart failure is explicitly reported");
      } else {
        check((registers[0] & kClockMask) == 0, "write/verification failure still restarts the clock");
      }
    }
    reset();
    sendSerial("b");
    check(Serial.output.find("RTC set to build time: ") == 0, "existing build-time control remains available");
    checkPreservedSettings();
    reset();
    failWrite = true;
    check(!RtcTests::setToBuildTime() && (registers[0] & kClockMask) == 0, "build-time setter uses the same failure-safe write path");
    std::cout << "PASS: RTC stop/write/readback/restart failures, oscillator flag, and shared build-time setter\n";
  }

  void testClockValidation() {
    struct InvalidRegister {
      uint8_t address;
      uint8_t value;
    };
    const InvalidRegister cases[] = {{kSeconds, 0x1A}, {static_cast<uint8_t>(kSeconds + 1), 0x2F}, {kHours, 0x1A}, {kDay, 0x1A}, {kMonth, 0x0A}, {kYear, 0x2A}};
    for (const auto &item: cases) {
      reset();
      registers[item.address] = item.value;
      const auto before = registers;
      RtcClock::DateTime time;
      check(RtcClock::readDateTime(time) == RtcClock::ReadResult::InvalidTime, "invalid BCD cannot masquerade as a valid decimal time");
      sendSerial("s");
      check(Serial.output.find("RTC read failed or date/time is invalid.") != std::string::npos, "invalid clock data is reported");
      const RtcTests::Result result = RtcTests::runFull();
      check(result.passed == 1 && result.failed == 1 && registers == before && writes == 0, "invalid clock data prevents destructive tests");
      check(RtcTests::status() == RtcTests::Status::Failed, "invalid-data failure reaches the shared result status");
    }
    for (uint8_t hour: {0x00, 0x13}) {
      reset();
      registers[AppConfig::kDs1307Enabled ? kHours : 0] |= AppConfig::kDs1307Enabled ? 0x40 : 0x02;
      registers[kHours] = static_cast<uint8_t>(hour | (AppConfig::kDs1307Enabled ? 0x40 : 0));
      RtcClock::DateTime time;
      check(RtcClock::readDateTime(time) == RtcClock::ReadResult::InvalidTime, "12-hour mode rejects zero and thirteen rather than applying modulo twelve");
    }
    reset();
    missingDevice = true;
    check(!RtcTests::begin() && RtcTests::status() == RtcTests::Status::Failed, "missing RTC is reflected in result status");
    sendSerial("t 2026-10-04 16:25:18\n");
    check(writes == 0 && Serial.output.find("device unavailable") != std::string::npos, "missing RTC rejects time changes");
    std::cout << "PASS: invalid BCD, invalid 12-hour values, non-destructive rejection and missing-device status\n";
  }

  void testCalendarHelpers() {
    RtcClock::DateTime value;
    check(RtcClock::buildDateTime("Feb 29 2024", "23:59:59", value) && RtcClock::addSeconds(value, 1), "build-date parsing and leap-day increment succeed");
    check(value.year == 2024 && value.month == 3 && value.day == 1 && value.weekday == 5 && value.hour == 0 && value.minute == 0 && value.second == 0, "calendar math carries all fields across a leap day");
    check(RtcClock::buildDateTime("Dec 31 2099", "23:59:59", value) && RtcClock::addSeconds(value, 1), "two-digit year rollover succeeds");
    check(value.year == 2000 && value.month == 1 && value.day == 1 && value.weekday == 5, "year wraps without inventing a century register");
    check(RtcClock::buildDateTime("Jan  1 2000", "00:00:00", value) && RtcClock::addSeconds(value, 366U * 86400U), "elapsed time can span a leap year");
    check(value.year == 2001 && value.month == 1 && value.day == 1 && value.weekday == 1, "whole-year elapsed time is correct");
    check(!RtcClock::buildDateTime("Foo  1 2026", "00:00:00", value), "unknown build month is not silently treated as January");
    check(!RtcClock::buildDateTime("Feb 29 2025", "00:00:00", value), "invalid build date is rejected");
    value = {};
    check(!RtcClock::addSeconds(value, 1), "invalid calendar never indexes the month table");
    std::cout << "PASS: shared build-date parsing and bounded calendar arithmetic\n";
  }

  void testDisplayUpdates() {
    reset();
    sendSerial("t 2026-10-04 16:25:18\n");
    FakeHardware::nowUs += 300000;
    RtcTests::updateDisplay();
    const unsigned int dateWrites = Screen.writes[2];
    const unsigned int timeWrites = Screen.writes[3];
    for (unsigned int poll = 0; poll < 4; ++poll) {
      FakeHardware::nowUs += 250000;
      RtcTests::updateDisplay();
    }
    check(Screen.writes[2] == dateWrites && Screen.writes[3] == timeWrites, "unchanged OLED lines are not rewritten every poll");
    registers[kSeconds] = 0x19;
    FakeHardware::nowUs += 250000;
    RtcTests::updateDisplay();
    check(Screen.writes[2] == dateWrites && Screen.writes[3] == timeWrites + 1, "a second change redraws only the time row");
    registers[kSeconds] = 0x1A;
    FakeHardware::nowUs += 250000;
    RtcTests::updateDisplay();
    check(Screen.lines[2] == "RTC invalid time" && Screen.lines[3].empty(), "bad reads do not leave a success-shaped stale clock");
    const size_t reported = Serial.output.size();
    FakeHardware::nowUs += 250000;
    RtcTests::updateDisplay();
    check(Serial.output.size() == reported, "unchanged display errors are reported once per transition");
    check(RtcTests::runFull().failed == 1, "invalid clock aborts a rerun");
    FakeHardware::nowUs += 250000;
    RtcTests::updateDisplay();
    check(Screen.lines[2] == "RTC invalid time" && Screen.lines[3].empty(), "a failed rerun does not leave Please wait on the display");
    registers[kSeconds] = 0x20;
    FakeHardware::nowUs += 250000;
    RtcTests::updateDisplay();
    check(Screen.lines[2] == "2026-10-04 W0" && Screen.lines[3] == "16:25:20" && !Screen.invalidWrite, "clock rows recover after an error without reinitialization");
    std::cout << "PASS: OLED change-only rendering and explicit error/recovery states\n";
  }

  void testSnapshotRestoration() {
    for (bool stopped: {false, true}) {
      reset();
      registers[AppConfig::kDs1307Enabled ? kHours : 0] |= AppConfig::kDs1307Enabled ? 0x40 : 0x02;
      if (stopped) {
        registers[0] |= kClockMask;
      }
      auto expected = registers;
      RtcClock::Snapshot snapshot = {};
      const unsigned int before = transfers;
      check(RtcClock::capture(snapshot) == RtcClock::ReadResult::Valid && transfers == before + 1, "snapshot uses one coherent register-map read");
      registers.fill(0);
      FakeHardware::nowUs += 1500000;
      check(RtcClock::restore(snapshot), "saved state is restored and verified");
      if (!stopped) {
        tickClock(expected);
        tickClock(expected);
      }
      check(registers == expected, "restoration preserves running/stopped state and 12-hour format for either chip");
    }
    reset();
    RtcClock::Snapshot snapshot = {};
    check(RtcClock::capture(snapshot) == RtcClock::ReadResult::Valid, "snapshot exists before restoration-failure test");
    failRegisterOnce = AppConfig::kDs1307Enabled ? 8 : 2;
    check(!RtcClock::restore(snapshot), "a failed configuration/RAM restore is not reported as successful");
    check((registers[0] & kClockMask) == 0, "restoration still returns the clock to its original running state after an independent error");
    reset();
    FakeHardware::nowUs = static_cast<uint64_t>(UINT32_MAX - 500U) * 1000;
    check(RtcClock::capture(snapshot) == RtcClock::ReadResult::Valid, "snapshot before millis rollover succeeds");
    FakeHardware::nowUs += 1500000;
    check(RtcClock::restore(snapshot) && registers[kSeconds] == 0x04, "snapshot elapsed time survives 32-bit millis rollover");
    std::cout << "PASS: coherent snapshots, stopped/running restoration, cleanup after failure and millis rollover\n";
  }

  void testFullSuite() {
    for (bool stopped: {false, true}) {
      reset();
      registers[AppConfig::kDs1307Enabled ? kHours : 0] |= AppConfig::kDs1307Enabled ? 0x40 : 0x02;
      if (stopped) {
        registers[0] |= kClockMask;
      }
      auto expected = registers;
      simulateClock = true;
      const uint64_t startedUs = FakeHardware::nowUs;
      const RtcTests::Result result = RtcTests::runFull();
      if (result.failed != 0) {
        std::cerr << Serial.output;
      }
      check(result.passed == (AppConfig::kDs1307Enabled ? 23 : 15) && result.failed == 0, "every feature check passes against the selected register/clock model");
      if (!stopped) {
        const uint64_t seconds = (FakeHardware::nowUs - startedUs + 500000) / 1000000;
        for (uint64_t index = 0; index < seconds; ++index) {
          tickClock(expected);
        }
      }
      check(registers == expected, "full suite restores time, stop state, hour mode, control/calibration and every RAM byte");
      check(!invalidPeriodicMode, "PCF minute/half-minute interrupts are tested only in normal calibration mode");
      check(RtcTests::status() == RtcTests::Status::Passed, "successful full suite reaches shared status");
      check(Serial.output.find(AppConfig::kDs1307Enabled ? "[MANUAL] SQW/OUT" : "[MANUAL] INT/CLKOUT") != std::string::npos && Serial.output.find("[MANUAL] Battery backup") != std::string::npos, "physical waveform and backup tests are not misreported as software passes");
    }
    for (bool *failure: {&failRamOnce, &failSquareWaveOnce, &stalledClock}) {
      reset();
      simulateClock = true;
      *failure = true;
      const RtcTests::Result result = RtcTests::runFull();
      check(result.failed > 0 && Serial.output.find("[FAIL]") != std::string::npos, "RAM, SQW readback and stopped oscillator faults are detected");
      check(Screen.lines[1] == "RTC TEST FAILURE", "hardware feature failures are visible on the OLED");
      check(RtcTests::status() == RtcTests::Status::Failed, "failed full suite reaches shared status for the result LED");
      check((registers[0] & kClockMask) == 0, "faulted full suite still restores the clock");
      checkPreservedSettings();
    }
    std::cout << "PASS: complete RTC feature suite, live/stopped state restoration, interrupt mode, RAM/output checks and fault injection\n";
  }
}

OSStatus MicoI2cInitialize(mico_i2c_device_t *device) {
  check(device->port == Arduino_I2C && device->address == (AppConfig::kDs1307Enabled ? 0x68 : 0x51) && device->address_width == I2C_ADDRESS_WIDTH_7BIT && device->speed_mode == I2C_STANDARD_SPEED_MODE, "RTC uses the selected 7-bit address on the native 100 kHz shared bus");
  return kNoErr;
}

bool MicoI2cProbeDevice(mico_i2c_device_t *, int) {
  return !missingDevice;
}

OSStatus MicoI2cBuildTxMessage(mico_i2c_message_t *message, const void *buffer, uint16_t length, uint16_t retries) {
  *message = {buffer, nullptr, length, 0, retries, false};
  return kNoErr;
}

OSStatus MicoI2cBuildCombinedMessage(mico_i2c_message_t *message, const void *tx, void *rx, uint16_t txLength, uint16_t rxLength, uint16_t retries) {
  *message = {tx, rx, txLength, rxLength, retries, true};
  return kNoErr;
}

OSStatus MicoI2cTransfer(mico_i2c_device_t *, mico_i2c_message_t *message, uint16_t count) {
  ++transfers;
  advanceClock();
  check(count == 1 && message->tx_length >= 1, "RTC transfers one bounded message");
  const uint8_t *data = static_cast<const uint8_t *>(message->tx_buffer);
  const uint8_t address = data[0];
  if (message->combined) {
    check(static_cast<size_t>(address) + message->rx_length <= registers.size(), "RTC reads stay inside the register map");
    if (address == kSeconds && message->rx_length == 7 && failReadback) {
      return -1;
    }
    memcpy(message->rx_buffer, registers.data() + address, message->rx_length);
    if (address == kSeconds && message->rx_length == 7 && oscillatorStopped) {
      uint8_t &seconds = static_cast<uint8_t *>(message->rx_buffer)[0];
      seconds = AppConfig::kDs1307Enabled ? static_cast<uint8_t>(seconds & ~0x80) : static_cast<uint8_t>(seconds | 0x80);
    }
  } else {
    ++writes;
    check(message->tx_length >= 2 && static_cast<size_t>(address) + message->tx_length - 1 <= registers.size(), "RTC writes stay inside the register map");
    if (address == 0 && message->tx_length == 2) {
      if ((failStop && (data[1] & kClockMask) != 0) || (failRestart && (data[1] & kClockMask) == 0)) {
        return -1;
      }
      if ((ignoreStop && (data[1] & kClockMask) != 0) || (ignoreRestart && (data[1] & kClockMask) == 0)) {
        return kNoErr;
      }
    }
    if (address == failRegisterOnce) {
      failRegisterOnce = -1;
      return -1;
    }
    if (address == kSeconds && message->tx_length == 8) {
      check((registers[0] & kClockMask) != 0, "time is written while the clock is stopped");
      if (AppConfig::kDs1307Enabled) {
        check((data[1] & 0x80) != 0, "DS1307 bulk time write preserves CH until explicit restart");
      }
      if (failWrite) {
        return -1;
      }
      if (ignoreWrite) {
        return kNoErr;
      }
    }
    if (address == (AppConfig::kDs1307Enabled ? 8 : 3) && failRamOnce) {
      failRamOnce = false;
      return -1;
    }
    if (address == (AppConfig::kDs1307Enabled ? 7 : 1) && (AppConfig::kDs1307Enabled ? (data[1] & 0x10) != 0 : true) && failSquareWaveOnce) {
      failSquareWaveOnce = false;
      registers[address] = static_cast<uint8_t>(data[1] ^ 0x01);
      return kNoErr;
    }
    if (!AppConfig::kDs1307Enabled && address == 0 && data[1] == 0x58) {
      registers = {{0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x01, 0x06, 0x01, 0x00}};
      nextTickUs = FakeHardware::nowUs + 1000000;
      return kNoErr;
    }
    if (!AppConfig::kDs1307Enabled && address == 1 && (data[1] & 0x30) != 0 && (registers[2] & 0x80) != 0) {
      invalidPeriodicMode = true;
    }
    const bool wasStopped = clockStopped();
    memcpy(registers.data() + address, data + 1, message->tx_length - 1);
    if (wasStopped && !clockStopped()) {
      nextTickUs = FakeHardware::nowUs + (AppConfig::kDs1307Enabled ? 1000000 : 500000);
    } else if (address == kSeconds && !clockStopped()) {
      nextTickUs = FakeHardware::nowUs + 1000000;
    }
  }
  return kNoErr;
}

int main() {
  testCommand();
  testCalendar();
  testInvalidInput();
  testFailures();
  testClockValidation();
  testCalendarHelpers();
  testDisplayUpdates();
  testSnapshotRestoration();
  testFullSuite();
}
