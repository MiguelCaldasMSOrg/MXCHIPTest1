#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class SerialRadioInput {
  public:
  static constexpr size_t kMaxPayloadLength = 77;
  static constexpr size_t kQueueDepth = 4;

  enum class Result {
    None,
    Queued,
    Empty,
    TooLong,
    InvalidCharacter,
    QueueFull
  };

  Result push(uint8_t value) {
    if (value == '\n' && skipLf) {
      skipLf = false;
      return Result::None;
    }
    skipLf = false;
    if (value == '\r' || value == '\n') {
      skipLf = value == '\r';
      Result result = error;
      if (result == Result::None) {
        if (used == 0) {
          result = Result::Empty;
        } else if (count == kQueueDepth) {
          result = Result::QueueFull;
        } else {
          const size_t tail = (head + count) % kQueueDepth;
          memcpy(messages[tail], line, used);
          lengths[tail] = static_cast<uint8_t>(used);
          count++;
          result = Result::Queued;
        }
      }
      used = 0;
      error = Result::None;
      return result;
    }
    if (error != Result::None) {
      return Result::None;
    }
    if (value != '\t' && (value < 32 || value > 126)) {
      error = Result::InvalidCharacter;
    } else if (used == kMaxPayloadLength) {
      error = Result::TooLong;
    } else {
      line[used++] = value;
    }
    return Result::None;
  }

  bool read(uint8_t (&payload)[kMaxPayloadLength], uint8_t &length) {
    if (count == 0) {
      return false;
    }
    length = lengths[head];
    memcpy(payload, messages[head], length);
    head = (head + 1) % kQueueDepth;
    count--;
    return true;
  }

  private:
  uint8_t line[kMaxPayloadLength] = {};
  size_t used = 0;
  bool skipLf = false;
  Result error = Result::None;
  uint8_t messages[kQueueDepth][kMaxPayloadLength] = {};
  uint8_t lengths[kQueueDepth] = {};
  size_t head = 0;
  size_t count = 0;
};
