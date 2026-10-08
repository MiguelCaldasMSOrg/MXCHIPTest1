#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "SensitiveMemory.h"

template <size_t MaxPayloadLength, size_t QueueDepth> class SerialLineInput {
  public:
  static_assert(MaxPayloadLength > 0 && MaxPayloadLength <= UINT8_MAX, "Serial line length must fit in one byte.");
  static_assert(QueueDepth > 0, "Serial input needs at least one queue slot.");
  static constexpr size_t kMaxPayloadLength = MaxPayloadLength;
  static constexpr size_t kQueueDepth = QueueDepth;

  enum class Result {
    None,
    Queued,
    Empty,
    TooLong,
    InvalidCharacter,
    QueueFull
  };

  void reset() {
    SensitiveMemory::clear(line, sizeof(line));
    SensitiveMemory::clear(messages, sizeof(messages));
    memset(lengths, 0, sizeof(lengths));
    used = head = count = 0;
    skipLf = false;
    error = Result::None;
  }

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
      SensitiveMemory::clear(line, sizeof(line));
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
    SensitiveMemory::clear(messages[head], sizeof(messages[head]));
    lengths[head] = 0;
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
