#include "DiagnosticChecks.h"
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
  void check(bool value, const char *message) {
    if (!value) {
      std::cerr << "FAIL: " << message << '\n';
      std::exit(1);
    }
  }

  DiagnosticChecks::HttpHeaders::Result parse(const std::string &text, int &status) {
    DiagnosticChecks::HttpHeaders headers;
    auto result = DiagnosticChecks::HttpHeaders::Result::More;
    for (unsigned char value: text) {
      result = headers.push(value);
      if (result != DiagnosticChecks::HttpHeaders::Result::More) {
        break;
      }
    }
    status = headers.status();
    return result;
  }
}

int main() {
  using namespace DiagnosticChecks;
  const uint8_t minimum[] = {0, 0x80};
  const uint8_t maximum[] = {0xFF, 0x7F};
  check(signed16(minimum) == -32768 && signed16(maximum) == 32767, "signed sensor/PCM decoding");
  float value = 0;
  check(interpolate(1000, -1000, 3000, 0, 40, value) && value == 20, "factory calibration interpolation");
  check(!interpolate(1, 0, 0, 0, 1, value), "invalid calibration denominator rejected");
  check(!inRange(NAN, 0, 100) && !inRange(INFINITY, 0, 100) && !inRange(-0.01f, 0, 100), "non-finite and out-of-range values rejected");
  check(inRange(0, 0, 100) && inRange(100, 0, 100), "inclusive sensor boundaries");
  AudioStatistics stats;
  for (size_t index = 0; index < 16000; ++index) {
    stats.add(index % 2 == 0 ? -32768 : 32767);
  }
  check(stats.peak == 32768 && stats.clipped == 16000 && stats.count == 16000, "full-scale negative amplitude and clipping count");
  check(stats.rms() > 32767 && stats.acRms() > 32767 && stats.mean() == -0.5, "64-bit square accumulation");
  AudioStatistics dc;
  for (size_t index = 0; index < 100; ++index) {
    dc.add(1234);
  }
  check(dc.acRms() == 0 && dc.rms() == 1234, "DC is not mistaken for a microphone signal");
  check(calendarKey(2027, 1, 1, 0, 0, 0) > calendarKey(2026, 12, 31, 23, 59, 59), "certificate date comparison across years");
  check(calendarKey(2028, 2, 29, 0, 0, 0) > calendarKey(2028, 2, 28, 23, 59, 59), "certificate date comparison across leap day");
  int status = 0;
  using Result = HttpHeaders::Result;
  check(parse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", status) == Result::Complete && status == 200, "complete HTTP response");
  check(parse("HTTP/1.0 301 Redirect\r\n\r\n", status) == Result::Complete && status == 301, "HTTP redirect is parsed, not followed");
  check(parse("HTTP/1.1 200 \r\n\r\n", status) == Result::Complete && status == 200, "empty reason phrase retains its required separator");
  check(parse("HTTP/1.1 200\r\n\r\n", status) == Result::Invalid, "missing status/reason separator rejected");
  check(parse("HTTP/1.1 200 OK\r\nX-Test: \tvalue: with colon\r\nEmpty:\r\n\r\n", status) == Result::Complete, "valid header names, whitespace and empty values");
  check(parse("HTTP/1.1 200 OK\r\n", status) == Result::More, "status line alone is not success");
  for (const char *bad: {"garbage\r\n\r\n", "HTTP/1.1 2x0 OK\r\n\r\n", "HTTP/1.1 200OK\r\n\r\n", "HTTP/1.1 099 bad\r\n\r\n", "HTTP/1.1 999 bad\r\n\r\n", "HTTP/1.1 200 OK\n\n", "HTTP/1.1 200 OK\rX"}) {
    check(parse(bad, status) == Result::Invalid, "malformed HTTP response rejected");
  }
  check(parse("HTTP/1.1 200 OK\r\nX: " + std::string(2048, 'x'), status) == Result::TooLong, "bounded aggregate HTTP headers");
  for (const char *bad: {"No-colon", ": unnamed", "Bad Name: value", "Bad\tName: value", " folded: value", "Bad(Name): value"}) {
    check(parse(std::string("HTTP/1.1 200 OK\r\n") + bad + "\r\n\r\n", status) == Result::Invalid, "malformed header fields rejected");
  }
  check(kFileBytes == 4096 && fileByte(0) == 'M' && fileByte(14) == 1, "versioned filesystem test record");
  std::cout << "PASS: calibrated ranges, PCM statistics/overflow, calendar ordering and bounded strict HTTP headers\n";
}
