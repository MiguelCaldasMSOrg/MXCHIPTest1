#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace DiagnosticChecks {
  inline int16_t signed16(const uint8_t *bytes) {
    const uint16_t value = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    return static_cast<int16_t>(value < 0x8000 ? value : static_cast<int32_t>(value) - 65536);
  }

  inline bool interpolate(int raw, int raw0, int raw1, float value0, float value1, float &value) {
    if (raw0 == raw1) {
      return false;
    }
    value = value0 + (static_cast<float>(raw - raw0) * (value1 - value0)) / (raw1 - raw0);
    return isfinite(value);
  }

  inline bool inRange(float value, float low, float high) {
    return isfinite(value) && value >= low && value <= high;
  }

  inline uint64_t calendarKey(int year, int month, int day, int hour, int minute, int second) {
    return (((((static_cast<uint64_t>(year) * 13 + month) * 32 + day) * 24 + hour) * 60 + minute) * 60 + second);
  }

  struct AudioStatistics {
    uint32_t count = 0;
    uint32_t peak = 0;
    uint32_t clipped = 0;
    int64_t sum = 0;
    uint64_t squares = 0;

    void add(int16_t sample) {
      const int32_t value = sample;
      const uint32_t magnitude = static_cast<uint32_t>(value < 0 ? -value : value);
      if (magnitude > peak) {
        peak = magnitude;
      }
      if (magnitude >= 32760) {
        ++clipped;
      }
      ++count;
      sum += value;
      squares += static_cast<uint64_t>(static_cast<int64_t>(value) * value);
    }

    double mean() const {
      return count == 0 ? 0 : static_cast<double>(sum) / count;
    }

    double rms() const {
      return count == 0 ? 0 : sqrt(static_cast<double>(squares) / count);
    }

    double acRms() const {
      if (count == 0) {
        return 0;
      }
      const double dc = mean();
      const double variance = static_cast<double>(squares) / count - dc * dc;
      return sqrt(variance > 0 ? variance : 0);
    }
  };

  constexpr size_t kFileBytes = 4096;
  inline uint8_t fileByte(size_t offset) {
    static const uint8_t signature[16] = {'M', 'X', 'C', 'H', 'I', 'P', '-', 'F', 'S', '-', 'T', 'E', 'S', 'T', 1, 0};
    return offset < sizeof(signature) ? signature[offset] : static_cast<uint8_t>((offset * 73 + (offset >> 8) * 19) ^ 0xA5);
  }

  class HttpHeaders {
    public:
    enum class Result {
      More,
      Complete,
      Invalid,
      TooLong
    };
    static constexpr size_t kMaxBytes = 2048;

    Result push(uint8_t byte) {
      if (++bytes > kMaxBytes) {
        return Result::TooLong;
      }
      if (byte != '\r' && byte != '\n' && byte != '\t' && (byte < 32 || byte > 126)) {
        return Result::Invalid;
      }
      if (byte == '\n') {
        if (!carriageReturn) {
          return Result::Invalid;
        }
        carriageReturn = false;
        if (firstLine) {
          if (used < 13 || (strncmp(line, "HTTP/1.0 ", 9) != 0 && strncmp(line, "HTTP/1.1 ", 9) != 0) || line[12] != ' ') {
            return Result::Invalid;
          }
          for (size_t index = 9; index < 12; ++index) {
            if (line[index] < '0' || line[index] > '9') {
              return Result::Invalid;
            }
          }
          code = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
          if (code < 100 || code > 599) {
            return Result::Invalid;
          }
          firstLine = false;
        } else if (used == 0) {
          return Result::Complete;
        } else if (!headerValue) {
          return Result::Invalid;
        }
        headerValue = false;
        used = 0;
        return Result::More;
      }
      if (carriageReturn) {
        return Result::Invalid;
      }
      if (byte == '\r') {
        carriageReturn = true;
        return Result::More;
      }
      if (firstLine) {
        if (used == sizeof(line) - 1) {
          return Result::TooLong;
        }
        line[used] = static_cast<char>(byte);
      } else if (!headerValue) {
        if (byte == ':' && used != 0) {
          headerValue = true;
        } else if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || strchr("!#$%&'*+-.^_`|~", byte) != nullptr)) {
          return Result::Invalid;
        }
      }
      ++used;
      return Result::More;
    }

    int status() const {
      return code;
    }

    private:
    char line[96] = {};
    size_t used = 0;
    size_t bytes = 0;
    int code = 0;
    bool firstLine = true;
    bool carriageReturn = false;
    bool headerValue = false;
  };
}
