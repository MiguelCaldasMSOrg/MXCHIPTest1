#include "RtcTests.h"
#include "AppConfig.h"
#include "RtcClock.h"
#include "SerialLineInput.h"

namespace {
  namespace Pcf = RtcClock::Pcf85063;
  namespace Ds = RtcClock::Ds1307;
  using RtcClock::DateTime;
  using RtcClock::ReadResult;
  using RtcClock::Snapshot;
  using RtcClock::readDateTime;
  using RtcClock::readRegister;
  using RtcClock::readRegisters;
  using RtcClock::sameDateTime;
  using RtcClock::setStopped;
  using RtcClock::writeDateTime;
  using RtcClock::writeRegister;
  using RtcClock::writeRegisters;
  using RtcClock::writeVerified;
  using TimeInput = SerialLineInput<RtcClock::kTimeInputLength, 1>;

  constexpr unsigned long kDisplayIntervalMs = 250;
  RtcTests::Status currentStatus = RtcTests::Status::NotReady;
  TimeInput timeInput;
  bool receivingTime = false;
  unsigned long lastDisplayMs = 0;
  ReadResult lastDisplayResult = ReadResult::Valid;
  char lastDateLine[20] = {};
  char lastTimeLine[17] = {};

  void invalidateDisplay() {
    lastDateLine[0] = '\0';
    lastTimeLine[0] = '\0';
  }

  const char *clockMarker(const DateTime &value) {
    if (value.oscillatorStopped) {
      return AppConfig::kDs1307Enabled ? " CH!" : " OS!";
    }
    return value.stopped ? " STOP" : "";
  }

  void printDateTime(const DateTime &value) {
    char line[48];
    snprintf(line, sizeof(line), "%04u-%02u-%02u W%u %02u:%02u:%02u%s", value.year, value.month, value.day, value.weekday, value.hour, value.minute, value.second, clockMarker(value));
    Serial.println(line);
  }

  void report(const __FlashStringHelper *name, bool passed, RtcTests::Result &result) {
    Serial.print(passed ? F("[PASS] ") : F("[FAIL] "));
    Serial.println(name);
    if (passed) {
      ++result.passed;
    } else {
      ++result.failed;
    }
  }

  void printResult(const RtcTests::Result &result) {
    currentStatus = result.failed == 0 ? RtcTests::Status::Passed : RtcTests::Status::Failed;
    Serial.print(F("Result: "));
    Serial.print(result.passed);
    Serial.print(F(" passed, "));
    Serial.print(result.failed);
    Serial.println(F(" failed."));
    Screen.print(1, result.failed == 0 ? "RTC I2C PASS" : "RTC TEST FAILURE");
  }

  bool testRegisterPatterns(uint8_t address, uint8_t first, uint8_t second) {
    return writeVerified(address, first) && writeVerified(address, second);
  }

  bool testControl1Bits(uint8_t original) {
    const uint8_t mask = 0x87;
    const uint8_t first = static_cast<uint8_t>((original & ~mask) | mask | Pcf::kStop);
    const uint8_t second = static_cast<uint8_t>((original & ~mask) | Pcf::kStop);
    const bool tested = setStopped(true) && writeVerified(Pcf::kControl1, first, mask) && writeVerified(Pcf::kControl1, second, mask);
    const bool restored = writeVerified(Pcf::kControl1, static_cast<uint8_t>(original | Pcf::kStop), Pcf::kControl1Mask);
    return tested && restored;
  }

  bool testOffsetModes() {
    uint8_t control;
    return setStopped(true) && readRegister(Pcf::kControl2, control) && writeVerified(Pcf::kControl2, control & 0x07) && testRegisterPatterns(Pcf::kOffset, 0x01, 0xFF);
  }

  bool testClockOutputSelections() {
    for (uint8_t selection = 0; selection < 8; ++selection) {
      if (!writeVerified(Pcf::kControl2, selection)) {
        return false;
      }
    }
    return true;
  }

  bool testStoppedClock() {
    const DateTime known = {2026, 1, 2, 5, 12, 34, 56, false, false};
    if (!setStopped(true) || !writeDateTime(known)) {
      return false;
    }
    delay(1200);
    DateTime actual;
    return readDateTime(actual) == ReadResult::Valid && actual.stopped && sameDateTime(known, actual);
  }

  bool setPcfHourMode(bool twelveHour) {
    uint8_t control;
    return readRegister(Pcf::kControl1, control) && writeVerified(Pcf::kControl1, static_cast<uint8_t>((control & ~Pcf::k12Hour) | (twelveHour ? Pcf::k12Hour : 0x00)), Pcf::kControl1Mask);
  }

  bool testLeapDayRollover() {
    const DateTime known = {2024, 2, 28, 3, 23, 59, 58, false, false};
    const DateTime expected = {2024, 2, 29, 4, 0, 0, 1, false, false};
    if (!setStopped(true) || !setPcfHourMode(false) || !writeDateTime(known) || !setStopped(false)) {
      return false;
    }
    delay(3100);
    DateTime actual;
    return readDateTime(actual) == ReadResult::Valid && sameDateTime(expected, actual);
  }

  bool test12HourRollover() {
    const DateTime known = {2025, 12, 31, 3, 23, 59, 58, false, false};
    const DateTime expected = {2026, 1, 1, 4, 0, 0, 1, false, false};
    if (!setStopped(true) || !setPcfHourMode(true) || !writeDateTime(known) || !setStopped(false)) {
      return false;
    }
    delay(3100);
    DateTime actual;
    uint8_t control;
    return readDateTime(actual) == ReadResult::Valid && sameDateTime(expected, actual) && readRegister(Pcf::kControl1, control) && (control & Pcf::k12Hour) != 0;
  }

  bool testPeriodicInterrupt(uint8_t interruptBit, uint8_t startSecond) {
    const DateTime known = {2026, 1, 2, 5, 12, 34, startSecond, false, false};
    uint8_t control2;
    // MI/HMI are specified only in normal offset mode; zero correction keeps the test interval constant.
    if (!setStopped(true) || !writeVerified(Pcf::kOffset, 0) || !writeDateTime(known) || !readRegister(Pcf::kControl2, control2) || !writeVerified(Pcf::kControl2, static_cast<uint8_t>((control2 & 0x07) | interruptBit)) || !setStopped(false)) {
      return false;
    }
    delay(3100);
    return readRegister(Pcf::kControl2, control2) && (control2 & Pcf::kTimerFlag) != 0;
  }

  void runPcfTests(const Snapshot &snapshot, RtcTests::Result &result) {
    report(F("free RAM byte read/write"), testRegisterPatterns(Pcf::kRam, 0xA5, 0x5A), result);
    report(F("offset calibration modes and signed value"), testOffsetModes(), result);
    report(F("CAP_SEL, 12/24, CIE, and EXT_TEST controls"), testControl1Bits(snapshot.registers[Pcf::kControl1]), result);
    report(F("all CLKOUT frequency selections"), testClockOutputSelections(), result);
    report(F("STOP freezes the time counters"), testStoppedClock(), result);
    report(F("24-hour leap-day calendar rollover"), testLeapDayRollover(), result);
    report(F("12-hour PM-to-AM year rollover"), test12HourRollover(), result);
    report(F("half-minute interrupt and timer flag"), testPeriodicInterrupt(Pcf::kHalfMinuteInterrupt, 28), result);
    report(F("minute interrupt and timer flag"), testPeriodicInterrupt(Pcf::kMinuteInterrupt, 58), result);
    report(F("software reset command"), writeRegister(Pcf::kControl1, Pcf::kSoftwareReset), result);
    delay(10);
    uint8_t registers[11];
    const uint8_t expected[] = {0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x01, 0x06, 0x01, 0x00};
    report(F("documented reset register values"), readRegisters(0, registers, sizeof(registers)) && memcmp(registers, expected, sizeof(expected)) == 0, result);
    uint8_t seconds;
    report(F("oscillator-stop flag can be cleared"), writeRegister(RtcClock::kSeconds, 0) && readRegister(RtcClock::kSeconds, seconds) && (seconds & Pcf::kOscillatorStopped) == 0, result);
  }

  bool testDsRollover(const DateTime &start, const DateTime &expected, bool twelveHour) {
    if (!setStopped(true) || !writeVerified(Ds::kHours, twelveHour ? 0x52 : 0x00) || !writeDateTime(start) || !setStopped(false)) {
      return false;
    }
    const unsigned long startedMs = millis();
    while (millis() - startedMs < 5000) {
      DateTime actual;
      if (readDateTime(actual) != ReadResult::Valid || actual.oscillatorStopped) {
        return false;
      }
      if (sameDateTime(actual, expected)) {
        uint8_t hours;
        return readRegister(Ds::kHours, hours) && ((hours & Ds::k12Hour) != 0) == twelveHour;
      }
      delay(25);
    }
    Serial.println(F("DS1307 clock/rollover timed out. Check VCC and the CR1225 backup battery."));
    return false;
  }

  bool testDsRam(uint8_t seed, bool addressPattern) {
    uint8_t expected[Ds::kRamSize];
    uint8_t actual[Ds::kRamSize];
    for (size_t index = 0; index < sizeof(expected); ++index) {
      expected[index] = static_cast<uint8_t>(seed ^ (addressPattern ? index : 0));
    }
    return writeRegisters(Ds::kRamStart, expected, sizeof(expected)) && readRegisters(Ds::kRamStart, actual, sizeof(actual)) && memcmp(actual, expected, sizeof(expected)) == 0;
  }

  bool testDsRandomAccess() {
    uint8_t first;
    uint8_t last;
    return writeRegister(Ds::kRamStart, 0xA5) && writeRegister(0x3F, 0x5A) && readRegister(Ds::kRamStart, first) && readRegister(0x3F, last) && first == 0xA5 && last == 0x5A;
  }

  void runDsTests(RtcTests::Result &result) {
    report(F("CH halts oscillator and freezes time"), testStoppedClock(), result);
    report(F("24-hour restart, minute and hour rollover"), testDsRollover({2026, 10, 4, 0, 12, 59, 58, false, false}, {2026, 10, 4, 0, 13, 0, 0, false, false}, false), result);
    report(F("leap-year February 28 to February 29"), testDsRollover({2024, 2, 28, 3, 23, 59, 58, false, false}, {2024, 2, 29, 4, 0, 0, 0, false, false}, false), result);
    report(F("leap-day February 29 to March 1"), testDsRollover({2024, 2, 29, 4, 23, 59, 58, false, false}, {2024, 3, 1, 5, 0, 0, 0, false, false}, false), result);
    report(F("non-leap February to March"), testDsRollover({2025, 2, 28, 5, 23, 59, 58, false, false}, {2025, 3, 1, 6, 0, 0, 0, false, false}, false), result);
    report(F("30-day month rollover"), testDsRollover({2026, 4, 30, 4, 23, 59, 58, false, false}, {2026, 5, 1, 5, 0, 0, 0, false, false}, false), result);
    report(F("12-hour AM to PM at noon"), testDsRollover({2026, 10, 4, 0, 11, 59, 58, false, false}, {2026, 10, 4, 0, 12, 0, 0, false, false}, true), result);
    report(F("12-hour PM to AM and year rollover"), testDsRollover({2025, 12, 31, 3, 23, 59, 58, false, false}, {2026, 1, 1, 4, 0, 0, 0, false, false}, true), result);
    report(F("weekday Sunday to Monday wrap"), testDsRollover({2026, 10, 4, 0, 23, 59, 58, false, false}, {2026, 10, 5, 1, 0, 0, 0, false, false}, false), result);
    report(F("two-digit year 99 to 00 wrap"), testDsRollover({2099, 12, 31, 4, 23, 59, 58, false, false}, {2000, 1, 1, 5, 0, 0, 0, false, false}, false), result);
    report(F("all 56 RAM bytes: zero pattern"), testDsRam(0x00, false), result);
    report(F("all 56 RAM bytes: all-one pattern"), testDsRam(0xFF, false), result);
    report(F("all 56 RAM bytes: address pattern"), testDsRam(0x00, true), result);
    report(F("all 56 RAM bytes: complemented address pattern"), testDsRam(0xFF, true), result);
    report(F("RAM random access at 0x08 and 0x3F"), testDsRandomAccess(), result);
    report(F("SQW disabled / OUT low and high register settings"), testRegisterPatterns(Ds::kControl, 0x00, 0x80), result);
    report(F("SQW 1 Hz register setting"), writeVerified(Ds::kControl, 0x10), result);
    report(F("SQW 4096 Hz register setting"), writeVerified(Ds::kControl, 0x11), result);
    report(F("SQW 8192 Hz register setting"), writeVerified(Ds::kControl, 0x12), result);
    report(F("SQW 32768 Hz register setting"), writeVerified(Ds::kControl, 0x13), result);
  }

  void dumpRegisters() {
    uint8_t data[RtcClock::kRegisterCount];
    if (!readRegisters(0, data, sizeof(data))) {
      Serial.println(F("RTC register dump failed."));
      return;
    }
    for (size_t first = 0; first < sizeof(data); first += 8) {
      char row[32];
      int used = snprintf(row, sizeof(row), "%02X:", static_cast<unsigned int>(first));
      for (size_t index = first; index < first + 8 && index < sizeof(data); ++index) {
        used += snprintf(row + used, sizeof(row) - used, " %02X", data[index]);
      }
      Serial.println(row);
    }
  }
}

namespace RtcTests {
  Status status() {
    return currentStatus;
  }

  bool begin() {
    currentStatus = Status::NotReady;
    receivingTime = false;
    invalidateDisplay();
    lastDisplayResult = ReadResult::Valid;
    lastDisplayMs = millis() - kDisplayIntervalMs;
    if (!RtcClock::begin()) {
      currentStatus = Status::Failed;
      Serial.println(AppConfig::kDs1307Enabled ? F("DS1307 not found at 0x68. Check wiring, VCC and battery; 3.3 V operation is outside the chip's 4.5-5.5 V rating.") : F("RTC not found at I2C address 0x51. Check Grove power and I2C cable orientation."));
      Screen.print(1, AppConfig::kDs1307Enabled ? "RTC 0x68 missing" : "RTC 0x51 missing");
      return false;
    }
    DateTime current;
    const ReadResult read = readDateTime(current);
    if (read == ReadResult::IoError) {
      currentStatus = Status::Failed;
      Serial.println(F("RTC detected, but its time registers could not be read."));
      Screen.print(1, "RTC read error");
      return false;
    }
    if (read == ReadResult::InvalidTime || current.oscillatorStopped) {
      Serial.println(F("RTC time is not trustworthy; setting it to the firmware build time."));
      if (!setToBuildTime()) {
        currentStatus = Status::Failed;
        Serial.println(F("Failed to initialize RTC time."));
        Screen.print(1, "RTC init failed");
        return false;
      }
    }
    return true;
  }

  Result runFull() {
    currentStatus = Status::Testing;
    invalidateDisplay();
    lastDisplayResult = ReadResult::Valid;
    Screen.print(1, "RTC testing...");
    Screen.print(2, "Please wait");
    Screen.print(3, "");
    Serial.println(AppConfig::kDs1307Enabled ? F("\nDS1307 (Grove RTC v1.2) full I2C feature test") : F("\nPCF85063TP full feature test"));
    Result result = {};
    Snapshot snapshot = {};
    const ReadResult read = RtcClock::capture(snapshot);
    report(AppConfig::kDs1307Enabled ? F("I2C communication and snapshot at 0x68") : F("I2C communication at 0x51"), read != ReadResult::IoError, result);
    if (read == ReadResult::IoError) {
      printResult(result);
      return result;
    }
    const bool valid = read == ReadResult::Valid && (AppConfig::kDs1307Enabled || !snapshot.dateTime.oscillatorStopped);
    report(F("valid BCD time, date and weekday"), valid, result);
    if (!valid) {
      Serial.println(F("Set a valid, trusted time with t before testing; no registers changed."));
      printResult(result);
      return result;
    }
    if (AppConfig::kDs1307Enabled) {
      runDsTests(result);
    } else {
      runPcfTests(snapshot, result);
    }
    report(AppConfig::kDs1307Enabled ? F("saved time, mode, control and all 56 RAM bytes restored") : F("original time, RAM, calibration, and controls restored"), RtcClock::restore(snapshot), result);
    printResult(result);
    Serial.println(AppConfig::kDs1307Enabled ? F("[MANUAL] SQW/OUT needs a scope or counter; register checks do not measure its waveform.") : F("[MANUAL] INT/CLKOUT register checks do not measure electrical waveforms; probes are required."));
    Serial.println(F("[MANUAL] Battery backup needs a fitted CR1225 and a power-removal/time-and-RAM retention test."));
    return result;
  }

  void updateDisplay() {
    if (!RtcClock::present() || millis() - lastDisplayMs < kDisplayIntervalMs) {
      return;
    }
    lastDisplayMs = millis();
    DateTime value;
    const ReadResult read = readDateTime(value);
    if (read != ReadResult::Valid) {
      if (read != lastDisplayResult) {
        Serial.println(read == ReadResult::IoError ? F("RTC display read failed.") : F("RTC display has invalid date/time."));
        Screen.print(2, read == ReadResult::IoError ? "RTC read error" : "RTC invalid time");
        Screen.print(3, "");
      }
      lastDisplayResult = read;
      invalidateDisplay();
      return;
    }
    lastDisplayResult = read;
    char date[20];
    char time[17];
    snprintf(date, sizeof(date), "%04u-%02u-%02u W%u", value.year, value.month, value.day, value.weekday);
    snprintf(time, sizeof(time), "%02u:%02u:%02u%s", value.hour, value.minute, value.second, clockMarker(value));
    if (strcmp(date, lastDateLine) != 0) {
      Screen.print(2, date);
      strcpy(lastDateLine, date);
    }
    if (strcmp(time, lastTimeLine) != 0) {
      Screen.print(3, time);
      strcpy(lastTimeLine, time);
    }
  }

  bool setToBuildTime() {
    DateTime value;
    if (!RtcClock::buildDateTime(__DATE__, __TIME__, value)) {
      Serial.println(F("RTC set failed: firmware build timestamp is invalid."));
      return false;
    }
    if (!RtcClock::setDateTime(value)) {
      return false;
    }
    Serial.print(F("RTC set to build time: "));
    printDateTime(value);
    return true;
  }

  void printHelp() {
    Serial.println(F("Commands: f = full test, s = show time, b = set build time, h = help"));
    Serial.println(F("t YYYY-MM-DD HH:MM:SS = set date/time (24-hour input, years 2000-2099); finish with Enter."));
    Serial.println(F("d = read-only register dump (includes battery-backed RAM)."));
    Serial.println(F("Button A reruns the full test. Button B sets the firmware build time."));
  }

  void handleSerial(char command) {
    if (receivingTime) {
      const TimeInput::Result result = timeInput.push(static_cast<uint8_t>(command));
      if (result == TimeInput::Result::None) {
        return;
      }
      receivingTime = false;
      uint8_t payload[TimeInput::kMaxPayloadLength];
      uint8_t length = 0;
      DateTime value = {};
      if (result != TimeInput::Result::Queued || !timeInput.read(payload, length) || !RtcClock::parseDateTime(payload, length, value)) {
        Serial.println(F("Invalid RTC date/time. Use t YYYY-MM-DD HH:MM:SS (2000-2099, valid date, 00:00:00-23:59:59)."));
      } else if (RtcClock::setDateTime(value)) {
        Serial.print(F("RTC set from serial: "));
        printDateTime(value);
      }
      return;
    }
    if (command == 't' || command == 'T') {
      timeInput = TimeInput();
      receivingTime = true;
    } else if (command == 'f' || command == 'F') {
      runFull();
    } else if (command == 's' || command == 'S') {
      DateTime value;
      if (readDateTime(value) == ReadResult::Valid) {
        printDateTime(value);
      } else {
        Serial.println(F("RTC read failed or date/time is invalid."));
      }
    } else if (command == 'd' || command == 'D') {
      dumpRegisters();
    } else if (command == 'b' || command == 'B') {
      setToBuildTime();
    } else if (command == 'h' || command == 'H' || command == '?') {
      printHelp();
    }
  }
}
